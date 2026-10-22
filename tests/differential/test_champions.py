"""Scenario tests of Step 15 with the emulator oracle: champions, the leader hand, inventory, items, money."""

import ctypes
import struct

import pytest

from dm2test import constants as C
from dm2test.input import Loop
from dm2test.interface import LeaderHand
from dm2test.scenario import gdat, scenario, start, square_event  # noqa: F401 (fixtures)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

G_LEADER_HAND, G_LEADER_HAND_WEIGHT, G_MAP_COLUMNS = 0x7FBB4, 0x7FBCE, 0x7F3C8
HAND_OFFSET = 252 + 4 * C.CHAMPION_SIZE  # offsetof(Game, leader_hand) in save.h
KEY_FORWARD, KEY_TURN_LEFT, KEY_TURN_RIGHT = C.KEYPAD_FORWARD, C.KEYPAD_TURN_LEFT, C.KEYPAD_TURN_RIGHT
LEFT = C.INPUT_BUTTON_LEFT
GOLD_COIN = 0x108
TABLE_COIN = 0xA8B8  # the gold coin on the table of the first room (map 22)
DUN_LINK_THING_TO_SQUARE, DUN_UNLINK_THING_FROM_SQUARE = 0x1D3DB, 0x1D542
DOOR_STATE, DOOR_OPEN, DOOR_CLOSED = C.SQUARE_DOOR_STATE, C.DOOR_STATE_OPEN, C.DOOR_STATE_CLOSED


@pytest.fixture
def loop(lib, gdat, start, tmp_path):  # noqa: F811
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return Loop(lib, gdat, start, tmp_path)


def hands(loop):
    """(C, original): the leader hand object and its weight."""
    game = ctypes.addressof(loop.game)
    mine = (ctypes.c_uint16.from_address(game + HAND_OFFSET).value, LeaderHand.in_dll(loop.lib, "g_leader_hand").weight)
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
    assert hands(loop)[0][0] == C.THING_NONE
    loop.assert_loop("end")
    loop.assert_equal("end")


SOLID_KEY = 0xA8B1  # on the shelf of the first room (map 22, (1, 5)), behind the table at (1, 6)
SUN_CREST = 0x130    # the item of the holder on the shelf face
SOLID_KEY_ITEM = 0x10B
CABINET_ITEMS = 0x1E1, 0x1EB, SOLID_KEY_ITEM  # the items of the cabinet on the shelf face, in the order of the clicks
G_PUSH_ARROW = 0x72246
PUSH_ARROW_LEFT = C.PUSH_ARROW_LEFT  # the table moves to the left of the party, the party steps forward


def test_door_out_of_the_first_room(loop, lib):
    # The party pushes the table (static creature 0x19) at (1, 6) to the south and steps to the shelf (1, 5). The
    # shelf face shows a sun crest holder (text 0x885c, ornament 0x74): a click gives a sun crest, a second click
    # puts it back. With the holder empty, the face shows the cabinet (text 0x8854, ornament 0x86): the party drops
    # the crest and takes the items of the cabinet until the solid key. The party puts the key into the keyhole
    # (4, 5) cell 3 and presses the door button (4, 4): the door (3, 4) opens. Both run the same keys and clicks tick
    # by tick; the test sets the push arrow under the mouse in both (the harness stubs the original arrow update).
    lib.item_get_index.restype = ctypes.c_uint16
    climb_ladder(loop)
    run(loop, walk(KEY_TURN_RIGHT, KEY_TURN_RIGHT, KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT, KEY_FORWARD), "table")
    assert (loop.game.party_x, loop.game.party_y) == (2, 6)
    run(loop, [("c", 100, 98), ("t", 16)], "push mode")  # the middle of the table
    assert loop.party.push_mode == 1
    loop.party.push_arrow = PUSH_ARROW_LEFT
    loop.emu.write(G_PUSH_ARROW, struct.pack("<h", PUSH_ARROW_LEFT))
    run(loop, [("c", 112, 90), ("t", 80)], "push")
    assert (loop.game.party_x, loop.game.party_y) == (1, 6)
    # the first key after the push goes into the input that the push click flushes (both drop it)
    run(loop, walk(KEY_TURN_RIGHT, KEY_TURN_RIGHT), "shelf")
    assert loop.game.party_dir == 0
    run(loop, [("c", 110, 80), ("t", 16)], "holder")
    assert lib.item_get_index(hands(loop)[0][0]) == SUN_CREST
    run(loop, [("c", 110, 80), ("t", 16)], "holder back")
    assert hands(loop)[0][0] == C.THING_NONE
    loop.assert_loop("shelf")
    run(loop, [("c", 110, 80), ("t", 16), ("c", 40, 165), ("t", 16)], "crest to the floor")
    assert hands(loop)[0][0] == C.THING_NONE
    for item in CABINET_ITEMS:
        run(loop, [("c", 110, 80), ("t", 16)], "cabinet")
        assert lib.item_get_index(hands(loop)[0][0]) == item
        if item != SOLID_KEY_ITEM:
            run(loop, [("c", 40, 165), ("t", 16)], "to the floor")
    assert hands(loop)[0][0] == SOLID_KEY & C.THING_ID_MASK
    run(loop, walk(KEY_TURN_RIGHT), "east")
    run(loop, walk(KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT, KEY_FORWARD, KEY_TURN_RIGHT), "keyhole")
    closed = square(loop, 22, 3, 4)
    run(loop, [("c", 111, 90), ("t", 16)], "keyhole")
    assert hands(loop)[0][0] == C.THING_NONE
    run(loop, walk(KEY_TURN_LEFT) + [("c", 173, 90), ("t", 136)], "button")
    loop.assert_loop("end")
    loop.assert_equal("end")
    mine, theirs = square(loop, 22, 3, 4)
    assert mine == theirs and mine & DOOR_STATE == DOOR_OPEN and closed[0] & DOOR_STATE == DOOR_CLOSED, (closed, mine)


