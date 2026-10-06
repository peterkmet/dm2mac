"""The control socket (port/src/control.c): requests over a Unix domain socket give input to the game and read its
state and screen; the program starts it with --control."""

import ctypes
import os
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import zlib
from contextlib import contextmanager

import pytest

from dm2tools.control_client import request
from test_input import session  # noqa: F401 (a fixture)
from conftest import ROOT
from test_move import gdat, start  # noqa: F401 (gdat and start are fixtures)

KEY_SHIFT, KEY_ALT = 0x200, 0x800
SCAN_A, SCAN_ENTER = 0x1E, 0x1C
ZONE_TURN_RIGHT = 0x29
INPUT_MODE_MOVE, LEFT = 5, 2
DIRECTIONS = ((0, -1), (1, 0), (0, 1), (-1, 0))
GAME_CHAMPIONS = 252  # offsetof(Game, champions) in save.h


@pytest.fixture
def control(lib, gdat, session):  # noqa: F811
    """start.DAT ready to play with its palette and the control socket open; yields the socket path, the game record,
    the main loop."""
    palette = (ctypes.c_uint8 * (256 * 3)).in_dll(lib, "palette")
    for c in range(256):
        assert lib.gdat_palette_color(gdat, c, ctypes.byref(palette, 3 * c)) == 0
    folder = tempfile.mkdtemp(prefix="dm2c", dir="/tmp")  # sun_path holds about 100 bytes
    path = os.path.join(folder, "sub", "control.sock")
    lib.control_open.argtypes = [ctypes.c_char_p]
    assert lib.control_open(path.encode())
    yield (path, *session)
    lib.control_close()
    assert not os.path.exists(path)
    shutil.rmtree(folder)


@contextmanager
def pumped(lib, ticks=True):
    """The main loop of the program in a thread: the control requests, and the game ticks when ticks is true."""
    stop = threading.Event()

    def loop():
        while not stop.is_set():
            lib.control_poll()
            if ticks:
                lib.game_tick()
            time.sleep(0.0005)

    thread = threading.Thread(target=loop)
    thread.start()
    try:
        yield
    finally:
        stop.set()
        thread.join()


def arrow_center(lib, zone):
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(zone, rect)
    return rect[0] + rect[2] // 2, rect[1] + rect[3] // 2


