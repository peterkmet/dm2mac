"""Scenario tests with the emulator oracle: start.DAT goes into the C port and into the memory of the
original code; both run the same commands and timeline events; then the game state must be equal."""

import ctypes
import struct

import pytest

from dm2test import constants as C
from dm2test.lib import load_lib
from dm2test.scenario import (
    CHAMPION_CELL, COMMAND_EXECUTE, COMMAND_RECORD, DIRS, ENDOFLIST, G_CHAMPION_COUNT, NONE, SKY_OBJECTS_SIZE,
    Command, Location, approaches, sensor_squares, square_event, square_things, squares,
)


@pytest.fixture(scope="module")
def lib():
    return load_lib()


def test_states_equal_after_load(scenario):
    scenario.assert_equal("load")


def test_walk_and_turn_at_start(scenario):
    # The start room: forward, turns, side steps, and moves into walls (bumps).
    F, L, R, B, SL, SR = C.CMD_MOVE_FORWARD, C.CMD_TURN_LEFT, C.CMD_TURN_RIGHT, C.CMD_MOVE_BACK, C.CMD_MOVE_LEFT, C.CMD_MOVE_RIGHT
    sequence = [F, F, F, R, F, F, L, L, SL, B, B, SR, F, F, F, F, R, F, F, F, L, F, F]
    for i, command in enumerate(sequence):
        for _ in range(4):
            scenario.step()
        mine, theirs = scenario.command(command)
        assert mine == theirs, (i, command)
        scenario.assert_equal((i, command))


def test_random_walks_on_all_maps(scenario, lib):
    # Random places on all maps, random moves and turns, and square events on the squares around the party:
    # walls, floors, doors, pits, teleporters, stairs, fake walls, and their sensors.
    import random
    rng = random.Random(14)
    places = squares(lib, (1, 2, 3, 4, 5, 6))
    for n in range(150):
        map_, x, y, _ = rng.choice(places)
        scenario.place(map_, x, y, rng.randrange(C.DIR_COUNT))
        for i in range(8):
            for _ in range(rng.randrange(1, 4)):
                scenario.step()
            if rng.random() < 0.3:
                m = scenario.game.party_map
                ex = scenario.game.party_x + rng.randrange(-2, 3)
                ey = scenario.game.party_y + rng.randrange(-2, 3)
                if 0 <= ex < lib.dungeon_map_width(m) and 0 <= ey < lib.dungeon_map_height(m):
                    square_event(scenario, m, ex, ey, rng.randrange(4), rng.randrange(4), rng.randrange(3))
            command = rng.choice((C.CMD_TURN_LEFT, C.CMD_TURN_RIGHT, C.CMD_MOVE_FORWARD, C.CMD_MOVE_FORWARD,
                              C.CMD_MOVE_FORWARD, C.CMD_MOVE_RIGHT, C.CMD_MOVE_BACK, C.CMD_MOVE_LEFT))
            mine, theirs = scenario.command(command)
            assert mine == theirs, (n, i, command)
            scenario.assert_same((n, map_, x, y, i, command))
        if n % 10 == 9:
            scenario.assert_equal((n, map_, x, y))  # the save files: the champions and the game record too


def test_bump_damage_hits_the_champions_of_the_bump_side(scenario, lib):
    # Four champions (copies of the first) in random cells, random places and directions, moves in all four
    # directions: on a bump into a wall the champions of the two cells on that side take the damage.
    import random
    from dm2test.interface import CHAMPION_SIZE, champion_bytes, champion_records, sync_champions
    rng = random.Random(15)
    first = bytes(champion_records(lib)[:CHAMPION_SIZE])
    scenario.game.champion_count = C.CHAMPION_MAX
    scenario.emu.write(G_CHAMPION_COUNT, struct.pack("<H", C.CHAMPION_MAX))
    places = squares(lib, (1,))
    bumps = 0
    for n in range(200):
        for champ, cell in enumerate(rng.sample(range(C.CELL_COUNT), C.CHAMPION_MAX)):
            champion_bytes(lib, champ, 0, first)
            champion_bytes(lib, champ, CHAMPION_CELL, [cell])
        sync_champions(lib, scenario)
        map_, x, y, _ = rng.choice(places)
        scenario.place(map_, x, y, rng.randrange(C.DIR_COUNT))
        command = rng.choice((C.CMD_MOVE_FORWARD, C.CMD_MOVE_RIGHT, C.CMD_MOVE_BACK, C.CMD_MOVE_LEFT))
        mine, theirs = scenario.command(command)
        assert mine == theirs, (n, command)
        bumps += any(scenario.pending()[0][:4])
        for _ in range(4):
            scenario.step()
        scenario.assert_same((n, map_, x, y, command))
    assert bumps > 25


