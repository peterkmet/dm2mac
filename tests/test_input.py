"""The input tree of SKULL.EXE: clicks and keys give the same commands in the C port and in the original."""

import ctypes
import random
import struct

import pytest

from test_move import Scenario, gdat, start  # noqa: F401 (gdat and start are fixtures)

INPUT_INIT_TABLES, INPUT_FIND_CLICK_COMMAND, INPUT_FIND_KEY_COMMAND = 0x20847, 0x203A3, 0x2048A
MOUSE_AREA_ALLOC_NODE = 0x14B1F
G_INPUT_MODE_ROOTS, G_MATCH_RECT, G_MATCH_X, G_MATCH_Y = 0x72ED1, 0x7F46C, 0x7F520, 0x7F51E
MODES = range(10)
G_ACTION_AREA_MODE = 0x757B8


class ActionArea(ctypes.Structure):
    _fields_ = [("champion", ctypes.c_int16), ("hand", ctypes.c_int16), ("mode", ctypes.c_int16)]


@pytest.fixture(scope="module")
def keys():
    """Every key code of the key lists of SKULL.EXE."""
    from conftest import ROOT
    from dm2tools.le import load_image
    image = load_image(ROOT / "game" / "SKULL.EXE")
    data = image.data[0x729B8 - image.base:0x72B9C - image.base]
    return sorted({struct.unpack_from("<H", data, i + 2)[0] for i in range(0, len(data), 4)})


@pytest.fixture
def tree(lib, gdat, start, tmp_path):
    scenario = Scenario(lib, gdat, start, tmp_path)
    scenario.emu.stub(MOUSE_AREA_ALLOC_NODE, lambda *_: 0)
    scenario.emu.call(INPUT_INIT_TABLES)
    lib.input_init()
    lib.input_find_command.restype = ctypes.c_uint16
    # the action area state: the original after the load; the C port starts it with the screen
    mode = struct.unpack("<h", scenario.emu.read(G_ACTION_AREA_MODE, 2))[0]
    action = ActionArea.in_dll(lib, "action")
    action.champion, action.mode = 0, mode
    return scenario


def original_command(emu, mode, x, y, buttons, key):
    root = G_INPUT_MODE_ROOTS + 4 * mode
    if key:
        command = emu.call(INPUT_FIND_KEY_COMMAND, root, key) & 0xFFFF
    else:
        command = emu.call(INPUT_FIND_CLICK_COMMAND, root, x, y, buttons) & 0xFFFF
    return command, emu.read(G_MATCH_RECT, 8)


def test_clicks_and_keys_match_original(tree, lib, keys):
    # All modes: random clicks of both buttons on the whole screen, and every key of the key lists.
    rng = random.Random(3)
    rect = (ctypes.c_int16 * 4)()
    found = 0
    for mode in MODES:
        for _ in range(400):
            x, y, buttons = rng.randrange(320), rng.randrange(200), rng.choice((1, 2))
            mine = lib.input_find_command(mode, x, y, buttons, 0, rect)
            theirs, their_rect = original_command(tree.emu, mode, x, y, buttons, 0)
            assert mine == theirs, (mode, x, y, buttons)
            if mine:
                assert bytes(rect) == their_rect, (mode, x, y, buttons)
                found += 1
        for key in keys:
            mine = lib.input_find_command(mode, 0, 0, 0, key, rect)
            theirs, their_rect = original_command(tree.emu, mode, 0, 0, 0, key)
            assert mine == theirs, (mode, hex(key))
            if mine:
                assert bytes(rect) == their_rect, (mode, hex(key))
    assert found > 300
