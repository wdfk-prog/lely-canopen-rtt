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


class MasterSdoHostHarnessTests(unittest.TestCase):
    def test_master_sdo_host_harness(self):
        repo_root = Path(__file__).resolve().parents[3]
        source = Path(__file__).resolve().with_suffix(".c")
        compiler = find_host_compiler()
        if not compiler:
            self.fail("host C compiler not found; set HOST_CC to a native compiler executable")

        with tempfile.TemporaryDirectory(prefix="lely-master-sdo-test-") as temp_dir:
            temp = Path(temp_dir)
            stub_root = temp / "stubs"
            csdo_h = stub_root / "lely" / "co" / "csdo.h"
            csdo_h.parent.mkdir(parents=True, exist_ok=True)
            csdo_h.write_text("\n", encoding="ascii")

            binary = temp / "test_master_sdo"
            subprocess.run(
                [
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
                ],
                cwd=repo_root,
                check=True,
            )
            completed = subprocess.run(
                [str(binary)],
                cwd=repo_root,
                check=True,
                text=True,
                capture_output=True,
            )

        self.assertIn("PASS block-transfer-success-and-ownership", completed.stdout)
        self.assertIn("PASS block-abort-and-start-failures", completed.stdout)
        self.assertIn("PASS queued-cancel-and-teardown-arbitration", completed.stdout)
        self.assertIn("PASS active-cancel-and-completion-pin", completed.stdout)
        self.assertIn("PASS stale-cancel-identity-is-ignored", completed.stdout)
        self.assertIn("Passed 5/5 host SDO cases", completed.stdout)


if __name__ == "__main__":
    unittest.main()
