import ctypes
import struct

import pytest

from dm2test.markers import ROOT
from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

START = ROOT / "tests" / "saves" / "start.DAT"
GRAPHICS = ROOT / "game" / "DATA" / "GRAPHICS.DAT"

# The file part of session_load: header, dungeon image, game state, things, timed effects, event links.
LOAD_BEGIN, LOAD_END = 0x372D6, 0x37586
# The file part of session_save, from the header in its stack frame to the last flushed bit.
SAVE_BEGIN, SAVE_END = 0x35236, 0x35674
SAVE_HEADER_LOCAL = 0x3C  # ESP offset of the 42-byte header buffer in session_save
G_SAVE_NAME = 0x7F8FE
MEM_ALLOC = 0x3BBBA
GDAT_ENTRY_INDEX = 0x3C5D3
WEATHER_INIT = 0x59F38
READ_LEVEL_TABLE = 0x367DA
GDAT_VALUE_TYPES = (0x0B, 0x0C)
# Globals of the game record: (address, size) by Game field.
RECORD_GLOBALS = {"game_time": (0x7F22C, 4), "random_seed": (0x70518, 4), "champion_count": (0x7F276, 2),
                  "party_x": (0x7F26C, 2), "party_y": (0x7F26E, 2), "party_dir": (0x7F254, 2), "party_map": (0x7F262, 2)}

SAVE_OK, SAVE_ERR_FORMAT, SAVE_ERR_DATA = 0, -2, -3
HEADER_SIZE = 0x2A
DUNGEON_HEADER_SIZE = 0x2C
STREAM = HEADER_SIZE + 43443  # start of the bit stream in start.DAT, after the dungeon image


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
    return START.read_bytes()


def original_load(lib, gdat, data):
    """Runs the original load of data; returns the emulator.

    GRAPHICS.DAT lookups go to the C gdat_find; the load stubs weather_init and the level digit table,
    which do not reach the file.
    """
    emu = Emu(load_image(ROOT / "game" / "SKULL.EXE"), max_insns=100_000_000)
    emu.serve_file(data)
    heap = [SCRATCH]

    def alloc(size, *_):
        heap[0] += size + 3 & ~3
        return heap[0] - (size + 3 & ~3)

    def entry_index(category, index, type_, entry):
        value = lib.gdat_find(gdat, category & 0xFF, index & 0xFF, type_ & 0xFF, entry & 0xFF)
        if type_ & 0xFF in GDAT_VALUE_TYPES:
            return max(value, 0)
        return value & 0xFFFF

    emu.stub(MEM_ALLOC, alloc)
    emu.stub(GDAT_ENTRY_INDEX, entry_index)
    emu.stub(WEATHER_INIT, lambda *_: 0)
    emu.stub(READ_LEVEL_TABLE, lambda *_: 0)
    emu.run(LOAD_BEGIN, LOAD_END)
    return emu


def original_save(emu, data):
    """Runs the original save into the slot of data; returns the written file.

    The save keeps the header of data, like a save into the slot that the game was loaded from.
    """
    emu.write(emu.frame() + SAVE_HEADER_LOCAL, data[:HEADER_SIZE])
    emu.write(G_SAVE_NAME, data[2:0x12])
    emu.run(SAVE_BEGIN, SAVE_END)
    return bytes(emu.written)


def original_round_trip(lib, gdat, data):
    """Runs the original load, then the original save; returns the written file and the record globals."""
    emu = original_load(lib, gdat, data)
    globals_ = {name: int.from_bytes(emu.read(addr, size), "little") for name, (addr, size) in RECORD_GLOBALS.items()}
    return original_save(emu, data), globals_


def c_round_trip(lib, gdat, data, tmp_path):
    path, bak = tmp_path / "SKSAVE0.DAT", tmp_path / "SKSAVE0.BAK"
    path.write_bytes(data)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    name = data[2:0x12].split(b"\0")[0]
    assert lib.save_write(str(path).encode(), str(bak).encode(), name, gdat) == SAVE_OK
    assert bak.read_bytes() == data
    return path.read_bytes()


