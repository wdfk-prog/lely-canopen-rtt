# SPDX-License-Identifier: Apache-2.0

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = REPO_ROOT / "tools" / "gen_cfg_dcf.py"
SPEC = importlib.util.spec_from_file_location("lely_gen_cfg_dcf_tested", GENERATOR_PATH)
GENERATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GENERATOR
SPEC.loader.exec_module(GENERATOR)


def make_entry(index, sub_index, value):
    return struct.pack("<HBI", index, sub_index, len(value)) + value


def make_dcf(*entries):
    return struct.pack("<I", len(entries)) + b"".join(entries)


VALID_DCF = make_dcf(make_entry(0x1017, 0, b"\xe8\x03"))


class GenCfgDcfTests(unittest.TestCase):
    def test_parse_concise_dcf_accepts_valid_entries(self):
        data = make_dcf(
            make_entry(0x1017, 0, b"\xe8\x03"),
            make_entry(0x2000, 2, b"\x01\x02\x03"),
        )
        entries = GENERATOR.parse_concise_dcf(data)
        self.assertEqual(2, len(entries))
        self.assertEqual((0x1017, 0, b"\xe8\x03"),
                         (entries[0].index, entries[0].sub_index, entries[0].value))
        self.assertEqual((0x2000, 2, b"\x01\x02\x03"),
                         (entries[1].index, entries[1].sub_index, entries[1].value))

    def test_parse_concise_dcf_rejects_invalid_framing(self):
        invalid_cases = {
            "short-count": b"\x01\x00\x00",
            "zero-entry": b"\x00\x00\x00\x00",
            "truncated-header": b"\x01\x00\x00\x00\x17\x10",
            "truncated-value": make_dcf(make_entry(0x1017, 0, b"\xe8\x03"))[:-1],
            "trailing-byte": VALID_DCF + b"\xa5",
        }
        for name, data in invalid_cases.items():
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    GENERATOR.parse_concise_dcf(data)

    def test_checked_in_node1_artifacts_reproduce_from_fixed_bin(self):
        entries = GENERATOR.parse_concise_dcf(VALID_DCF)
        source = GENERATOR.render_source(
            "master_cfg_dcf",
            "master_node1_cfg_dcf",
            entries,
            "master_cfg.yml; slave section node1",
        )
        header = GENERATOR.render_header("master_cfg_dcf", "master_node1_cfg_dcf")
        expected_c = REPO_ROOT / "examples" / "master_node1" / "master_cfg_dcf.c"
        expected_h = REPO_ROOT / "examples" / "master_node1" / "master_cfg_dcf.h"
        self.assertEqual(expected_c.read_bytes(), GENERATOR._crlf_ascii(source))
        self.assertEqual(expected_h.read_bytes(), GENERATOR._crlf_ascii(header))

    def test_publish_pair_restores_previous_pair_if_second_replace_fails(self):
        with tempfile.TemporaryDirectory(prefix="lely-cfg-publish-test-") as temp_dir:
            out_dir = Path(temp_dir)
            final_c = out_dir / "cfg.c"
            final_h = out_dir / "cfg.h"
            old_c = b"old-c\r\n"
            old_h = b"old-h\r\n"
            final_c.write_bytes(old_c)
            final_h.write_bytes(old_h)
            real_replace = GENERATOR.os.replace
            replace_calls = 0

            def fail_second_replace(source, destination):
                nonlocal replace_calls
                replace_calls += 1
                if replace_calls == 2:
                    raise OSError("injected second publish failure")
                return real_replace(source, destination)

            with mock.patch.object(GENERATOR.os, "replace", side_effect=fail_second_replace):
                with self.assertRaises(OSError):
                    GENERATOR.publish_pair(out_dir, "cfg", "new-c\n", "new-h\n")

            self.assertEqual(old_c, final_c.read_bytes())
            self.assertEqual(old_h, final_h.read_bytes())
            self.assertFalse((out_dir / ".cfg.c.tmp").exists())
            self.assertFalse((out_dir / ".cfg.h.tmp").exists())

    def test_generate_with_dcfgen_selects_staged_slave_bin(self):
        with tempfile.TemporaryDirectory(prefix="lely-cfg-dcfgen-test-") as temp_dir:
            root = Path(temp_dir)
            yml = root / "master_cfg.yml"
            yml.write_text("master:\n  node_id: 127\n", encoding="ascii")
            captured = {}

            def fake_run(args, cwd, check):
                self.assertTrue(check)
                captured["args"] = list(args)
                captured["cwd"] = Path(cwd)
                stage = Path(args[args.index("-d") + 1])
                (stage / "node1.bin").write_bytes(VALID_DCF)
                (stage / "master.dcf").write_text("staging-only", encoding="ascii")

            with mock.patch.object(GENERATOR.subprocess, "run", side_effect=fake_run):
                data = GENERATOR.generate_with_dcfgen(
                    yml,
                    "node1",
                    "fake-dcfgen",
                    no_strict=False,
                    verbose=False,
                )

            self.assertEqual(VALID_DCF, data)
            self.assertEqual(root, captured["cwd"])
            self.assertEqual("master_cfg.yml", captured["args"][-1])
            self.assertFalse((root / "master.dcf").exists())


if __name__ == "__main__":
    unittest.main()
