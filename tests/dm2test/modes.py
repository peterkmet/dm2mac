"""The game modes of the C port: the mode switch, the advanced mode fixture, and the keys."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import run_ticks
from dm2test.structs import Session


VANILLA, ADVANCED = C.GAME_MODE_VANILLA, C.GAME_MODE_ADVANCED
SCAN_Q, SCAN_W, SCAN_E, SCAN_A, SCAN_S, SCAN_D = 0x10, 0x11, 0x12, 0x1E, 0x1F, 0x20
KEY_F5, KEY_F9, KEY_PAUSE = C.KEY_QUICK_SAVE, C.KEY_QUICK_LOAD, 0x01
INPUT_MODE_DIALOG, INPUT_MODE_MOVE, INPUT_MODE_INVENTORY, INPUT_MODE_NAME = (
    C.INPUT_MODE_DIALOG, C.INPUT_MODE_MOVE, C.INPUT_MODE_INVENTORY, C.INPUT_MODE_NAME)
ZONE_MESSAGE_AREA, TEXT_LINE_HEIGHT, SCREEN_W, UI_WHITE = C.ZONE_MESSAGE_AREA, C.TEXT_LINE_HEIGHT, C.SCREEN_W, C.UI_WHITE
STEP_TICKS = 40  # enough timer ticks for a key to give its command and the party to finish a step


def set_mode(lib, mode):
    ctypes.c_int.in_dll(lib, "g_game_mode").value = mode


@pytest.fixture
def advanced(lib, session, tmp_path):
    """start.DAT in the advanced mode, with the game folder in tmp_path; vanilla again after the test."""
    (tmp_path / "DATA").mkdir()
    state = Session.in_dll(lib, "g_session")
    state.data_dir = str(tmp_path).encode()
    state.slot, state.loaded, state.new_game = -1, 0, 0
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    lib.text_print_message.argtypes = [ctypes.c_int, ctypes.c_char_p]
    set_mode(lib, ADVANCED)
    yield session[0], tmp_path / "DATA"
    set_mode(lib, VANILLA)


def press(lib, key):
    lib.input_key_down(key)
    run_ticks(lib, STEP_TICKS)


def key_command(lib, mode, key):
    lib.input_key_command.restype = ctypes.c_uint16
    rect = (ctypes.c_int16 * 4)()
    return lib.input_key_command(mode, key, rect)
