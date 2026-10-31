# SPDX-License-Identifier: GPL-3.0-or-later
"""The start folder (src/platform/install.c): the change to the folder of the program, and the absolute path arguments."""

import ctypes

import pytest

from dm2test import constants as C

GAME = "game"  # INSTALL_GAME_FOLDER, a string define that dm2test.constants does not read
BASE = b"/the/program/folder/"
START = "/the/start/folder"


def start_folder(lib, folder, base):
    """The folder that install_start_folder selects, or None to stay."""
    result = lib.install_start_folder(str(folder).encode(), base)
    return None if result is None else result.decode()


@pytest.mark.parametrize("make_game", [
    lambda game: game.mkdir(),
    lambda game: game.write_bytes(b""),
    lambda game: game.symlink_to(game.parent / "elsewhere"),
], ids=["folder", "file", "dangling link"])
def test_a_game_entry_keeps_the_start_folder(lib, tmp_path, make_game):
    make_game(tmp_path / GAME)
    assert start_folder(lib, tmp_path, BASE) is None


def test_no_game_entry_changes_to_the_program_folder(lib, tmp_path):
    assert start_folder(lib, tmp_path, BASE) == BASE.decode()


def test_no_program_folder_keeps_the_start_folder(lib, tmp_path):
    assert start_folder(lib, tmp_path, None) is None


@pytest.mark.parametrize("base, expected", [
    ("/home/me/DM2Mac/DM2Mac.app/Contents/Resources/", "/home/me/DM2Mac/"),
    ("/home/me/DM2Mac/DM2Mac.app/Contents/MacOS/", "/home/me/DM2Mac/"),
    ("/home/me/DM2Mac/DM2Mac.app/Contents/MacOS", "/home/me/DM2Mac/"),
    ("/DM2Mac.app/Contents/MacOS/", "/"),
    ("/home/me/DM2Mac/", "/home/me/DM2Mac/"),
    ("/home/me/Contents/MacOS/", "/home/me/Contents/MacOS/"),
    ("/home/me/.app/Contents/MacOS/", "/home/me/.app/Contents/MacOS/"),
    ("/home/me/DM2Mac.app/Contents/", "/home/me/DM2Mac.app/Contents/"),
], ids=["bundle resources", "bundle executable", "bundle without the last slash", "bundle in the root", "plain folder", "no .app", "no app name",
        "not MacOS"])
def test_a_bundle_gives_the_folder_that_holds_it(lib, base, expected):
    folder = ctypes.create_string_buffer(C.OS_DIR_SIZE)
    assert lib.install_program_folder(base.encode(), folder, C.OS_DIR_SIZE)
    assert folder.value.decode() == expected


def test_a_program_folder_that_does_not_fit_fails(lib):
    base = b"/home/me/DM2Mac/DM2Mac.app/Contents/MacOS/"
    size = len("/home/me/DM2Mac/")  # one byte short for the end of the string
    folder = ctypes.create_string_buffer(size)
    assert not lib.install_program_folder(base, folder, size)


@pytest.mark.parametrize("path, expected", [
    ("game", f"{START}/game"),
    ("saves/SKSAVE0.DAT", f"{START}/saves/SKSAVE0.DAT"),
    ("../other", f"{START}/../other"),
    ("/abs/control.sock", "/abs/control.sock"),
])
def test_a_relative_path_joins_the_start_folder(lib, path, expected):
    full = ctypes.create_string_buffer(C.OS_PATH_SIZE)
    assert lib.install_absolute_path(START.encode(), path.encode(), full, C.OS_PATH_SIZE)
    assert full.value.decode() == expected


def test_a_path_that_does_not_fit_fails(lib):
    size = len(START) + len("/game")  # one byte short for the end of the string
    full = ctypes.create_string_buffer(size)
    assert not lib.install_absolute_path(START.encode(), b"game", full, size)

