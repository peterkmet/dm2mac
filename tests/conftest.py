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


@pytest.fixture(scope="session")
def lib():
    subprocess.run(["make", "-C", "port"], cwd=ROOT, check=True, capture_output=True)
    return ctypes.CDLL(str(ROOT / "build" / "libdm2.dylib"))
