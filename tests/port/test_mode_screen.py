"""The mode screen of the program start (src/ui/mode_screen.c): the cursor, the keys, and the clicks on the items."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.lib import load_lib

Rect = ctypes.c_int16 * C.RECT_SIZE


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.mode_screen_key.argtypes = [ctypes.c_int16]
    lib.mode_screen_click.argtypes = [ctypes.c_int16, ctypes.c_int16]
    lib.mode_screen_item_box.argtypes = [ctypes.c_int16, ctypes.c_void_p]
    return lib


@pytest.fixture
def screen(lib):
    lib.mode_screen_start()
    return lib


def box(lib, item):
    rect = Rect()
    assert lib.mode_screen_item_box(item, rect)
    return list(rect)


def test_cursor_starts_on_advanced(screen):
    assert screen.mode_screen_cursor() == C.GAME_MODE_ADVANCED
    assert not screen.mode_screen_is_done()


@pytest.mark.parametrize("keys, cursor", [
    ([C.MODE_SCREEN_UP], C.GAME_MODE_VANILLA),
    ([C.MODE_SCREEN_UP, C.MODE_SCREEN_UP], C.GAME_MODE_VANILLA),  # stops at the top
    ([C.MODE_SCREEN_DOWN], C.GAME_MODE_ADVANCED),  # stops at the bottom
    ([C.MODE_SCREEN_UP, C.MODE_SCREEN_DOWN], C.GAME_MODE_ADVANCED),
])
def test_keys_move_the_cursor_and_stop_at_the_ends(screen, keys, cursor):
    for key in keys:
        screen.mode_screen_key(key)
    assert screen.mode_screen_cursor() == cursor
    assert not screen.mode_screen_is_done()


@pytest.mark.parametrize("keys, mode", [
    ([C.MODE_SCREEN_ENTER], C.GAME_MODE_ADVANCED),
    ([C.MODE_SCREEN_UP, C.MODE_SCREEN_ENTER], C.GAME_MODE_VANILLA),
])
def test_enter_selects_the_mode_at_the_cursor(screen, keys, mode):
    for key in keys:
        screen.mode_screen_key(key)
    assert screen.mode_screen_is_done()
    assert screen.mode_screen_cursor() == mode
    screen.mode_screen_key(C.MODE_SCREEN_DOWN)  # a selected screen takes no more keys
    assert screen.mode_screen_cursor() == mode


def test_item_boxes_are_centered_text_boxes(screen):
    # a box is the text width (glyphs of FONT_ADVANCE without the last spacing) plus 8 pixels on each side, 12 high,
    # centered; the boxes start at 88 and step 16 down
    for mode, glyphs in ((C.GAME_MODE_VANILLA, len("VANILLA")), (C.GAME_MODE_ADVANCED, len("ADVANCED"))):
        width = glyphs * C.FONT_ADVANCE - C.FONT_SPACING + 2 * 8
        assert box(screen, mode) == [(C.SCREEN_W - width) // 2, 88 + 16 * mode, width, 12]
    assert not screen.mode_screen_item_box(2, Rect()) and not screen.mode_screen_item_box(-1, Rect())


@pytest.mark.parametrize("mode", [C.GAME_MODE_VANILLA, C.GAME_MODE_ADVANCED])
def test_click_on_an_item_selects_it(screen, mode):
    x, y, w, h = box(screen, mode)
    screen.mode_screen_click(x + w - 1, y + h - 1)
    assert screen.mode_screen_is_done()
    assert screen.mode_screen_cursor() == mode


def test_click_outside_the_items_does_nothing(screen):
    x, y, w, h = box(screen, C.GAME_MODE_VANILLA)
    for point in ((x - 1, y), (x + w, y), (x, y - 1), (C.SCREEN_W // 2, y + h)):  # y + h is the gap between boxes
        screen.mode_screen_click(*point)
    assert not screen.mode_screen_is_done()
    assert screen.mode_screen_cursor() == C.GAME_MODE_ADVANCED
