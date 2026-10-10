"""The input tree of SKULL.EXE: clicks and keys give the same commands in the C port and in the original."""

import ctypes
import random
import shutil
import struct

import pytest

from conftest import ROOT
from test_save import GRAPHICS
from test_move import COMMAND_RECORD, TIMELINE_ADD_EVENT, Scenario, gdat, start  # noqa: F401 (gdat and start are fixtures)

INPUT_INIT_TABLES, INPUT_FIND_CLICK_COMMAND, INPUT_FIND_KEY_COMMAND = 0x20847, 0x203A3, 0x2048A
MOUSE_AREA_ALLOC_NODE = 0x14B1F
G_INPUT_MODE_ROOTS, G_MATCH_RECT, G_MATCH_X, G_MATCH_Y = 0x72ED1, 0x7F46C, 0x7F520, 0x7F51E
MODES = range(10)
G_ACTION_AREA_MODE = 0x757B8
G_DIALOG_SCREEN_MODE, G_DIALOG_BUTTON_COUNT = 0x717BC, 0x7F200


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
    # the dialog screen mode and button count of the input conditions 1 and 2 (dialog.h DialogState)
    dialog = (ctypes.c_int * 2).in_dll(lib, "dialog")
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


INPUT_REFINE_CHAMPION_CLICK = 0x2186C
CHAMPION_ACTION_DELAY, CHAMPION_HANDS = 0x2A, 0xC3


def test_champion_clicks_match_original(tree, lib):
    # Clicks on the champion hands (status boxes) and on the champion icons (party icons): 1 to 4 champions in
    # random cells and party directions, busy hands and spells, empty hands, random points in the icon.
    from test_interface import CHAMPION_SIZE, champion_bytes, champion_records, sync_champions
    from test_move import CHAMPION_CELL, COMMAND_RECORD, G_CHAMPION_COUNT, Command
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
                champion_bytes(lib, champ, CHAMPION_HANDS + 2 * rng.randrange(2), b"\xff\xff")
        sync_champions(lib, tree)
        tree.game.party_dir = rng.randrange(4)
        tree.emu.write(0x7F254, struct.pack("<h", tree.game.party_dir))
        command = rng.choice(list(range(0x5F, 0x63)) + list(range(0x74, 0x7C)))
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
    assert refined & set(range(0x74, 0x7C)) and refined & set(range(0x10, 0x14)) and refined & set(range(0x5F, 0x63))


INPUT_SET_MODE, MOUSE_MOVE_EVENT, MOUSE_SET_HOVER_AREA, MOUSE_AREA_ADD, MOUSE_AREA_REMOVE = (
    0x20707, 0x14483, 0x14400, 0x14B4F, 0x14C0E)
G_CURSOR_ZONES, G_INPUT_ZONES, G_MOUSE_AREAS, G_MOUSE_HOVER_INVALID = 0x722D4, 0x72D1F, 0x7F098, 0x7F0A0
G_CURSOR_HAS_ITEM, G_LEADER, INPUT_MOUSE_CLICK, G_CHAMP_DRAG_STATE = 0x7F094, 0x759A4, 0x20C5D, 0x7F0B4


def test_cursor_matches_original(tree, lib):
    # The cursor image (arrow, hand, held object, dragged champion) at random pointer positions and in each shown
    # cursor zone, in all input modes, with and without an object in the leader hand and a leader, after mode changes
    # in a random order, then in all input modes while a champion is dragged.
    from test_interface import set_leader_hand
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
    drag, action = ctypes.c_int.in_dll(lib, "input_drag_state"), ActionArea.in_dll(lib, "action")
    seen = set()
    for mode, dragged in [(mode, 0) for mode in modes] + [(mode, 1) for mode in MODES]:
        if dragged:  # the party icons show the dragged champion with the hands in the action area (action mode 0)
            drag.value, action.mode = 1, 0
            emu.write(G_CHAMP_DRAG_STATE, b"\1\0")
            emu.write(G_ACTION_AREA_MODE, b"\0\0")
        lib.input_set_mode(mode)
        emu.call(INPUT_SET_MODE, mode, allow_io=True)
        points = [(rng.randrange(320), rng.randrange(200)) for _ in range(300)]
        area = emu.read_u32(G_MOUSE_AREAS)
        while area:  # a point in each shown cursor zone: node of the zone = next zone, rectangle
            node = emu.read_u32(area + 6)
            x, y, w, h = struct.unpack("<4h", emu.read(node + 4, 8))
            points.append((x + rng.randrange(w), y + rng.randrange(h)))
            area = emu.read_u32(node)
        for x, y in points:
            hand, leader = rng.choice((0x0400, 0xFFFF)), rng.choice((0, 0, 0xFFFF))
            game.leader = leader
            set_leader_hand(lib, tree, hand)
            emu.write_u32(G_CURSOR_HAS_ITEM, hand != 0xFFFF)
            emu.write(G_LEADER, struct.pack("<H", leader))
            emu.write(G_MOUSE_HOVER_INVALID, b"\1\0")
            theirs = emu.call(MOUSE_MOVE_EVENT, x, y, allow_io=True) & 0xFFFF
            assert lib.input_cursor_at(x, y) == theirs, (mode, x, y, hex(hand), leader)
            seen.add((mode, theirs))
    drag.value = 0  # the library lives through the module: later tests start without a dragged champion
    assert {cursor for _, cursor in seen} == {0, 1, 2, 3}
    assert {mode for mode, cursor in seen if cursor == 2} >= {5, 8}
    assert {mode for mode, cursor in seen if cursor == 3} == {5, 8}


