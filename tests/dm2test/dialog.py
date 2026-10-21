"""The dialog and session state of the C port, and the reset of the session after each test."""

import ctypes

import pytest


G_DIALOG_SCREEN_MODE, G_SCREEN_BITMAP, G_DISK_PATH_KIND, G_DATA_PATH, G_SAVE_PATH = 0x717BC, 0x71550, 0x7F98C, 0x70536, 0x7053A


class DialogState(ctypes.Structure):
    _fields_ = [("screen_mode", ctypes.c_int), ("button_count", ctypes.c_int), ("button_category", ctypes.c_uint8),
                ("button_index", ctypes.c_uint8), ("button_entry", ctypes.c_uint8), ("progress_enabled", ctypes.c_int),
                ("progress_width", ctypes.c_int16), ("id", ctypes.c_int)]


class Session(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ("new_game", "use_dungenb", "loaded", "completed", "restart", "offer_restore",
                                            "quit_code", "slot")] + [
        ("load_time", ctypes.c_uint32), ("save_time", ctypes.c_uint32), ("exit_code", ctypes.c_int),
        ("save_name", ctypes.c_char * 32), ("data_dir", ctypes.c_char * 1024),
        ("save_dir", ctypes.c_char * 1024)]


def reset_session(lib):
    """The session state of the program start, so that later tests start from it."""
    session = Session.in_dll(lib, "session")
    for name in ("new_game", "use_dungenb", "loaded", "completed", "restart", "offer_restore", "quit_code", "exit_code"):
        setattr(session, name, 0)
    session.slot = -1
    state = DialogState.in_dll(lib, "dialog")
    state.screen_mode, state.button_count = 1, 0
    ctypes.c_int.from_address(ctypes.addressof(ctypes.c_int.in_dll(lib, "main_loop")) + 24).value = 0  # MainLoop.quit
    ctypes.c_void_p.in_dll(lib, "play_frame_hook").value = None


@pytest.fixture(autouse=True)
def program_start_after(lib):
    yield
    reset_session(lib)


class InputMatch(ctypes.Structure):
    _fields_ = [("zone", ctypes.c_uint16), ("origin", ctypes.c_int16), ("key", ctypes.c_uint16), ("choice", ctypes.c_int16),
                ("x", ctypes.c_int16), ("y", ctypes.c_int16), ("value", ctypes.c_uint16), ("rect", ctypes.c_int16 * 4)]


def main_loop_quit(lib):
    """MainLoop.quit (after tick_threshold, tick_count, timer_ticks, running, view_hidden, end)."""
    return ctypes.c_int.from_address(ctypes.addressof(ctypes.c_int.in_dll(lib, "main_loop")) + 24)


def find_point(lib, mode, test):
    """A screen point whose left click in input mode gives a match that test accepts."""
    rect = (ctypes.c_int16 * 4)()
    match = InputMatch.in_dll(lib, "input_match")
    lib.input_find_command.restype = ctypes.c_uint16
    for y in range(1, 200, 2):
        for x in range(1, 320, 2):
            if lib.input_find_command(mode, x, y, 2, 0, rect) and test(match):
                return x, y
    raise AssertionError("no such click point")
