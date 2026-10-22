"""The main loop harness: start.DAT ready to play in the C port, and the main loop of both run tick by tick."""

import ctypes
import struct

import pytest

from dm2test.scenario import Scenario


INPUT_INIT_TABLES, INPUT_FIND_CLICK_COMMAND, INPUT_FIND_KEY_COMMAND = 0x20847, 0x203A3, 0x2048A


class MainLoop(ctypes.Structure):
    _fields_ = [("tick_threshold", ctypes.c_int), ("tick_count", ctypes.c_int), ("timer_ticks", ctypes.c_uint32),
                ("running", ctypes.c_int), ("view_hidden", ctypes.c_int), ("end", ctypes.c_int),
                ("quit", ctypes.c_int)]


@pytest.fixture
def session(lib, gdat, start, tmp_path):
    """start.DAT ready to play, as build/dm2 --save starts it."""
    from dm2test.scenario import GameRecord, Party
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    game = GameRecord.in_dll(lib, "g_game")
    party = Party.in_dll(lib, "g_party")
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
        from dm2test.scenario import G_GAME_TIME, G_MOVE_COOLDOWN, G_THROW_COOLDOWN, GLOBALS, ignore_stale
        game = self.game
        mine = {name: getattr(self.loop, name) for name in LOOP_GLOBALS}
        mine |= {"sleeping": ctypes.c_int.in_dll(self.lib, "g_party_sleeping").value, "time": game.game_time,
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
