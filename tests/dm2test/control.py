"""The control socket of the C port on start.DAT ready to play, and the main loop in a thread."""

import ctypes
import os
import shutil
import tempfile
import threading
import time
from contextlib import contextmanager

import pytest

from dm2test.scenario import WorldGame


SCREEN_W, SCREEN_H, PALETTE_COLORS, RGB = 320, 200, 256, 3
CHAMPION_SIZE, NAME_SIZE, SCAN_QUOTE = 263, 8, 0x28


class GameWithChampions(WorldGame):
    """save.h Game up to the champion records."""
    _fields_ = [("global_flags", ctypes.c_uint8 * 8), ("global_bytes", ctypes.c_uint8 * 64),
                ("global_words", ctypes.c_uint16 * 64), ("champions", ctypes.c_uint8 * CHAMPION_SIZE * 4)]


@pytest.fixture
def control(lib, gdat, session):  # noqa: F811
    """start.DAT ready to play with its palette and the control socket open; yields the socket path, the game record,
    the main loop."""
    palette = (ctypes.c_uint8 * (PALETTE_COLORS * RGB)).in_dll(lib, "palette")
    for c in range(PALETTE_COLORS):
        assert lib.gdat_palette_color(gdat, c, ctypes.byref(palette, RGB * c)) == 0
    folder = tempfile.mkdtemp(prefix="dm2c", dir="/tmp")  # sun_path holds about 100 bytes
    path = os.path.join(folder, "sub", "control.sock")
    lib.transport_open.argtypes = [ctypes.c_char_p]
    lib.transport_open.restype = ctypes.c_bool
    assert lib.transport_open(path.encode())
    yield (path, *session)
    lib.transport_close()
    assert not os.path.exists(path)
    shutil.rmtree(folder)


@contextmanager
def pumped(lib, ticks=True):
    """The main loop of the program in a thread: the control requests, and the game ticks when ticks is true."""
    stop = threading.Event()

    def loop():
        while not stop.is_set():
            lib.transport_poll()
            if ticks:
                lib.play_tick()
            time.sleep(0.0005)

    thread = threading.Thread(target=loop)
    thread.start()
    try:
        yield
    finally:
        stop.set()
        thread.join()
