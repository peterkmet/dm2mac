"""Smooth projectile flight of the advanced mode (src/ui/view.c, src/ui/play.c): the views between game steps draw
each projectile part of the way from its place of the game step before; they change no game state."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import MainLoop, session  # noqa: F401 (session is a fixture)
from dm2test.lib import load_lib
from dm2test.modes import ADVANCED, VANILLA, advanced, set_mode  # noqa: F401 (advanced is a fixture)
from dm2test.scenario import gdat, start  # noqa: F401 (gdat and start are fixtures)
from dm2test.view import Bitmap


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.view_flight_shift.restype = ViewPoint
    lib.view_flight_shift.argtypes = [ViewPoint, ViewPoint, ctypes.c_int, ctypes.c_int]
    return lib


class ViewPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16)]


class ProjectileLaunch(ctypes.Structure):
    _fields_ = [("payload", ctypes.c_uint16), ("x", ctypes.c_int16), ("y", ctypes.c_int16), ("cell", ctypes.c_int16),
                ("dir", ctypes.c_int16), ("energy", ctypes.c_uint8), ("hit_chance", ctypes.c_uint8),
                ("step", ctypes.c_uint8), ("from_sensor", ctypes.c_bool)]


STEP = 8  # timer ticks of a game step
STEP_EAST, STEP_NORTH = (0, 1, 0, -1), (-1, 0, 1, 0)  # the square ahead by direction (y grows to the south)


@pytest.mark.parametrize("tick, expected", [(0, (-40, 16)), (2, (-30, 12)), (4, (-20, 8)), (STEP, (0, 0)), (STEP + 3, (0, 0))])
def test_flight_shift_goes_from_the_old_corner_to_the_new_one(lib, tick, expected):
    shift = lib.view_flight_shift(ViewPoint(60, 30), ViewPoint(100, 14), tick, STEP)
    assert (shift.x, shift.y) == expected


def test_flight_shift_without_an_old_corner_or_a_step_is_none(lib):
    for old, ticks in ((ViewPoint(100, 14), STEP), (ViewPoint(60, 30), 0)):
        shift = lib.view_flight_shift(old, ViewPoint(100, 14), 0, ticks)
        assert (shift.x, shift.y) == (0, 0)


def snapshot(lib, gdat, folder, n):
    """The whole game state as save file bytes; the random seed that the write takes stays. The second write gives the
    bytes: the first write after a projectile move can still change a byte of the save image."""
    seed = ctypes.c_uint32.in_dll(lib, "g_random_seed")
    kept = seed.value
    for k in range(2):
        path = folder / f"S{n}_{k}.DAT"
        assert lib.save_write(str(path).encode(), str(folder / f"S{n}_{k}.BAK").encode(), b"FLIGHT", gdat) == 0
        seed.value = kept
    return path.read_bytes()


def test_flight_views_move_the_projectile_and_change_no_game_state(lib, gdat, advanced, tmp_path):  # noqa: F811
    game, _ = advanced
    loop = MainLoop.in_dll(lib, "g_main_loop")
    view = Bitmap.in_dll(lib, "g_view_viewport")
    size = C.VIEWPORT_WIDTH * C.VIEWPORT_HEIGHT
    for direction in range(C.DIR_COUNT):  # a direction with open floor ahead
        x, y = game.party_x + STEP_EAST[direction], game.party_y + STEP_NORTH[direction]
        if lib.dungeon_square(game.party_map, x, y) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_FLOOR:
            break
    else:
        pytest.skip("no open floor next to the party")
    game.party_dir = direction
    n = 0
    for mode in (VANILLA, ADVANCED):
        set_mode(lib, mode)
        lib.play_tick()
        lib.projectile_create(ctypes.byref(ProjectileLaunch(
            C.SPELL_FIREBALL, game.party_x, game.party_y, direction, direction, 255, 100, 10)))
        changes = 0
        for _ in range(3 * STEP):
            seed = ctypes.c_uint32.in_dll(lib, "g_random_seed").value
            before, pixels = snapshot(lib, gdat, tmp_path, n), bytes(view.pixels[:size])
            lib.play_tick()
            n += 1
            if loop.tick_count == 0:  # a game step ran
                continue
            assert ctypes.c_uint32.in_dll(lib, "g_random_seed").value == seed
            assert snapshot(lib, gdat, tmp_path, n) == before
            n += 1
            changes += bytes(view.pixels[:size]) != pixels
        if mode == VANILLA:
            assert changes == 0, "the vanilla view changes only at a game step"
        else:
            assert changes > 3, "the advanced view moves the fireball between game steps"
        for _ in range(30 * STEP):  # the fireball explodes and goes away
            lib.play_tick()
