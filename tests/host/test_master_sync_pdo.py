# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def find_host_compiler():
    override = os.environ.get("HOST_CC")
    if override:
        return shutil.which(override)

    for candidate in ("cc", "gcc", "clang"):
        compiler = shutil.which(candidate)
        if compiler:
            return compiler
    return None



class MasterSyncPdoHostHarnessTests(unittest.TestCase):
    def test_master_sync_pdo_host_harness(self):
        repo_root = Path(__file__).resolve().parents[2]
        source = repo_root / "tests" / "host" / "test_master_sync_pdo.c"
        compiler = find_host_compiler()
        if not compiler:
            self.fail("host C compiler not found; set HOST_CC to a native compiler executable")

        with tempfile.TemporaryDirectory(prefix="lely-master-b9-test-") as temp_dir:
            temp = Path(temp_dir)
            stub_dir = temp / "stubs" / "lely" / "co"
            stub_dir.mkdir(parents=True)
            for header in ("dev.h", "obj.h", "pdo.h", "sync.h", "tpdo.h"):
                (stub_dir / header).write_text("\n", encoding="ascii")

            binary = temp / "test_master_sync_pdo"
            compile_cmd = [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(temp / "stubs"),
                str(source),
                "-o",
                str(binary),
            ]
            subprocess.run(compile_cmd, cwd=repo_root, check=True)
            completed = subprocess.run(
                [str(binary)],
                cwd=repo_root,
                check=True,
                text=True,
                capture_output=True,
            )

        for name in (
            "sync-snapshot-and-callback",
            "sync-bind-ownership",
            "sync-period-control",
            "pdo-transmission-control",
            "tpdo-event-modes",
            "owner-thread-wait-rejected",
        ):
            self.assertIn(f"PASS {name}", completed.stdout)
        self.assertIn("Passed 6/6 host B9 cases", completed.stdout)



if __name__ == "__main__":
    unittest.main()
