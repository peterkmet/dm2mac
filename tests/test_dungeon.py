import ctypes
import struct

import pytest

from conftest import ROOT
from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image

DUNGEON = ROOT / "game" / "DATA" / "DUNGEON.DAT"

DUNGEON_LOAD = 0x36909
MEM_ALLOC = 0x3BBBA
GDAT_ENTRY_INDEX = 0x3C5D3
TIMELINE_INIT = 0x55D10
WEATHER_INIT = 0x59F38
LINK_THINGS_AFTER_LOAD = 0x3624F
READ_LEVEL_TABLE = 0x367DA
SET_CURRENT_MAP = 0x1C724
SQUARE_FIRST_THING = 0x1D391

G_THING_SIZE = 0x71808
G_THING_DATA = 0x7F288
G_SQUARE_FIRST_THINGS = 0x7F388
G_MAP_DESCS = 0x7F3C4
G_MAP_COLUMNS = 0x7F3C8
G_TEXT_DATA = 0x7F3CC
G_LEVEL_MAP_LISTS = 0x7F3D8
G_DUNGEON_HEADER = 0x7F3E8
G_LEVEL_MAP_LIST_OFFSETS = 0x7F3EC
THING_SLACK = 0x71818


class Original:
    """The DUNGEON.DAT load of SKULL.EXE in the emulator; the systems that a load also starts are stubbed."""

    def __init__(self):
        self.emu = emu = Emu(load_image(ROOT / "game" / "SKULL.EXE"))
        emu.serve_file(DUNGEON.read_bytes())
        self.heap = SCRATCH
        emu.stub(MEM_ALLOC, self.alloc)
        for addr in (GDAT_ENTRY_INDEX, TIMELINE_INIT, WEATHER_INIT, LINK_THINGS_AFTER_LOAD, READ_LEVEL_TABLE):
            emu.stub(addr, lambda *_: 0)
        emu.call(DUNGEON_LOAD, 1)

    def alloc(self, size, *_):
        addr, self.heap = self.heap, self.heap + (size + 3 & ~3)
        return addr

    def ptr(self, addr):
        return self.emu.read_u32(addr)

    def u16(self, addr):
        return struct.unpack("<H", self.emu.read(addr, 2))[0]

    def header(self, offset):
        return self.ptr(G_DUNGEON_HEADER) + offset

    def map_count(self):
        return self.emu.read(self.header(4), 1)[0]

    def map_word(self, m):
        return self.u16(self.ptr(G_MAP_DESCS) + 16 * m + 8)

    def column(self, m, x, height):
        return self.emu.read(self.ptr(self.ptr(self.ptr(G_MAP_COLUMNS) + 4 * m) + 4 * x), height)

    def square_first_thing(self, m, x, y):
        self.emu.call(SET_CURRENT_MAP, m)
        return self.emu.call(SQUARE_FIRST_THING, x, y) & 0xFFFF


@pytest.fixture(scope="module")
def original():
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return Original()


@pytest.fixture(scope="module")
def dungeon(lib):
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    lib.dungeon_text_data.restype = ctypes.c_void_p
    lib.dungeon_square_things.restype = ctypes.c_void_p
    lib.dungeon_level_maps.restype = ctypes.c_void_p
    lib.dungeon_party_start.restype = ctypes.c_uint16
    lib.dungeon_square.restype = ctypes.c_uint8
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    if not DUNGEON.exists():
        pytest.skip("game/DATA/DUNGEON.DAT missing")
    assert lib.dungeon_load(str(DUNGEON).encode()) == 1
    return lib


def test_header(original, dungeon):
    assert dungeon.dungeon_map_count() == original.map_count() == 44
    assert dungeon.dungeon_party_start() == original.u16(original.header(8))
    assert [dungeon.dungeon_thing_size(t) for t in range(16)] == list(original.emu.read(G_THING_SIZE, 16))


def test_maps_and_squares(original, dungeon):
    for m in range(original.map_count()):
        word = original.map_word(m)
        width, height = (word >> 6 & 0x1F) + 1, (word >> 11) + 1
        assert (dungeon.dungeon_map_width(m), dungeon.dungeon_map_height(m)) == (width, height), m
        assert dungeon.dungeon_map_level(m) == word & 0x3F, m
        for x in range(width):
            assert bytes(dungeon.dungeon_square(m, x, y) for y in range(height)) == original.column(m, x, height), (m, x)
            for y in range(height):
                assert dungeon.dungeon_square_first_thing(m, x, y) == original.square_first_thing(m, x, y), (m, x, y)


