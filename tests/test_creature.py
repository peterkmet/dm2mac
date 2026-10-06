"""Scenario tests of Step 16 with the emulator oracle: creature groups, the creature AI, projectiles, explosions,
magic, and minions run in the C port and in the original code from start.DAT; then the game state must be equal."""

import ctypes
import struct

import pytest

from test_move import Scenario, gdat, start  # noqa: F401 (gdat and start are fixtures)

CreatureScenario = Scenario  # the scenario harness runs the creatures since Step 16

@pytest.fixture
def creatures(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return CreatureScenario(lib, gdat, start, tmp_path)


def test_activation_after_load(creatures):
    creatures.assert_same("activation")


def test_creature_updates(creatures):
    # The update events of all creatures of the dungeon: moods, plans, actions, animation steps, and the timing of the
    # next update; every 50 steps the save files (the save deactivates and activates the groups again).
    for n in range(300):
        creatures.step()
        creatures.assert_same(n)
        if n % 50 == 49:
            creatures.assert_equal(n)


def creature_sensor_cells(lib, kinds):
    """(map, x, y, cell) of each sensor of the kinds."""
    from test_move import ENDOFLIST
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    out = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                t = lib.dungeon_square_first_thing(map_, x, y)
                while t != ENDOFLIST:
                    if t >> 10 & 15 == 3 and lib.thing_word(t, 2) & 0x7F in kinds:
                        out.append((map_, x, y, t >> 14))
                    t = lib.dungeon_next_thing(t)
    return sorted(set(out))


def test_creature_sensors(creatures, lib):
    # Square events on the sensors that turn creatures (0x42), alert them or make them vanish (0x28, 0x0b), and
    # animate static creatures (0x3a); then the creature updates that follow.
    import random
    from test_move import square_event
    rng = random.Random(16)
    cells = creature_sensor_cells(lib, (0x0B, 0x28, 0x42, 0x3A))
    assert len(cells) > 5
    for n, (map_, x, y, cell) in enumerate(cells):
        for effect in (0, 1, 2):
            square_event(creatures, map_, x, y, cell, effect, rng.randrange(2))
            for _ in range(rng.randrange(1, 4)):
                creatures.step()
            creatures.assert_same((n, map_, x, y, cell, effect))
    creatures.assert_equal("end")


def creature_fronts(lib):
    """(map, x, y, direction) of a free floor square in front of each creature group, the party looking at it."""
    lib.creature_group_at.restype = ctypes.c_uint16
    fronts = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                if lib.creature_group_at(map_, x, y) == 0xFFFF:
                    continue
                for d, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
                    px, py = x - dx, y - dy
                    if 0 <= px < lib.dungeon_map_width(map_) and 0 <= py < lib.dungeon_map_height(map_) \
                            and lib.dungeon_square(map_, px, py) >> 5 == 1 and lib.creature_group_at(map_, px, py) == 0xFFFF:
                        fronts.append((map_, px, py, d))
                        break
    return fronts


MOVE_PUSH_CREATURE = 0x23F9B


CH_CELL, CH_HEALTH, CH_STAMINA, CH_MAX_STAMINA, CH_STRENGTH = 0x1D, 0x36, 0x3A, 0x3C, 0x4C
G_CHAMPION_COUNT = 0x7F276


def random_champions(s, rng):
    """1 to 4 champions (copies of the first) with random strength, stamina, and health (some dead), in both."""
    from test_interface import CHAMPION_SIZE, champion_bytes, champion_records, sync_champions
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


def test_push_creatures(creatures, lib):
    # The party (1 to 4 champions of random strength, stamina, and health) stands in front of creature groups of
    # all maps and pushes them with each push arrow.
    import random
    rng = random.Random(17)
    fronts = creature_fronts(lib)
    assert len(fronts) > 20
    for n, (map_, x, y, d) in enumerate(rng.sample(fronts * 3, min(200, 3 * len(fronts)))):
        random_champions(creatures, rng)
        creatures.place(map_, x, y, d)
        arrow = rng.randrange(6)
        lib.move_push_creature(arrow)
        creatures.emu.call(MOVE_PUSH_CREATURE, arrow)
        creatures.assert_same((n, map_, x, y, d, arrow, "push"))
        for _ in range(rng.randrange(1, 5)):
            creatures.step()
        creatures.assert_same((n, map_, x, y, d, arrow))
    creatures.assert_equal("end")


def test_animated_creature_views(creatures, lib):
    # The views of the creature groups while they run: the frames, the random shift and mirror, and the moves of the
    # active groups must draw the same pictures as the original.
    import random
    from test_world import draw_both
    rng = random.Random(18)
    fronts = creature_fronts(lib)
    shown = 0
    for n, (map_, x, y, d) in enumerate(rng.sample(fronts, min(40, len(fronts)))):
        creatures.place(map_, x, y, d)
        for _ in range(rng.randrange(1, 6)):
            creatures.step()
        creatures.assert_same((n, "steps"))
        mine, original = draw_both(creatures)
        assert mine == original, (n, map_, x, y, d)
        shown += len(set(original)) > 2
    assert shown > 20


def test_merchant_at_the_counter(lib, gdat, tmp_path):
    # shop.DAT: the party stands still in front of the counter of the dwarf merchant (map 28). The merchant puts the
    # item of the shop display on the counter, then waits for an offer: the same actions, sounds, and counter in both.
    from test_move import ROOT
    shop = ROOT / "tests" / "saves" / "shop.DAT"
    if not shop.exists():
        pytest.skip("tests/saves/shop.DAT missing")
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    s = CreatureScenario(lib, gdat, shop.read_bytes(), tmp_path)
    s.place(28, 8, 6, 2)
    for n in range(300):
        s.step()
        s.assert_same(n)
    s.assert_equal("end")