@pytest.fixture(scope="module")
def original_run(lib, gdat, start):
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return original_round_trip(lib, gdat, start)


@pytest.fixture(scope="module")
def original(original_run):
    return original_run[0]


def test_original_round_trip_is_not_byte_equal(start, original):
    # Measured: the load creates the creature groups and items again in other records, so the
    # first-thing table and the thing records change. The bit-packed state stays equal.
    assert len(original) == len(start)
    assert original != start
    assert original[:HEADER_SIZE + DUNGEON_HEADER_SIZE] == start[:HEADER_SIZE + DUNGEON_HEADER_SIZE]


def test_write_equals_original_writer(lib, gdat, start, original, tmp_path):
    assert c_round_trip(lib, gdat, start, tmp_path) == original


def test_second_round_trip_is_byte_equal(lib, gdat, original, tmp_path):
    assert original_round_trip(lib, gdat, original)[0] == original
    assert c_round_trip(lib, gdat, original, tmp_path) == original


def test_load_state_equals_original_load(lib, gdat, start, original_run, tmp_path):
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    game = Game.in_dll(lib, "game")
    loaded = {name: getattr(game, name) for name in RECORD_GLOBALS if name != "random_seed"}
    loaded["random_seed"] = ctypes.c_uint32.in_dll(lib, "random_seed").value
    assert loaded == original_run[1]


def test_new_header_without_old_file(lib, gdat, start, tmp_path):
    path = tmp_path / "SKSAVE1.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    path.unlink()
    assert lib.save_write(str(path).encode(), str(tmp_path / "SKSAVE1.BAK").encode(), b"GAME 1", gdat) == SAVE_OK
    assert path.read_bytes()[:HEADER_SIZE] == b"\1\0GAME 1" + bytes(HEADER_SIZE - 8)


def damaged_saves(start):
    compressed = bytearray(start)
    struct.pack_into("<H", compressed, HEADER_SIZE, 0x8104)
    bad_map = bytearray(start)
    struct.pack_into("<H", bad_map, HEADER_SIZE + DUNGEON_HEADER_SIZE, 0xFFFF)
    ones = bytearray(start)
    ones[-2000:] = b"\xff" * 2000  # every list bit says "one more thing" until the records run out
    zeros = bytearray(start)
    zeros[-6000:] = bytes(6000)
    champions = bytearray(start)
    champions[STREAM + 5] |= 0xE0  # game record bits 40 to 42: champion count 7
    return {
        "header_only": (start[:HEADER_SIZE], SAVE_ERR_FORMAT),
        "short_header": (start[:20], SAVE_ERR_FORMAT),
        "truncated_dungeon": (start[:20000], SAVE_ERR_FORMAT),
        "compressed_dungeon": (bytes(compressed), SAVE_ERR_FORMAT),
        "map_outside_data": (bytes(bad_map), SAVE_ERR_FORMAT),
        "truncated_state": (start[:-3000], SAVE_ERR_DATA),
        "last_byte_missing": (start[:-1], SAVE_ERR_DATA),
        "list_bits_overrun": (bytes(ones), SAVE_ERR_DATA),
        "zeroed_things": (bytes(zeros), None),
        "too_many_champions": (bytes(champions), SAVE_ERR_DATA),
    }


@pytest.mark.parametrize("name", ["header_only", "short_header", "truncated_dungeon", "compressed_dungeon",
                                  "map_outside_data", "truncated_state", "last_byte_missing", "list_bits_overrun",
                                  "zeroed_things", "too_many_champions"])
def test_damaged_save(lib, gdat, start, tmp_path, name):
    data, expected = damaged_saves(start)[name]
    path = tmp_path / "damaged.DAT"
    path.write_bytes(data)
    result = lib.save_load(str(path).encode(), gdat)
    if expected is None:
        assert result in (SAVE_OK, SAVE_ERR_DATA)  # damaged but maybe still parseable: no crash is the test
    else:
        assert result == expected
    if result != SAVE_OK:
        assert lib.dungeon_map_count() == 0


