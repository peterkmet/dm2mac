"""The square kinds of the advanced automap (src/ui/automap.c): the imaginary wall and the pressure pad."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import session  # noqa: F401 (session is a fixture)
from dm2test.scenario import gdat, start, square_things  # noqa: F401 (gdat and start are fixtures)
from dm2test.lib import load_lib

FAKE_WALL, FAKE_WALL_OPEN, FLAG_0, FLOOR = C.SQUARE_FAKE_WALL, C.SQUARE_FAKE_WALL_OPEN, 0x01, 1
THING_SENSOR = 3
FLOOR_ANY = 0x01
WALL_SENSOR_TYPES = 0x7F  # the sensor type is bits 0-6 of the type word


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.automap_is_imaginary_wall.argtypes = [ctypes.c_uint8]
    lib.automap_is_imaginary_wall.restype = ctypes.c_bool
    lib.automap_is_pressure_pad.argtypes = [ctypes.c_int16] * 3
    lib.automap_is_pressure_pad.restype = ctypes.c_bool
    lib.dungeon_square.restype = ctypes.c_uint8
    lib.thing_word.restype = ctypes.c_uint16
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


def test_dungeon_has_imaginary_walls_that_the_party_walks_through(lib, session):  # noqa: F811
    found = [s for *_, s in all_squares(lib) if lib.automap_is_imaginary_wall(s)]
    assert found
    assert all(not lib.dun_square_blocks_movement(s) for s in found)


def test_pressure_pad_is_a_floor_square_with_a_stepping_sensor(lib, session):  # noqa: F811
    pads, others = [], []
    for map_, x, y, s in all_squares(lib):
        kinds = [lib.thing_word(t, 2) & WALL_SENSOR_TYPES for t in square_things(lib, map_, x, y)
                 if t >> 10 & 15 == THING_SENSOR]
        if s >> 5 == FLOOR and any(k in (FLOOR_ANY, 2, 3, 7, 8) for k in kinds):
            pads.append((map_, x, y))
        elif s >> 5 == FLOOR and not kinds:
            others.append((map_, x, y))
    assert pads and others
    assert all(lib.automap_is_pressure_pad(*p) for p in pads)
    assert not any(lib.automap_is_pressure_pad(*p) for p in others[:200])
