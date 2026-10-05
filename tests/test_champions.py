"""Scenario tests of Step 15 with the emulator oracle: champions, the leader hand, inventory, items, money."""

import ctypes
import struct

import pytest

from test_input import Loop
from test_move import gdat, start  # noqa: F401 (fixtures)

G_LEADER_HAND, G_LEADER_HAND_WEIGHT, G_MAP_COLUMNS = 0x7FBB4, 0x7FBCE, 0x7F3C8
HAND_OFFSET = 252 + 4 * 263  # offsetof(Game, leader_hand) in save.h
KEY_FORWARD, KEY_TURN_LEFT, KEY_TURN_RIGHT = 0x4C, 0x4B, 0x4D
LEFT = 2
GOLD_COIN = 0x108
DUN_LINK_THING_TO_SQUARE, DUN_UNLINK_THING_FROM_SQUARE = 0x1D3DB, 0x1D542
DOOR_STATE, DOOR_OPEN, DOOR_CLOSED = 7, 0, 4


class LeaderHand(ctypes.Structure):
    _fields_ = [("attr", ctypes.c_uint16), ("weight", ctypes.c_uint16), ("icon", ctypes.c_uint8)]


@pytest.fixture
def loop(lib, gdat, start, tmp_path):  # noqa: F811
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return Loop(lib, gdat, start, tmp_path)


def hands(loop):
    """(C, original): the leader hand object and its weight."""
    game = ctypes.addressof(loop.game)
    mine = (ctypes.c_uint16.from_address(game + HAND_OFFSET).value, LeaderHand.in_dll(loop.lib, "leader_hand").weight)
    theirs = struct.unpack("<H", loop.emu.read(G_LEADER_HAND, 2)) + struct.unpack("<H", loop.emu.read(G_LEADER_HAND_WEIGHT, 2))
    return mine, theirs


def run(loop, actions, where):
    """Keys (("k", key)), left clicks (("c", x, y)), and ticks (("t", n)) in both; compares the state on each tick."""
    for n, action in enumerate(actions):
        if action[0] == "k":
            loop.key(action[1])
        elif action[0] == "c":
            loop.click(action[1], action[2], LEFT)
        else:
            for t in range(action[1]):
                loop.tick()
                loop.assert_tick((where, n, t))
        mine, theirs = hands(loop)
        assert mine == theirs, (where, n, action)


def walk(*keys):
    """Each key, then the ticks until its move ends."""
    out = []
    for key in keys:
        out += [("k", key), ("t", 40 if key == KEY_FORWARD else 16)]
    return out


def square(loop, map_, x, y):
    """(C, original) square byte."""
    columns = loop.emu.read_u32(G_MAP_COLUMNS)
    table = loop.emu.read_u32(columns + 4 * map_)
    return loop.lib.dungeon_square(map_, x, y), loop.emu.read(loop.emu.read_u32(table + 4 * x) + y, 1)[0]


def climb_ladder(loop):
    """The party turns to the ladder behind it at the start and climbs it to the first room (map 22)."""
    run(loop, walk(KEY_TURN_RIGHT, KEY_TURN_RIGHT) + [("c", 112, 100), ("t", 16)], "ladder")
    assert (loop.game.party_map, loop.game.party_x, loop.game.party_y) == (22, 3, 8)


def test_coin_from_the_table(loop, lib):
    # The party takes the gold coin from the table into the leader hand; the keyhole beside the door does not take
    # it; the party puts it back onto the table. Both run the same keys and clicks tick by tick.
    lib.item_get_index.restype = ctypes.c_uint16
    climb_ladder(loop)
    run(loop, walk(KEY_TURN_RIGHT, KEY_TURN_RIGHT, KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT, KEY_FORWARD), "table")
    run(loop, [("c", 77, 101), ("t", 16)], "take")
    hand = hands(loop)[0][0]
    assert lib.item_get_index(hand) == GOLD_COIN, hex(hand)
    loop.assert_loop("take")
    run(loop, walk(KEY_TURN_RIGHT, KEY_TURN_RIGHT, KEY_FORWARD, KEY_TURN_LEFT, KEY_FORWARD, KEY_TURN_RIGHT), "keyhole")
    run(loop, [("c", 111, 90), ("t", 16)], "keyhole")
    assert hands(loop)[0][0] == hand
    run(loop, walk(KEY_TURN_RIGHT, KEY_FORWARD, KEY_TURN_RIGHT, KEY_FORWARD) + [("c", 77, 101), ("t", 16)], "back")
    assert hands(loop)[0][0] == 0xFFFF
    loop.assert_loop("end")
    loop.assert_equal("end")


