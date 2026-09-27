#!/usr/bin/env python3

import contextlib
import ctypes
import io
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from zi_test_residue import AGE, Residue


class ResidueTest(unittest.TestCase):
    def setUp(self):
        self.base = tempfile.TemporaryDirectory()
        self.addCleanup(self.base.cleanup)
        self.env = patch.dict(os.environ, ZI_LOGDIR=self.base.name)
        self.env.start()
        self.addCleanup(self.env.stop)

    def test_pass_and_failure(self):
        with Residue("python-residue") as run:
            run.path("output").write_text("pass")
            passed = run.directory
            run.success()
        self.assertFalse(passed.exists())
        with self.assertRaisesRegex(RuntimeError, "failure"):
            with Residue("python-residue") as run:
                run.path("output").write_text("diagnostic")
                failed = run.directory
                raise RuntimeError("failure")
        self.assertEqual((failed / "output").read_text(), "diagnostic")
        self.assertTrue((failed / ".failed").exists())

    def test_aging_and_live_owner(self):
        live = Residue("python-age")
        self.addCleanup(live.finish, True)
        for i in range(AGE + 2):
            run = Residue("python-age")
            run.path("output").write_text(str(i))
            run.finish(False)
        failures = list(Path(self.base.name).glob("python-age.*/.failed"))
        self.assertEqual(len(failures), AGE)
        self.assertTrue(live.directory.exists())
        live.finish(True)

    def test_invalid_names_and_idempotence(self):
        for name in ("", "../escape", "a/b", ".hidden"):
            with self.assertRaises(ValueError):
                Residue(name)
        run = Residue("python-valid")
        with self.assertRaises(ValueError):
            run.path("../escape")
        run.finish(True)
        run.finish(False)

    def test_tmp_dir_and_diagnostics(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            run = Residue("python-temp")
            registry = run.tmp_dir("registry")
            (registry / "pid").write_text("123")
            run.finish(False)
        self.assertTrue(registry.exists())
        self.assertIn(str(registry), out.getvalue())
        # The enclosing test owns its deliberately retained fixture.
        import shutil
        shutil.rmtree(registry)

    def test_tmp_file_and_shared_memory(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            run = Residue("python-shm")
            blocker = run.tmp_file("registry")
            self.assertTrue(blocker.is_file())
            name = run.shm("python-shm-test-" + str(os.getpid()))
            if os.name == "posix":
                libc = ctypes.CDLL(None, use_errno=True)
                fd = libc.shm_open(("/" + name + ".ctrl").encode(),
                                   os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
                self.assertGreaterEqual(fd, 0)
                os.close(fd)
            run.finish(False)
        retained = blocker.with_name(blocker.name + ".failed")
        self.assertTrue(retained.exists())
        self.assertIn(str(retained), out.getvalue())
        if os.name == "posix":
            self.assertFalse(Path("/dev/shm", name + ".ctrl").exists())
        retained.unlink()

    def test_other_process_stays_active(self):
        env = dict(os.environ,
                   PYTHONPATH=str(Path(__file__).resolve().parent))
        child = subprocess.Popen([sys.executable, "-c", """
from zi_test_residue import Residue
import sys
run = Residue('python-parallel')
print(run.directory, flush=True)
sys.stdin.readline()
run.finish(True)
"""], env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
        try:
            live = Path(child.stdout.readline().strip())
            self.assertTrue(live.is_dir())
            for _ in range(AGE + 2):
                run = Residue("python-parallel")
                run.finish(False)
            self.assertTrue((live / ".active").exists())
            child.stdin.write("done\n")
            child.stdin.flush()
            child.communicate(timeout=10)
            self.assertEqual(child.returncode, 0)
            self.assertFalse(live.exists())
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate()

    def test_term_retains_diagnostics(self):
        if not hasattr(signal, "SIGTERM"):
            self.skipTest("SIGTERM unavailable")
        env = dict(os.environ,
                   PYTHONPATH=str(Path(__file__).resolve().parent))
        child = subprocess.run([sys.executable, "-c", """
from zi_test_residue import Residue
import os, signal
with Residue('python-term') as run:
    run.path('output').write_text('interrupted')
    os.kill(os.getpid(), signal.SIGTERM)
"""], env=env, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(child.returncode, 0)
        self.assertIn("# residue:", child.stdout)
        failed = list(Path(self.base.name).glob("python-term.*/output"))
        self.assertEqual(len(failed), 1)
        self.assertEqual(failed[0].read_text(), "interrupted")

    def test_relative_root(self):
        os.environ["ZI_LOGDIR"] = os.path.relpath(self.base.name)
        run = Residue("python-relative")
        self.assertEqual(run.root, Path(self.base.name).resolve())
        run.finish(True)
        self.assertFalse(run.directory.exists())

    def test_tmp_file_aging(self):
        name = "python-file-age-" + str(os.getpid())
        prefix = f"ZiTest.{name}.block."
        base = Path(tempfile.gettempdir())
        try:
            for _ in range(AGE + 2):
                run = Residue(name)
                run.tmp_file("block")
                run.finish(False)
            self.assertEqual(len(list(base.glob(prefix + "*.failed"))), AGE)
        finally:
            for path in base.glob(prefix + "*.failed"):
                path.unlink()


if __name__ == "__main__":
    unittest.main()
