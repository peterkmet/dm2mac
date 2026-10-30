"""Differential tests of the start of a new game, the load and save dialogs, the dialogs, the start menu, and the end of the game
against the original code in dm2tools.emu."""

import ctypes
import struct

import pytest

from dm2test import constants as C
from dm2test.markers import ROOT
from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image
from dm2test.save import GDAT_VALUE_TYPES, GRAPHICS
from dm2test.scenario import square_things
from dm2test.lib import load_lib

pytestmark = pytest.mark.usefixtures("program_start_after")


@pytest.fixture(scope="module")
def lib():
    return load_lib()

DUNGEON = ROOT / "game" / "DATA" / "DUNGEON.DAT"
DUNGEON_LOAD, MEM_ALLOC, GDAT_ENTRY_INDEX, WEATHER_INIT = 0x36909, 0x3BBBA, 0x3C5D3, 0x59F38
SENSOR_START_TIMERS, LINK_THINGS_AFTER_LOAD = 0x594A1, 0x3624F
G_NEW_GAME, G_RANDOM_SEED, G_GAME_TIME, G_LEVEL_DIGITS = 0x7F280, 0x70518, 0x7F22C, 0x7F3D0
G_THING_DATA, G_MAP_COLUMNS, G_SQUARE_FIRST_THINGS, G_DUNGEON_HEADER = 0x7F288, 0x7F3C8, 0x7F388, 0x7F3E8
G_TIMELINE_EVENTS, G_TIMELINE_COUNT = 0x760E8, 0x8042A
THING_SIZES = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)


@pytest.fixture(scope="module")
def gdat(lib):
    if not GRAPHICS.exists() or not DUNGEON.exists() or not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game files missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == C.GDAT_OK
    lib.draw_init(g)
    assert lib.text_init(g)
    yield g
    lib.gdat_free(g)


def entry_index_stub(lib, gdat):
    """gdat_entry_index of the original, answered from GRAPHICS.DAT by the C port."""
    def entry_index(category, index, type_, entry, *_):
        value = lib.gdat_find(gdat, category & 0xFF, index & 0xFF, type_ & 0xFF, entry & 0xFF)
        if type_ & 0xFF in GDAT_VALUE_TYPES:
            return max(value, 0)
        return value & 0xFFFF
    return entry_index


class NewGame:
    """The DUNGEON.DAT load of a new game in the original: the dungeon, the link pass, the level digits, the timeline,
    and the timer sensors; the weather start is a stub."""

    def __init__(self, lib, gdat, before_link=None):
        """before_link(emu): runs when the original reaches the link pass."""
        self.emu = emu = Emu(load_image(ROOT / "game" / "SKULL.EXE"), max_insns=200_000_000)
        emu.serve_file(DUNGEON.read_bytes())
        self.heap = SCRATCH
        emu.stub(MEM_ALLOC, self.alloc)
        emu.stub(GDAT_ENTRY_INDEX, entry_index_stub(lib, gdat))
        emu.stub(WEATHER_INIT, lambda *_: 0)
        if before_link:
            from unicorn import UC_HOOK_CODE
            emu.uc.hook_add(UC_HOOK_CODE, lambda *_: before_link(emu), begin=LINK_THINGS_AFTER_LOAD,
                            end=LINK_THINGS_AFTER_LOAD)
        emu.write(G_NEW_GAME, struct.pack("<H", 1))
        emu.call(DUNGEON_LOAD, 1)

    def alloc(self, size, *_):
        addr, self.heap = self.heap, self.heap + (size + 3 & ~3)
        self.emu.write(addr, bytes(size))
        return addr


def c_new_game(lib, gdat, before_link=None):
    """The same in the C port: the steps of session.c load_new_game, then the timer sensors."""
    ctypes.c_uint32.in_dll(lib, "g_random_seed").value = 0
    assert lib.dungeon_load(str(DUNGEON).encode()) == 1
    lib.timeline_init(50 + lib.dungeon_thing_count(C.THING_GROUP)
                       + lib.dungeon_thing_count(C.THING_PROJECTILE) + lib.dungeon_thing_count(C.THING_EXPLOSION))
    if before_link:
        before_link(None)
    lib.dungeon_link_things_after_load()
    lib.dungeon_read_level_table()


def dungeon_differences(lib, emu):
    """Where the dungeons of the C port and the original differ: squares, first things, thing records."""
    out = []
    columns = emu.read_u32(G_MAP_COLUMNS)
    for map_ in range(lib.dungeon_map_count()):
        w, h = lib.dungeon_map_width(map_), lib.dungeon_map_height(map_)
        table = emu.read_u32(columns + 4 * map_)
        for x in range(w):
            col = emu.read(emu.read_u32(table + 4 * x), h)
            mine = bytes(lib.dungeon_square(map_, x, y) for y in range(h))
            if col != mine:
                out.append(("squares", map_, x, mine.hex(), col.hex()))
    count = lib.dungeon_square_thing_count()
    lib.dungeon_square_things.restype = ctypes.c_void_p
    if ctypes.string_at(lib.dungeon_square_things(), 2 * count) != emu.read(emu.read_u32(G_SQUARE_FIRST_THINGS), 2 * count):
        out.append("first things")
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    for t in range(C.THING_TYPE_COUNT):
        n, size = lib.dungeon_thing_count(t), THING_SIZES[t]
        if not n:
            continue
        mine = ctypes.string_at(lib.dungeon_thing_record(t << 10), n * size)
        theirs = emu.read(emu.read_u32(G_THING_DATA + 4 * t), n * size)
        out += [("thing", t, i, mine[i * size:(i + 1) * size].hex(), theirs[i * size:(i + 1) * size].hex())
                for i in range(n) if mine[i * size:(i + 1) * size] != theirs[i * size:(i + 1) * size]]
    return out


