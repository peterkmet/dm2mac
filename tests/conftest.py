import ctypes
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent


@pytest.fixture
def game_dir():
    path = ROOT / "game"
    if not (path / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return path


def pytest_configure(config):
    # builds once in the main process, before the xdist workers start
    if not hasattr(config, "workerinput"):
        sdl = subprocess.run(["brew", "--prefix", "sdl3"], capture_output=True, text=True)
        have_sdl = sdl.returncode == 0 and (Path(sdl.stdout.strip()) / "include/SDL3/SDL.h").exists()
        result = subprocess.run(["make", "all" if have_sdl else "lib"], cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            raise pytest.UsageError("make failed:\n" + result.stderr[-4000:])


def pytest_sessionfinish(session, exitstatus):
    # records the git tree of a green full run of a clean checkout; tools/test-suite skips a rerun of that tree
    if hasattr(session.config, "workerinput") or exitstatus != 0 or session.config.args != ["tests"]:
        return
    git = lambda *a: subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    if git("status", "--porcelain", "--untracked-files=no"):
        return
    with open(Path(git("rev-parse", "--path-format=absolute", "--git-common-dir")) / "green-trees", "a") as f:
        f.write(git("rev-parse", "HEAD^{tree}") + "\n")


# full is a c_void_p: a c_char_p argument reaches the callback as a copy in a bytes object
FULL_PATH = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t)


@FULL_PATH
def full_path(path, full, size):
    # the full path hook of data/os.c, which main.c sets with realpath
    resolved = os.path.realpath(path)
    if len(resolved) >= size:
        return False
    ctypes.memmove(full, resolved + b"\0", len(resolved) + 1)
    return True


@pytest.fixture(scope="module")
def lib(tmp_path_factory):
    # a copy per test module gives each module fresh globals; dlopen of one path returns one shared image
    path = tmp_path_factory.mktemp("lib") / "libdm2.dylib"
    shutil.copyfile(ROOT / "build" / "libdm2.dylib", path)
    lib = ctypes.CDLL(str(path))
    # a C bool fills only the low byte of the return register
    for header in (ROOT / "src").rglob("*.h"):
        for name in re.findall(r"^bool (\w+)\(", header.read_text(), re.M):
            getattr(lib, name).restype = ctypes.c_bool
    ctypes.c_void_p.in_dll(lib, "g_os_full_path_hook").value = ctypes.cast(full_path, ctypes.c_void_p).value
    return lib