def test_missing_file(lib, gdat, tmp_path):
    assert lib.save_load(str(tmp_path / "none.DAT").encode(), gdat) == -1


class Game(ctypes.Structure):
    _fields_ = [("game_time", ctypes.c_uint32), ("champion_count", ctypes.c_uint16), ("party_x", ctypes.c_uint16),
                ("party_y", ctypes.c_uint16), ("party_dir", ctypes.c_uint16), ("party_map", ctypes.c_uint16)]


class Event(ctypes.Structure):
    _fields_ = [("time_map", ctypes.c_uint32), ("type", ctypes.c_uint8), ("priority", ctypes.c_uint8),
                ("data", ctypes.c_uint8 * 6)]


class Timeline(ctypes.Structure):
    _fields_ = [("events", ctypes.POINTER(Event)), ("queue", ctypes.c_void_p), ("fix_slot", ctypes.c_int16),
                ("first_free", ctypes.c_int16), ("capacity", ctypes.c_uint16), ("count", ctypes.c_uint16),
                ("used_end", ctypes.c_int16)]


def test_projectile_and_explosion_round_trip(lib, gdat, start, tmp_path):
    # start.DAT has no projectile and no explosion: add one of each on the party square.
    path, bak = tmp_path / "SKSAVE0.DAT", tmp_path / "SKSAVE0.BAK"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    game = Game.in_dll(lib, "game")
    square = (game.party_map, game.party_x, game.party_y)
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16

    projectile = lib.dungeon_unused_thing(14)
    lib.set_thing_word(projectile, 2, 0xFF80 | 5)  # payload: a spell
    lib.set_thing_word(projectile, 4, 0x1234)
    move = Event(game.game_time + 10, 0x1D, 0, (ctypes.c_uint8 * 6)(*struct.pack("<H4x", projectile | 1 << 14)))
    lib.set_thing_word(projectile, 6, lib.timeline_add_event(ctypes.byref(move)))
    assert lib.dungeon_link_thing_to_square(projectile | 1 << 14, *square) == 1

    explosion = lib.dungeon_unused_thing(15)
    lib.set_thing_word(explosion, 2, 0xFF87)
    step = Event(game.game_time + 3, 0x19, 0, (ctypes.c_uint8 * 6)(*struct.pack("<2xH2x", explosion | 2 << 14)))
    lib.timeline_add_event(ctypes.byref(step))
    assert lib.dungeon_link_thing_to_square(explosion | 2 << 14, *square) == 1

    assert lib.save_write(str(path).encode(), str(bak).encode(), b"GAME 0", gdat) == SAVE_OK
    first = path.read_bytes()
    timeline = Timeline.in_dll(lib, "timeline")
    written_events = [bytes(timeline.events[i])[4:] for i in range(timeline.capacity)]
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    things = []
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    thing = lib.dungeon_square_first_thing(*square)
    while thing != 0xFFFE:
        things.append(thing)
        thing = lib.dungeon_next_thing(thing)
    loaded = [t for t in things if t >> 10 & 0xF in (14, 15)]
    assert [(t >> 10 & 0xF, t >> 14) for t in loaded] == [(14, 1), (15, 2)]
    assert lib.thing_word(loaded[0], 2) == 0xFF85 and lib.thing_word(loaded[0], 4) == 0x1234
    events = ctypes.cast(ctypes.c_void_p.in_dll(lib, "timeline"), ctypes.POINTER(Event))
    move_index = lib.thing_word(loaded[0], 6)
    assert events[move_index].type == 0x1D and bytes(events[move_index].data[:2]) == struct.pack("<H", loaded[0])
    explosion_index = 4  # after the 3 events of start.DAT and the projectile move
    assert events[explosion_index].type == 0x19 and bytes(events[explosion_index].data[2:4]) == struct.pack("<H", loaded[1])
    assert [bytes(events[i])[4:] for i in range(timeline.capacity)] == written_events
    assert lib.save_write(str(path).encode(), str(bak).encode(), b"GAME 0", gdat) == SAVE_OK
    assert path.read_bytes() == first


