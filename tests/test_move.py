"""Scenario tests of Step 14 with the emulator oracle: start.DAT goes into the C port and into the memory of the
original code; both run the same commands and timeline events; then the game state must be equal."""

import ctypes
import struct

import pytest

from conftest import ROOT
from dm2tools.emu import SCRATCH
from test_save import GRAPHICS, START, original_save
from test_view import OriginalView


def original_view_of(lib, gdat, start):
    """The original code after loading start, with the GDAT data of the C port (test_view.OriginalView)."""
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.gdat_image_colormap.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.gdat_image_colormap.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.gdat_decode_image.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
    lib.gdat_image_size.argtypes = [ctypes.c_void_p, ctypes.c_int] + [ctypes.POINTER(ctypes.c_int)] * 3
    lib.gdat_image_offset.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3
    lib.gdat_exists.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.text_find_int_field.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    return OriginalView(lib, gdat, start)

ENDOFLIST, NONE = 0xFFFE, 0xFFFF

# Original functions of Step 14.
COMMAND_EXECUTE, MOVE_PARTY, MOVE_PARTY_TURN, MOVE_THING = 0x21D6C, 0x235BF, 0x2339D, 0x4B108
TIMELINE_PROCESS, TIMELINE_ADD_EVENT, DUN_SET_PARTY_LOCATION = 0x59A5C, 0x56390, 0x1C82D
SENSOR_PROCESS_FLOOR, GAME_ENTER_MAP = 0x4CDCC, 0x24629
# Functions of later steps and of the DOS layer: (address, stack arguments, result).
STUBS = {
    0x15BAA: (5, 0), 0x15CA9: (4, 0), 0x1616D: (0, 0), 0x160DB: (0, 0),     # sound
    0x138D9: (0, 0), 0x42AE4: (0, 0),                                       # screen, movement arrows
    0x209E0: (0, 0), 0x14C55: (0, 0), 0x14C68: (0, 0),                     # input flush, mouse (test_view stubs the poll)
    0x5599F: (0, 0),                                                         # message area
    0x4722A: (0, 0), 0x4766B: (0, 0),                                        # champion damage
    0x24E62: (2, 0), 0x24328: (0, 0), 0x24171: (0, 0), 0x23E5B: (0, 0),     # creature alert, push
    0x49EF8: (0, 0), 0x34106: (0, 0), 0x2F8F5: (0, 0), 0x306A8: (0, 0),     # creature turn, activation
    0x3085A: (0, 0), 0x3064D: (0, 0), 0x30D45: (1, 0), 0x30FE3: (0, 0),     # creature groups
    0x4A096: (1, 0), 0x4A005: (1, 0), 0x4A0FE: (0, 0), 0x30BA6: (1, NONE), # retry, missiles, create group
    0x2538C: (0, 0), 0x57A63: (0, 0), 0x56BA5: (4, 0), 0x56B39: (0, 0),     # sensors of creatures, missiles
    0x57E6C: (4, 0), 0x5A073: (0, 0),                                       # object transfer, weather
}
# Globals of the movement state: name -> (address, struct format).
GLOBALS = {
    "cur_map": (0x72244, "<h"), "square_x": (0x7F27C, "<h"), "square_y": (0x7F27A, "<h"), "square_map": (0x7F27E, "<h"),
    "ref_dir": (0x7F260, "<h"), "move_steps": (0x7F258, "<h"), "move_command": (0x7F252, "<H"),
    "view_x": (0x7F266, "<h"), "view_y": (0x7F264, "<h"), "view_dir": (0x7F274, "<h"),
    "alt_view_active": (0x7F230, "<i"), "alt_view_x": (0x7F25C, "<h"), "alt_view_y": (0x7F25E, "<h"),
    "alt_view_dir": (0x7F272, "<h"), "alt_view_map": (0x7F278, "<h"), "new_map": (0x717F4, "<h"),
    "dest_x": (0x80026, "<h"), "dest_y": (0x80024, "<h"), "dest_map": (0x80028, "<h"), "dest_cell": (0x80022, "<h"),
    "redraw": (0x7F38C, "<I"),
}
G_GAME_TIME, G_MOVE_COOLDOWN, G_THROW_COOLDOWN, G_PUSH_PENDING = 0x7F22C, 0x7F26A, 0x7F25A, 0x72FFC
G_ACTIVE_GROUPS, G_COMMAND_STOP = 0x7F898, 0x7F484
COMMAND_RECORD = SCRATCH + 0xF8000
SCHEDULED_EVENT = SCRATCH + 0xF8100
SENSOR_SCHEDULE_SQUARE_EVENT, GDAT_LOAD_MAP_GRAPHICS = 0x4BBE4, 0x3AB31
# The wall sensor that creates an item (kind 0x3c) in timeline_event_wall_square: Step 17.
WALL_CREATE_ITEM, WALL_NEXT_THING = 0x58B04, 0x58C56


