"""The screen filter of the advanced mode (src/ui/filter.c) and its row on the cheat screen (src/ui/cheats.c): the
start from --filter and SETTINGS.TXT, the cycle, the SETTINGS.TXT write, the window hook, and Esc, which pauses."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import run_ticks
from dm2test.modes import (ADVANCED, INPUT_MODE_MOVE, VANILLA, key_command, press, set_mode)

NONE, SCALE2X, SCALE3X = C.SETTINGS_FILTER_NONE, C.SETTINGS_FILTER_SCALE2X, C.SETTINGS_FILTER_SCALE3X
ROW_OPTIONS, ROW_FILTER, ROW_QUICK_SAVE = 6, 7, 8
SCAN_ESCAPE, SCAN_RETURN, SCAN_S = 0x01, 0x1C, 0x1F
HOOK = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_int)


class Cursor(ctypes.Structure):
    _fields_ = [("page", ctypes.c_int16), ("row", ctypes.c_int16)]


@pytest.fixture(scope="module", autouse=True)
def lib_prototypes(lib):
    lib.cheats_get_cursor.restype = Cursor  # Cursor is a mirror of this module


@pytest.fixture
def screen(lib, advanced):
    """The advanced mode with the filter none and the cheat screen open on SCREEN FILTER; everything closed after
    the test."""
    lib.upscale_start(NONE)
    lib.cheats_open()
    for _ in range(ROW_FILTER - 1):  # the selection skips the OPTIONS subtitle
        lib.cheats_move_selection(1)
    yield advanced
    ctypes.c_int.in_dll(lib, "g_advanced_screen").value = C.ADVANCED_SCREEN_NONE
    ctypes.c_void_p.in_dll(lib, "g_upscale_filter_hook").value = None


def state(lib):
    c = lib.cheats_get_cursor()
    return c.row, lib.upscale_get_filter()


def cycle(lib):
    assert lib.cheats_run_selected_row() == C.CHEATS_STAY


def test_the_row_cycles_the_filter(lib, screen):
    assert state(lib) == (ROW_FILTER, NONE)
    for filter in (SCALE2X, SCALE3X, NONE, SCALE2X):
        cycle(lib)
        assert state(lib) == (ROW_FILTER, filter)
    lib.cheats_move_selection(-1)
    assert state(lib)[0] == ROW_OPTIONS - 1, "the selection skips the OPTIONS subtitle"
    lib.cheats_move_selection(1)
    assert state(lib)[0] == ROW_FILTER


def test_click_cycles_the_filter_and_the_subtitle_does_nothing(lib, screen):
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(C.ZONE_VIEWPORT, rect)
    x, y = rect[0] + rect[2] // 2, rect[1] + 22 + 5
    assert lib.cheats_process_click(x, y + ROW_FILTER * 10) == C.CHEATS_STAY and state(lib) == (ROW_FILTER, SCALE2X)
    assert lib.cheats_process_click(x, y + ROW_OPTIONS * 10) == C.CHEATS_STAY
    assert state(lib) == (ROW_FILTER, SCALE2X), "the subtitle"


def test_a_change_goes_to_the_file_once(lib, screen):
    _, data = screen
    cycle(lib)
    assert (data / "SETTINGS.TXT").read_text() == "filter=scale2x\n"
    (data / "SETTINGS.TXT").unlink()
    lib.cheats_move_selection(1)
    lib.cheats_move_selection(-1)
    assert not (data / "SETTINGS.TXT").exists(), "no change, no write"
    cycle(lib)
    assert (data / "SETTINGS.TXT").read_text() == "filter=scale3x\n"


def test_the_window_refuses_a_filter(lib, screen):
    seen = []
    hook = HOOK(lambda filter: seen.append(filter) or filter != SCALE3X)
    ctypes.c_void_p.in_dll(lib, "g_upscale_filter_hook").value = ctypes.cast(hook, ctypes.c_void_p).value
    cycle(lib)
    cycle(lib)
    assert seen == [SCALE2X, SCALE3X] and state(lib) == (ROW_FILTER, SCALE2X)
    assert (screen[1] / "SETTINGS.TXT").read_text() == "filter=scale2x\n"


def test_failed_write_keeps_the_filter(lib, screen):
    _, data = screen
    (data / "SETTINGS.TMP").mkdir()
    cycle(lib)
    assert state(lib) == (ROW_FILTER, SCALE2X) and not (data / "SETTINGS.TXT").exists()


@pytest.mark.parametrize("given, file, expected", [(C.SETTINGS_FILTER_UNSET, b"filter=scale3x\n", SCALE3X), (SCALE2X, b"filter=scale3x\n", SCALE2X),
                                                   (C.SETTINGS_FILTER_UNSET, None, NONE)])
def test_start_takes_the_file_unless_a_filter_is_given(lib, screen, given, file, expected):
    _, data = screen
    if file:
        (data / "SETTINGS.TXT").write_bytes(file)
    shown = []
    hook = HOOK(lambda filter: shown.append(filter) or True)
    ctypes.c_void_p.in_dll(lib, "g_upscale_filter_hook").value = ctypes.cast(hook, ctypes.c_void_p).value
    lib.upscale_start(given)
    assert lib.upscale_get_filter() == expected
    assert shown == ([expected] if expected != NONE and not given else [])
    set_mode(lib, VANILLA)
    lib.upscale_start(C.SETTINGS_FILTER_UNSET)
    assert lib.upscale_get_filter() == NONE, "the vanilla mode reads no file"


def test_escape_pauses_in_the_advanced_mode_as_in_vanilla(lib, advanced):
    game, _ = advanced
    screen = ctypes.c_int.in_dll(lib, "g_advanced_screen")
    assert key_command(lib, INPUT_MODE_MOVE, SCAN_ESCAPE) == C.CMD_PAUSE
    set_mode(lib, VANILLA)
    assert key_command(lib, INPUT_MODE_MOVE, SCAN_ESCAPE) == C.CMD_PAUSE
    set_mode(lib, ADVANCED)
    press(lib, SCAN_ESCAPE)
    assert screen.value == C.ADVANCED_SCREEN_NONE and lib.input_get_mode() == C.INPUT_MODE_PAUSE
    time = game.game_time
    run_ticks(lib, 40)
    assert game.game_time == time, "the time stops"
    press(lib, SCAN_ESCAPE)
    assert lib.input_get_mode() == INPUT_MODE_MOVE
    run_ticks(lib, 40)
    assert game.game_time > time
