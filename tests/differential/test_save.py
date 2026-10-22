import ctypes
import struct

import pytest

from dm2test import constants as C
from dm2test.lib import load_lib
from dm2test.markers import ROOT
from dm2test.save import (
    HEADER_SIZE, RECORD_GLOBALS, SAVE_ERR_DATA, SAVE_ERR_FORMAT, SAVE_OK, GRAPHICS, START, Event, Game, Timeline,
    c_round_trip, original_load, original_save,
)


@pytest.fixture(scope="module")
def lib():
    return load_lib()

DUNGEON_HEADER_SIZE = 0x2C
STREAM = HEADER_SIZE + 43443  # start of the bit stream in start.DAT, after the dungeon image


@pytest.fixture(scope="module")
def gdat(lib):
    if not GRAPHICS.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == C.GDAT_OK
    yield g
    lib.gdat_free(g)


@pytest.fixture(scope="module")
def start():
    if not START.exists():
        pytest.skip("tests/saves/start.DAT missing")
    return START.read_bytes()


def original_round_trip(lib, gdat, data):
    """Runs the original load, then the original save; returns the written file and the record globals."""
    emu = original_load(lib, gdat, data)
    globals_ = {name: int.from_bytes(emu.read(addr, size), "little") for name, (addr, size) in RECORD_GLOBALS.items()}
    return original_save(emu, data), globals_



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
    loaded = {name: getattr(game, name) for name in RECORD_GLOBALS if name != "g_random_seed"}
    loaded["g_random_seed"] = ctypes.c_uint32.in_dll(lib, "g_random_seed").value
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
    assert lib.save_load(str(tmp_path / "none.DAT").encode(), gdat) == C.SAVE_ERR_IO


def test_projectile_and_explosion_round_trip(lib, gdat, start, tmp_path):
    # start.DAT has no projectile and no explosion: add one of each on the party square.
    path, bak = tmp_path / "SKSAVE0.DAT", tmp_path / "SKSAVE0.BAK"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    game = Game.in_dll(lib, "game")
    square = (game.party_map, game.party_x, game.party_y)
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16

    projectile = lib.dungeon_unused_thing(C.THING_PROJECTILE)
    lib.set_thing_word(projectile, 2, C.SPELL_FIREBALL | 5)  # payload: a spell
    lib.set_thing_word(projectile, 4, 0x1234)
    move = Event(game.game_time + 10, C.EVENT_PROJECTILE_MOVE, 0, (ctypes.c_uint8 * 6)(*struct.pack("<H4x", projectile | 1 << C.THING_CELL_SHIFT)))
    lib.set_thing_word(projectile, 6, lib.timeline_add_event(ctypes.byref(move)))
    assert lib.dungeon_link_thing_to_square(projectile | 1 << C.THING_CELL_SHIFT, *square) == 1

    explosion = lib.dungeon_unused_thing(C.THING_EXPLOSION)
    lib.set_thing_word(explosion, 2, C.SPELL_POISON_CLOUD)
    step = Event(game.game_time + 3, C.EVENT_EXPLOSION, 0, (ctypes.c_uint8 * 6)(*struct.pack("<2xH2x", explosion | 2 << C.THING_CELL_SHIFT)))
    lib.timeline_add_event(ctypes.byref(step))
    assert lib.dungeon_link_thing_to_square(explosion | 2 << C.THING_CELL_SHIFT, *square) == 1

    assert lib.save_write(str(path).encode(), str(bak).encode(), b"GAME 0", gdat) == SAVE_OK
    first = path.read_bytes()
    timeline = Timeline.in_dll(lib, "g_timeline")
    written_events = [bytes(timeline.events[i])[4:] for i in range(timeline.capacity)]
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    things = []
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    thing = lib.dungeon_square_first_thing(*square)
    while thing != C.THING_ENDOFLIST:
        things.append(thing)
        thing = lib.dungeon_next_thing(thing)
    loaded = [t for t in things if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK in (C.THING_PROJECTILE, C.THING_EXPLOSION)]
    assert [(t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK, t >> C.THING_CELL_SHIFT) for t in loaded] == [
        (C.THING_PROJECTILE, 1), (C.THING_EXPLOSION, 2)]
    assert lib.thing_word(loaded[0], 2) == C.SPELL_FIREBALL | 5 and lib.thing_word(loaded[0], 4) == 0x1234
    events = ctypes.cast(ctypes.c_void_p.in_dll(lib, "g_timeline"), ctypes.POINTER(Event))
    move_index = lib.thing_word(loaded[0], 6)
    assert events[move_index].type == C.EVENT_PROJECTILE_MOVE and bytes(events[move_index].data[:2]) == struct.pack("<H", loaded[0])
    explosion_index = 4  # after the 3 events of start.DAT and the projectile move
    assert events[explosion_index].type == C.EVENT_EXPLOSION and bytes(events[explosion_index].data[2:4]) == struct.pack("<H", loaded[1])
    assert [bytes(events[i])[4:] for i in range(timeline.capacity)] == written_events
    assert lib.save_write(str(path).encode(), str(bak).encode(), b"GAME 0", gdat) == SAVE_OK
    assert path.read_bytes() == first


