# SPDX-License-Identifier: Apache-2.0

import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
COMPACTOR_PATH = REPO_ROOT / "tools" / "compact_master_dcf.py"
NORMALIZER_PATH = REPO_ROOT / "tools" / "normalize_sdev_compact_defaults.py"
MASTER_DCF = REPO_ROOT / "examples" / "master_node1" / "master.dcf"
MASTER_SDEV = REPO_ROOT / "examples" / "master_node1" / "master_sdev.c"
MASTER_META = REPO_ROOT / "examples" / "master_node1" / "master_sdev.meta"
MASTER_YML = REPO_ROOT / "examples" / "master_node1" / "master.yml"
GEN_SDEV = REPO_ROOT / "tools" / "gen_sdev.ps1"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


COMPACTOR = load_module("lely_compact_master_tested", COMPACTOR_PATH)
NORMALIZER = load_module("lely_sdev_default_normalizer_tested", NORMALIZER_PATH)


def concise_dcf(*entries):
    data = bytearray(struct.pack("<I", len(entries)))
    for index, subindex, value in entries:
        data += struct.pack("<HBI", index, subindex, len(value))
        data += value
    return bytes(data)


def section_text(text, name):
    marker = f"[{name}]"
    start = text.index(marker)
    next_section = text.find("\n[", start + len(marker))
    return text[start:] if next_section == -1 else text[start:next_section]


def c_object_text(text, index):
    marker = f"\t\t.idx = 0x{index:04x},"
    start = text.index(marker)
    next_object = text.find("\n\t}, {", start)
    return text[start:] if next_object == -1 else text[start:next_object]


