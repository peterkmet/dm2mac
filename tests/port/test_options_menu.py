"""The options menu of the advanced mode (src/ui/optmenu.c, advanced.c): the Esc key, the rows, the filter, and
SETTINGS.TXT."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import run_ticks
from dm2test.modes import (ADVANCED, INPUT_MODE_INVENTORY, INPUT_MODE_MOVE, VANILLA, key_command, press, set_mode)

NONE, SCALE2X, SCALE3X = C.SETTINGS_FILTER_NONE, C.SETTINGS_FILTER_SCALE2X, C.SETTINGS_FILTER_SCALE3X
FILTER, CONTINUE = C.OPTMENU_ROW_FILTER, C.OPTMENU_ROW_CONTINUE
SCAN_ESCAPE, SCAN_RETURN, SCAN_W, SCAN_A, SCAN_S, SCAN_D = 0x01, 0x1C, 0x11, 0x1E, 0x1F, 0x20
HOOK = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_int)


@pytest.fixture
def menu(lib, advanced):
    """The advanced mode with the filter none and the menu open; everything closed after the test."""
    lib.optmenu_start(NONE)
    lib.optmenu_open()
    yield advanced
    ctypes.c_int.in_dll(lib, "g_advanced_screen").value = C.ADVANCED_SCREEN_NONE
    ctypes.c_void_p.in_dll(lib, "g_optmenu_filter_hook").value = None


def state(lib):
    return lib.optmenu_get_row(), lib.optmenu_get_filter()


def test_rows_wrap_and_close(lib, menu):
    _, data = menu
    assert state(lib) == (FILTER, NONE)
    assert not lib.optmenu_process_key(SCAN_W) and state(lib) == (FILTER, NONE), "no row above FILTER"
    assert not lib.optmenu_process_key(SCAN_S) and state(lib) == (CONTINUE, NONE)
    assert not lib.optmenu_process_key(SCAN_S) and state(lib) == (CONTINUE, NONE), "no row below CONTINUE"
    assert not lib.optmenu_process_key(SCAN_D) and state(lib) == (CONTINUE, NONE), "A and D act on FILTER only"
    assert lib.optmenu_process_key(SCAN_RETURN), "Return on CONTINUE closes"
    lib.optmenu_process_key(SCAN_W)
    for key, filter in ((SCAN_D, SCALE2X), (SCAN_D, SCALE3X), (SCAN_D, NONE), (SCAN_A, SCALE3X), (SCAN_A, SCALE2X),
                        (SCAN_A, NONE), (SCAN_RETURN, SCALE2X), (C.KEYPAD_TURN_RIGHT, SCALE3X)):
        assert not lib.optmenu_process_key(key) and state(lib) == (FILTER, filter), key
    assert lib.optmenu_process_key(SCAN_ESCAPE)
    assert not lib.optmenu_process_key(C.KEY_CHEATS)


def test_click_steps_the_filter_or_closes(lib, menu):
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(C.ZONE_VIEWPORT, rect)
    x, y = rect[0] + rect[2] // 2, rect[1] + 22
    assert not lib.optmenu_process_click(x, y + 5) and state(lib) == (FILTER, SCALE2X)
    assert not lib.optmenu_process_click(x, rect[1] + 2) and state(lib) == (FILTER, SCALE2X), "the title"
    assert lib.optmenu_process_click(x, y + 10 + 5) and state(lib)[0] == CONTINUE


def test_a_change_goes_to_the_file_once(lib, menu):
    _, data = menu
    lib.optmenu_process_key(SCAN_D)
    assert (data / "SETTINGS.TXT").read_text() == "filter=scale2x\n"
    (data / "SETTINGS.TXT").unlink()
    lib.optmenu_process_key(SCAN_S)
    lib.optmenu_process_key(SCAN_W)
    assert not (data / "SETTINGS.TXT").exists(), "no change, no write"
    lib.optmenu_process_key(SCAN_A)
    lib.optmenu_process_key(SCAN_A)
    assert (data / "SETTINGS.TXT").read_text() == "filter=scale3x\n"


def test_the_window_refuses_a_filter(lib, menu):
    seen = []
    hook = HOOK(lambda filter: seen.append(filter) or filter != SCALE3X)
    ctypes.c_void_p.in_dll(lib, "g_optmenu_filter_hook").value = ctypes.cast(hook, ctypes.c_void_p).value
    lib.optmenu_process_key(SCAN_D)
    lib.optmenu_process_key(SCAN_D)
    assert seen == [SCALE2X, SCALE3X] and state(lib) == (FILTER, SCALE2X)
    assert (menu[1] / "SETTINGS.TXT").read_text() == "filter=scale2x\n"


def test_failed_write_keeps_the_filter(lib, menu):
    _, data = menu
    (data / "SETTINGS.TMP").mkdir()
    lib.optmenu_process_key(SCAN_D)
    assert state(lib) == (FILTER, SCALE2X) and not (data / "SETTINGS.TXT").exists()


@pytest.mark.parametrize("given, file, expected", [(0, b"filter=scale3x\n", SCALE3X), (SCALE2X, b"filter=scale3x\n", SCALE2X),
                                                   (0, None, NONE)])
def test_start_takes_the_file_unless_a_filter_is_given(lib, menu, given, file, expected):
    _, data = menu
    if file:
        (data / "SETTINGS.TXT").write_bytes(file)
    shown = []
    hook = HOOK(lambda filter: shown.append(filter) or True)
    ctypes.c_void_p.in_dll(lib, "g_optmenu_filter_hook").value = ctypes.cast(hook, ctypes.c_void_p).value
    lib.optmenu_start(given)
    assert lib.optmenu_get_filter() == expected
    assert shown == ([expected] if expected != NONE and not given else [])
    set_mode(lib, VANILLA)
    lib.optmenu_start(0)
    assert lib.optmenu_get_filter() == NONE, "the vanilla mode reads no file"


def test_escape_opens_the_menu_in_the_advanced_mode_and_pauses_in_vanilla(lib, advanced):
    game, _ = advanced
    screen = ctypes.c_int.in_dll(lib, "g_advanced_screen")
    assert key_command(lib, INPUT_MODE_MOVE, SCAN_ESCAPE) == C.CMD_OPTIONS
    assert key_command(lib, INPUT_MODE_INVENTORY, SCAN_ESCAPE) != C.CMD_OPTIONS
    set_mode(lib, VANILLA)
    assert key_command(lib, INPUT_MODE_MOVE, SCAN_ESCAPE) == C.CMD_PAUSE
    set_mode(lib, ADVANCED)
    press(lib, SCAN_ESCAPE)
    assert screen.value == C.ADVANCED_SCREEN_OPTIONS
    time = game.game_time
    run_ticks(lib, 40)
    assert game.game_time == time, "the time stops"
    press(lib, SCAN_ESCAPE)
    assert screen.value == C.ADVANCED_SCREEN_NONE
    run_ticks(lib, 40)
    assert game.game_time > time
