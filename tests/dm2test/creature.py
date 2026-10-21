"""The creature scenario harness and its random champions."""

import ctypes
import struct

import pytest

from dm2test.scenario import Scenario


CreatureScenario = Scenario  # the scenario harness runs the creatures since Step 16


@pytest.fixture
def creatures(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return CreatureScenario(lib, gdat, start, tmp_path)


CH_CELL, CH_HEALTH, CH_STAMINA, CH_MAX_STAMINA, CH_STRENGTH = 0x1D, 0x36, 0x3A, 0x3C, 0x4C
G_CHAMPION_COUNT = 0x7F276


def random_champions(s, rng):
    """1 to 4 champions (copies of the first) with random strength, stamina, and health (some dead), in both."""
    from dm2test.interface import CHAMPION_SIZE, champion_bytes, champion_records, sync_champions
    lib = s.lib
    first = bytes(champion_records(lib)[:CHAMPION_SIZE])
    count = rng.randrange(1, 5)
    s.game.champion_count = count
    s.emu.write(G_CHAMPION_COUNT, struct.pack("<H", count))
    for champ, cell in enumerate(rng.sample(range(4), count)):
        champion_bytes(lib, champ, 0, first)
        champion_bytes(lib, champ, CH_CELL, [cell])
        max_stamina = rng.randrange(200, 2000)
        champion_bytes(lib, champ, CH_HEALTH, struct.pack("<h", 0 if champ and rng.random() < 0.2 else 50))
        champion_bytes(lib, champ, CH_STAMINA, struct.pack("<hh", rng.randrange(max_stamina + 1), max_stamina))
        champion_bytes(lib, champ, CH_STRENGTH, bytes([rng.randrange(5, 100)] * 2))
    sync_champions(lib, s)
