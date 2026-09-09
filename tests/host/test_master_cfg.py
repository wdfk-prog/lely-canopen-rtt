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


class MasterCfgHostHarnessTests(unittest.TestCase):
    def test_master_cfg_host_harness(self):
        repo_root = Path(__file__).resolve().parents[2]
        source = repo_root / "tests" / "host" / "test_master_cfg.c"
        compiler = find_host_compiler()
        if not compiler:
            self.fail("host C compiler not found; set HOST_CC to a native compiler executable")

        with tempfile.TemporaryDirectory(prefix="lely-master-cfg-test-") as temp_dir:
            temp = Path(temp_dir)
            stub_dir = temp / "stubs" / "lely" / "co"
            stub_dir.mkdir(parents=True)
            for header in ("csdo.h", "dev.h", "obj.h"):
                (stub_dir / header).write_text("\n", encoding="ascii")

            binary = temp / "test_master_cfg"
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

        self.assertIn("PASS registration-and-framing", completed.stdout)
        self.assertIn("PASS manual-only-auto-cfg", completed.stdout)
        self.assertIn("PASS manual-success-diagnostic", completed.stdout)
        self.assertIn("PASS manual-abort-diagnostic", completed.stdout)
        self.assertIn("PASS local-reset-barrier", completed.stdout)
        self.assertIn("PASS nmt-destroy-barrier", completed.stdout)
        self.assertIn("PASS stopped-not-barrier", completed.stdout)
        self.assertIn("Passed 7/7 host CFG cases", completed.stdout)


if __name__ == "__main__":
    unittest.main()