def test_square_things(original, dungeon):
    count = dungeon.dungeon_square_thing_count()
    assert count == original.u16(original.header(10)) == 2660
    table = ctypes.string_at(dungeon.dungeon_square_things(), 2 * count)
    assert table == original.emu.read(original.ptr(G_SQUARE_FIRST_THINGS), 2 * count)


def test_thing_slack_table(original, dungeon):
    slack = original.emu.read(THING_SLACK, 16)
    in_file = struct.unpack_from("<16H", DUNGEON.read_bytes(), 12)
    assert [dungeon.dungeon_thing_count(t) - in_file[t] for t in range(16)] == list(slack)


def test_things(original, dungeon):
    for t in range(16):
        count = original.u16(original.header(12 + 2 * t))
        assert dungeon.dungeon_thing_count(t) == count, t
        size = count * dungeon.dungeon_thing_size(t)
        if size:
            record = ctypes.string_at(dungeon.dungeon_thing_record(t << 10), size)
            assert record == original.emu.read(original.ptr(G_THING_DATA + 4 * t), size), t


def test_text_and_level_lists(original, dungeon):
    words = dungeon.dungeon_text_word_count()
    assert words == original.u16(original.header(6))
    assert ctypes.string_at(dungeon.dungeon_text_data(), 2 * words) == original.emu.read(original.ptr(G_TEXT_DATA), 2 * words)
    offsets = original.emu.read(original.ptr(G_LEVEL_MAP_LIST_OFFSETS), 63)
    for level in range(63):
        assert dungeon.dungeon_level_maps(level) - dungeon.dungeon_level_maps(0) == offsets[level], level
        maps = original.emu.read(original.ptr(G_LEVEL_MAP_LISTS) + offsets[level], 64).split(b"\xff")[0]
        assert ctypes.string_at(dungeon.dungeon_level_maps(level), 64).split(b"\xff")[0] == maps, level


def synthetic_dungeon(counts):
    """A dungeon file without maps and text, with zeroed records of the given thing counts."""
    header = struct.pack("<6H16H", 0, 0, 0, 0, 0, 0, *counts)
    return header + bytes(16 * sum(counts))


def test_thing_count_cap(lib, tmp_path):
    path = tmp_path / "groups.dat"
    path.write_bytes(synthetic_dungeon([0, 0, 0, 0, 1000] + [0] * 11))
    assert lib.dungeon_load(str(path).encode()) == 1
    assert lib.dungeon_thing_count(4) == 0x400
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    assert ctypes.string_at(lib.dungeon_thing_record(4 << 10 | 0x3FF), 2) == b"\xff\xff"
    path.write_bytes(synthetic_dungeon([0, 0, 0, 0, 0x401] + [0] * 11))
    assert lib.dungeon_load(str(path).encode()) == 0


def test_bad_files(lib, tmp_path):
    compressed = tmp_path / "compressed.dat"
    compressed.write_bytes(b"\x04\x81" + bytes(0x2A))
    paths = [compressed, tmp_path / "missing.dat"]
    if DUNGEON.exists():
        data = DUNGEON.read_bytes()
        paths.append(tmp_path / "truncated.dat")
        paths[-1].write_bytes(data[:1000])
        map_count = data[4]
        columns = 0x2C + 16 * map_count
        widths = sum((struct.unpack_from("<H", data, 0x2C + 16 * m + 8)[0] >> 6 & 0x1F) + 1 for m in range(map_count))
        square_things = columns + 2 * widths
        damages = {"map_outside": (0x2C, 0xFFFF), "bad_column": (columns, 0xFFFF), "bad_thing": (square_things, 3 << 10 | 0x3FF)}
        for name, (offset, value) in damages.items():
            damaged = bytearray(data)
            struct.pack_into("<H", damaged, offset, value)
            paths.append(tmp_path / f"{name}.dat")
            paths[-1].write_bytes(damaged)
    for path in paths:
        assert lib.dungeon_load(str(path).encode()) == 0
        assert lib.dungeon_map_count() == 0
