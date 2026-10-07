import ctypes
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
        subprocess.run(["make", "-C", "port"], cwd=ROOT, check=True, capture_output=True)


def pytest_sessionfinish(session, exitstatus):
    # records the git tree of a green full run of a clean checkout; tools/test-suite skips a rerun of that tree
    if hasattr(session.config, "workerinput") or exitstatus != 0 or session.config.args != ["tests"]:
        return
    git = lambda *a: subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    if git("status", "--porcelain", "--untracked-files=no"):
        return
    with open(Path(git("rev-parse", "--path-format=absolute", "--git-common-dir")) / "green-trees", "a") as f:
        f.write(git("rev-parse", "HEAD^{tree}") + "\n")


@pytest.fixture(scope="module")
def lib(tmp_path_factory):
    # a copy per test module gives each module fresh globals; dlopen of one path returns one shared image
    path = tmp_path_factory.mktemp("lib") / "libdm2.dylib"
    shutil.copyfile(ROOT / "build" / "libdm2.dylib", path)
    lib = ctypes.CDLL(str(path))
    # a C bool fills only the low byte of the return register
    for header in (ROOT / "port" / "src").rglob("*.h"):
        for name in re.findall(r"^bool (\w+)\(", header.read_text(), re.M):
            getattr(lib, name).restype = ctypes.c_bool
    return lib