@pytest.fixture(scope="module")
def new_game(lib, gdat):
    original = NewGame(lib, gdat)
    c_new_game(lib, gdat)
    return original


def test_new_game_dungeon_matches_original(lib, new_game):
    # The link pass of a new game: teleporter links and shared lists, level link marks, sensor and text start states,
    # creature health, homes, and possession cells (random), against the original dungeon_load(1).
    emu = new_game.emu
    assert dungeon_differences(lib, emu) == []
    raw = lib.dungeon_thing_count(C.THING_GROUP)
    assert raw and all(struct.unpack_from("<H", emu.read(emu.read_u32(G_THING_DATA + 16) + 16 * i + 6, 2))[0]
                       for i in range(20))  # the groups got their health
    assert ctypes.c_uint32.in_dll(lib, "g_random_seed").value == emu.read_u32(G_RANDOM_SEED)


def test_level_digits_match_original(lib, new_game):
    emu = new_game.emu
    last = max(lib.dungeon_map_level(m) for m in range(lib.dungeon_map_count()))
    digits = emu.read(emu.read_u32(G_LEVEL_DIGITS), last + 1)
    assert [lib.dungeon_level_digit(level) for level in range(last + 1)] == list(digits)
    assert len(set(digits)) > 1


def test_timer_sensors_start_like_original(lib, new_game):
    # sensor_start_timers of a new game. DUNGEON.DAT has 18 timer sensors, all off: every second one is turned on
    # (state bit) and busy in both, at a game time that is not 0; then the timer events and the busy bits match.
    from dm2test.structs import Timeline
    emu = new_game.emu
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    base = emu.read_u32(G_THING_DATA + 12)
    timers = 0
    for i in range(lib.dungeon_thing_count(C.THING_SENSOR)):
        rec = lib.dungeon_thing_record(C.THING_SENSOR << 10 | i)
        kind = ctypes.c_uint16.from_address(rec + 2).value & 0x7F
        if kind != 0x1E and not 0x33 <= kind <= 0x37:
            continue
        flags = ctypes.c_uint16.from_address(rec + 4)
        flags.value |= 1 | (4 if timers % 2 else 0)
        emu.write(base + 8 * i + 4, struct.pack("<H", flags.value))
        timers += 1
    assert timers == 18
    ctypes.c_uint32.in_dll(lib, "g_game").value = 1234  # Game.game_time is the first field
    emu.write_u32(G_GAME_TIME, 1234)
    lib.sensor_start_timers()
    emu.call(SENSOR_START_TIMERS)
    tl = Timeline.in_dll(lib, "g_timeline")
    assert tl.count == struct.unpack("<H", emu.read(G_TIMELINE_COUNT, 2))[0] == 9
    mine = ctypes.string_at(ctypes.cast(tl.events, ctypes.c_void_p), tl.capacity * C.TIMELINE_EVENT_SIZE)
    theirs = emu.read(emu.read_u32(G_TIMELINE_EVENTS), tl.capacity * C.TIMELINE_EVENT_SIZE)
    assert mine == theirs
    assert dungeon_differences(lib, emu) == []


def test_shared_teleporter_list_keeps_first_movable_thing(lib, gdat):
    # A teleporter pair with numbered link sensors: square (3, 19, 3) gets two free weapons at the end of its list,
    # square (2, 19, 14) one. The shared list keeps the first weapon of the target square and drops the second, in
    # both. DUNGEON.DAT has no movable thing on such a target square.
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    weapons, links = [], []

    def add_weapons(emu):
        if not links:  # the C port first: its lists before the link pass
            weapons.extend([C.THING_WEAPON << 10 | i for i in range(lib.dungeon_thing_count(C.THING_WEAPON))
                            if ctypes.c_uint16.from_address(lib.dungeon_thing_record(C.THING_WEAPON << 10 | i)).value == C.THING_NONE][:3])
            links.extend([(square_things(lib, 3, 19, 3)[-1], weapons[0]), (weapons[0], weapons[1]),
                          (weapons[1], C.THING_ENDOFLIST), (square_things(lib, 2, 19, 14)[-1], weapons[2]), (weapons[2], C.THING_ENDOFLIST)])
        base = emu.read_u32(G_THING_DATA + 4 * C.THING_WEAPON) if emu else None
        for thing, nxt in links:
            if emu:
                t = thing >> C.THING_TYPE_SHIFT
                emu.write(emu.read_u32(G_THING_DATA + 4 * t) + THING_SIZES[t] * (thing & C.THING_INDEX_MASK), struct.pack("<H", nxt))
            else:
                ctypes.c_uint16.from_address(lib.dungeon_thing_record(thing)).value = nxt
        for w in weapons:  # a dagger
            if emu:
                emu.write(base + 4 * (w & C.THING_INDEX_MASK) + 2, struct.pack("<H", 8))
            else:
                ctypes.c_uint16.from_address(lib.dungeon_thing_record(w) + 2).value = 8

    c_new_game(lib, gdat, add_weapons)
    original = NewGame(lib, gdat, add_weapons)
    assert dungeon_differences(lib, original.emu) == []
    first, dropped, other = weapons
    shared = square_things(lib, 3, 19, 3)[1:]  # after the teleporter of each square
    assert shared == square_things(lib, 2, 19, 14)[1:]
    assert first in shared and other in shared and dropped not in shared
