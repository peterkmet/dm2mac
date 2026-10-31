"""The game file table and the check (src/data/gamefile.c)."""

import ctypes
import zlib

import pytest

from dm2test import gamefiles
from dm2test.gamefiles import GameFile
from dm2test.markers import GAME, needs_game


@pytest.fixture(scope="module", autouse=True)
def lib_prototypes(lib):
    lib.gamefile_table.restype = ctypes.POINTER(GameFile)  # the test indexes the result
    lib.gamefile_find_bad.restype = ctypes.POINTER(GameFile)  # the test reads .contents


def find_bad(lib, tmp_path, files):
    """The name of the first bad file of the generated files in tmp_path, or None."""
    rows = gamefiles.table(gamefiles.contents())
    gamefiles.write(tmp_path, files)
    bad = lib.gamefile_find_bad(ctypes.cast(rows, ctypes.c_void_p), str(tmp_path).encode())
    return bad.contents.name.decode() if bad else None


def test_all_files_pass(lib, tmp_path):
    assert find_bad(lib, tmp_path, gamefiles.contents()) is None


def test_missing_file(lib, tmp_path):
    files = gamefiles.contents()
    del files["DATA/SONGLIST.DAT"]
    assert find_bad(lib, tmp_path, files) == "DATA/SONGLIST.DAT"


def test_wrong_crc(lib, tmp_path):
    files = gamefiles.contents()
    files["FTL"] = bytes([files["FTL"][0] ^ 1]) + files["FTL"][1:]
    assert find_bad(lib, tmp_path, files) == "FTL"


@pytest.mark.parametrize("change", [lambda d: d + b"\0", lambda d: d[:-1]])
def test_longer_and_shorter_file(lib, tmp_path, change):
    files = gamefiles.contents()
    files["END"] = change(files["END"])
    assert find_bad(lib, tmp_path, files) == "END"


def test_first_bad_file_in_table_order(lib, tmp_path):
    files = gamefiles.contents()
    for name in ("DATA/DUNGEON.DAT", "CREDITS"):
        files[name] = b"x" + files[name][1:]
    assert find_bad(lib, tmp_path, files) == "CREDITS"


@needs_game
def test_real_files(lib):
    assert not lib.gamefile_find_bad(lib.gamefile_table(), str(GAME).encode())
    rows = lib.gamefile_table()
    for i, name in enumerate(gamefiles.NAMES):
        data = (GAME / name).read_bytes()
        assert rows[i].name.decode() == name
        assert (rows[i].size, rows[i].crc) == (len(data), zlib.crc32(data))
