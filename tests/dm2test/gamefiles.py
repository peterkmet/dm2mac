"""The game file table of src/data/gamefile.c for the tests: a mirror of GameFile and small generated files."""

import ctypes
import zlib
from pathlib import Path

from dm2test.structs import mirror

NAMES = [
    "INTRPLAY.PCX", "FTL", "INTRO", "END", "CREDITS", "DRUM.BNK", "MELODIC.BNK",
    "DATA/SONGLIST.DAT", "DATA/GRAPHICS.DAT", "DATA/DUNGEON.DAT",
]  # fmt: skip


@mirror("gamefile.h")
class GameFile(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char_p), ("size", ctypes.c_uint32), ("crc", ctypes.c_uint32)]


def contents():
    """A few hundred bytes per name, different for each name."""
    return {name: bytes((i * 7 + k) & 0xFF for k in range(200 + 13 * i)) for i, name in enumerate(NAMES)}


def table(contents):
    """The table of GameFile rows for the given contents."""
    rows = (GameFile * len(NAMES))()
    for row, name in zip(rows, NAMES):
        row.name, row.size, row.crc = name.encode(), len(contents[name]), zlib.crc32(contents[name])
    return rows


def write(folder, contents):
    """Writes the files below folder."""
    for name, data in contents.items():
        path = Path(folder) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
