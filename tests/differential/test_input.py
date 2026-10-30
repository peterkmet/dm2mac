"""The input tree of SKULL.EXE: clicks and keys give the same commands in the C port and in the original."""

import ctypes
import random
import shutil
import struct

import pytest

from dm2test import constants as C
from dm2test.markers import ROOT
from dm2test.save import GRAPHICS
from dm2test.input import (
    INPUT_FIND_CLICK_COMMAND, INPUT_FIND_KEY_COMMAND, INPUT_INIT_TABLES, KEY_FORWARD, KEY_PAUSE, KEY_TURN_RIGHT, Loop, run_ticks,
)
from dm2test.scenario import COMMAND_RECORD, TIMELINE_ADD_EVENT, Scenario
from dm2test.structs import ActionArea
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

MOUSE_AREA_ALLOC_NODE = 0x14B1F
G_INPUT_MODE_ROOTS, G_MATCH_RECT, G_MATCH_X, G_MATCH_Y = 0x72ED1, 0x7F46C, 0x7F520, 0x7F51E
MODES = range(10)
G_ACTION_AREA_MODE = 0x757B8
G_DIALOG_SCREEN_MODE, G_DIALOG_BUTTON_COUNT = 0x717BC, 0x7F200


@pytest.fixture(scope="module")
def keys():
    """Every key code of the key lists of SKULL.EXE."""
    from dm2test.markers import ROOT
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
    lib.input_click_command.restype = ctypes.c_uint16
    lib.input_key_command.restype = ctypes.c_uint16
    # the action area state: the original after the load; the C port starts it with the screen
    mode = struct.unpack("<h", scenario.emu.read(G_ACTION_AREA_MODE, 2))[0]
    action = ActionArea.in_dll(lib, "g_action")
    action.champion, action.mode = 0, mode
    # the dialog screen mode and button count of the input conditions 1 and 2 (dialog.h DialogState)
    dialog = (ctypes.c_int * 2).in_dll(lib, "g_dialog")
    dialog[0] = scenario.emu.read_u32(G_DIALOG_SCREEN_MODE)
    dialog[1] = struct.unpack("<h", scenario.emu.read(G_DIALOG_BUTTON_COUNT, 2))[0]
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
            x, y, buttons = rng.randrange(C.SCREEN_W), rng.randrange(C.SCREEN_H), rng.choice((C.INPUT_BUTTON_RIGHT, C.INPUT_BUTTON_LEFT))
            mine = lib.input_click_command(mode, x, y, buttons, rect)
            theirs, their_rect = original_command(tree.emu, mode, x, y, buttons, 0)
            assert mine == theirs, (mode, x, y, buttons)
            if mine:
                assert bytes(rect) == their_rect, (mode, x, y, buttons)
                found += 1
        for key in keys:
            mine = lib.input_key_command(mode, key, rect)
            theirs, their_rect = original_command(tree.emu, mode, 0, 0, 0, key)
            assert mine == theirs, (mode, hex(key))
            if mine:
                assert bytes(rect) == their_rect, (mode, hex(key))
    assert found > 300


INPUT_REFINE_CHAMPION_CLICK = 0x2186C
CHAMPION_ACTION_DELAY, CHAMPION_HANDS = 0x2A, 0xC3