class Party(ctypes.Structure):
    _fields_ = [(name, ctypes.c_int16) for name in ("cur_map", "square_x", "square_y", "square_map", "ref_dir",
                                                     "move_steps")] + [
        ("move_command", ctypes.c_uint16), ("view_x", ctypes.c_int16), ("view_y", ctypes.c_int16),
        ("view_dir", ctypes.c_int16), ("alt_view_active", ctypes.c_int), ("alt_view_x", ctypes.c_int16),
        ("alt_view_y", ctypes.c_int16), ("alt_view_dir", ctypes.c_int16), ("alt_view_map", ctypes.c_int16),
        ("new_map", ctypes.c_int16), ("fell", ctypes.c_int16), ("dest_x", ctypes.c_int16), ("dest_y", ctypes.c_int16),
        ("dest_map", ctypes.c_int16), ("dest_cell", ctypes.c_int16), ("dest_dir", ctypes.c_int16),
        ("teleporter_flag", ctypes.c_int), ("redraw", ctypes.c_uint32), ("push_pending_group", ctypes.c_uint16),
        ("push_mode", ctypes.c_int), ("push_arrow", ctypes.c_int16)]


class GameRecord(ctypes.Structure):
    _fields_ = [("game_time", ctypes.c_uint32), ("champion_count", ctypes.c_uint16), ("party_x", ctypes.c_uint16),
                ("party_y", ctypes.c_uint16), ("party_dir", ctypes.c_uint16), ("party_map", ctypes.c_uint16),
                ("leader", ctypes.c_uint16), ("party_move_cooldown", ctypes.c_uint16), ("time_a", ctypes.c_uint32),
                ("time_b", ctypes.c_uint32), ("throw_cooldown", ctypes.c_uint16), ("throw_dir", ctypes.c_uint16)]


class Command(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16), ("command", ctypes.c_uint16), ("rect", ctypes.c_int16 * 4)]


@pytest.fixture(scope="module")
def gdat(lib):
    if not GRAPHICS.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == 0
    yield g
    lib.gdat_free(g)


@pytest.fixture(scope="module")
def start():
    if not START.exists():
        pytest.skip("tests/saves/start.DAT missing")
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return START.read_bytes()