G_CHAMPIONS, CHAMPION_SIZE, CHAMPION_DAMAGE_EVENT = 0x7FBD0, C.CHAMPION_SIZE, 0x2E


def test_load_relinks_champion_damage_events_like_original(lib, gdat, start, tmp_path):
    # A save with queued hide damage events (0x0c) for champion 0 and an altar rebirth event (0x0d): after the load
    # each champion points at its hide damage event (or 0xffff), in the C port and in the original.
    path, bak = tmp_path / "SKSAVE0.DAT", tmp_path / "SKSAVE0.BAK"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    game = Game.in_dll(lib, "game")
    for delay, type_, priority in ((40, C.EVENT_ALTAR_REBIRTH, 0), (20, C.EVENT_HIDE_DAMAGE, 0), (90, C.EVENT_HIDE_DAMAGE, 0)):
        event = Event(game.game_time + delay | game.party_map << 24, type_, priority, (ctypes.c_uint8 * 6)())
        lib.timeline_add_event(ctypes.byref(event))
    assert lib.save_write(str(path).encode(), str(bak).encode(), b"GAME 0", gdat) == SAVE_OK
    saved = path.read_bytes()
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    records = ctypes.string_at(ctypes.addressof(game) + 252, C.CHAMPION_MAX * CHAMPION_SIZE)  # offsetof(Game, champions)
    mine = [records[c * CHAMPION_SIZE + CHAMPION_DAMAGE_EVENT:][:2] for c in range(C.CHAMPION_MAX)]
    emu = original_load(lib, gdat, saved)
    theirs = [emu.read(G_CHAMPIONS + c * CHAMPION_SIZE + CHAMPION_DAMAGE_EVENT, 2) for c in range(C.CHAMPION_MAX)]
    assert mine == theirs
    assert struct.unpack("<H", mine[0])[0] not in (0, 0xFFFF) and mine[1:] == [b"\xff\xff"] * 3


G_THING_DATA = 0x7F288
G_TIMELINE_EVENTS = 0x760E8
G_TIMELINE_EVENT_MAX = 0x80428
G_TIMELINE_EVENT_COUNT = 0x8042A
THING_SIZES = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
GROUP, CONTAINER, MISC, PROJECTILE, EXPLOSION = C.THING_GROUP, C.THING_CONTAINER, C.THING_MISC, C.THING_PROJECTILE, C.THING_EXPLOSION
ENDOFLIST, NONE = C.THING_ENDOFLIST, C.THING_NONE


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
                         if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == GROUP and t & C.THING_INDEX_MASK)
    minion_map = next(CONTAINER << 10 | i for i in range(lib.dungeon_thing_count(CONTAINER))
                      if lib.thing_word(CONTAINER << 10 | i, 0) != NONE and lib.thing_word(CONTAINER << 10 | i, 4) >> 1 & 3 == 1)
    lib.set_thing_word(minion_map, 2, GROUP << 10)

    gap = add_event(lib, C.EVENT_DOOR_ANIMATION, game.game_time + 1, bytes(6))
    lib.dungeon_unlink_thing_from_square(group, *square)
    add_event(lib, C.EVENT_MOVE_GROUP, game.game_time + 2, struct.pack("<2xH2x", group))
    projectile = lib.dungeon_unused_thing(PROJECTILE)
    lib.set_thing_word(projectile, 2, C.SPELL_FIREBALL | 5)
    lib.set_thing_word(projectile, 6, add_event(lib, C.EVENT_PROJECTILE_MOVE, game.game_time + 10, struct.pack("<H4x", projectile | 1 << C.THING_CELL_SHIFT)))
    assert lib.dungeon_link_thing_to_square(projectile | 1 << C.THING_CELL_SHIFT, *square) == 1
    explosion = lib.dungeon_unused_thing(EXPLOSION)
    lib.set_thing_word(explosion, 2, C.SPELL_POISON_CLOUD)
    add_event(lib, C.EVENT_EXPLOSION, game.game_time + 3, struct.pack("<2xH2x", explosion | 2 << C.THING_CELL_SHIFT))
    assert lib.dungeon_link_thing_to_square(explosion | 2 << C.THING_CELL_SHIFT, *square) == 1
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
    bases = {t: emu.read_u32(G_THING_DATA + 4 * t) for t in range(C.THING_TYPE_COUNT) if THING_SIZES[t]}
    for t, base in bases.items():
        records = c_records(lib, t)
        assert emu.read(base, len(records)) == records

    link_things(lib, Game.in_dll(lib, "game"))
    timeline = Timeline.in_dll(lib, "g_timeline")
    assert timeline.used_end <= int.from_bytes(emu.read(G_TIMELINE_EVENT_MAX, 2), "little")
    emu.write(emu.read_u32(G_TIMELINE_EVENTS), ctypes.string_at(timeline.events, timeline.used_end * C.TIMELINE_EVENT_SIZE))
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