def test_champion_clicks_match_original(tree, lib):
    # Clicks on the champion hands (status boxes) and on the champion icons (party icons): 1 to 4 champions in
    # random cells and party directions, busy hands and spells, empty hands, random points in the icon.
    from dm2test.interface import CHAMPION_SIZE, champion_bytes, champion_records, sync_champions
    from dm2test.scenario import CHAMPION_CELL, COMMAND_RECORD, G_CHAMPION_COUNT, Command
    rng = random.Random(16)
    first = bytes(champion_records(lib)[:CHAMPION_SIZE])
    results = set()
    for n in range(600):
        count = rng.randrange(1, 5)
        tree.game.champion_count = count
        tree.emu.write(G_CHAMPION_COUNT, struct.pack("<H", count))
        for champ, cell in enumerate(rng.sample(range(4), count)):
            champion_bytes(lib, champ, 0, first)
            champion_bytes(lib, champ, CHAMPION_CELL, [cell])
            champion_bytes(lib, champ, CHAMPION_ACTION_DELAY, [rng.choice((0, 0, 3)) for _ in range(3)])
            if rng.random() < 0.3:
                champion_bytes(lib, champ, CHAMPION_HANDS + 2 * rng.randrange(C.SLOT_HAND_COUNT), b"\xff\xff")
        sync_champions(lib, tree)
        tree.game.party_dir = rng.randrange(C.DIR_COUNT)
        tree.emu.write(0x7F254, struct.pack("<h", tree.game.party_dir))
        command = rng.choice(list(range(C.CMD_PARTY_ICON_FIRST, C.CMD_PARTY_ICON_LAST + 1)) + list(range(C.CMD_HAND_FIRST, C.CMD_HAND_LAST + 1)))
        w, h = rng.randrange(8, 40), rng.randrange(8, 30)
        rect = (rng.randrange(200), rng.randrange(150), w, h)
        c = Command(rect[0] + rng.randrange(w), rect[1] + rng.randrange(h), command, (ctypes.c_int16 * 4)(*rect))
        tree.emu.write(COMMAND_RECORD, bytes(c))
        lib.input_refine_champion_click(ctypes.byref(c))
        tree.emu.call(INPUT_REFINE_CHAMPION_CLICK, COMMAND_RECORD)
        theirs = struct.unpack("<H", tree.emu.read(COMMAND_RECORD + 4, 2))[0]
        assert c.command == theirs, (n, hex(command))
        results.add((command, theirs))
    assert len(results) > 30
    refined = {theirs for _, theirs in results}
    assert refined & set(range(C.CMD_HAND_FIRST, C.CMD_HAND_LAST + 1)) and refined & set(range(C.CMD_LEADER_FIRST, C.CMD_LEADER_LAST + 1)) and refined & set(
        range(C.CMD_PARTY_ICON_FIRST, C.CMD_PARTY_ICON_LAST + 1))


INPUT_SET_MODE, MOUSE_MOVE_EVENT, MOUSE_SET_HOVER_AREA, MOUSE_AREA_ADD, MOUSE_AREA_REMOVE = (
    0x20707, 0x14483, 0x14400, 0x14B4F, 0x14C0E)
G_CURSOR_ZONES, G_INPUT_ZONES, G_MOUSE_AREAS, G_MOUSE_HOVER_INVALID = 0x722D4, 0x72D1F, 0x7F098, 0x7F0A0
G_CURSOR_HAS_ITEM, G_LEADER, INPUT_MOUSE_CLICK, G_CHAMP_DRAG_STATE = 0x7F094, 0x759A4, 0x20C5D, 0x7F0B4


