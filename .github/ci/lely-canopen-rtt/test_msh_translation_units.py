# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
MSH_ROOT = REPO_ROOT / "port" / "rtthread" / "src"


def find_host_compiler():
    override = os.environ.get("HOST_CC")
    if override:
        return shutil.which(override)
    for candidate in ("cc", "gcc", "clang"):
        compiler = shutil.which(candidate)
        if compiler:
            return compiler
    return None


class MshTranslationUnitCompileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = find_host_compiler()
        if not cls.compiler:
            raise unittest.SkipTest(
                "host C compiler not found; set HOST_CC to a native compiler executable"
            )

    @staticmethod
    def _write_stubs(stub_root):
        headers = {
            "rtthread.h": r"""
                #ifndef RTTHREAD_H_
                #define RTTHREAD_H_
                #include <stddef.h>
                #include <stdint.h>
                typedef int rt_bool_t;
                typedef int rt_err_t;
                typedef int8_t rt_int8_t;
                typedef int16_t rt_int16_t;
                typedef int32_t rt_int32_t;
                typedef int64_t rt_int64_t;
                typedef uint8_t rt_uint8_t;
                typedef uint16_t rt_uint16_t;
                typedef uint32_t rt_uint32_t;
                typedef uint64_t rt_uint64_t;
                typedef size_t rt_size_t;
                typedef void *rt_device_t;
                struct rt_can_status { int unused; };
                #define RT_FALSE 0
                #define RT_TRUE 1
                #define RT_NULL ((void *)0)
                #define RT_EOK 0
                #define RT_ERROR 1
                #define RT_EINVAL 22
                #define RT_EBUSY 16
                #define RT_ENOSYS 38
                #define RT_WAITING_FOREVER (-1)
                int rt_kprintf(const char *format, ...);
                #endif
            """,
            "rtdevice.h": r"""
                #ifndef RTDEVICE_H_
                #define RTDEVICE_H_
                #include <rtthread.h>
                #endif
            """,
            "finsh.h": r"""
                #ifndef FINSH_H_
                #define FINSH_H_
                #define MSH_CMD_EXPORT(fn, desc)
                #endif
            """,
            "lely/io2/can/err.h": r"""
                #ifndef LELY_IO2_CAN_ERR_H_
                #define LELY_IO2_CAN_ERR_H_
                struct can_err { int unused; };
                #endif
            """,
            "lely/co/dev.h": r"""
                #ifndef LELY_CO_DEV_H_
                #define LELY_CO_DEV_H_
                #define CO_NUM_NODES 127u
                #endif
            """,
            "lely/co/nmt.h": r"""
                #ifndef LELY_CO_NMT_H_
                #define LELY_CO_NMT_H_
                #define CO_NMT_ST_BOOTUP 0x00u
                #define CO_NMT_ST_STOP 0x04u
                #define CO_NMT_ST_START 0x05u
                #define CO_NMT_ST_RESET_NODE 0x81u
                #define CO_NMT_ST_RESET_COMM 0x82u
                #define CO_NMT_ST_PREOP 0x7fu
                #endif
            """,
            "lely/co/pdo.h": r"""
                #ifndef LELY_CO_PDO_H_
                #define LELY_CO_PDO_H_
                #define CO_NUM_PDOS 512u
                #endif
            """,
        }
        for rel, text in headers.items():
            path = stub_root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="ascii")

    def _compile_case(self, temp, name, macros, sources):
        case_dir = temp / name
        case_dir.mkdir()
        stub_root = case_dir / "stubs"
        self._write_stubs(stub_root)

        common_flags = [
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-function",
            "-I",
            str(stub_root),
            "-I",
            str(REPO_ROOT / "port" / "rtthread" / "include"),
            "-I",
            str(MSH_ROOT / "msh"),
        ]
        for macro in sorted(macros):
            common_flags.append(f"-D{macro}=1")

        objects = []
        for source_rel in sources:
            source = MSH_ROOT / source_rel
            obj = case_dir / (source_rel.replace("/", "_") + ".o")
            obj.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(
                [self.compiler, *common_flags, "-c", str(source), "-o", str(obj)],
                cwd=REPO_ROOT,
                check=True,
            )
            objects.append(obj)

        # Relocatable linking preserves production translation-unit boundaries,
        # catches duplicate definitions, and permits unresolved RT-Thread/Lely symbols.
        subprocess.run(
            [self.compiler, "-r", *(str(obj) for obj in objects), "-o", str(case_dir / "msh.o")],
            cwd=REPO_ROOT,
            check=True,
        )

    def test_representative_msh_feature_matrix_compiles_as_separate_units(self):
        base_macros = {
            "PKG_LELY_APP_AUTO_INIT",
            "PKG_LELY_USING_MSH",
            "PKG_LELY_USING_MASTER_COMMAND",
        }
        base_sources = (
            "msh.c",
            "msh/msh_common.c",
            "msh/msh_nmt.c",
        )
        cases = (
            ("base", set(), ()),
            ("nmt_cfg", {"PKG_LELY_USING_MASTER_NMT_CFG"}, ("msh/msh_cfg.c",)),
            ("local_od", {"PKG_LELY_USING_LOCAL_OD"}, ("msh/msh_od.c",)),
            (
                "pdo_tx",
                {"PKG_LELY_USING_LOCAL_OD", "PKG_LELY_USING_MASTER_PDO_TX"},
                ("msh/msh_od.c", "msh/msh_pdo.c"),
            ),
            (
                "sync_pdo",
                {
                    "PKG_LELY_USING_LOCAL_OD",
                    "PKG_LELY_USING_MASTER_PDO_TX",
                    "PKG_LELY_USING_MASTER_SYNC_PDO",
                },
                ("msh/msh_od.c", "msh/msh_pdo.c"),
            ),
            ("emcy", {"PKG_LELY_USING_MASTER_EMCY"}, ("msh/msh_emcy.c",)),
            ("time", {"PKG_LELY_USING_MASTER_TIME"}, ("msh/msh_time.c",)),
            ("sdo", {"PKG_LELY_USING_MASTER_SDO"}, ("msh/msh_sdo.c",)),
            (
                "all_features",
                {
                    "PKG_LELY_USING_LOCAL_OD",
                    "PKG_LELY_USING_MASTER_EMCY",
                    "PKG_LELY_USING_MASTER_NMT_CFG",
                    "PKG_LELY_USING_MASTER_PDO_TX",
                    "PKG_LELY_USING_MASTER_SDO",
                    "PKG_LELY_USING_MASTER_SYNC_PDO",
                    "PKG_LELY_USING_MASTER_TIME",
                },
                (
                    "msh/msh_cfg.c",
                    "msh/msh_od.c",
                    "msh/msh_pdo.c",
                    "msh/msh_emcy.c",
                    "msh/msh_time.c",
                    "msh/msh_sdo.c",
                ),
            ),
        )

        with tempfile.TemporaryDirectory(prefix="lely-msh-tu-test-") as temp_dir:
            temp = Path(temp_dir)
            for name, feature_macros, feature_sources in cases:
                with self.subTest(case=name):
                    self._compile_case(
                        temp,
                        name,
                        base_macros | feature_macros,
                        base_sources + feature_sources,
                    )

    def test_sync_pdo_source_guard_is_self_contained_without_kconfig_closure(self):
        # Kconfig selects MASTER_PDO_TX when MASTER_SYNC_PDO is enabled. This raw
        # macro case intentionally bypasses that closure so msh_pdo.c cannot rely
        # on an unrelated branch or another translation unit to provide PDO limits.
        macros = {
            "PKG_LELY_APP_AUTO_INIT",
            "PKG_LELY_USING_MSH",
            "PKG_LELY_USING_LOCAL_OD",
            "PKG_LELY_USING_MASTER_COMMAND",
            "PKG_LELY_USING_MASTER_SYNC_PDO",
        }
        with tempfile.TemporaryDirectory(prefix="lely-msh-sync-guard-test-") as temp_dir:
            self._compile_case(
                Path(temp_dir),
                "sync_pdo_raw_guard",
                macros,
                ("msh/msh_pdo.c",),
            )


if __name__ == "__main__":
    unittest.main()
