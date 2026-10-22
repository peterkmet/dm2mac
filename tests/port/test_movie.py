"""The movie script of the DOS launcher (src/ui/movie.c): the order of the steps, the skip rules of the modes,
the failed steps, and the game screen after the movies."""

import ctypes
import os
import shutil
import tempfile
import threading
import time

import pytest

from dm2tools.control_client import request
from dm2test import constants as C
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

SCRIPT_NONE, SCRIPT_START, SCRIPT_COMPLETED, SCRIPT_PARTY_DEAD = (
    C.MOVIE_SCRIPT_NONE, C.MOVIE_SCRIPT_START, C.MOVIE_SCRIPT_COMPLETED, C.MOVIE_SCRIPT_PARTY_DEAD)
STEP_NONE, SPLASH, FTL, INTRO, END, CREDITS = (
    C.MOVIE_STEP_NONE, C.MOVIE_STEP_SPLASH, C.MOVIE_STEP_FTL, C.MOVIE_STEP_INTRO, C.MOVIE_STEP_END, C.MOVIE_STEP_CREDITS)
VANILLA, ADVANCED = C.GAME_MODE_VANILLA, C.GAME_MODE_ADVANCED
EXIT_QUIT, EXIT_COMPLETED, EXIT_PARTY_DEAD, EXIT_RESTART, EXIT_GAME_LOST = (
    C.EXIT_QUIT, C.EXIT_COMPLETED, C.EXIT_PARTY_DEAD, C.EXIT_RESTART, C.EXIT_GAME_LOST)
SECOND = 10**9
SCREEN_W, SCREEN_H, COLORS, RGB = C.SCREEN_W, C.SCREEN_H, C.PALETTE_COLORS, 3
FILES = {"INTRPLAY.PCX": "INTRPLAY.PCX", "FTL": "FTL", "INTRO": "INTRO", "END": "END", "CREDITS": "CREDITS"}


class MoviePicture(ctypes.Structure):
    """movie.h MoviePicture."""
    _fields_ = [("pixels", ctypes.c_void_p), ("width", ctypes.c_int), ("height", ctypes.c_int),
                ("shown_width", ctypes.c_int), ("screen_width", ctypes.c_int), ("screen_height", ctypes.c_int),
                ("palette", ctypes.c_void_p)]


@pytest.fixture
def movies(lib, game_dir, tmp_path):
    """Starts scripts on a data folder of links to game/: start(script, mode, **files) with files that replace a
    link (a game/ name, bytes, or None for a missing file); yields start."""
    lib.movie_start.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_uint64]
    lib.movie_update.argtypes = [ctypes.c_uint64]
    mode = ctypes.c_int.in_dll(lib, "g_game_mode")

    def start(script, game_mode, **files):
        folder = tmp_path / f"data{len(list(tmp_path.iterdir()))}"
        folder.mkdir()
        for name, source in {**FILES, **files}.items():
            if isinstance(source, bytes):
                (folder / name).write_bytes(source)
            elif source:
                (folder / name).symlink_to(game_dir / source)
        mode.value = game_mode
        return lib.movie_start(script, str(folder).encode(), 0)

    yield start
    lib.movie_stop()
    mode.value = VANILLA


def picture(lib):
    p = MoviePicture()
    lib.movie_picture(ctypes.byref(p))
    return p


def test_script_after_each_exit_code(lib):
    expected = {EXIT_QUIT: SCRIPT_NONE, EXIT_COMPLETED: SCRIPT_COMPLETED, EXIT_PARTY_DEAD: SCRIPT_PARTY_DEAD,
                EXIT_RESTART: SCRIPT_NONE, EXIT_GAME_LOST: SCRIPT_NONE}
    assert {code: lib.movie_script_after(code) for code in expected} == expected


def test_advanced_click_stops_each_step(lib, movies):
    assert movies(SCRIPT_START, ADVANCED)
    assert lib.movie_current_step() == SPLASH and lib.movie_shows_splash()
    screens = []
    for expected in (FTL, INTRO, STEP_NONE):
        lib.movie_click()
        lib.movie_update(1)
        assert lib.movie_current_step() == expected
        if expected != STEP_NONE:
            p = picture(lib)
            screens.append((p.width, p.height, p.shown_width, p.screen_width, p.screen_height))
    assert screens == [(432, 320, 432, 640, 480), (432, 320, 576, 640, 350)]
    assert not lib.movie_is_playing()


