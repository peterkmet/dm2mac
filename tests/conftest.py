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


@pytest.fixture(scope="session")
def lib():
    return ctypes.CDLL(str(ROOT / "build" / "libdm2.dylib"))