G_CHAMPIONS, CHAMPION_SIZE, CHAMPION_DAMAGE_EVENT = 0x7FBD0, 263, 0x2E


def test_load_relinks_champion_damage_events_like_original(lib, gdat, start, tmp_path):
    # A save with queued hide damage events (0x0c) for champion 0 and an altar rebirth event (0x0d): after the load
    # each champion points at its hide damage event (or 0xffff), in the C port and in the original.
    path, bak = tmp_path / "SKSAVE0.DAT", tmp_path / "SKSAVE0.BAK"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    game = Game.in_dll(lib, "game")
    for delay, type_, priority in ((40, 0x0D, 0), (20, 0x0C, 0), (90, 0x0C, 0)):
        event = Event(game.game_time + delay | game.party_map << 24, type_, priority, (ctypes.c_uint8 * 6)())
        lib.timeline_add_event(ctypes.byref(event))
    assert lib.save_write(str(path).encode(), str(bak).encode(), b"GAME 0", gdat) == SAVE_OK
    saved = path.read_bytes()
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    records = ctypes.string_at(ctypes.addressof(game) + 252, 4 * CHAMPION_SIZE)  # offsetof(Game, champions)
    mine = [records[c * CHAMPION_SIZE + CHAMPION_DAMAGE_EVENT:][:2] for c in range(4)]
    emu = original_load(lib, gdat, saved)
    theirs = [emu.read(G_CHAMPIONS + c * CHAMPION_SIZE + CHAMPION_DAMAGE_EVENT, 2) for c in range(4)]
    assert mine == theirs
    assert struct.unpack("<H", mine[0])[0] not in (0, 0xFFFF) and mine[1:] == [b"\xff\xff"] * 3


G_THING_DATA = 0x7F288
G_TIMELINE_EVENTS = 0x760E8
G_TIMELINE_EVENT_MAX = 0x80428
G_TIMELINE_EVENT_COUNT = 0x8042A
THING_SIZES = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
GROUP, CONTAINER, MISC, PROJECTILE, EXPLOSION = 4, 9, 10, 14, 15
ENDOFLIST, NONE = 0xFFFE, 0xFFFF


def c_records(lib, type_):
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    return ctypes.string_at(lib.dungeon_thing_record(type_ << 10), lib.dungeon_thing_count(type_) * THING_SIZES[type_])


def square_lists(lib):
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                things, thing = [], lib.dungeon_square_first_thing(map_, x, y)
                while thing != ENDOFLIST:
                    things.append(thing)
                    thing = lib.dungeon_next_thing(thing)
                yield (map_, x, y), things


def add_event(lib, type_, time, data):
    return lib.timeline_add_event(ctypes.byref(Event(time, type_, 0, (ctypes.c_uint8 * 6)(*data))))


def link_things(lib, game):
    """Puts a group into a move-group event, links a minion map to another group, adds a projectile and an
    explosion, and leaves a gap in the timeline. The group then has another bit stream position than record."""
    lib.thing_word.restype = ctypes.c_uint16
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    square, group = next((sq, t) for sq, things in square_lists(lib) for t in things[1:]
                         if t >> 10 & 0xF == GROUP and t & 0x3FF)
    minion_map = next(CONTAINER << 10 | i for i in range(lib.dungeon_thing_count(CONTAINER))
                      if lib.thing_word(CONTAINER << 10 | i, 0) != NONE and lib.thing_word(CONTAINER << 10 | i, 4) >> 1 & 3 == 1)
    lib.set_thing_word(minion_map, 2, GROUP << 10)

    gap = add_event(lib, 0x01, game.game_time + 1, bytes(6))
    lib.dungeon_unlink_thing_from_square(group, *square)
    add_event(lib, 0x3C, game.game_time + 2, struct.pack("<2xH2x", group))
    projectile = lib.dungeon_unused_thing(PROJECTILE)
    lib.set_thing_word(projectile, 2, 0xFF80 | 5)
    lib.set_thing_word(projectile, 6, add_event(lib, 0x1D, game.game_time + 10, struct.pack("<H4x", projectile | 1 << 14)))
    assert lib.dungeon_link_thing_to_square(projectile | 1 << 14, *square) == 1
    explosion = lib.dungeon_unused_thing(EXPLOSION)
    lib.set_thing_word(explosion, 2, 0xFF87)
    add_event(lib, 0x19, game.game_time + 3, struct.pack("<2xH2x", explosion | 2 << 14))
    assert lib.dungeon_link_thing_to_square(explosion | 2 << 14, *square) == 1
    lib.timeline_delete_event(gap)


