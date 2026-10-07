import csv
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
CODE_END = 0x70000  # data object starts here
SUBSYSTEMS = {
    "runtime library", "memory", "file I/O", "graphics", "palette", "text", "input", "sound", "music",
    "dungeon data", "dungeon view", "outdoor view", "weather", "day and night", "interface", "champions",
    "inventory", "items", "money and shops", "creatures", "minions", "projectiles", "magic", "timeline",
    "Skullkeep machines", "main loop", "save and load", "start menu", "end of game",
}


@pytest.fixture(scope="module")
def rows():
    with open(ROOT / "docs/skull-symbols.csv", newline="") as f:
        return list(csv.DictReader(f))


def test_columns_and_values(rows):
    assert list(rows[0]) == ["address", "name", "signature", "subsystem"]
    for r in rows:
        assert re.fullmatch(r"0x[0-9a-f]{8}", r["address"]), r
        assert re.fullmatch(r"[A-Za-z_]\w*", r["name"]), r
        assert r["subsystem"] in SUBSYSTEMS, r


def test_unique_and_sorted(rows):
    addresses = [int(r["address"], 16) for r in rows]
    assert addresses == sorted(set(addresses))
    names = [r["name"] for r in rows]
    assert len(names) == len(set(names))


def test_functions_match_decomp(rows):
    decomp = ROOT / "reference/decomp/SKULL.c"
    if not decomp.exists():
        pytest.skip("reference/decomp/SKULL.c missing")
    starts = {int(a, 16) for a in re.findall(r"^/\* ([0-9a-f]{8}) \*/$", decomp.read_text(), re.M)}
    functions = {int(r["address"], 16) for r in rows if int(r["address"], 16) < CODE_END}
    assert functions == starts
