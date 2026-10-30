"""The square kinds of the advanced automap (src/ui/automap.c): the imaginary wall, the pressure pad, and the level squares."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.scenario import Location, Position, square_things
from dm2test.lib import load_lib

FAKE_WALL, FAKE_WALL_OPEN, FLAG_0, FLOOR = C.SQUARE_FAKE_WALL, C.SQUARE_FAKE_WALL_OPEN, 0x01, 1
THING_SENSOR = 3
FLOOR_ANY = 0x01
SENSOR_TYPE_MASK = 0x7F  # the sensor type is bits 0-6 of the type word
STEP_TYPES = (FLOOR_ANY, 2, 3, 4, 7, 8)  # floor sensors that a step or an item triggers
ORNAMENT_SHIFT = 12  # the ornament ordinal is bits 12-15 of the sensor flags (0: none)


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.automap_is_imaginary_wall.argtypes = [ctypes.c_uint8]
    lib.automap_is_imaginary_wall.restype = ctypes.c_bool
    lib.automap_is_pressure_pad.argtypes = [Position]
    lib.automap_is_pressure_pad.restype = ctypes.c_bool
    lib.dungeon_square.restype = ctypes.c_uint8
    lib.thing_word.restype = ctypes.c_uint16
    lib.automap_level_square.argtypes = [Location, ctypes.POINTER(Position)]
    lib.automap_level_square.restype = ctypes.c_bool
    lib.automap_level_seen.argtypes = [Location]
    lib.automap_level_seen.restype = ctypes.c_bool
    lib.seen_mark.argtypes = [Position, ctypes.c_int16]
    lib.seen_is_seen.argtypes = [Position]
    lib.seen_is_seen.restype = ctypes.c_bool
    return lib


def all_squares(lib):
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                yield map_, x, y, lib.dungeon_square(map_, x, y)


def test_imaginary_wall_is_a_closed_fake_wall_with_flag_0(lib):
    for fake in (FAKE_WALL << 5 | bits for bits in range(16)):
        expected = bool(fake & FLAG_0) and not fake & FAKE_WALL_OPEN
        assert lib.automap_is_imaginary_wall(fake) == expected
    assert not lib.automap_is_imaginary_wall(C.SQUARE_WALL << 5 | FLAG_0)
    assert not lib.automap_is_imaginary_wall(FLOOR << 5 | FLAG_0)


def test_dungeon_has_imaginary_walls_that_the_party_walks_through(lib, session):
    found = [s for *_, s in all_squares(lib) if lib.automap_is_imaginary_wall(s)]
    assert found
    assert all(not lib.dun_square_blocks_movement(s) for s in found)


def test_pressure_pad_is_a_floor_trigger_that_the_view_draws(lib, session):
    visible, hidden = [], []
    for map_, x, y, s in all_squares(lib):
        if s >> 5 != FLOOR:
            continue
        for t in square_things(lib, map_, x, y):
            if t >> 10 & 15 == THING_SENSOR and lib.thing_word(t, 2) & SENSOR_TYPE_MASK in STEP_TYPES:
                (visible if lib.thing_word(t, 4) >> ORNAMENT_SHIFT else hidden).append((map_, x, y))
    assert visible and hidden
    assert all(lib.automap_is_pressure_pad(Position(*p)) for p in visible if p not in hidden)
    assert not any(lib.automap_is_pressure_pad(Position(*p)) for p in hidden if p not in visible)


def level_square(lib, x, y):
    p = Position()
    return (p.map, p.x, p.y) if lib.automap_level_square(Location(x, y), ctypes.byref(p)) else None


def test_level_square_takes_the_map_that_has_the_square(lib, session):
    # The owner rule of the original minion map; a position without an owner keeps the square of its first map.
    assert level_square(lib, 22, 44) == (0, 1, 8)    # the party square
    assert level_square(lib, 52, 28) == (40, 6, 6)   # map 12 has SQUARE_UNKNOWN there, map 40 a wall
    assert level_square(lib, 49, 26) == (12, 22, -1)  # SQUARE_UNKNOWN of map 40, the edge wall of map 12
    assert level_square(lib, 23, 0) == (35, 0, 0)    # SQUARE_UNKNOWN of map 35, no owner
    assert level_square(lib, 21, 0) is None          # no map covers it


def test_level_position_is_seen_when_any_covering_map_saw_it(lib, session):
    lib.seen_reset()
    assert not lib.automap_level_seen(Location(52, 28))
    lib.seen_mark(Position(12, 25, 1), C.DIR_NORTH)  # the view marks the square of map 12 that shows map 40
    assert lib.automap_level_seen(Location(52, 28)) and not lib.seen_is_seen(Position(40, 6, 6))
    lib.seen_reset()


def test_edge_wall_in_a_margin_that_no_map_covers(lib, session):
    # The view draws the edge wall of map 12 at (13, -1) (dungeon_square_edge); the automap shows it once a square
    # of map 12 next to it is seen.
    assert level_square(lib, 40, 26) == (12, 13, -1)
    assert level_square(lib, 21, 35) is None  # the margin of map 0 beside no open square: outside
    lib.seen_reset()
    lib.seen_mark(Position(12, 13, 2), C.DIR_SOUTH)  # the squares two rows away
    assert not lib.automap_level_seen(Location(40, 26))
    lib.seen_mark(Position(12, 13, 0), C.DIR_NORTH)  # the square next to the edge wall
    assert lib.automap_level_seen(Location(40, 26))
    lib.seen_reset()


def test_edge_wall_over_an_unknown_square_of_another_map(lib, session):
    # Map 17 has SQUARE_UNKNOWN at the level position (34, 60), the margin of map 24 below (4, 3); the view of map 24
    # draws the edge wall there, so the automap shows it too.
    game, _ = session
    saved = game.party_map, game.party_x, game.party_y
    game.party_map, game.party_x, game.party_y = 24, 4, 3
    try:
        assert level_square(lib, 34, 60) == (24, 4, 4)
        lib.seen_reset()
        lib.seen_mark(Position(24, 4, 3), C.DIR_SOUTH)
        assert lib.automap_level_seen(Location(34, 60))
    finally:
        game.party_map, game.party_x, game.party_y = saved
        lib.seen_reset()
