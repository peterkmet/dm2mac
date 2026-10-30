"""The dialog and session state of the C port, and the reset of the session after each test."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.structs import MainLoop, Session, mirror


G_DIALOG_SCREEN_MODE, G_SCREEN_BITMAP, G_DISK_PATH_KIND, G_DATA_PATH, G_SAVE_PATH = 0x717BC, 0x71550, 0x7F98C, 0x70536, 0x7053A


@mirror("dialog.h")
class DialogState(ctypes.Structure):
    _fields_ = [("screen_mode", ctypes.c_int), ("button_count", ctypes.c_int), ("progress_enabled", ctypes.c_bool),
                ("progress_width", ctypes.c_int16), ("id", ctypes.c_int), ("right_choice", ctypes.c_int)]


def reset_session(lib):
    """The session state of the program start, so that later tests start from it."""
    session = Session.in_dll(lib, "g_session")
    for name in ("new_game", "use_dungenb", "loaded", "completed", "restart", "offer_restore", "quit_code", "exit_code"):
        setattr(session, name, 0)
    session.slot = -1
    state = DialogState.in_dll(lib, "g_dialog")
    state.screen_mode, state.button_count = 1, 0
    main_loop_quit(lib).value = 0
    ctypes.c_void_p.in_dll(lib, "g_play_frame_hook").value = None


@pytest.fixture
def program_start_after(lib):
    yield
    reset_session(lib)


@mirror("input.h")
class InputMatch(ctypes.Structure):
    _fields_ = [("zone", ctypes.c_uint16), ("origin", ctypes.c_int16), ("key", ctypes.c_uint16), ("choice", ctypes.c_int16),
                ("x", ctypes.c_int16), ("y", ctypes.c_int16), ("value", ctypes.c_uint16), ("rect", ctypes.c_int16 * C.RECT_SIZE)]


def main_loop_quit(lib):
    """MainLoop.quit."""
    return ctypes.c_bool.from_address(ctypes.addressof(MainLoop.in_dll(lib, "g_main_loop")) + MainLoop.quit.offset)


def find_point(lib, mode, test):
    """A screen point whose left click in input mode gives a match that test accepts."""
    rect = (ctypes.c_int16 * 4)()
    match = InputMatch.in_dll(lib, "g_input_match")
    lib.input_click_command.restype = ctypes.c_uint16
    for y in range(1, C.SCREEN_H, 2):
        for x in range(1, C.SCREEN_W, 2):
            if lib.input_click_command(mode, x, y, 2, rect) and test(match):
                return x, y
    raise AssertionError("no such click point")
