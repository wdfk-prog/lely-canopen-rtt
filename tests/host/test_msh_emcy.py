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


class MshEmcyHostHarnessTests(unittest.TestCase):
    def test_msh_emcy_host_harness(self):
        repo_root = Path(__file__).resolve().parents[2]
        source = repo_root / "tests" / "host" / "test_msh_emcy.c"
        compiler = find_host_compiler()
        if not compiler:
            self.fail("host C compiler not found; set HOST_CC to a native compiler executable")

        with tempfile.TemporaryDirectory(prefix="lely-msh-emcy-test-") as temp_dir:
            temp = Path(temp_dir)
            stub_root = temp / "stubs"
            for rel in (
                "lely/co/dev.h",
                "lely/co/nmt.h",
                "lely/rtthread/runtime.h",
                "finsh.h",
                "rtthread.h",
            ):
                path = stub_root / rel
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("\n", encoding="ascii")

            binary = temp / "test_msh_emcy"
            compile_cmd = [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-function",
                "-I",
                str(stub_root),
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

        self.assertIn("PASS push-hex-forms-and-boundaries", completed.stdout)
        self.assertIn("PASS push-rejects-invalid-fields", completed.stdout)
        self.assertIn("PASS pop-clear-and-query-routing", completed.stdout)
        self.assertIn("Passed 3/3 host EMCY MSH cases", completed.stdout)


if __name__ == "__main__":
    unittest.main()
