"""The push arrow under the mouse (src/ui/input.c): it stays pressed in the arrow panel while the mouse selects it."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import run_ticks
from dm2test.scenario import Party
from dm2test.lib import load_lib
from dm2test.view import Bitmap

KEY_FORWARD, KEY_TURN_LEFT, KEY_TURN_RIGHT = C.KEYPAD_FORWARD, C.KEYPAD_TURN_LEFT, C.KEYPAD_TURN_RIGHT
LEFT_ARROW_ZONE = C.ZONE_ARROW_FIRST  # the zone of PUSH_ARROW_LEFT
MOUSE_LEFT, MOUSE_MIDDLE = (20, 101), (112, 101)  # left of the viewport middle, and the middle without an arrow


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    return lib


def keys(lib, *keys):
    for key in keys:
        lib.input_key_down(key)
        run_ticks(lib, 40 if key == KEY_FORWARD else 16)


def click(lib, x, y):
    lib.input_mouse_click(x, y, C.INPUT_BUTTON_LEFT)
    run_ticks(lib, 16)


def arrow_pixels(lib, mouse):
    """The pixels of the left arrow zone after some ticks with the mouse at mouse."""
    ctypes.c_int16.in_dll(lib, "g_input_mouse_x").value, ctypes.c_int16.in_dll(lib, "g_input_mouse_y").value = mouse
    run_ticks(lib, 16)
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(LEFT_ARROW_ZONE, rect)
    screen = Bitmap.in_dll(lib, "g_draw_screen")
    base = ctypes.addressof(screen.pixels.contents)
    return [ctypes.string_at(base + (rect[1] + y) * screen.width + rect[0], rect[2]) for y in range(rect[3])]


def test_the_push_arrow_under_the_mouse_stays_pressed(lib, session):
    # The party climbs the ladder to the first room (map 22), walks to the table (1, 6), and grabs it.
    game, _ = session
    keys(lib, KEY_TURN_RIGHT, KEY_TURN_RIGHT)
    click(lib, 112, 100)
    keys(lib, KEY_TURN_RIGHT, KEY_TURN_RIGHT, KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT, KEY_FORWARD)
    assert (game.party_map, game.party_x, game.party_y) == (22, 2, 6)
    click(lib, 100, 98)
    party = Party.in_dll(lib, "g_party")
    assert party.push_mode == 1
    released = arrow_pixels(lib, MOUSE_MIDDLE)
    assert party.push_arrow == C.PUSH_ARROW_NONE
    pressed = arrow_pixels(lib, MOUSE_LEFT)
    assert party.push_arrow == C.PUSH_ARROW_LEFT
    assert pressed != released
    assert arrow_pixels(lib, MOUSE_MIDDLE) == released