class Scenario:
    """start.DAT in the C port and in the original code, run step by step."""

    def __init__(self, lib, gdat, start, tmp_path):
        self.lib, self.gdat, self.start, self.tmp = lib, gdat, start, tmp_path
        path = tmp_path / "SKSAVE0.DAT"
        path.write_bytes(start)
        assert lib.save_load(str(path).encode(), gdat) == 0
        lib.draw_init(gdat)
        self.game = GameRecord.in_dll(lib, "game")
        self.party = Party.in_dll(lib, "party")
        assert lib.zone_load(gdat) == 1 and lib.pal_load_color_ramps(gdat) == 1
        view = original_view_of(lib, gdat, start)
        self.emu = emu = view.emu
        for addr, (stack, result) in STUBS.items():
            emu.stub(addr, lambda *_, r=result: r, stack_args=stack)
        self.fix_square_events()
        self.view = view
        emu.stub(GDAT_LOAD_MAP_GRAPHICS, lambda *_: self.load_map_graphics())
        self.skip(WALL_CREATE_ITEM, WALL_NEXT_THING)
        emu.write(GLOBALS["cur_map"][0], struct.pack("<h", -1))
        emu.write(G_PUSH_PENDING, struct.pack("<H", NONE))
        for name, (addr, fmt) in GLOBALS.items():  # the runtime state that a save does not store
            setattr(self.party, name, struct.unpack(fmt, emu.read(addr, struct.calcsize(fmt)))[0])
        self.party.fell, self.party.push_mode, self.party.push_pending_group = 0, 0, NONE
        self.party.cur_map = -1
        self.place(self.game.party_map, self.game.party_x, self.game.party_y, self.game.party_dir)

    def load_map_graphics(self):
        """The view globals of the party map, as the C view_enter_map sets them."""
        import test_view as tv
        lib, emu, view = self.lib, self.emu, self.view
        map_ = struct.unpack("<h", emu.read(0x7F262, 2))[0]
        graphics, flags = lib.dungeon_map_graphics(map_), lib.dungeon_map_flags(map_)
        gfx_set = graphics >> 4 & 0xF
        emu.write(tv.G_MAP_GFX_SET, bytes([gfx_set]))
        emu.write(tv.G_CUR_MAP_DOOR_TYPES, bytes([graphics >> 8 & 0xF if flags & 0x80 else 0xFF,
                                                  graphics >> 12 & 0xF if flags & 0x100 else 0xFF]))
        emu.write(tv.G_MAP_WALL_COLOR_KEY, struct.pack("<H", view.entry_index(8, gfx_set, 0x0B, 0x64)))
        emu.write(tv.G_MAP_FLIP_FLAGS, struct.pack("<H", view.entry_index(8, gfx_set, 0x0B, 0x65)))
        emu.write(tv.G_VIEW_CLEAR, bytes(2))
        weather = tv.WeatherGame.in_dll(lib, "game")
        sky = view.sky_objects(gfx_set, weather.sky_darkness, weather.cloud_cover) if weather.weather_visible else b"\xff" + bytes(9)
        emu.write(tv.G_SKY_OBJECTS, sky)
        emu.write(tv.G_SKY_STATE, struct.pack("<H", view.entry_index(8, gfx_set, 0x0B, 0x66)))
        lib.view_enter_map(map_)

    def skip(self, begin, end):
        """Runs the original from end when it reaches begin: a code part of a later step."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EIP
        self.emu.uc.hook_add(UC_HOOK_CODE, lambda uc, *_: uc.reg_write(UC_X86_REG_EIP, end), begin=begin, end=begin)

    def fix_square_events(self):
        """The original sensor_schedule_square_event leaves 2 data bytes (and the priority of a hold) uninitialized;
        this runs it with them cleared, like the C port, and then the original timeline_add_event."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, \
            UC_X86_REG_ESP
        emu = self.emu

        def hook(uc, *_):
            try:
                schedule(uc)
            except Exception as e:  # exceptions do not cross the Unicorn callbacks
                emu._fail(f"square event stub: {e!r}")

        def schedule(uc):
            x, y, cell, effect = (uc.reg_read(r) & 0xFF for r in (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                                                                  UC_X86_REG_ECX))
            sp = uc.reg_read(UC_X86_REG_ESP)
            ret, time = emu.read_u32(sp), emu.read_u32(sp + 4)
            cur_map = struct.unpack("<h", emu.read(GLOBALS["cur_map"][0], 2))[0]
            event = struct.pack("<IBB6B", (time | (cur_map & 0xFF) << 24) & 0xFFFFFFFF, 4, (1, 3, 2, 3)[effect & 3],
                                x, y, cell, effect, 0, 0)
            emu.write(SCHEDULED_EVENT, event)
            emu.write_u32(sp + 4, ret)
            uc.reg_write(UC_X86_REG_ESP, sp + 4)
            uc.reg_write(UC_X86_REG_EAX, SCHEDULED_EVENT)
            uc.reg_write(UC_X86_REG_EIP, TIMELINE_ADD_EVENT)

        emu.uc.hook_add(UC_HOOK_CODE, hook, begin=SENSOR_SCHEDULE_SQUARE_EVENT, end=SENSOR_SCHEDULE_SQUARE_EVENT)

    def place(self, map_, x, y, direction):
        """Puts the party on (x, y) of map_ in both, like a load."""
        self.game.party_dir = direction
        self.emu.write(0x7F254, struct.pack("<h", direction))
        for i in range(self.game.champion_count):
            pass
        self.party.cur_map = -1
        self.emu.write(GLOBALS["cur_map"][0], struct.pack("<h", -1))
        self.lib.dun_set_party_location(x, y, map_)
        self.emu.call(DUN_SET_PARTY_LOCATION, x, y, map_)
        self.load_map_graphics()

    def time(self):
        return self.game.game_time

    def set_time(self, t):
        self.game.game_time = t
        self.emu.write_u32(G_GAME_TIME, t)

    def command(self, command):
        """Executes a command in both; returns the two results."""
        c = Command(0, 0, command)
        self.emu.write(COMMAND_RECORD, bytes(c))
        mine = self.lib.command_execute(ctypes.byref(c))
        theirs = self.emu.call(COMMAND_EXECUTE, COMMAND_RECORD)
        return mine, theirs

    def step(self):
        """The game loop part that this test runs per step: the timeline, the walk, and the cooldowns."""
        self.lib.timeline_process()
        self.emu.call(TIMELINE_PROCESS)
        if self.party.move_steps:
            if self.party.move_steps == 1:
                self.lib.move_party(self.party.move_command)
                self.emu.call(MOVE_PARTY, self.party.move_command)
            self.party.move_steps -= 1
            self.emu.write(GLOBALS["move_steps"][0], struct.pack("<h", self.party.move_steps))
        for addr, field in ((G_MOVE_COOLDOWN, "party_move_cooldown"), (G_THROW_COOLDOWN, "throw_cooldown")):
            value = getattr(self.game, field)
            if value:
                setattr(self.game, field, value - 1)
                self.emu.write(addr, struct.pack("<H", value - 1))
        self.set_time(self.time() + 1)

    def state(self):
        """(C, original) save files and movement globals."""
        self.emu.written = bytearray()
        theirs = original_save(self.emu, self.start)
        path, bak = self.tmp / "SKSAVE0.DAT", self.tmp / "SKSAVE0.BAK"
        path.write_bytes(self.start)
        name = self.start[2:0x12].split(b"\0")[0]
        assert self.lib.save_write(str(path).encode(), str(bak).encode(), name, self.gdat) == 0
        mine = path.read_bytes()
        mine_globals = {name: getattr(self.party, name) for name in GLOBALS}
        their_globals = {name: struct.unpack(fmt, self.emu.read(addr, struct.calcsize(fmt)))[0]
                         for name, (addr, fmt) in GLOBALS.items()}
        return (mine, mine_globals), (theirs, their_globals)

    def differences(self):
        """Where the dungeons differ: squares, thing records, timeline events."""
        lib, emu = self.lib, self.emu
        out = []
        columns = emu.read_u32(0x7F3C8)
        for map_ in range(lib.dungeon_map_count()):
            w, h = lib.dungeon_map_width(map_), lib.dungeon_map_height(map_)
            table = emu.read_u32(columns + 4 * map_)
            for x in range(w):
                col = emu.read(emu.read_u32(table + 4 * x), h)
                for y in range(h):
                    if col[y] != lib.dungeon_square(map_, x, y):
                        out.append(("square", map_, x, y, hex(lib.dungeon_square(map_, x, y)), hex(col[y])))
        lib.dungeon_thing_record.restype = ctypes.c_void_p
        sizes = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
        for t in range(16):
            n = lib.dungeon_thing_count(t)
            if not n:
                continue
            mine = ctypes.string_at(lib.dungeon_thing_record(t << 10), n * sizes[t])
            theirs = emu.read(emu.read_u32(0x7F288 + 4 * t), n * sizes[t])
            for i in range(n):
                a, b = mine[i * sizes[t]:(i + 1) * sizes[t]], theirs[i * sizes[t]:(i + 1) * sizes[t]]
                if a != b:
                    out.append(("thing", t, i, a.hex(), b.hex()))
        import test_save
        tl = test_save.Timeline.in_dll(lib, "timeline")
        mine = ctypes.string_at(ctypes.cast(tl.events, ctypes.c_void_p), tl.capacity * 12)
        theirs = emu.read(emu.read_u32(0x760E8), tl.capacity * 12)
        for i in range(tl.capacity):
            if mine[i * 12:i * 12 + 12] != theirs[i * 12:i * 12 + 12]:
                out.append(("event", i, mine[i * 12:i * 12 + 12].hex(), theirs[i * 12:i * 12 + 12].hex()))
        return out

    def assert_same(self, where):
        """The quick check: movement globals and the dungeon records and events."""
        mine = {name: getattr(self.party, name) for name in GLOBALS}
        theirs = {name: struct.unpack(fmt, self.emu.read(addr, struct.calcsize(fmt)))[0]
                  for name, (addr, fmt) in GLOBALS.items()}
        assert mine == theirs, where
        assert self.differences() == [], where

    def assert_equal(self, where):
        (mine, mg), (theirs, tg) = self.state()
        assert mg == tg, where
        if mine != theirs:
            for d in self.differences()[:20]:
                print(d)
            first = next(i for i in range(min(len(mine), len(theirs))) if mine[i] != theirs[i])
            pytest.fail(f"{where}: save files differ from byte {first:#x} ({len(mine)} and {len(theirs)} bytes)")