@pytest.fixture(scope="module")
def linked(lib, gdat, start, tmp_path_factory):
    """The C writer output and the original writer output for the same state with linked things."""
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    emu = original_load(lib, gdat, start)
    path = tmp_path_factory.mktemp("linked") / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    bases = {t: emu.read_u32(G_THING_DATA + 4 * t) for t in range(16) if THING_SIZES[t]}
    for t, base in bases.items():
        records = c_records(lib, t)
        assert emu.read(base, len(records)) == records

    link_things(lib, Game.in_dll(lib, "game"))
    timeline = Timeline.in_dll(lib, "timeline")
    assert timeline.used_end <= int.from_bytes(emu.read(G_TIMELINE_EVENT_MAX, 2), "little")
    emu.write(emu.read_u32(G_TIMELINE_EVENTS), ctypes.string_at(timeline.events, timeline.used_end * 12))
    emu.write(G_TIMELINE_EVENT_COUNT, struct.pack("<H", timeline.count))
    for t, base in bases.items():
        emu.write(base, c_records(lib, t))

    original = original_save(emu, start)
    assert lib.save_write(str(path).encode(), str(path.with_suffix(".BAK")).encode(), b"GAME 0", gdat) == SAVE_OK
    return path.read_bytes(), original


def test_linked_things_equal_original_writer(linked):
    written, original = linked
    assert written == original


def test_linked_things_round_trip_equals_original(lib, gdat, linked, tmp_path):
    assert c_round_trip(lib, gdat, linked[0], tmp_path) == original_round_trip(lib, gdat, linked[0])[0]


EXPLOSION_EVENT_BIT = 44239 * 8 + 3  # first bit of the explosion's event index in the linked save


def test_linked_things_damaged(lib, gdat, linked, tmp_path):
    written = linked[0]
    # The last stored value is the position of the minion map's group, 1: bits 7 to 0 end with the last 1 bit, then bits 9 and 8.
    i = len(written.rstrip(b"\0")) - 1
    last_one = i * 8 + 8 - (written[i] & -written[i]).bit_length()
    bad_index = bytearray(written)
    for bit in range(last_one - 7, last_one + 3):
        bad_index[bit // 8] |= 0x80 >> bit % 8
    old_format = bytearray(written)  # a format 0 file stores no group in a move-group event
    old_format[0] = 0
    bad_event = bytearray(written)  # the explosion's event index 4 gets bits 9 and 8: 772 is past the timeline
    start = EXPLOSION_EVENT_BIT
    for bit in (start + 8, start + 9):
        bad_event[bit // 8] |= 0x80 >> bit % 8
    for data in (bad_index, old_format, bad_event):
        path = tmp_path / "damaged.DAT"
        path.write_bytes(data)
        assert lib.save_load(str(path).encode(), gdat) == SAVE_ERR_DATA


def test_reserved_misc_records(lib, gdat, start, tmp_path):
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    lib.draw_init(gdat)  # the discard pass reads the creature info of the drawing data
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    while lib.dungeon_unused_thing(MISC) != NONE:
        pass
    count = lib.dungeon_thing_count(MISC)
    assert [lib.thing_word(MISC << 10 | i, 0) for i in range(count - 3, count)] == [NONE] * 3
