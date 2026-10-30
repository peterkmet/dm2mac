"""The main loop harness: start.DAT ready to play in the C port, and the main loop of both run tick by tick."""

import ctypes
import struct

import pytest

from dm2test import constants as C
from dm2test.interface import c_screen, diff
from dm2test.scenario import Scenario


INPUT_INIT_TABLES, INPUT_FIND_CLICK_COMMAND, INPUT_FIND_KEY_COMMAND = 0x20847, 0x203A3, 0x2048A


class MainLoop(ctypes.Structure):
    _fields_ = [("tick_threshold", ctypes.c_int), ("tick_count", ctypes.c_int), ("timer_ticks", ctypes.c_uint32),
                ("running", ctypes.c_bool), ("view_hidden", ctypes.c_bool), ("end", ctypes.c_bool),
                ("quit", ctypes.c_bool), ("closed", ctypes.c_bool)]


@pytest.fixture
def session(lib, gdat, start, tmp_path):
    """start.DAT ready to play, as build/dm2 --save starts it."""
    from dm2test.structs import CurrentMap, Game, Party
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    game = Game.in_dll(lib, "g_game")
    party = Party.in_dll(lib, "g_party")
    CurrentMap.in_dll(lib, "g_current_map").map = -1
    party.new_map, party.move_steps, party.push_pending_group = -1, 0, 0xFFFF
    lib.view_enter_map(game.party_map)
    lib.ui_reset()
    lib.money_init_coin_table()
    lib.ui_draw_game_screen()
    lib.input_init()
    lib.play_start_session()
    return game, MainLoop.in_dll(lib, "g_main_loop")


def run_ticks(lib, n):
    for _ in range(n):
        lib.play_tick()


KEY_FORWARD, KEY_TURN_RIGHT, KEY_PAUSE = 0x4C, 0x4D, 0x01