SOLID_KEY = 0xA8B1  # on the shelf behind the table (map 22, (1, 5)), which the party cannot reach from the start


def test_key_in_the_keyhole_opens_the_door(loop, lib):
    # The solid key lies on the floor beside the chest (the test moves it there in both); the party takes it, puts it
    # into the keyhole beside the door, and presses the door button: the door opens.
    climb_ladder(loop)
    floor = SOLID_KEY & 0x3FFF | 1 << 14
    lib.dungeon_unlink_thing_from_square(SOLID_KEY, 22, 1, 5)
    loop.emu.call(DUN_UNLINK_THING_FROM_SQUARE, SOLID_KEY, 0, 1, 5)
    lib.dungeon_link_thing_to_square(floor, 22, 2, 6)
    loop.emu.call(DUN_LINK_THING_TO_SQUARE, floor, 0, 2, 6)
    run(loop, walk(KEY_TURN_RIGHT, KEY_TURN_RIGHT, KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT), "key")
    run(loop, [("c", 150, 145), ("t", 16)], "take")
    assert hands(loop)[0][0] == SOLID_KEY & 0x3FFF
    run(loop, walk(KEY_TURN_RIGHT, KEY_FORWARD, KEY_TURN_RIGHT), "keyhole")
    closed = square(loop, 22, 3, 4)
    run(loop, [("c", 111, 90), ("t", 16)], "keyhole")
    assert hands(loop)[0][0] == 0xFFFF
    run(loop, walk(KEY_TURN_LEFT) + [("c", 173, 90), ("t", 136)], "button")
    loop.assert_loop("end")
    loop.assert_equal("end")
    mine, theirs = square(loop, 22, 3, 4)
    assert mine == theirs and mine & DOOR_STATE == DOOR_OPEN and closed[0] & DOOR_STATE == DOOR_CLOSED, (closed, mine)


KEY_INVENTORY = 0x02
MODE_MOVE, MODE_INVENTORY = 5, 8
CMD_SLOT_FIRST, CMD_MOUTH, CMD_HAND_FIRST, CMD_ACTION_FIRST = 0x14 + 8, 0x46, 0x74, 0x71
CHAMPION_WATER = 0x46


def find_click(lib, mode, command):
    """A screen point whose left click gives command in mode, with the C port state."""
    lib.input_find_command.restype = ctypes.c_uint16
    rect = (ctypes.c_int16 * 4)()
    return next((x, y) for y in range(0, 200, 2) for x in range(0, 320, 2)
                if lib.input_find_command(mode, x, y, LEFT, 0, rect) == command)


def test_drink_and_hand_actions(loop, lib):
    # The inventory of the first champion: the bota of the pouch goes into the leader hand, a click on the mouth
    # drinks from it, and it goes back into the pouch. Then the action menu of the ready hand and its first action,
    # several times, with the busy hand counting down between them.
    from test_interface import CHAMPION_SIZE, champion_records
    run(loop, [("k", KEY_INVENTORY), ("t", 16)], "inventory")
    pouch = find_click(lib, MODE_INVENTORY, CMD_SLOT_FIRST + 13)
    run(loop, [("c", *pouch), ("t", 8)], "take bota")
    assert hands(loop)[0][0] != 0xFFFF
    water = struct.unpack_from("<h", bytes(champion_records(lib)), CHAMPION_WATER)[0]
    run(loop, [("c", *find_click(lib, MODE_INVENTORY, CMD_MOUTH)), ("t", 8)], "drink")
    assert struct.unpack_from("<h", bytes(champion_records(lib)), CHAMPION_WATER)[0] != water
    run(loop, [("c", *pouch), ("t", 8), ("k", KEY_INVENTORY), ("t", 16)], "put back")
    assert hands(loop)[0][0] == 0xFFFF
    loop.assert_loop("inventory")
    actions = 0
    for n in range(6):
        run(loop, [("c", *find_click(lib, MODE_MOVE, CMD_HAND_FIRST)), ("t", 4)], ("menu", n))
        try:
            point = find_click(lib, MODE_MOVE, CMD_ACTION_FIRST)
        except StopIteration:
            continue
        run(loop, [("c", *point), ("t", 60)], ("action", n))
        actions += 1
        loop.assert_loop(("action", n))
    assert actions >= 3
    loop.assert_equal("end")
