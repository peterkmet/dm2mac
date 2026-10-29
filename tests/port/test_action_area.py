"""The action area (src/ui/action_area.c): the input zones follow its panels."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import run_ticks, session  # noqa: F401 (session is a fixture)
from dm2test.interface import GAME_LEADER_HAND, things_of
from dm2test.lib import load_lib
from dm2test.save import Game
from dm2test.scenario import gdat, start  # noqa: F401 (gdat and start are fixtures)

ACTION_HAND = 1  # the slot of the action hand
STEP_TICKS = 40  # timer ticks that run a click to its end


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.item_is_moneybox.argtypes = [ctypes.c_uint16]
    lib.item_is_moneybox.restype = ctypes.c_bool
    lib.champ_add_object_to_slot.argtypes = [ctypes.c_int16, ctypes.c_uint16, ctypes.c_int16]
    lib.champ_remove_object_from_slot.restype = ctypes.c_uint16
    lib.input_command_rect.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_int16)]
    lib.input_command_rect.restype = ctypes.c_bool
    lib.input_mouse_click.argtypes = [ctypes.c_int16, ctypes.c_int16, ctypes.c_int]
    lib.input_cursor_at.argtypes = [ctypes.c_int16, ctypes.c_int16]
    return lib


def test_an_open_money_box_shows_the_leader_hand_object_as_the_cursor(lib, session):  # noqa: F811
    # regression: the action area did not mark the input zones of a panel it opened, so the cursor over an open
    # money box was the arrow until the next input mode change
    box = things_of(lib, C.THING_CONTAINER, 1, lib.item_is_moneybox)
    if not box:
        pytest.skip("no money box in the dungeon")
    lib.champ_remove_object_from_slot(0, ACTION_HAND)
    lib.champ_add_object_to_slot(0, box[0], ACTION_HAND)
    run_ticks(lib, STEP_TICKS)
    rect = (ctypes.c_int16 * 4)()
    assert lib.input_command_rect(C.CMD_HAND_FIRST + ACTION_HAND, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, C.INPUT_BUTTON_LEFT)
    run_ticks(lib, STEP_TICKS)
    weapon = things_of(lib, C.THING_WEAPON, 1)[0]
    ctypes.c_uint16.from_address(ctypes.addressof(Game.in_dll(lib, "g_game")) + GAME_LEADER_HAND).value = weapon
    assert lib.zone_get_rect(C.ZONE_ACTION_AREA, rect)
    assert lib.input_cursor_at(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2) == C.CURSOR_OBJECT