# The original main loop (game_loop) in parts: the timer interrupt body, one pass of the input wait (the wall drag
# check and command_process_queue), the end of a step, and a step up to the input wait.
TIMER_ISR_BODY, TIMER_ISR_END = 0x10CD1, 0x10D07
LOOP_START, LOOP_STEP, LOOP_WAIT, LOOP_WAIT_END, LOOP_STEP_END = 0x24696, 0x246AC, 0x248F2, 0x2496E, 0x24989
GAME_START_SESSION, INPUT_MOUSE_CLICK, INPUT_POLL, INPUT_FLUSH = 0x551D4, 0x20C5D, 0x210DC, 0x209E0
KBD_KEY_AVAILABLE, KBD_READ_KEY = 0x114B8, 0x11413
G_TIMER_STEP, G_TICK_ELAPSED, G_PARTY_SLEEPING = 0x7EFAC, 0x717F2, 0x7F234
# The loop functions that are not ported: music; the mouse areas of an input mode (not ported).
LOOP_STUBS = (0x10AF6, 0x14B1F,
              # game_start_session: the screen fill, the retrace wait, and the dialog after a load with its disk
              # check
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
        self.loop = MainLoop.in_dll(lib, "g_main_loop")
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
        from dm2test.scenario import G_GAME_TIME, G_MOVE_COOLDOWN, G_THROW_COOLDOWN, GLOBALS, ignore_stale, port_globals
        game = self.game
        mine = {name: getattr(self.loop, name) for name in LOOP_GLOBALS}
        mine |= {"sleeping": ctypes.c_bool.in_dll(self.lib, "g_party_sleeping").value, "time": game.game_time,
                 "cooldown": game.party_move_cooldown, "throw": game.throw_cooldown}
        mine |= port_globals(self.party, self.current_map)
        values = LOOP_GLOBALS | GLOBALS | {"sleeping": (G_PARTY_SLEEPING, "<I"), "time": (G_GAME_TIME, "<I"),
                                           "cooldown": (G_MOVE_COOLDOWN, "<H"), "throw": (G_THROW_COOLDOWN, "<H")}
        theirs = {name: struct.unpack(fmt, self.emu.read(addr, struct.calcsize(fmt)))[0]
                  for name, (addr, fmt) in values.items()}
        assert ignore_stale(mine) == ignore_stale(theirs), where
        self.assert_same_damage(where)

    def champions(self):
        """(C, original) champion records without the redraw flags: the original status box drawing is a stub."""
        from dm2test.interface import CHAMPION_SIZE, G_CHAMPIONS, champion_records

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


CHAMPION_UNCOMPARED = ((0x32, 2),)  # redraw flags


# The click feedback of the original: the script of a command and its run.
INPUT_GET_CLICK_FEEDBACK_SCRIPT, INPUT_RUN_CLICK_FEEDBACK = 0x20EE0, 0x21394
G_CURRENT_COMMAND, G_CLICK_FEEDBACK_SCRIPT, G_CLICK_FEEDBACK_DELAY = 0x7F4CE, 0x7F47C, 0x7F52A
G_INPUT_DISABLED, G_COMMAND_QUEUE_HEAD, G_COMMAND_QUEUE_TAIL = 0x7F50A, 0x7F50E, 0x7F516
G_FEEDBACK_WAITING, G_FEEDBACK_BUSY = 0x7F474, 0x7F488


def find_click(lib, mode, cmd):
    """The first screen point whose left click in input mode gives command cmd, and the rect of its zone."""
    rect = (ctypes.c_int16 * 4)()
    lib.input_click_command.restype = ctypes.c_uint16
    x, y = next((x, y) for y in range(C.SCREEN_H) for x in range(C.SCREEN_W)
                if lib.input_click_command(mode, x, y, C.INPUT_BUTTON_LEFT, rect) == cmd)
    return x, y, tuple(rect)


def click_feedback(lib, original, cmd, where, mode=C.INPUT_MODE_MOVE, clicks=None):
    """A left click on the button of command cmd in input mode in the port; the original starts the click feedback
    script of the same command and rect. Both run the script tick by tick until it ends; the screens must be equal
    after each tick. The queued commands and the disabled input are cleared after the script. clicks: the click
    points of the caller (command: x, y, rect); a missing command gets the point of find_click."""
    emu = original.emu
    clicks = {} if clicks is None else clicks
    if cmd not in clicks:
        clicks[cmd] = find_click(lib, mode, cmd)
    x, y, rect = clicks[cmd]
    loop = MainLoop.in_dll(lib, "g_main_loop")
    lib.input_reset_states()
    lib.input_flush()
    emu.write(G_CLICK_FEEDBACK_DELAY, bytes(2))
    emu.write_u32(G_FEEDBACK_WAITING, 0)
    emu.write_u32(G_FEEDBACK_BUSY, 0)
    lib.input_set_mode(mode)
    lib.input_mouse_click(x, y, C.INPUT_BUTTON_LEFT)
    emu.write(G_CURRENT_COMMAND, struct.pack("<3H4h", x, y, cmd, *rect))
    emu.write_u32(G_CLICK_FEEDBACK_SCRIPT, emu.call(INPUT_GET_CLICK_FEEDBACK_SCRIPT, G_CURRENT_COMMAND))
    for tick in range(20):
        loop.timer_ticks += 1
        lib.input_poll()
        emu.call(INPUT_RUN_CLICK_FEEDBACK, int(tick > 0))
        mine, theirs = c_screen(lib), original.screen()
        assert mine == theirs, (where, "feedback", hex(cmd), tick, diff(mine, theirs, C.SCREEN_W))
        if not emu.read_u32(G_CLICK_FEEDBACK_SCRIPT):
            break
    assert not emu.read_u32(G_CLICK_FEEDBACK_SCRIPT), where
    lib.input_flush()
    emu.write(G_INPUT_DISABLED, bytes(2))
    emu.write(G_COMMAND_QUEUE_HEAD, bytes(2))
    emu.write(G_COMMAND_QUEUE_TAIL, bytes(2))
