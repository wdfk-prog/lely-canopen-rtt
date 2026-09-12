# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().with_suffix(".c")


def find_host_compiler():
    override = os.environ.get("HOST_CC")
    if override:
        return shutil.which(override)
    for candidate in ("cc", "gcc", "clang"):
        compiler = shutil.which(candidate)
        if compiler:
            return compiler
    return None


class MasterOdHooksTests(unittest.TestCase):
    def test_application_od_hooks_and_notification(self):
        compiler = find_host_compiler()
        if not compiler:
            self.skipTest("no host C compiler is available; set HOST_CC to a native compiler")

        with tempfile.TemporaryDirectory(prefix="lely-od-hooks-") as temp_dir:
            root = Path(temp_dir)
            for relative in ("lely/co/dev.h", "lely/co/obj.h", "lely/co/sdo.h"):
                header = root / relative
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text("", encoding="ascii")
            binary = root / "test_master_od_hooks"
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Werror=incompatible-pointer-types",
                    "-I",
                    str(root),
                    str(SOURCE),
                    "-o",
                    str(binary),
                ],
                cwd=REPO_ROOT,
                check=True,
            )
            completed = subprocess.run(
                [str(binary)],
                cwd=REPO_ROOT,
                check=True,
                text=True,
                capture_output=True,
            )

        self.assertIn("MASTER_OD_HOOKS_PASS", completed.stdout)


if __name__ == "__main__":
    unittest.main()
