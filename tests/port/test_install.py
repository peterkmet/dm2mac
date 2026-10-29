"""The install of the game files (src/platform/install.c): the check of game/, the search for a source, the copy."""

import ctypes
import os
import zipfile

import pytest

from dm2test import constants as C
from dm2test import gamefiles
from dm2test.iso import build_iso
from dm2test.lib import load_lib
from dm2test.markers import GAME, needs_game

CONTENTS = gamefiles.contents()
KEPT = {"DATA/SKSAVE0.DAT": b"a save", "DATA/QUICK.DAT": b"a quick save", "DATA/SETTINGS.TXT": b"filter=none\n"}
OLD_TIME_NS = 1_000_000_000 * 1_000_000_000  # a modification time that a rewrite changes
READ_ONLY, WRITABLE = 0o555, 0o755
SHORT_MESSAGE = 40
LOG_HOOK = ctypes.CFUNCTYPE(None, ctypes.c_char_p)
LOG = []


@LOG_HOOK
def log_line(message):
    LOG.append(message.decode())


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.install_game_files.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
    lib.install_game_files.restype = ctypes.c_int
    lib.gamefile_table.restype = ctypes.c_void_p
    ctypes.c_void_p.in_dll(lib, "g_os_log_hook").value = ctypes.cast(log_line, ctypes.c_void_p).value
    return lib


@pytest.fixture(autouse=True)
def clear_log():
    LOG.clear()


def install(lib, folder, rows=None, size=C.INSTALL_MESSAGE_SIZE):
    """The result and the message of install_game_files in folder."""
    rows = gamefiles.table(CONTENTS) if rows is None else rows
    message = ctypes.create_string_buffer(size)
    result = lib.install_game_files(str(folder).encode(), ctypes.cast(rows, ctypes.c_void_p), message, size)
    return result, message.value.decode()


def write_kept(folder):
    gamefiles.write(folder / "game", KEPT)


def check_installed(folder, contents=CONTENTS):
    """Invariants 2 and 5: the 10 files of game/ pass, no temporary file, the saves and the settings unchanged."""
    game = folder / "game"
    for name, data in contents.items():
        assert (game / name).read_bytes() == data, name
    assert not list(game.rglob("*.TMP"))
    for name, data in KEPT.items():
        assert (game / name).read_bytes() == data


def write_zip(path, files, root="dumast2/"):
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in files.items():
            archive.writestr(root + name, data, compress_type=zipfile.ZIP_DEFLATED)


def damaged(files, name):
    return {**files, name: bytes([files[name][0] ^ 1]) + files[name][1:]}


def snapshot(path):
    paths = [path] if path.is_file() else sorted(p for p in path.rglob("*") if p.is_file())
    return {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in paths}


def full(path):
    return os.path.realpath(path)


def missing_message(folder, *lines):
    head = ["The game files of Dungeon Master II are missing.",
            "You need your own copy of the PC DOS version of Dungeon Master II (English, 1995).",
            f"Copy its folder, its ZIP archive (.zip), or its CD image (.iso, .img, .bin) into {full(folder)}."]
    return "\n".join(head + list(lines))


def make_source(folder, form):
    """Writes a good source of the form into folder; its path."""
    if form == "folder":
        gamefiles.write(folder / "dumast2", CONTENTS)
        return folder / "dumast2"
    if form == "zip":
        write_zip(folder / "game.zip", CONTENTS)
        return folder / "game.zip"
    path = folder / ("game.img" if form == "raw" else "game.iso")
    path.write_bytes(build_iso(CONTENTS, raw=form == "raw"))
    return path


@pytest.mark.parametrize("form", ["folder", "zip", "iso", "raw"])
def test_install_from_each_form(lib, tmp_path, form):
    source = make_source(tmp_path, form)
    write_kept(tmp_path)
    before = snapshot(source)
    assert install(lib, tmp_path)[0] == C.INSTALL_DONE
    check_installed(tmp_path)
    assert snapshot(source) == before
    assert f"installing the game files from {source}" in LOG
    assert LOG[-1] == f"installed {len(CONTENTS)} game files"


def test_folder_three_levels_deep(lib, tmp_path):
    found, too_deep = tmp_path / "found", tmp_path / "too_deep"
    gamefiles.write(found / "a" / "b" / "dumast2", CONTENTS)
    gamefiles.write(too_deep / "a" / "b" / "c" / "dumast2", CONTENTS)
    write_kept(found)
    assert install(lib, found)[0] == C.INSTALL_DONE
    check_installed(found)
    assert install(lib, too_deep)[0] == C.INSTALL_NO_SOURCE