def test_cursor_matches_original(tree, lib):
    # The cursor image (arrow, hand, held object, dragged champion) at random pointer positions and in each shown
    # cursor zone, in all input modes, with and without an object in the leader hand and a leader, after mode changes
    # in a random order, then in all input modes while a champion is dragged.
    from dm2test.interface import set_leader_hand
    emu, game = tree.emu, tree.game
    for addr in (MOUSE_AREA_ADD, MOUSE_AREA_REMOVE):
        emu.uc.hook_del(tree.stubs[addr])
    for stub, args in ((MOUSE_SET_HOVER_AREA, 1), (INPUT_MOUSE_CLICK, 0)):
        emu.stub(stub, lambda *_: 0, stack_args=args)
    emu.write_u32(G_MOUSE_AREAS, 0)
    for i in range(0x12):  # the list nodes of the cursor zones (the stubbed mouse_area_alloc_node)
        zone = G_CURSOR_ZONES + 10 * i
        emu.write(zone + 3, bytes([emu.read(zone + 3, 1)[0] & 0x0F]))
        emu.write_u32(zone + 6, tree.view.put(bytes(12)))
    for i in range(0x3E):
        emu.write(G_INPUT_ZONES + 7 * i + 6, bytes([emu.read(G_INPUT_ZONES + 7 * i + 6, 1)[0] & 0x3F]))
    rng = random.Random(19)
    modes = [*MODES, *rng.sample(MODES, 10), *rng.sample(MODES, 10)]
    drag, action = ctypes.c_int.in_dll(lib, "g_input_drag_state"), ActionArea.in_dll(lib, "g_action")
    seen = set()
    for mode, dragged in [(mode, 0) for mode in modes] + [(mode, 1) for mode in MODES]:
        if dragged:  # the party icons show the dragged champion with the hands in the action area (action mode 0)
            drag.value, action.mode = 1, 0
            emu.write(G_CHAMP_DRAG_STATE, b"\1\0")
            emu.write(G_ACTION_AREA_MODE, b"\0\0")
        lib.input_set_mode(mode)
        emu.call(INPUT_SET_MODE, mode, allow_io=True)
        points = [(rng.randrange(C.SCREEN_W), rng.randrange(C.SCREEN_H)) for _ in range(300)]
        area = emu.read_u32(G_MOUSE_AREAS)
        while area:  # a point in each shown cursor zone: node of the zone = next zone, rectangle
            node = emu.read_u32(area + 6)
            x, y, w, h = struct.unpack("<4h", emu.read(node + 4, 8))
            points.append((x + rng.randrange(w), y + rng.randrange(h)))
            area = emu.read_u32(node)
        for x, y in points:
            hand, leader = rng.choice((0x0400, C.THING_NONE)), rng.choice((0, 0, C.LEADER_NONE))
            game.leader = leader
            set_leader_hand(lib, tree, hand)
            emu.write_u32(G_CURSOR_HAS_ITEM, hand != C.THING_NONE)
            emu.write(G_LEADER, struct.pack("<H", leader))
            emu.write(G_MOUSE_HOVER_INVALID, b"\1\0")
            theirs = emu.call(MOUSE_MOVE_EVENT, x, y, allow_io=True) & 0xFFFF
            assert lib.input_cursor_at(x, y) == theirs, (mode, x, y, hex(hand), leader)
            seen.add((mode, theirs))
    drag.value = 0  # the library lives through the module: later tests start without a dragged champion
    assert {cursor for _, cursor in seen} == {C.CURSOR_ARROW, C.CURSOR_HAND, C.CURSOR_OBJECT, C.CURSOR_CHAMPION}
    assert {mode for mode, cursor in seen if cursor == C.CURSOR_OBJECT} >= {C.INPUT_MODE_MOVE, C.INPUT_MODE_INVENTORY}
    assert {mode for mode, cursor in seen if cursor == C.CURSOR_CHAMPION} == {C.INPUT_MODE_MOVE, C.INPUT_MODE_INVENTORY}


def test_keys_walk_and_turn_in_the_main_loop(session, lib):
    # A game step every 8 timer ticks; the forward key moves the party (a slow walk ends a step later), the right
    # key turns it, the pause key stops the game time until it is pressed again.
    game, loop = session
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    time, x, y, direction = game.game_time, game.party_x, game.party_y, game.party_dir
    run_ticks(lib, 16)
    assert game.game_time == time + 2 and loop.tick_threshold == 8
    lib.input_key_down(KEY_FORWARD)
    run_ticks(lib, 40)
    dx, dy = ((0, -1), (1, 0), (0, 1), (-1, 0))[direction]
    assert (game.party_x, game.party_y) == (x + dx, y + dy)
    lib.input_key_down(KEY_TURN_RIGHT)
    run_ticks(lib, 16)
    assert game.party_dir == (direction + 1) % C.DIR_COUNT
    lib.input_key_down(KEY_PAUSE)
    run_ticks(lib, 8)
    paused = game.game_time
    run_ticks(lib, 40)
    assert game.game_time == paused and not loop.running
    lib.input_key_down(KEY_PAUSE)
    run_ticks(lib, 24)
    assert loop.running and game.game_time > paused