@pytest.fixture
def scenario(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return Scenario(lib, gdat, start, tmp_path)


def test_states_equal_after_load(scenario):
    scenario.assert_equal("load")


def test_walk_and_turn_at_start(scenario):
    # The start room: forward, turns, side steps, and moves into walls (bumps).
    sequence = [3, 3, 3, 2, 3, 3, 1, 1, 6, 5, 5, 4, 3, 3, 3, 3, 2, 3, 3, 3, 1, 3, 3]
    for i, command in enumerate(sequence):
        for _ in range(4):
            scenario.step()
        mine, theirs = scenario.command(command)
        assert mine == theirs, (i, command)
        scenario.assert_equal((i, command))


def square_event(scenario, map_, x, y, cell, effect, delay):
    """Queues the same square event (type 4) in both."""
    import test_save
    e = test_save.Event((scenario.time() + delay) | map_ << 24, 4, {0: 1, 1: 3, 2: 2, 3: 3}[effect],
                        (ctypes.c_uint8 * 6)(x, y, cell, effect, 0, 0))
    scenario.emu.write(COMMAND_RECORD, bytes(e))
    scenario.lib.timeline_add_event(ctypes.byref(e))
    scenario.emu.call(TIMELINE_ADD_EVENT, COMMAND_RECORD)


def squares(lib, kinds):
    """All (map, x, y, square) whose square type is in kinds."""
    out = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                s = lib.dungeon_square(map_, x, y)
                if s >> 5 in kinds:
                    out.append((map_, x, y, s))
    return out


def test_random_walks_on_all_maps(scenario, lib):
    # Random places on all maps, random moves and turns, and square events on the squares around the party:
    # walls, floors, doors, pits, teleporters, stairs, fake walls, and their sensors.
    import random
    rng = random.Random(14)
    places = squares(lib, (1, 2, 3, 4, 5, 6))
    for n in range(150):
        map_, x, y, _ = rng.choice(places)
        scenario.place(map_, x, y, rng.randrange(4))
        for i in range(8):
            for _ in range(rng.randrange(1, 4)):
                scenario.step()
            if rng.random() < 0.3:
                m = scenario.game.party_map
                ex = scenario.game.party_x + rng.randrange(-2, 3)
                ey = scenario.game.party_y + rng.randrange(-2, 3)
                if 0 <= ex < lib.dungeon_map_width(m) and 0 <= ey < lib.dungeon_map_height(m):
                    square_event(scenario, m, ex, ey, rng.randrange(4), rng.randrange(4), rng.randrange(3))
            command = rng.choice((1, 2, 3, 3, 3, 4, 5, 6))
            mine, theirs = scenario.command(command)
            assert mine == theirs, (n, i, command)
            scenario.assert_same((n, map_, x, y, i, command))
        if n % 10 == 9:
            scenario.assert_equal((n, map_, x, y))  # the save files: the champions and the game record too