@pytest.mark.parametrize("part", range(4))
def test_square_events_on_all_sensors(scenario, lib, part):
    # Every sensor and text cell of the dungeon gets square events with each effect; the timeline then runs the
    # relays, timers, counters, animations, doors, pits, teleporters, and fake walls that they reach.
    import random
    rng = random.Random(part)
    cells = sensor_squares(lib)[part::4]
    for n, (map_, x, y, cell) in enumerate(cells):
        square_event(scenario, map_, x, y, cell, rng.randrange(4), rng.randrange(2))
        for _ in range(rng.randrange(1, 3)):
            scenario.step()
        scenario.assert_same((n, map_, x, y, cell))
        if n % 100 == 99:
            scenario.assert_equal((n, map_, x, y, cell))
    for _ in range(40):
        scenario.step()
    scenario.assert_equal("end")


@pytest.mark.parametrize("kind, condition, name", [
    (C.SQUARE_STAIRS, lambda s: True, "stairs"), (C.SQUARE_PIT, lambda s: s & C.SQUARE_PIT_OPEN, "open pits"),
    (C.SQUARE_TELEPORTER, lambda s: s & C.SQUARE_TELEPORTER_ON, "active teleporters"),
    (C.SQUARE_DOOR, lambda s: True, "doors"), (C.SQUARE_FAKE_WALL, lambda s: True, "fake walls")])
def test_step_into_special_squares(scenario, lib, kind, condition, name):
    # The party steps onto stairs, open pits, active teleporters, doors, and fake walls, then turns and steps on.
    places = approaches(lib, kind, condition)
    assert places, name
    moved = 0
    for n, (map_, x, y, d) in enumerate(places):
        scenario.place(map_, x, y, d)
        for command in (C.CMD_MOVE_FORWARD, C.CMD_TURN_LEFT, C.CMD_MOVE_FORWARD, C.CMD_TURN_RIGHT, C.CMD_TURN_RIGHT,
                        C.CMD_MOVE_FORWARD):
            for _ in range(3):
                scenario.step()
            mine, theirs = scenario.command(command)
            assert mine == theirs, (name, n, command)
            scenario.assert_same((name, n, map_, x, y, d, command))
        moved += (scenario.game.party_map, scenario.game.party_x, scenario.game.party_y) != (map_, x, y)
        if n % 20 == 19:
            scenario.assert_equal((name, n))
    assert moved > len(places) // 3, name


SENSOR_PROCESS_WALL_CLICK = 0x4C134


def mirror(lib, map_, x, y, cell):
    """True for a champion mirror (sensor kind 0x7e) on the cell of a wall."""
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    t = lib.dungeon_square_first_thing(map_, x, y)
    while t != ENDOFLIST:
        if t >> 10 & C.THING_TYPE_MASK == C.THING_SENSOR and t >> C.THING_CELL_SHIFT == cell \
                and lib.thing_word(t, 2) & 0x7F == C.WALL_CLICK_PORTRAIT:
            return True
        t = lib.dungeon_next_thing(t)
    return False


@pytest.mark.parametrize("part", range(2))
def test_wall_clicks_on_all_wall_cells(scenario, lib, part):
    # A click with an empty hand on every wall cell with a sensor or a text: buttons, switches, counters, item
    # holders, ladders, fountains; then the timeline runs what they trigger.
    lib.sensor_process_wall_click.argtypes = [Location, ctypes.c_int] + [ctypes.c_uint16] * 2
    cells = [c for c in sensor_squares(lib) if lib.dungeon_square(c[0], c[1], c[2]) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_WALL][part::2]
    for n, (map_, x, y, cell) in enumerate(cells):
        if mirror(lib, map_, x, y, cell):  # the champion of a mirror joins the party that stands in front of it
            scenario.place(map_, x + DIRS[cell][0], y + DIRS[cell][1], (cell + 2) & 3)
        else:
            scenario.place(map_, x, y, (cell + 2) & 3)
        lib.sensor_process_wall_click(Location(x, y), cell, NONE, NONE)
        scenario.emu.call(SENSOR_PROCESS_WALL_CLICK, x, y, cell, NONE, NONE)
        scenario.assert_same(("click", n, map_, x, y, cell))
        for _ in range(3):
            scenario.step()
        scenario.assert_same(("steps", n, map_, x, y, cell))
        if n % 100 == 99:
            scenario.assert_equal(n)
    scenario.assert_equal("end")