class MainLoop(ctypes.Structure):
    _fields_ = [("tick_threshold", ctypes.c_int), ("tick_count", ctypes.c_int), ("timer_ticks", ctypes.c_uint32),
                ("running", ctypes.c_int), ("view_hidden", ctypes.c_int), ("end", ctypes.c_int),
                ("quit", ctypes.c_int)]


@pytest.fixture
def session(lib, gdat, start, tmp_path):
    """start.DAT ready to play, as build/dm2 --save starts it."""
    from test_move import GameRecord
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    game = GameRecord.in_dll(lib, "game")
    party = __import__("test_move").Party.in_dll(lib, "party")
    party.cur_map, party.new_map, party.move_steps, party.push_pending_group = -1, -1, 0, 0xFFFF
    lib.view_enter_map(game.party_map)
    lib.ui_reset()
    lib.money_init_coin_table()
    lib.ui_draw_game_screen()
    lib.input_init()
    lib.play_start_session()
    return game, MainLoop.in_dll(lib, "main_loop")


def run_ticks(lib, n):
    for _ in range(n):
        lib.play_tick()


KEY_FORWARD, KEY_TURN_RIGHT, KEY_PAUSE = 0x4C, 0x4D, 0x01


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
    assert game.party_dir == (direction + 1) % 4
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
    assert lib.zone_get_rect(0x28, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, 2)
    run_ticks(lib, 16)
    assert game.party_dir == (direction + 3) % 4
    assert lib.zone_get_rect(0x29, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, 2)
    run_ticks(lib, 16)
    assert game.party_dir == direction
    x, y = game.party_x, game.party_y
    assert lib.zone_get_rect(0x2A, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, 2)
    run_ticks(lib, 40)
    assert (game.party_x, game.party_y) != (x, y)


def test_save_start_matches_a_menu_load(start, tmp_path):
    # build/dm2 --save starts start.DAT as the menu load does: the first view has the party light of the save, not the
    # light level of the program start, and the level digits come from the dungeon (the first load reads them).
    shutil.copyfile(ROOT / "build" / "libdm2.dylib", tmp_path / "libdm2.dylib")
    lib = ctypes.CDLL(str(tmp_path / "libdm2.dylib"))  # its own copy: the globals of the program start
    gdat = ctypes.c_void_p()
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(gdat)) == 0
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    lib.input_init()
    assert lib.session_start_save(str(path).encode()) == 1
    level = ctypes.c_int.in_dll(lib, "view_light_level")
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
    shutil.copyfile(ROOT / "build" / "libdm2.dylib", tmp_path / "libdm2.dylib")
    lib = ctypes.CDLL(str(tmp_path / "libdm2.dylib"))
    gdat = ctypes.c_void_p()
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(gdat)) == 0
    path = tmp_path / "SKSAVE0.DAT"
    shutil.copyfile(shell, path)
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    lib.input_init()
    assert lib.session_start_save(str(path).encode()) == 1
    expiry = (ctypes.c_uint32 * 3).in_dll(lib, "message_expiry")
    assert sum(t != 0xFFFFFFFF for t in expiry) == 1
    lib.gdat_free(gdat)


