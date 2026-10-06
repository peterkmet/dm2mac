import ctypes
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
    # records the git tree of a green full run of a clean checkout; scripts/test-suite skips a rerun of that tree
    if hasattr(session.config, "workerinput") or exitstatus != 0 or session.config.args != ["tests"]:
        return
    git = lambda *a: subprocess.run(["git", *a], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    if git("status", "--porcelain", "--untracked-files=no"):
        return
    with open(Path(git("rev-parse", "--path-format=absolute", "--git-common-dir")) / "green-trees", "a") as f:
        f.write(git("rev-parse", "HEAD^{tree}") + "\n")


@pytest.fixture(scope="session")
def lib():
    return ctypes.CDLL(str(ROOT / "build" / "libdm2.dylib"))