def test_clicks_in_the_view(scenario, lib):
    # The party faces walls with sensors (and doors); both draw the view, then a click goes to the center of each
    # click zone of the view (wall ornaments, door buttons, objects) and to the front wall.
    from dm2test import view as tv
    scenario.lib.view_draw_dungeon.argtypes = [Location, ctypes.c_int16]
    zones = (ctypes.c_uint8 * (C.VIEW_CLICK_ZONE_MAX * 12)).in_dll(lib, "g_view_click_zones")
    count = ctypes.c_int.in_dll(lib, "g_view_click_zone_count")
    seed = ctypes.c_uint32.in_dll(lib, "g_random_seed")
    places = []
    for map_, x, y, cell in sensor_squares(lib):
        if lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT:
            continue
        px, py = x + DIRS[cell][0], y + DIRS[cell][1]
        if 0 <= px < lib.dungeon_map_width(map_) and 0 <= py < lib.dungeon_map_height(map_) \
                and lib.dungeon_square(map_, px, py) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_FLOOR:
            places.append((map_, px, py, (cell + 2) & 3))
    places += [(m, x, y, d) for m, x, y, d in approaches(lib, C.SQUARE_DOOR, limit=40)]
    clicked = 0
    for n, (map_, x, y, d) in enumerate(places[::2]):
        scenario.place(map_, x, y, d)
        lib.weather_sky_objects.restype = ctypes.c_void_p
        sky = ctypes.string_at(lib.weather_sky_objects(), SKY_OBJECTS_SIZE)  # the weather of the C port in both
        scenario.view.draw(map_, x, y, d, ctypes.c_int.in_dll(lib, "g_view_light_level").value, sky)
        seed.value = tv.VIEW_SEED
        lib.view_draw_dungeon(Location(x, y), d)
        n_zones = count.value
        raw = bytes(zones)
        targets = []
        for i in range(n_zones):
            zx, zy, zw, zh = struct.unpack_from("<4h", raw, i * 12)
            if raw[i * 12 + 11]:
                targets.append((zx + zw // 2, zy + zh // 2 + 40))
        targets.append((112, 40 + 60))  # the middle of the view: the front wall or a creature
        for tx, ty in targets:
            c = Command(tx, ty, C.CMD_VIEWPORT)
            scenario.emu.write(COMMAND_RECORD, bytes(c))
            mine = lib.command_execute(ctypes.byref(c))
            theirs = scenario.emu.call(COMMAND_EXECUTE, COMMAND_RECORD)
            assert mine == theirs, (n, map_, x, y, d, tx, ty)
            scenario.assert_same(("click", n, map_, x, y, d, tx, ty))
            clicked += 1
        for _ in range(3):
            scenario.step()
        scenario.assert_same(("steps", n, map_, x, y, d))
        if n % 50 == 49:
            scenario.assert_equal(n)
    assert clicked > 100
    scenario.assert_equal("end")


def test_teleporter_square_whose_first_thing_is_no_teleporter(scenario, lib):
    # A damaged dungeon (port only): a chest with the target words of the teleporter goes before the things of an
    # active party teleporter square; the party steps onto the square and stays there.
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    party_scope = (C.TELEPORTER_SCOPE_MASK & ~C.TELEPORTER_SCOPE_CREATURES) << C.TELEPORTER_SCOPE_SHIFT
    for map_, px, py, d in approaches(lib, C.SQUARE_TELEPORTER, lambda s: s & C.SQUARE_TELEPORTER_ON):
        x, y = px + DIRS[d][0], py + DIRS[d][1]
        tele = lib.dungeon_square_first_thing(map_, x, y)
        target = lib.thing_word(tele, C.TELEPORTER_TARGET)
        if target & party_scope and (target & C.LOCATION_COORD_MASK, target >> C.LOCATION_Y_SHIFT & C.LOCATION_COORD_MASK) != (x, y):
            break
    else:
        pytest.fail("no active party teleporter")
    chest = lib.dungeon_unused_thing(C.THING_CONTAINER)
    for field in (C.TELEPORTER_TARGET, C.TELEPORTER_MAP):
        lib.thing_set_word(chest, field, lib.thing_word(tele, field))
    things = square_things(lib, map_, x, y)
    for t in things:
        lib.dungeon_unlink_thing_from_square(t, map_, x, y)
    for t in [chest] + things:
        lib.dungeon_link_thing_to_square(t, map_, x, y)
    assert lib.dungeon_square_first_thing(map_, x, y) == chest
    scenario.place(map_, px, py, d)
    for _ in range(3):
        scenario.step()
    lib.command_execute(ctypes.byref(Command(0, 0, C.CMD_MOVE_FORWARD)))
    for _ in range(3):
        scenario.step()
    game = scenario.game
    assert (game.party_map, game.party_x, game.party_y) == (map_, x, y)


def test_discard_search_on_a_dungeon_of_one_map(tmp_path):
    # regression: the search for a discarded record looped forever when the party map is the only map
    own = load_lib()  # an own dungeon of one map; the scenario keeps start.DAT in the module library
    counts = [0] * C.THING_TYPE_COUNT
    header = struct.pack("<HHBBHHH16H", 0, 1, 1, 0, 0, 0, 0, *counts)  # map data of 1 byte, 1 map, no square things
    map_desc = bytes(16)  # map data at offset 0, 1 x 1 squares, level 0
    path = tmp_path / "one_map.dat"
    path.write_bytes(header + map_desc + struct.pack("<H", 0) + b"\0")  # column 0 starts at thing 0; a wall
    assert own.dungeon_load(str(path).encode()) == 1
    assert own.dungeon_map_count() == 1
    own.dun_get_discarded_thing.restype = ctypes.c_uint16
    assert own.dun_get_discarded_thing(C.THING_WEAPON, C.DISCARD_ANY_KIND) == C.THING_NONE