MONEY_BOXES = 0xA401, 0xA40F  # on map 0 at (3, 2), and on the shelf of map 22 at (1, 5)
MONEYBOX_WORD, MONEYBOX_FLAG = C.MONEYBOX_FLAGS, C.MONEYBOX_PLACED
DUN_GET_THING_DATA = 0x1D2DA


def write_word(loop, thing, offset, value):
    """Writes a word of a thing record in both."""
    loop.lib.set_thing_word(thing, offset, value)
    loop.emu.write(loop.emu.call(DUN_GET_THING_DATA, thing & C.THING_ID_MASK) + offset, struct.pack("<H", value))


def test_money_box_flags_in_the_view(loop, lib):
    # A money box flag clears when the party takes the box from the floor, and when the party drops an object onto
    # the possession cell of the table where another money box lies.
    lib.thing_word.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    floor_box, table_box = MONEY_BOXES
    lib.dungeon_unlink_thing_from_square(floor_box, 0, 3, 2)
    loop.emu.call(DUN_UNLINK_THING_FROM_SQUARE, floor_box, 0, 3, 2)
    climb_ladder(loop)
    lib.dungeon_unlink_thing_from_square(table_box, 22, 1, 5)
    loop.emu.call(DUN_UNLINK_THING_FROM_SQUARE, table_box, 0, 1, 5)
    floor = floor_box & C.THING_ID_MASK | 1 << C.THING_CELL_SHIFT
    lib.dungeon_link_thing_to_square(floor, 22, 2, 6)
    loop.emu.call(DUN_LINK_THING_TO_SQUARE, floor, 0, 2, 6)
    last = TABLE_COIN  # the last possession of the table
    while lib.thing_word(last, 0) != C.THING_ENDOFLIST:
        last = lib.thing_word(last, 0)
    write_word(loop, last, 0, table_box & C.THING_ID_MASK | 2 << C.THING_CELL_SHIFT)
    write_word(loop, table_box, 0, C.THING_ENDOFLIST)
    for box in MONEY_BOXES:
        write_word(loop, box, MONEYBOX_WORD, lib.thing_word(box, MONEYBOX_WORD) | MONEYBOX_FLAG)
    loop.assert_loop("boxes")
    run(loop, walk(KEY_TURN_RIGHT, KEY_TURN_RIGHT, KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT), "floor")
    run(loop, [("c", 150, 145), ("t", 16)], "take")
    assert hands(loop)[0][0] == floor_box & C.THING_ID_MASK
    run(loop, walk(KEY_FORWARD) + [("c", 77, 101), ("t", 16)], "drop")
    assert hands(loop)[0][0] == C.THING_NONE
    loop.assert_loop("end")
    assert not any(lib.thing_word(box, MONEYBOX_WORD) & MONEYBOX_FLAG for box in MONEY_BOXES)