def test_clicks_on_the_movement_arrows(session, lib):
    # Left clicks on the turn left, turn right, and forward arrows (layout zones 0x28, 0x29, and 0x2a).
    game, loop = session
    rect = (ctypes.c_int16 * 4)()
    direction = game.party_dir
    assert lib.zone_get_rect(C.ZONE_ARROW_FIRST, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, C.INPUT_BUTTON_LEFT)
    run_ticks(lib, 16)
    assert game.party_dir == (direction + 3) % C.DIR_COUNT
    assert lib.zone_get_rect(C.ZONE_ARROW_FIRST + 1, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, C.INPUT_BUTTON_LEFT)
    run_ticks(lib, 16)
    assert game.party_dir == direction
    x, y = game.party_x, game.party_y
    assert lib.zone_get_rect(C.ZONE_ARROW_FIRST + 2, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, C.INPUT_BUTTON_LEFT)
    run_ticks(lib, 40)
    assert (game.party_x, game.party_y) != (x, y)


def test_save_start_matches_a_menu_load(start, tmp_path):
    # build/dm2 --save starts start.DAT as the menu load does: the first view has the party light of the save, not the
    # light level of the program start, and the level digits come from the dungeon (the first load reads them).
    lib = load_lib()  # its own copy: the globals of the program start
    gdat = ctypes.c_void_p()
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(gdat)) == 0
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    lib.input_init()
    assert lib.session_start_save(str(path).encode()) == 1
    level = ctypes.c_int.in_dll(lib, "g_view_light_level")
    loaded = level.value
    levels = range(max(lib.dungeon_map_level(m) for m in range(lib.dungeon_map_count())) + 1)
    digits = [lib.dungeon_level_digit(n) for n in levels]
    lib.light_update_party_light()
    lib.dungeon_read_level_table()
    assert loaded == level.value == 1
    assert digits == [lib.dungeon_level_digit(n) for n in levels] and len(set(digits)) > 1
    lib.gdat_free(gdat)


def test_save_start_prints_the_hand_object_name_once(tmp_path):
    # shell.DAT has an object in the leader hand: a menu load prints its name in one message row.
    shell = ROOT / "tests" / "saves" / "shell.DAT"
    if not shell.exists():
        pytest.skip("tests/saves/shell.DAT missing")
    lib = load_lib()
    gdat = ctypes.c_void_p()
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(gdat)) == 0
    path = tmp_path / "SKSAVE0.DAT"
    shutil.copyfile(shell, path)
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    lib.input_init()
    assert lib.session_start_save(str(path).encode()) == 1
    expiry = (ctypes.c_uint32 * C.MESSAGE_ROWS).in_dll(lib, "g_text_message_expiry")
    assert sum(t != C.MESSAGE_NEVER for t in expiry) == 1
    lib.gdat_free(gdat)