# The original main loop (game_loop) in parts: the timer interrupt body, one pass of the input wait (the wall drag
# check and command_process_queue), the end of a step, and a step up to the input wait.
TIMER_ISR_BODY, TIMER_ISR_END = 0x10CD1, 0x10D07
LOOP_START, LOOP_STEP, LOOP_WAIT, LOOP_WAIT_END, LOOP_STEP_END = 0x24696, 0x246AC, 0x248F2, 0x2496E, 0x24989
GAME_START_SESSION, INPUT_MOUSE_CLICK, INPUT_POLL, INPUT_FLUSH = 0x551D4, 0x20C5D, 0x210DC, 0x209E0
KBD_KEY_AVAILABLE, KBD_READ_KEY = 0x114B8, 0x11413
G_TIMER_STEP, G_TICK_ELAPSED, G_PARTY_SLEEPING = 0x7EFAC, 0x717F2, 0x7F234
# The loop functions of later steps: music (Step 19); the mouse areas of an input mode (not ported).
LOOP_STUBS = (0x10AF6, 0x14B1F,
              # game_start_session: the screen fill, the retrace wait, and the dialog after a load with its disk
              # check (Step 20)
              0x13B4D, 0x1C5FD, 0x1A37D, 0x1A0A4, 0x3D187, 0x346D1, 0x3D111,
              0x557B5,  # the expired rows of the message area: the screen only
              0x3F62D)  # the key sequence that prints the free memory (a debug message, not ported)
LOOP_GLOBALS = {"tick_threshold": (0x7F268, "<h"), "tick_count": (0x7F256, "<h"), "timer_ticks": (0x7F228, "<I"),
                "running": (0x7F244, "<I"), "view_hidden": (0x803EC, "<I")}


class Loop(Scenario):
    """start.DAT in the C port and in the original; both start the game session and run the main loop tick by tick."""

    def __init__(self, lib, gdat, start, tmp_path):
        super().__init__(lib, gdat, start, tmp_path, place_party=False)
        emu = self.emu
        emu.uc.hook_del(self.view.no_ops[INPUT_POLL])
        emu.uc.hook_del(self.stubs.pop(INPUT_FLUSH))
        for addr in LOOP_STUBS:
            emu.stub(addr, lambda *_: 0)
        self.keys = []
        emu.stub(KBD_KEY_AVAILABLE, lambda *_: int(bool(self.keys)))
        emu.stub(KBD_READ_KEY, lambda *_: self.keys.pop(0))
        emu.write(G_TIMER_STEP, struct.pack("<H", 1))
        emu.call(INPUT_INIT_TABLES)
        emu.write_u32(LOOP_GLOBALS["timer_ticks"][0], 0)
        # the C port as build/dm2 starts: the loop state of the program start
        self.loop = MainLoop.in_dll(lib, "main_loop")
        self.loop.tick_count, self.loop.timer_ticks, self.loop.end, self.loop.quit = 0, 0, 0, 0
        assert lib.text_init(gdat)
        lib.ui_reset()
        lib.money_init_coin_table()
        lib.ui_draw_game_screen()
        lib.input_init()
        lib.input_key_down.argtypes = [ctypes.c_uint16]
        lib.play_start_session()
        emu.call(GAME_START_SESSION)
        emu.run(LOOP_START, LOOP_STEP)
        emu.run(LOOP_STEP, LOOP_WAIT)

    def tick(self):
        """One timer tick in both."""
        self.lib.play_tick()
        emu = self.emu
        emu.run(TIMER_ISR_BODY, TIMER_ISR_END)
        emu.run(LOOP_WAIT, LOOP_WAIT_END)
        if struct.unpack("<H", emu.read(G_TICK_ELAPSED, 2))[0] and emu.read_u32(LOOP_GLOBALS["running"][0]):
            emu.run(LOOP_STEP_END, LOOP_STEP)
            emu.run(LOOP_STEP, LOOP_WAIT)

    def key(self, key):
        self.lib.input_key_down(key)
        self.keys.append(key)

    def click(self, x, y, buttons):
        self.lib.input_mouse_click(x, y, buttons)
        self.emu.call(INPUT_MOUSE_CLICK, x, y, buttons)

    def assert_tick(self, where):
        """The main loop state, the game time, the cooldowns, the movement state, the pending damage, and the champions."""
        from test_move import G_GAME_TIME, G_MOVE_COOLDOWN, G_THROW_COOLDOWN, GLOBALS, ignore_stale
        game = self.game
        mine = {name: getattr(self.loop, name) for name in LOOP_GLOBALS}
        mine |= {"sleeping": ctypes.c_int.in_dll(self.lib, "party_sleeping").value, "time": game.game_time,
                 "cooldown": game.party_move_cooldown, "throw": game.throw_cooldown}
        mine |= {name: getattr(self.party, name) for name in GLOBALS}
        values = LOOP_GLOBALS | GLOBALS | {"sleeping": (G_PARTY_SLEEPING, "<I"), "time": (G_GAME_TIME, "<I"),
                                           "cooldown": (G_MOVE_COOLDOWN, "<H"), "throw": (G_THROW_COOLDOWN, "<H")}
        theirs = {name: struct.unpack(fmt, self.emu.read(addr, struct.calcsize(fmt)))[0]
                  for name, (addr, fmt) in values.items()}
        assert ignore_stale(mine) == ignore_stale(theirs), where
        self.assert_same_damage(where)

    def champions(self):
        """(C, original) champion records without the redraw flags: the original status box drawing is a stub."""
        from test_interface import CHAMPION_SIZE, G_CHAMPIONS, champion_records

        def records(data):
            out = []
            for i in range(0, 4 * CHAMPION_SIZE, CHAMPION_SIZE):
                r = bytearray(data[i:i + CHAMPION_SIZE])
                for offset, size in CHAMPION_UNCOMPARED:
                    r[offset:offset + size] = bytes(size)
                out.append(bytes(r))
            return out
        return records(bytes(champion_records(self.lib))), records(self.emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE))

    def assert_loop(self, where):
        """assert_tick, the champion records (the save leaves out the damage display and the aura), and the
        dungeon records and events of both."""
        self.assert_tick(where)
        mine, theirs = self.champions()
        assert mine == theirs, where
        self.assert_same(where)