DUN_GET_UNUSED_THING, THING_SENSOR, FLOOR_PARTY_POSSESSES = 0x1DDD7, C.THING_SENSOR, 8
SENSOR_TOGGLE, SENSOR_INVERTED = C.EFFECT_TOGGLE << C.SENSOR_EFFECT_SHIFT, C.SENSOR_INVERTED  # sensor flags: the effect, and the inverted trigger


def test_floor_sensor_sees_the_leader_hand(loop, lib):
    # Two floor sensors under the party that test for a gold coin in the party possessions (new sensor records in
    # both), with the door of the room as target: one toggles the door when the leader takes the coin from the
    # table, the inverted one toggles it when the leader puts the coin back.
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.item_get_index.restype = ctypes.c_uint16
    climb_ladder(loop)
    run(loop, walk(KEY_TURN_RIGHT, KEY_TURN_RIGHT, KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT, KEY_FORWARD), "table")
    door_x, door_y = 3, 4
    x, y = loop.game.party_x, loop.game.party_y
    for flags in (SENSOR_TOGGLE, SENSOR_TOGGLE | SENSOR_INVERTED):
        sensor = lib.dungeon_unused_thing(THING_SENSOR)
        assert loop.emu.call(DUN_GET_UNUSED_THING, THING_SENSOR) & 0xFFFF == sensor
        for offset, value in ((0, C.THING_ENDOFLIST), (2, FLOOR_PARTY_POSSESSES | GOLD_COIN << C.SENSOR_DATA_SHIFT), (4, flags),
                              (6, door_x << C.SENSOR_TARGET_X_SHIFT | door_y << C.SENSOR_TARGET_Y_SHIFT)):
            write_word(loop, sensor, offset, value)
        lib.dungeon_link_thing_to_square(sensor, 22, x, y)
        loop.emu.call(DUN_LINK_THING_TO_SQUARE, sensor, 0, x, y)
    loop.assert_loop("sensors")
    closed = square(loop, 22, door_x, door_y)
    run(loop, [("c", 77, 101), ("t", 16)], "take")
    assert lib.item_get_index(hands(loop)[0][0]) == GOLD_COIN
    loop.assert_loop("take")
    run(loop, [("t", 60)], "door")
    mine, theirs = square(loop, 22, door_x, door_y)
    assert mine == theirs and mine & DOOR_STATE != closed[0] & DOOR_STATE, (closed, mine)
    run(loop, [("c", 77, 101), ("t", 16)], "back")
    assert hands(loop)[0][0] == C.THING_NONE
    run(loop, [("t", 60)], "door")
    loop.assert_loop("end")
    mine, theirs = square(loop, 22, door_x, door_y)
    assert mine == theirs and mine & DOOR_STATE == closed[0] & DOOR_STATE, (closed, mine)


KEY_INVENTORY = 0x02
MODE_MOVE, MODE_INVENTORY = C.INPUT_MODE_MOVE, C.INPUT_MODE_INVENTORY
CMD_SLOT_FIRST, CMD_MOUTH, CMD_HAND_FIRST, CMD_ACTION_FIRST = C.CMD_SLOT_INVENTORY_FIRST, C.CMD_MOUTH, C.CMD_HAND_FIRST, 0x71
CHAMPION_WATER = 0x46


def find_click(lib, mode, command):
    """A screen point whose left click gives command in mode, with the C port state."""
    lib.input_click_command.restype = ctypes.c_uint16
    rect = (ctypes.c_int16 * 4)()
    return next((x, y) for y in range(0, C.SCREEN_H, 2) for x in range(0, C.SCREEN_W, 2)
                if lib.input_click_command(mode, x, y, LEFT, rect) == command)