def test_vanilla_click_keeps_splash_and_ftl(lib, movies):
    assert movies(SCRIPT_START, VANILLA)
    lib.movie_click()
    lib.movie_update(SECOND)
    assert lib.movie_current_step() == SPLASH
    lib.movie_update(3 * SECOND)
    assert lib.movie_current_step() == FTL
    lib.movie_click()
    lib.movie_update(3 * SECOND + 1)
    assert lib.movie_current_step() == FTL
    lib.movie_update(10 * SECOND)  # FTL is about 5.5 seconds; the click of FTL does not stop INTRO
    assert lib.movie_current_step() == INTRO
    lib.movie_update(11 * SECOND)
    assert lib.movie_current_step() == INTRO
    lib.movie_click()
    lib.movie_update(11 * SECOND)
    assert lib.movie_current_step() == STEP_NONE


def test_movie_plays_to_its_end_on_time(lib, movies):
    assert movies(SCRIPT_START, ADVANCED, **{"INTRPLAY.PCX": None, "INTRO": None})
    assert lib.movie_current_step() == FTL
    lib.movie_update(5 * SECOND)
    assert lib.movie_current_step() == FTL
    lib.movie_update(6 * SECOND)
    assert lib.movie_current_step() == STEP_NONE


@pytest.mark.parametrize("case, files, first", [
    ("missing", {"FTL": None}, INTRO),
    ("vga", {"FTL": "FTL.VGA"}, INTRO),
    ("junk", {"FTL": b"MZ" + bytes(5000)}, INTRO),
    ("truncated", {"FTL": "truncated"}, INTRO),
    ("no splash", {"INTRPLAY.PCX": None}, FTL),
    ("bad splash", {"INTRPLAY.PCX": b"\x0a\x05\x01\x08" + bytes(2000)}, FTL),
])
def test_failed_steps_are_skipped(lib, movies, game_dir, case, files, first):
    if case == "truncated":
        data = (game_dir / "FTL").read_bytes()
        files = {"FTL": data[:len(data) // 2]}
    assert movies(SCRIPT_START, ADVANCED, **files)
    if lib.movie_current_step() == SPLASH:
        lib.movie_update(3 * SECOND)
    if case == "truncated":
        assert lib.movie_current_step() == FTL
        lib.movie_update(20 * SECOND)  # the movie plays up to the cut, then the next step starts
    assert lib.movie_current_step() == first


def test_no_playable_step_starts_nothing(lib, movies):
    assert not movies(SCRIPT_START, ADVANCED, **{name: None for name in FILES})
    assert lib.movie_current_step() == STEP_NONE and not lib.movie_is_playing()
    assert not movies(SCRIPT_NONE, ADVANCED)


def test_end_scripts(lib, movies):
    assert movies(SCRIPT_PARTY_DEAD, ADVANCED)
    assert lib.movie_current_step() == CREDITS
    assert movies(SCRIPT_COMPLETED, ADVANCED)
    assert lib.movie_current_step() == END
    lib.movie_click()
    lib.movie_update(1)
    assert lib.movie_current_step() == CREDITS


def test_script_end_restores_the_game_screen(lib, movies):
    fb = (ctypes.c_uint8 * (SCREEN_W * SCREEN_H)).in_dll(lib, "fb")
    pal = (ctypes.c_uint8 * (COLORS * RGB)).in_dll(lib, "palette")
    for i in range(len(fb)):
        fb[i] = i % 7
    for i in range(len(pal)):
        pal[i] = i % 64
    screen, colors = bytes(fb), bytes(pal)
    assert movies(SCRIPT_START, ADVANCED)
    assert bytes(fb) != screen and bytes(pal) != colors  # the splash shows on the game screen
    for _ in range(3):
        lib.movie_click()
        lib.movie_update(1)
    assert (bytes(fb), bytes(pal)) == (screen, colors)


def test_control_state_and_click_during_a_movie(lib, movies):
    folder = tempfile.mkdtemp(prefix="dm2c", dir="/tmp")  # sun_path holds about 100 bytes
    path = os.path.join(folder, "control.sock")
    lib.transport_open.argtypes = [ctypes.c_char_p]
    lib.transport_open.restype = ctypes.c_bool
    assert lib.transport_open(path.encode())
    stop = threading.Event()

    def loop():
        while not stop.is_set():
            lib.transport_poll()
            time.sleep(0.0005)

    thread = threading.Thread(target=loop)
    thread.start()
    try:
        assert movies(SCRIPT_START, ADVANCED)
        state = request({"cmd": "state"}, path)
        assert state["ok"] and state["mode"] == "movie" and state["dialog"] is None
        assert request({"cmd": "key", "key": "esc"}, path) == {"ok": True}
        lib.movie_update(1)
        assert lib.movie_current_step() == SPLASH  # a key does not skip
        assert request({"cmd": "click", "x": 10, "y": 10}, path) == {"ok": True}
        lib.movie_update(1)
        assert lib.movie_current_step() == FTL
        lib.movie_stop()
        assert request({"cmd": "state"}, path)["mode"] != "movie"
    finally:
        stop.set()
        thread.join()
        lib.transport_close()
        shutil.rmtree(folder)