@pytest.fixture
def loop(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return Loop(lib, gdat, start, tmp_path)


def test_session_start_matches_original(loop):
    # game_start_session and the first game step of game_loop: movement state, loop state, and the save files.
    from test_dialog import G_DIALOG_SCREEN_MODE, DialogState
    loop.assert_loop("start")
    loop.assert_equal("start")
    # game_loop starts with the dialogs in the game viewport
    theirs = struct.unpack("<h", loop.emu.read(G_DIALOG_SCREEN_MODE, 2))[0]
    assert DialogState.in_dll(loop.lib, "dialog").screen_mode == theirs == 0


def test_keys_and_move_cooldowns_match_original(loop, lib):
    # Movement keys at random ticks, often before the cooldown of the last move ends, turns, side steps, and the
    # pause key: the commands, the walks, the cooldowns, and the game steps tick by tick.
    rng = random.Random(17)
    keys = [0x4C] * 6 + [0x4B, 0x4D, 0x4F, 0x50, 0x51]
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
    return next((x, y) for y in range(200) for x in range(320)
                if lib.input_find_command(mode, x, y, INPUT_BUTTON_LEFT, 0, rect) == command)


INPUT_BUTTON_LEFT, MODE_INVENTORY, CMD_SLEEP, KEY_INVENTORY, KEY_WAKE = 2, 8, 0x8E, 0x02, 0x1C


def test_sleep_and_wake_match_original(loop, lib):
    # The inventory key, then a click on the sleep button of the inventory: a game step on each tick while the
    # party sleeps; the wake key ends it.
    lib.input_find_command.restype = ctypes.c_uint16
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
CHAMPION_UNCOMPARED = ((0x32, 2),)  # redraw flags
EVENT_HIDE_DAMAGE, EVENT_AURA = 0x0C, 0x48


def test_damage_and_aura_events_match_original(loop, lib):
    # The timeline of the main loop: the damage display of a champion ends (event 0x0c); the aura of a champion
    # weakens twice (event 0x48), the second time below 0.
    import test_save
    from test_interface import champion_bytes, sync_champions
    champion_bytes(lib, 0, CHAMPION_AURA, [3, 50, 0])
    champion_bytes(lib, 0, CHAMPION_DAMAGE_EVENT, [0, 0])  # the damage display shows, its event is the first one
    sync_champions(lib, loop)
    now, map_ = loop.game.game_time, loop.game.party_map
    for delay, type_, priority, data in ((2, EVENT_HIDE_DAMAGE, 0, (0,) * 6), (3, EVENT_AURA, 1, (7, 0, 0, 0, 0, 0)),
                                         (5, EVENT_AURA, 1, (60, 0, 0, 0, 0, 0))):
        e = test_save.Event((now + delay) | map_ << 24, type_, priority, (ctypes.c_uint8 * 6)(*data))
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
    for zone in range(0x28, 0x2E):
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