def test_drink_and_hand_actions(loop, lib):
    # The inventory of the first champion: the bota of the pouch goes into the leader hand, a click on the mouth
    # drinks from it, and it goes back into the pouch. Then the action menu of the ready hand and its first action,
    # several times, with the busy hand counting down between them.
    from dm2test.interface import champion_records
    run(loop, [("k", KEY_INVENTORY), ("t", 16)], "inventory")
    pouch = find_click(lib, MODE_INVENTORY, CMD_SLOT_FIRST + 13)
    run(loop, [("c", *pouch), ("t", 8)], "take bota")
    assert hands(loop)[0][0] != C.THING_NONE
    water = struct.unpack_from("<h", bytes(champion_records(lib)), CHAMPION_WATER)[0]
    run(loop, [("c", *find_click(lib, MODE_INVENTORY, CMD_MOUTH)), ("t", 8)], "drink")
    assert struct.unpack_from("<h", bytes(champion_records(lib)), CHAMPION_WATER)[0] != water
    run(loop, [("c", *pouch), ("t", 8), ("k", KEY_INVENTORY), ("t", 16)], "put back")
    assert hands(loop)[0][0] == C.THING_NONE
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


MODE_RECRUIT, CMD_RECRUIT, CMD_RECRUIT_CANCEL = C.INPUT_MODE_RECRUIT, C.CMD_RECRUIT, C.CMD_RECRUIT_CANCEL
G_CANDIDATE, G_INVENTORY = 0x7F284, 0x7F972


def viewport_on_screen(lib):
    """The viewport zone (7) of the C screen."""
    from dm2test.interface import SCREEN_W, VIEW_W, VIEW_H, c_screen
    rect = (ctypes.c_int16 * 4)()
    lib.zone_get_rect(C.ZONE_VIEWPORT, rect)
    screen = c_screen(lib)
    return b"".join(screen[(rect[1] + y) * SCREEN_W + rect[0]:][:VIEW_W] for y in range(VIEW_H))


def test_recruit_and_cancel(loop, lib):
    # The party walks to the first mirror and recruits its champion; then it walks to the next mirror, takes that
    # champion as a candidate, and sends it back. The candidate panel shows on the screen, as the original
    # ui_draw_champion_state (0x48890) presents the viewport after it draws the inventory.
    from dm2test.interface import UiState, c_view
    ui = UiState.in_dll(lib, "g_ui")
    run(loop, walk(KEY_FORWARD, KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT), "mirror")
    for n, command in enumerate((CMD_RECRUIT, CMD_RECRUIT_CANCEL)):
        if n:
            run(loop, walk(KEY_TURN_RIGHT, KEY_FORWARD, KEY_FORWARD, KEY_TURN_LEFT), "next mirror")
        run(loop, [("c", 112, 77), ("t", 16)], ("candidate", n))
        assert (ui.candidate, struct.unpack("<H", loop.emu.read(G_CANDIDATE, 2))[0]) == (n + 2, n + 2), (
            n, loop.game.party_x, loop.game.party_y, loop.game.party_dir)
        assert ui.inventory == struct.unpack("<h", loop.emu.read(G_INVENTORY, 2))[0]
        assert viewport_on_screen(lib) == c_view(lib), ("candidate panel not on screen", n)
        loop.assert_loop(("candidate", n))
        run(loop, [("c", *find_click(lib, MODE_RECRUIT, command)), ("t", 16)], ("finish", n))
        loop.assert_loop(("finish", n))
    assert loop.game.champion_count == 2 and ui.candidate == 0
    loop.assert_equal("end")


SENSOR_CREATE_ITEM = 0x3C


def test_sensors_create_items(scenario, lib):
    # Every item creation sensor (kind 0x3c) of the dungeon gets a set and a clear square event, plain and inverted:
    # the new item (its record, gdat index, charges, the do-not-discard bit) goes to the target cell, or to a random
    # cell of an open target square.
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    sensors = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                t = lib.dungeon_square_first_thing(map_, x, y)
                while t != C.THING_ENDOFLIST:
                    if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == C.THING_SENSOR and lib.thing_word(t, 2) & C.SENSOR_TYPE_MASK == SENSOR_CREATE_ITEM:
                        sensors.append((map_, x, y, t))
                    t = lib.dungeon_next_thing(t)
    assert len(sensors) >= 9
    for n, (map_, x, y, sensor) in enumerate(sensors):
        scenario.place(map_, x, y, 0)
        flags = lib.thing_word(sensor, 4)
        for inverted in (0, C.SENSOR_INVERTED):
            write_word(scenario, sensor, 4, flags & ~C.SENSOR_INVERTED | inverted)
            for effect in (0, 1):
                square_event(scenario, map_, x, y, sensor >> C.THING_CELL_SHIFT, effect, 0)
                scenario.step()
                scenario.assert_same(("create", n, map_, x, y, inverted, effect))
    scenario.assert_equal("end")