class SdevGenerationTests(unittest.TestCase):
    def test_checked_in_compact_defaults_match_dcf_semantics(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        c_text = MASTER_SDEV.read_text(encoding="ascii")
        normalized, changes = NORMALIZER.normalize_c_text(dcf_text, c_text)

        self.assertEqual([], changes)
        self.assertEqual(c_text, normalized)
        self.assertIn(".def = { .u32 = 0x80000000lu },", c_text)
        self.assertIn(".val = { .u32 = 0x00000081lu },", c_text)

    def test_checked_in_master_uses_1f25_unsigned32_and_identity_values(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        c_text = MASTER_SDEV.read_text(encoding="ascii")

        dcf_1f25 = section_text(dcf_text, "1F25")
        self.assertIn("DataType=0x0007", dcf_1f25)
        self.assertNotIn("DataType=0x0005", dcf_1f25)

        c_1f25 = c_object_text(c_text, 0x1F25)
        self.assertIn(".type = CO_DEFTYPE_UNSIGNED32,", c_1f25)
        self.assertIn(".val = { .u32 = CO_UNSIGNED32_MIN },", c_1f25)

        self.assertIn("[1F87Value]\nNrOfEntries=1\n1=0x00000001", dcf_text)
        self.assertIn("[1F88Value]\nNrOfEntries=1\n1=0x00000001", dcf_text)
        self.assertIn(".val = { .u32 = 0x00000001lu },", c_object_text(c_text, 0x1F87))
        self.assertIn(".val = { .u32 = 0x00000001lu },", c_object_text(c_text, 0x1F88))

    def test_compactor_preserves_1f25_unsigned32_configuration_request(self):
        generated = """\
[1F25]
ParameterName=Configuration request
ObjectType=0x08
DataType=0x0007
AccessType=wo
CompactSubObj=127
"""
        compacted = COMPACTOR.compact_dcf(
            generated,
            error_history_depth=8,
            max_subobjects=256,
        )

        dcf_1f25 = section_text(compacted[0], "1F25")
        self.assertIn("DataType=0x0007", dcf_1f25)
        self.assertIn("CompactSubObj=1", dcf_1f25)
        self.assertIn(("1F25", 127, 1), compacted[1])

    def test_compactor_normalizes_dcfgen_unsigned8_1f25_configuration_request(self):
        generated = """\
[1F25]
ParameterName=Configuration request
ObjectType=0x08
DataType=0x0005
AccessType=wo
CompactSubObj=127
"""
        compacted = COMPACTOR.compact_dcf(
            generated,
            error_history_depth=8,
            max_subobjects=256,
        )

        dcf_1f25 = section_text(compacted[0], "1F25")
        self.assertIn("DataType=0x0007", dcf_1f25)
        self.assertNotIn("DataType=0x0005", dcf_1f25)

    def test_compactor_rejects_unknown_1f25_data_type(self):
        generated = """\
[1F25]
ParameterName=Configuration request
ObjectType=0x08
DataType=0x0006
AccessType=wo
CompactSubObj=127
"""
        with self.assertRaisesRegex(
            ValueError,
            r"0x1F25 Configuration request must use UNSIGNED32 .*0x0007.*0x0006",
        ):
            COMPACTOR.compact_dcf(
                generated,
                error_history_depth=8,
                max_subobjects=256,
            )

    def test_compactor_materializes_master_bin_identity_values(self):
        expected = MASTER_DCF.read_text(encoding="utf-8-sig")
        generated_only = expected.replace(
            "[1F87Value]\nNrOfEntries=1\n1=0x00000001\n\n", "", 1
        ).replace(
            "[1F88Value]\nNrOfEntries=1\n1=0x00000001\n\n", "", 1
        )
        self.assertNotEqual(expected, generated_only)

        payload = concise_dcf(
            (0x1F87, 1, struct.pack("<I", 1)),
            (0x1F88, 1, struct.pack("<I", 1)),
        )
        with tempfile.TemporaryDirectory(prefix="lely-master-bin-test-") as temp_dir:
            master_bin = Path(temp_dir) / "master.bin"
            master_bin.write_bytes(payload)
            compacted = COMPACTOR.compact_dcf(
                generated_only,
                master_bin=master_bin,
                error_history_depth=8,
                max_subobjects=256,
            )

        self.assertEqual(expected, compacted[0])
        self.assertEqual(["0x1F87:01", "0x1F88:01"], compacted[3])

    def test_compactor_rejects_unsupported_master_bin_writes(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        payload = concise_dcf((0x1F25, 1, b"\x01"))
        with tempfile.TemporaryDirectory(prefix="lely-master-bin-test-") as temp_dir:
            master_bin = Path(temp_dir) / "master.bin"
            master_bin.write_bytes(payload)
            with self.assertRaisesRegex(ValueError, "unsupported master.bin entry"):
                COMPACTOR.compact_dcf(
                    dcf_text,
                    master_bin=master_bin,
                    error_history_depth=8,
                    max_subobjects=256,
                )

    def test_compactor_materializes_other_supported_master_bin_writes(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        payload = concise_dcf(
            (0x1018, 4, struct.pack("<I", 0x11223344)),
            (0x1F55, 1, struct.pack("<I", 0x55667788)),
        )
        with tempfile.TemporaryDirectory(prefix="lely-master-bin-test-") as temp_dir:
            master_bin = Path(temp_dir) / "master.bin"
            master_bin.write_bytes(payload)
            compacted = COMPACTOR.compact_dcf(
                dcf_text,
                master_bin=master_bin,
                error_history_depth=8,
                max_subobjects=256,
            )

        self.assertIn("ParameterValue=0x11223344", section_text(compacted[0], "1018sub4"))
        self.assertIn("[1F55Value]\nNrOfEntries=1\n1=0x55667788", compacted[0])
        self.assertEqual(["0x1018:04", "0x1F55:01"], compacted[3])

    def test_compactor_rejects_malformed_master_bin_framing(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        payload = struct.pack("<I", 1) + b"\x87\x1f\x01"
        with tempfile.TemporaryDirectory(prefix="lely-master-bin-test-") as temp_dir:
            master_bin = Path(temp_dir) / "master.bin"
            master_bin.write_bytes(payload)
            with self.assertRaisesRegex(ValueError, "truncated header"):
                COMPACTOR.compact_dcf(
                    dcf_text,
                    master_bin=master_bin,
                    error_history_depth=8,
                    max_subobjects=256,
                )

    def test_compactor_rejects_wrong_master_bin_value_size(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        payload = concise_dcf((0x1F87, 1, b"\x01\x00"))
        with tempfile.TemporaryDirectory(prefix="lely-master-bin-test-") as temp_dir:
            master_bin = Path(temp_dir) / "master.bin"
            master_bin.write_bytes(payload)
            with self.assertRaisesRegex(ValueError, "must be 4 bytes"):
                COMPACTOR.compact_dcf(
                    dcf_text,
                    master_bin=master_bin,
                    error_history_depth=8,
                    max_subobjects=256,
                )

    def test_compactor_rejects_non_unsigned32_master_bin_targets(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        targets = (
            ("1018sub4", 0x1018, 4),
            ("1F55", 0x1F55, 1),
            ("1F87", 0x1F87, 1),
            ("1F88", 0x1F88, 1),
        )

        for section_name, index, subidx in targets:
            with self.subTest(target=f"0x{index:04X}:{subidx:02X}"):
                section = section_text(dcf_text, section_name)
                self.assertIn("DataType=0x0007", section)
                drifted_section = section.replace(
                    "DataType=0x0007", "DataType=0x0005", 1
                )
                drifted_dcf = dcf_text.replace(section, drifted_section, 1)
                payload = concise_dcf((index, subidx, struct.pack("<I", 1)))

                with tempfile.TemporaryDirectory(
                    prefix="lely-master-bin-test-"
                ) as temp_dir:
                    master_bin = Path(temp_dir) / "master.bin"
                    master_bin.write_bytes(payload)
                    with self.assertRaisesRegex(
                        ValueError,
                        r"must use UNSIGNED32 .*DataType=0x0007.*0x0005",
                    ):
                        COMPACTOR.compact_dcf(
                            drifted_dcf,
                            master_bin=master_bin,
                            error_history_depth=8,
                            max_subobjects=256,
                        )

    def test_compactor_rejects_out_of_range_master_bin_subindex(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        payload = concise_dcf((0x1F87, 128, struct.pack("<I", 1)))
        with tempfile.TemporaryDirectory(prefix="lely-master-bin-test-") as temp_dir:
            master_bin = Path(temp_dir) / "master.bin"
            master_bin.write_bytes(payload)
            with self.assertRaisesRegex(ValueError, "unsupported master.bin entry"):
                COMPACTOR.compact_dcf(
                    dcf_text,
                    master_bin=master_bin,
                    error_history_depth=8,
                    max_subobjects=256,
                )

    def test_compactor_rejects_master_bin_node_above_compact_network_range(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")

        for index in (0x1F55, 0x1F87, 0x1F88):
            with self.subTest(index=f"0x{index:04X}"):
                payload = concise_dcf((index, 2, struct.pack("<I", 1)))
                with tempfile.TemporaryDirectory(prefix="lely-master-bin-test-") as temp_dir:
                    master_bin = Path(temp_dir) / "master.bin"
                    master_bin.write_bytes(payload)
                    with self.assertRaisesRegex(
                        ValueError,
                        r"node-indexed entry exceeds.*highest configured remote node-ID is 1",
                    ):
                        COMPACTOR.compact_dcf(
                            dcf_text,
                            master_bin=master_bin,
                            error_history_depth=8,
                            max_subobjects=256,
                        )

    def test_powershell_passes_staged_master_bin_to_compactor(self):
        script = GEN_SDEV.read_text(encoding="utf-8-sig")
        create = script.index('$generatedMasterBin = Join-Path $stageRoot "master.bin"')
        option = script.index('$compactArgs += @("--master-bin", $generatedMasterBin)', create)
        invoke = script.index(
            "Invoke-NativeTool -FilePath $venvPython -Arguments $compactArgs", option
        )
        publish = script.index("Publish-GeneratedArtifacts -Artifacts", invoke)

        self.assertLess(create, option)
        self.assertLess(option, invoke)
        self.assertLess(invoke, publish)

    def test_checked_in_master_is_fileless_and_within_compact_limit(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        compacted = COMPACTOR.compact_dcf(
            dcf_text,
            error_history_depth=8,
            max_subobjects=256,
        )
        output_text = compacted[0]
        self.assertNotIn("UploadFile=", output_text)
        self.assertNotIn("DownloadFile=", output_text)
        self.assertLessEqual(compacted[-1], 256)

    def test_checked_in_meta_hashes_match_current_artifacts(self):
        metadata = {}
        for line in MASTER_META.read_text(encoding="ascii").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                metadata[key] = value

        def normalized_sha256(path):
            data = path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
            return hashlib.sha256(data).hexdigest()

        self.assertEqual(normalized_sha256(MASTER_YML), metadata["INPUT_SHA256"])
        self.assertEqual(normalized_sha256(MASTER_DCF), metadata["DCF_SHA256"])
        self.assertEqual(normalized_sha256(MASTER_SDEV), metadata["SDEV_SHA256"])

    def test_normalizer_repairs_compact_default_without_changing_current_value(self):
        dcf_text = MASTER_DCF.read_text(encoding="utf-8-sig")
        c_text = MASTER_SDEV.read_text(encoding="ascii")
        broken = c_text.replace(
            ".def = { .u32 = 0x80000000lu },",
            ".def = { .u32 = 0x00000081lu },",
            1,
        )
        self.assertNotEqual(c_text, broken)

        repaired, changes = NORMALIZER.normalize_c_text(dcf_text, broken)

        self.assertIn("0x1028:01", changes[0])
        self.assertEqual(c_text, repaired)
        self.assertIn(".val = { .u32 = 0x00000081lu },", repaired)

    def test_compact_master_rejects_file_backed_od_values(self):
        file_backed_dcf = """\
[1F22]
ParameterName=Concise DCF
ObjectType=0x08
DataType=0x000F
CompactSubObj=1

[1F22sub1]
ParameterName=Node-ID 1
DataType=0x000F
AccessType=ro
UploadFile=node1.bin
"""
        with self.assertRaisesRegex(ValueError, "LELY_NO_CO_OBJ_FILE=1"):
            COMPACTOR.compact_dcf(
                file_backed_dcf,
                error_history_depth=8,
                max_subobjects=256,
            )

    def test_powershell_guard_runs_before_atomic_publish(self):
        script = GEN_SDEV.read_text(encoding="utf-8-sig")
        dcf2c_marker = 'throw "dcf2c did not produce a non-empty $Name.c"'
        guard_start = script.index("normalize_sdev_compact_defaults.py")
        generated_guard = script.index("CO_OBJ_FLAGS_(UPLOAD|DOWNLOAD)_FILE", guard_start)
        publish = script.index("Publish-GeneratedArtifacts -Artifacts", script.index(dcf2c_marker))

        self.assertLess(generated_guard, publish)
        self.assertIn("LELY_NO_CO_OBJ_FILE=1", script[generated_guard:publish])
        self.assertIn("gen_cfg_dcf.py", script[generated_guard:publish])


if __name__ == "__main__":
    unittest.main()
