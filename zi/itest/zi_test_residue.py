"""Owned residue for Python integration tests.

Call ``success()`` only after every TAP assertion has passed.  Files from a
failed run are retained; registered shared memory is always unlinked.
"""

import atexit
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path
import re
import shutil
import signal
import tempfile

try:
    import fcntl
except ImportError:  # Windows
    fcntl = None
    import msvcrt


AGE = 8
_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")


def _name(value):
    if not isinstance(value, str) or not _NAME.fullmatch(value) or ".." in value:
        raise ValueError(f"invalid residue name: {value!r}")
    return value


def _root():
    root = Path(os.environ.get("ZI_LOGDIR") or os.getcwd()).resolve()
    root.mkdir(parents=True, exist_ok=True)
    return root


def _pid_alive(pid):
    if os.name == "posix":
        try:
            os.kill(pid, 0)
            return True
        except PermissionError:
            return True
        except ProcessLookupError:
            return False
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    open_process = kernel.OpenProcess
    open_process.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    open_process.restype = ctypes.c_void_p
    handle = open_process(0x1000, False, pid)
    if not handle:
        return ctypes.get_last_error() == 5
    try:
        status = ctypes.c_ulong()
        get_exit = kernel.GetExitCodeProcess
        get_exit.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
        return bool(get_exit(handle, ctypes.byref(status))) and status.value == 259
    finally:
        close_handle = kernel.CloseHandle
        close_handle.argtypes = [ctypes.c_void_p]
        close_handle(handle)


def _locked(file, exclusive=True, blocking=True):
    if fcntl is not None:
        mode = fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH
        if not blocking:
            mode |= fcntl.LOCK_NB
        fcntl.flock(file if isinstance(file, int) else file.fileno(), mode)
    else:
        file.seek(0)
        msvcrt.locking(file.fileno(),
                       msvcrt.LK_LOCK if blocking else msvcrt.LK_NBLCK, 1)


@contextmanager
def _root_guard(root):
    if fcntl is not None:
        guard = os.open(root, os.O_RDONLY)
    else:
        guard = (root / ".zi-residue.lock").open("a+b")
        if guard.tell() == 0:
            guard.write(b"\0")
            guard.flush()
    try:
        _locked(guard)
        yield
    finally:
        if fcntl is not None:
            os.close(guard)
        else:
            guard.close()


def _age(root, prefix):
    """Remove old finished runs; a locked .active marker means still running."""
    with _root_guard(root):
        stale = []
        for child in root.iterdir():
            if not child.name.startswith(prefix) or child.is_symlink():
                continue
            if child.is_file():
                if child.name.endswith(".failed"):
                    stale.append(child)
                else:
                    pid_text = child.name[len(prefix):].split(".", 1)[0]
                    if (pid_text.isdigit() and int(pid_text) != os.getpid()
                            and not _pid_alive(int(pid_text))):
                        failed = child.with_name(child.name + ".failed")
                        child.rename(failed)
                        stale.append(failed)
                continue
            if not child.is_dir():
                continue
            active = child / ".active"
            if active.exists():
                with active.open("rb") as marker:
                    try:
                        _locked(marker, blocking=False)
                    except (BlockingIOError, OSError):
                        continue
                active.rename(child / ".failed")
            if (child / ".failed").exists():
                stale.append(child)
        stale.sort(key=lambda p: p.stat().st_mtime_ns, reverse=True)
        for child in stale[AGE:]:
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()


class Residue:
    def __init__(self, name):
        self.name = _name(name)
        self._pid = os.getpid()
        self.root = _root()
        self._passed = False
        self._done = False
        self._shm = []
        self._tmp = []
        self._tmp_files = []
        self._tmp_prefix = {}
        self._signals = {}
        prefix = self.name + "."
        self.directory = Path(tempfile.mkdtemp(prefix=prefix, dir=self.root))
        self._marker = (self.directory / ".active").open("wb")
        self._marker.write(str(os.getpid()).encode())
        self._marker.flush()
        self._marker.seek(0)
        _locked(self._marker)
        _age(self.root, prefix)
        atexit.register(self.finish, False)
        if hasattr(signal, "SIGTERM"):
            try:
                self._signals[signal.SIGTERM] = signal.getsignal(signal.SIGTERM)
                signal.signal(signal.SIGTERM, self._term)
            except ValueError:
                pass  # A worker thread cannot install process signal handlers.

    def path(self, name):
        return self.directory / _name(name)

    def dir(self, name):
        path = self.path(name)
        path.mkdir()
        return path

    def tmp_dir(self, tag):
        prefix = f"ZiTest.{self.name}.{_name(tag)}."
        base = Path(tempfile.gettempdir()).resolve()
        path = Path(tempfile.mkdtemp(prefix=prefix, dir=base))
        marker = (path / ".active").open("wb")
        marker.write(str(os.getpid()).encode())
        marker.flush()
        marker.seek(0)
        _locked(marker)
        self._tmp.append((path, marker))
        self._tmp_prefix[path] = prefix
        _age(base, prefix)
        return path

    def tmp_file(self, tag):
        prefix = f"ZiTest.{self.name}.{_name(tag)}."
        base = Path(tempfile.gettempdir()).resolve()
        fd, name = tempfile.mkstemp(prefix=f"{prefix}{self._pid}.", dir=base)
        os.close(fd)
        path = Path(name)
        self._tmp_files.append((path, prefix))
        _age(base, prefix)
        return path

    def shm(self, name):
        name = _name(name)
        if name in self._shm:
            raise ValueError(f"duplicate shared-memory name: {name}")
        self._shm.append(name)
        return name

    def success(self):
        self._passed = True

    def finish(self, passed):
        if os.getpid() != self._pid:
            return
        if self._done:
            return
        self._done = True
        for signum, previous in self._signals.items():
            signal.signal(signum, previous)
        self._signals.clear()
        error = None
        if os.name == "posix":
            unlink = ctypes.CDLL(None, use_errno=True).shm_unlink
            unlink.argtypes = [ctypes.c_char_p]
            for name in self._shm:
                for suffix in (".ctrl", ".data"):
                    result = unlink(("/" + name + suffix).encode())
                    if result and ctypes.get_errno() != 2:
                        error = OSError(f"shm_unlink failed: {name}{suffix}")
        for path, marker in self._tmp + [(self.directory, self._marker)]:
            try:
                with _root_guard(path.parent):
                    marker.close()
                    if passed:
                        shutil.rmtree(path)
                    else:
                        (path / ".active").rename(path / ".failed")
                        os.utime(path)
                        print(f"# residue: {path}", flush=True)
            except OSError as exc:
                error = exc
        for path, prefix in self._tmp_files:
            try:
                with _root_guard(path.parent):
                    if passed:
                        path.unlink()
                    else:
                        path.rename(path.with_name(path.name + ".failed"))
                        print(f"# residue: {path}.failed", flush=True)
            except OSError as exc:
                error = exc
        if not passed:
            _age(self.root, self.name + ".")
            for path, _ in self._tmp:
                _age(path.parent, self._tmp_prefix[path])
            for path, prefix in self._tmp_files:
                _age(path.parent, prefix)
        if error:
            if passed:
                raise error
            print(f"# residue cleanup error: {error}", flush=True)

    def __enter__(self):
        return self

    @staticmethod
    def _term(signum, frame):
        raise SystemExit(128 + signum)

    def __exit__(self, typ, value, trace):
        try:
            self.finish(self._passed and typ is None)
        except OSError:
            if typ is None:
                raise
        return False