@pytest.fixture
def loop(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return Loop(lib, gdat, start, tmp_path)


def test_session_start_matches_original(loop):
    # game_start_session and the first game step of game_loop: movement state, loop state, and the save files.
    from dm2test.dialog import G_DIALOG_SCREEN_MODE, DialogState
    loop.assert_loop("start")
    loop.assert_equal("start")
    # game_loop starts with the dialogs in the game viewport
    theirs = struct.unpack("<h", loop.emu.read(G_DIALOG_SCREEN_MODE, 2))[0]
    assert DialogState.in_dll(loop.lib, "g_dialog").screen_mode == theirs == 0


def test_keys_and_move_cooldowns_match_original(loop, lib):
    # Movement keys at random ticks, often before the cooldown of the last move ends, turns, side steps, and the
    # pause key: the commands, the walks, the cooldowns, and the game steps tick by tick.
    rng = random.Random(17)
    keys = [C.KEYPAD_FORWARD] * 6 + [C.KEYPAD_TURN_LEFT, C.KEYPAD_TURN_RIGHT, C.KEYPAD_LEFT, C.KEYPAD_BACK, C.KEYPAD_RIGHT]
    for t in range(600):
        if rng.random() < 0.25:
            loop.key(rng.choice(keys))
        if t in (300, 340):
            loop.key(KEY_PAUSE)
        loop.tick()
        loop.assert_tick(t)
        if t % 100 == 99:
            loop.assert_loop(t)
    assert loop.game.game_time > 60
    loop.assert_equal("end")


def find_click(lib, mode, command):
    """A screen point whose left click gives command in mode."""
    rect = (ctypes.c_int16 * 4)()
    return next((x, y) for y in range(C.SCREEN_H) for x in range(C.SCREEN_W)
                if lib.input_click_command(mode, x, y, INPUT_BUTTON_LEFT, rect) == command)


INPUT_BUTTON_LEFT, MODE_INVENTORY, CMD_SLEEP, KEY_INVENTORY, KEY_WAKE = C.INPUT_BUTTON_LEFT, C.INPUT_MODE_INVENTORY, C.CMD_SLEEP, 0x02, 0x1C


def test_sleep_and_wake_match_original(loop, lib):
    # The inventory key, then a click on the sleep button of the inventory: a game step on each tick while the
    # party sleeps; the wake key ends it.
    lib.input_click_command.restype = ctypes.c_uint16
    lib.input_key_command.restype = ctypes.c_uint16
    loop.key(KEY_INVENTORY)
    for t in range(16):
        loop.tick()
        loop.assert_tick(("inventory", t))
    loop.click(*find_click(lib, MODE_INVENTORY, CMD_SLEEP), INPUT_BUTTON_LEFT)
    slept = 0
    for t in range(120):
        if t == 80:
            loop.key(KEY_WAKE)
        loop.tick()
        loop.assert_tick(t)
        slept += loop.loop.tick_threshold == 1
    assert slept > 40 and loop.loop.tick_threshold == 8
    loop.assert_loop("end")
    loop.assert_equal("end")


CHAMPION_AURA, CHAMPION_DAMAGE_EVENT = 0x102, 0x2E
EVENT_HIDE_DAMAGE, EVENT_AURA = C.EVENT_HIDE_DAMAGE, C.EVENT_AURA


def test_damage_and_aura_events_match_original(loop, lib):
    # The timeline of the main loop: the damage display of a champion ends (event 0x0c); the aura of a champion
    # weakens twice (event 0x48), the second time below 0.
    from dm2test.structs import TimelineEvent
    from dm2test.interface import champion_bytes, sync_champions
    champion_bytes(lib, 0, CHAMPION_AURA, [3, 50, 0])
    champion_bytes(lib, 0, CHAMPION_DAMAGE_EVENT, [0, 0])  # the damage display shows, its event is the first one
    sync_champions(lib, loop)
    now, map_ = loop.game.game_time, loop.game.party_map
    for delay, type_, priority, data in ((2, EVENT_HIDE_DAMAGE, 0, (0,) * 6), (3, EVENT_AURA, 1, (7, 0, 0, 0, 0, 0)),
                                         (5, EVENT_AURA, 1, (60, 0, 0, 0, 0, 0))):
        e = TimelineEvent((now + delay) | map_ << C.EVENT_MAP_SHIFT, type_, priority, (ctypes.c_uint8 * 6)(*data))
        loop.emu.write(COMMAND_RECORD, bytes(e))
        lib.timeline_add_event(ctypes.byref(e))
        loop.emu.call(TIMELINE_ADD_EVENT, COMMAND_RECORD)
    for t in range(64):
        loop.tick()
        loop.assert_loop(t)
    loop.assert_equal("end")


def test_arrow_clicks_and_feedback_delays_match_original(loop, lib):
    # Left clicks on the six movement arrows (zones 0x28 to 0x2d) and in the view at random ticks, often while the
    # feedback of the last click still shows the pressed arrow or waits for the queue.
    rng = random.Random(18)
    rect = (ctypes.c_int16 * 4)()
    targets = []
    for zone in range(C.ZONE_ARROW_FIRST, C.ZONE_ARROW_FIRST + C.ZONE_ARROW_COUNT):
        assert lib.zone_get_rect(zone, rect)
        targets.append(tuple(rect))
    targets.append((80, 70, 60, 50))
    moves = 0
    for t in range(500):
        if rng.random() < 0.2:
            x, y, w, h = rng.choice(targets)
            loop.click(x + rng.randrange(w), y + rng.randrange(h), INPUT_BUTTON_LEFT)
        before = (loop.game.party_x, loop.game.party_y, loop.game.party_dir)
        loop.tick()
        loop.assert_tick(t)
        moves += before != (loop.game.party_x, loop.game.party_y, loop.game.party_dir)
    assert moves > 20
    loop.assert_loop("end")
    loop.assert_equal("end")
