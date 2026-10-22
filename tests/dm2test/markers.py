"""Paths of the project and the skip marker for tests that need the original game files."""

from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
GAME = ROOT / "game"
SAVES = ROOT / "tests" / "saves"

needs_game = pytest.mark.skipif(not (GAME / "SKULL.EXE").exists(), reason="game/SKULL.EXE missing")
