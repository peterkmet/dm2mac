"""The save file helpers: the original load and save in the emulator, the C round trip, and the C save structs."""

import ctypes

from dm2test.markers import ROOT
from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image


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



def c_round_trip(lib, gdat, data, tmp_path):
    path, bak = tmp_path / "SKSAVE0.DAT", tmp_path / "SKSAVE0.BAK"
    path.write_bytes(data)
    assert lib.save_load(str(path).encode(), gdat) == SAVE_OK
    name = data[2:0x12].split(b"\0")[0]
    assert lib.save_write(str(path).encode(), str(bak).encode(), name, gdat) == SAVE_OK
    assert bak.read_bytes() == data
    return path.read_bytes()


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