def test_search_order(lib, tmp_path):
    gamefiles.write(tmp_path / "dumast2", damaged(CONTENTS, "FTL"))
    write_zip(tmp_path / "game.zip", CONTENTS)
    (tmp_path / "game.iso").write_bytes(build_iso(CONTENTS, raw=False))
    gamefiles.write(tmp_path / "game", {"FTL": CONTENTS["FTL"][:-1]})
    write_kept(tmp_path)
    assert install(lib, tmp_path)[0] == C.INSTALL_DONE
    check_installed(tmp_path)
    assert f"{tmp_path}/dumast2: FTL is missing or wrong" in LOG
    assert f"installing the game files from {tmp_path}/game.zip" in LOG
    assert not [line for line in LOG if line.startswith(f"{tmp_path}/game:") or "game.iso" in line]


@pytest.mark.parametrize("good", ["dumast2", "z.iso"])
def test_a_failed_folder_does_not_stop_the_search(lib, tmp_path, good):
    wrong = {name: b"\0\5\x16\7AppleDouble" for name in CONTENTS}
    gamefiles.write(tmp_path / "__MACOSX" / "dumast2", wrong)
    (tmp_path / "x.bin").write_bytes(b"not an image " * 400)
    if good == "dumast2":
        gamefiles.write(tmp_path / "dumast2", CONTENTS)
    else:
        (tmp_path / good).write_bytes(build_iso(CONTENTS, raw=False))
    write_kept(tmp_path)
    assert install(lib, tmp_path)[0] == C.INSTALL_DONE
    check_installed(tmp_path)
    assert f"{tmp_path}/__MACOSX/dumast2: INTRPLAY.PCX is missing or wrong" in LOG
    if good == "z.iso":
        assert f"{tmp_path}/x.bin: not a CD image or damaged" in LOG


def test_wrong_file_message(lib, tmp_path):
    write_zip(tmp_path / "game.zip", damaged(CONTENTS, "DATA/GRAPHICS.DAT"))
    result, message = install(lib, tmp_path)
    assert result == C.INSTALL_NO_SOURCE
    assert message == missing_message(tmp_path, f"{tmp_path}/game.zip: DATA/GRAPHICS.DAT is missing or wrong.")
    assert not [name for name in CONTENTS if (tmp_path / "game" / name).exists()]


def test_message_names_the_bad_file_of_game(lib, tmp_path):
    gamefiles.write(tmp_path / "game", damaged({"FTL": CONTENTS["FTL"]}, "FTL"))
    result, message = install(lib, tmp_path)
    assert result == C.INSTALL_NO_SOURCE
    assert message == missing_message(tmp_path, "game/FTL is missing or wrong.")
    _, short = install(lib, tmp_path, size=SHORT_MESSAGE)
    assert short == message[:SHORT_MESSAGE - 4] + "..."


def test_interrupted_install(lib, tmp_path):
    game = tmp_path / "game"
    good = gamefiles.NAMES[:4]
    gamefiles.write(game, {name: CONTENTS[name] for name in good})
    gamefiles.write(game, {"DATA/GRAPHICS.DAT": CONTENTS["DATA/GRAPHICS.DAT"][:50], "DATA/GRAPHICS.DAT.TMP": b"cut"})
    for name in good:
        os.utime(game / name, ns=(OLD_TIME_NS, OLD_TIME_NS))
    gamefiles.write(tmp_path / "dumast2", CONTENTS)
    write_kept(tmp_path)
    assert install(lib, tmp_path)[0] == C.INSTALL_DONE
    check_installed(tmp_path)
    assert all((game / name).stat().st_mtime_ns == OLD_TIME_NS for name in good)
    assert LOG[-1] == f"installed {len(CONTENTS) - len(good)} game files"


def test_later_start_reads_no_source(lib, tmp_path):
    gamefiles.write(tmp_path / "game", CONTENTS)
    write_kept(tmp_path)
    (tmp_path / "game.zip").write_bytes(b"damaged archive")
    assert install(lib, tmp_path)[0] == C.INSTALL_READY
    check_installed(tmp_path)
    assert not [line for line in LOG if "game.zip" in line]


def test_unwritable_game_folder(lib, tmp_path):
    data = tmp_path / "game" / "DATA"
    data.mkdir(parents=True)
    gamefiles.write(tmp_path / "dumast2", CONTENTS)
    data.chmod(READ_ONLY)
    try:
        result, message = install(lib, tmp_path)
        assert result == C.INSTALL_WRITE_FAILED
        assert message == f"cannot write the game files into {full(tmp_path / 'game')}"
        assert not list((tmp_path / "game").rglob("*.TMP"))
    finally:
        data.chmod(WRITABLE)


@needs_game
def test_real_files(lib, tmp_path):
    for name in gamefiles.NAMES:
        link = tmp_path / "dumast2" / name
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(GAME / name)
    assert install(lib, tmp_path, ctypes.c_void_p(lib.gamefile_table()))[0] == C.INSTALL_DONE
    for name in gamefiles.NAMES:
        assert (tmp_path / "game" / name).read_bytes() == (GAME / name).read_bytes()
