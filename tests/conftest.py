from pathlib import Path

import pytest


@pytest.fixture
def game_dir():
    path = Path(__file__).resolve().parent.parent / "game"
    if not (path / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return path