def read_png(path):
    """The RGB rows of a PNG file of 8-bit RGB without filters."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    chunks, pos = {}, 8
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        assert struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])[0] == zlib.crc32(kind + body)
        chunks[kind] = chunks.get(kind, b"") + body
        pos += 12 + n
    width, height, depth, color = struct.unpack(">IIBB", chunks[b"IHDR"][:10])
    assert (width, height, depth, color) == (320, 200, 8, 2)
    raw = zlib.decompress(chunks[b"IDAT"])
    rows = [raw[y * 961:(y + 1) * 961] for y in range(200)]
    assert all(row[0] == 0 for row in rows)
    return [[tuple(row[1 + 3 * x:4 + 3 * x]) for x in range(320)] for row in rows]


def screen_rgb(lib):
    lib.video_argb.restype = ctypes.c_uint32
    colors = [lib.video_argb(c) for c in range(256)]
    fb = (ctypes.c_uint8 * (320 * 200)).in_dll(lib, "fb")
    return [[((a := colors[fb[y * 320 + x]]) >> 16 & 255, a >> 8 & 255, a & 255) for x in range(320)] for y in range(200)]


def test_state_tells_the_party_and_the_champions(control, lib):
    path, game, loop = control
    with pumped(lib, ticks=False):
        state = request({"cmd": "state"}, path)
    assert state["ok"] and state["mode"] == "move" and state["dialog"] is None
    assert state["time"] == game.game_time and state["ticks"] == loop.timer_ticks
    assert state["party"] == {"map": game.party_map, "x": game.party_x, "y": game.party_y, "dir": game.party_dir}
    assert state["champion_count"] == game.champion_count == len(state["champions"]) and state["leader"] == 0
    champion = state["champions"][0]
    name = (ctypes.c_char * 8).from_address(ctypes.addressof(game) + GAME_CHAMPIONS).value.decode()
    assert champion["name"] == name
    assert champion["health"][0] > 0 and champion["health"][0] <= champion["health"][1]
    assert state["inventory"] == state["candidate"] == -1 and state["end"] == state["quit"] == 0
    assert 0 <= state["sound_volume"] <= 7 and 0 <= state["music_volume"] <= 7


def test_keys_by_name_and_code(control, lib):
    path, *_ = control
    lib.input_read_key.restype = ctypes.c_uint16
    with pumped(lib, ticks=False):
        for key in ("alt+a", "A", "shift+right", "enter", SCAN_ENTER):
            assert request({"cmd": "key", "key": key}, path) == {"ok": True}
    assert [lib.input_read_key() for _ in range(5)] == [SCAN_A | KEY_ALT, SCAN_A | KEY_SHIFT, 0x51, SCAN_ENTER, SCAN_ENTER]


def test_key_click_and_command_move_the_party(control, lib):
    path, game, _ = control
    direction, x, y = game.party_dir, game.party_x, game.party_y
    lib.input_find_command.restype = ctypes.c_uint16
    turn_right = lib.input_find_command(INPUT_MODE_MOVE, *arrow_center(lib, ZONE_TURN_RIGHT), LEFT, 0, (ctypes.c_int16 * 4)())
    with pumped(lib):
        assert request({"cmd": "key", "key": "up"}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 40}, path)["ok"]
        dx, dy = DIRECTIONS[direction]
        assert (game.party_x, game.party_y) == (x + dx, y + dy)
        cx, cy = arrow_center(lib, ZONE_TURN_RIGHT)
        assert request({"cmd": "click", "x": cx, "y": cy}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 16}, path)["ok"]
        assert game.party_dir == (direction + 1) % 4
        assert ctypes.c_int.in_dll(lib, "input_mouse_x").value == cx
        assert request({"cmd": "command", "id": turn_right}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 16}, path)["ok"]
        assert game.party_dir == (direction + 2) % 4
        assert request({"cmd": "click", "x": 10, "y": 100, "button": "right"}, path)["ok"]


def test_wait_answers_after_the_ticks(control, lib):
    path, _, loop = control
    with pumped(lib):
        before = loop.timer_ticks
        reply = request({"cmd": "wait", "ticks": 30}, path)
        after = loop.timer_ticks
    assert reply["ok"] and reply["ticks"] >= 30 and after - before >= 30


def test_screenshot_is_the_screen_with_the_pointer(control, lib, tmp_path):
    path, *_ = control
    ctypes.c_int.in_dll(lib, "input_mouse_x").value = -1
    with pumped(lib, ticks=False):
        reply = request({"cmd": "screenshot", "path": str(tmp_path / "a.png")}, path)
        assert reply == {"ok": True, "path": os.path.realpath(tmp_path / "a.png")}
        assert request({"cmd": "move", "x": 100, "y": 100}, path) == {"ok": True}
        assert request({"cmd": "screenshot", "path": str(tmp_path / "b.png")}, path)["ok"]
    screen = screen_rgb(lib)
    assert read_png(tmp_path / "a.png") == screen and len({rgb for row in screen for rgb in row}) > 16
    pointed = read_png(tmp_path / "b.png")
    changed = {(x, y) for y in range(200) for x in range(320) if pointed[y][x] != screen[y][x]}
    assert changed and all(100 <= x < 108 and 100 <= y < 111 for x, y in changed)


def test_bad_requests_get_errors(control, lib):
    path, *_ = control
    bad = [({"x": 1}, "missing cmd"), ({"cmd": "jump"}, "unknown cmd"), ({"cmd": "key", "key": "nokey"}, "unknown key"),
           ({"cmd": "click", "x": 320, "y": 0}, "x must be 0-319 and y 0-199"),
           ({"cmd": "click", "x": 1, "y": 1, "button": "middle"}, "button must be left or right"),
           ({"cmd": "wait", "ticks": 0}, "ticks must be 1-6000"), ({"cmd": "screenshot"}, "missing path"),
           ({"cmd": "screenshot", "path": "/nonexistent/a.png"}, "cannot write the PNG file")]
    with pumped(lib, ticks=False):
        for payload, error in bad:
            assert request(payload, path) == {"ok": False, "error": error}


def test_program_with_control_answers_and_quits(start, tmp_path):  # noqa: F811
    folder = tempfile.mkdtemp(prefix="dm2c", dir="/tmp")
    path = os.path.join(folder, "control.sock")
    env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
    program = subprocess.Popen([str(ROOT / "build/dm2"), "--control", path, "--save", str(ROOT / "tests/saves/start.DAT")],
                               cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(200):
            if os.path.exists(path):
                break
            time.sleep(0.05)
        state = request({"cmd": "state"}, path, timeout=10)
        assert state["ok"] and state["mode"] == "move" and state["champion_count"] == 1
        assert request({"cmd": "key", "key": "up"}, path, timeout=10)["ok"]
        assert request({"cmd": "wait", "ticks": 60}, path, timeout=10)["ticks"] >= 60
        moved = request({"cmd": "state"}, path, timeout=10)["party"]
        assert (moved["x"], moved["y"]) != (state["party"]["x"], state["party"]["y"])
        shot = request({"cmd": "screenshot", "path": str(tmp_path / "s.png")}, path, timeout=10)
        assert len(read_png(shot["path"])) == 200
        assert request({"cmd": "quit"}, path, timeout=10) == {"ok": True}
        assert program.wait(timeout=10) == 0
        assert not os.path.exists(path)
    finally:
        program.kill()
        shutil.rmtree(folder)
