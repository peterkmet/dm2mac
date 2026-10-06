"""The control socket (port/src/control.c): requests over a Unix domain socket give input to the game and read its
state and screen; the program starts it with --control."""

import ctypes
import json
import os
import shutil
import socket
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
from test_move import WorldGame, gdat, start  # noqa: F401 (gdat and start are fixtures)
from test_interface import UiState, things_of

KEY_SHIFT, KEY_ALT = 0x200, 0x800
SCAN_A, SCAN_ENTER = 0x1E, 0x1C
ZONE_TURN_RIGHT = 0x29
ZONE_EYE_CLICK, ZONE_PANEL_ORIGIN, CMD_EYE, INPUT_MODE_INVENTORY = 0x222, 7, 0x47, 8  # the eye zone is in the panel
PANEL_MODE_SKILLS, PANEL_MODE_SCROLL, THING_SCROLL = 2, 5, 7
ZONE_MOUTH_CLICK, CMD_MOUTH = 0x221, 0x46
INPUT_MODE_MOVE, LEFT = 5, 2
DIRECTIONS = ((0, -1), (1, 0), (0, 1), (-1, 0))
SCREEN_W, SCREEN_H, PALETTE_COLORS, RGB = 320, 200, 256, 3
PNG_ROW = 1 + RGB * SCREEN_W  # the filter byte and the pixels of a row
CHAMPION_SIZE, NAME_SIZE, SCAN_QUOTE = 263, 8, 0x28
LONG_LINE = 5000  # more than the request buffer of 4096 bytes
TOO_BIG = 2**32 + 1  # 1 after a cast to int


class GameWithChampions(WorldGame):
    """save.h Game up to the champion records."""
    _fields_ = [("global_flags", ctypes.c_uint8 * 8), ("global_bytes", ctypes.c_uint8 * 64),
                ("global_words", ctypes.c_uint16 * 64), ("champions", ctypes.c_uint8 * CHAMPION_SIZE * 4)]


@pytest.fixture
def control(lib, gdat, session):  # noqa: F811
    """start.DAT ready to play with its palette and the control socket open; yields the socket path, the game record,
    the main loop."""
    palette = (ctypes.c_uint8 * (PALETTE_COLORS * RGB)).in_dll(lib, "palette")
    for c in range(PALETTE_COLORS):
        assert lib.gdat_palette_color(gdat, c, ctypes.byref(palette, RGB * c)) == 0
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
    assert (width, height, depth, color) == (SCREEN_W, SCREEN_H, 8, 2)
    raw = zlib.decompress(chunks[b"IDAT"])
    rows = [raw[y * PNG_ROW:(y + 1) * PNG_ROW] for y in range(SCREEN_H)]
    assert all(row[0] == 0 for row in rows)
    return [[tuple(row[1 + RGB * x:1 + RGB * (x + 1)]) for x in range(SCREEN_W)] for row in rows]


def screen_rgb(lib):
    lib.video_argb.restype = ctypes.c_uint32
    colors = [lib.video_argb(c) for c in range(PALETTE_COLORS)]
    fb = (ctypes.c_uint8 * (SCREEN_W * SCREEN_H)).in_dll(lib, "fb")
    return [[((a := colors[fb[y * SCREEN_W + x]]) >> 16 & 255, a >> 8 & 255, a & 255) for x in range(SCREEN_W)]
            for y in range(SCREEN_H)]


def raw_lines(path, data, count):
    """Sends raw bytes and returns the first count reply lines as objects."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(10)
        s.connect(path)
        s.sendall(data)
        received = b""
        while received.count(b"\n") < count:
            chunk = s.recv(65536)
            assert chunk, received
            received += chunk
    return [json.loads(line) for line in received.split(b"\n")[:count]]


def test_state_tells_the_party_and_the_champions(control, lib):
    path, game, loop = control
    with pumped(lib, ticks=False):
        state = request({"cmd": "state"}, path)
    assert state["ok"] and state["mode"] == "move" and state["dialog"] is None
    assert state["time"] == game.game_time and state["ticks"] == loop.timer_ticks
    assert state["party"] == {"map": game.party_map, "x": game.party_x, "y": game.party_y, "dir": game.party_dir}
    assert state["champion_count"] == game.champion_count == len(state["champions"]) and state["leader"] == 0
    champion = state["champions"][0]
    name = (ctypes.c_char * NAME_SIZE).from_address(
        ctypes.addressof(game) + GameWithChampions.champions.offset).value.decode()
    assert champion["name"] == name
    assert champion["health"][0] > 0 and champion["health"][0] <= champion["health"][1]
    assert state["inventory"] == state["candidate"] == -1 and state["end"] == state["quit"] == 0
    assert 0 <= state["sound_volume"] <= 7 and 0 <= state["music_volume"] <= 7


def test_keys_by_name_and_code(control, lib):
    path, *_ = control
    lib.input_read_key.restype = ctypes.c_uint16
    with pumped(lib, ticks=False):
        for key in ("alt+a", "A", "shift+right", "enter", SCAN_ENTER, '"'):
            assert request({"cmd": "key", "key": key}, path) == {"ok": True}
    assert [lib.input_read_key() for _ in range(6)] == [SCAN_A | KEY_ALT, SCAN_A | KEY_SHIFT, 0x51, SCAN_ENTER, SCAN_ENTER,
                                                         SCAN_QUOTE | KEY_SHIFT]


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


def test_eye_and_mouth_hold_while_the_left_button_is_down(control, lib, tmp_path):
    # The original eye command 0x47 (input_hold_right_button) sets the eye only while the left button is down and hides
    # the pointer; the release of the button (event 4, input_cancel_mouse_capture) ends it. The eye shows the skills
    # with an empty leader hand, else the leader hand object.
    path, *_ = control
    ui = UiState.in_dll(lib, "ui")
    lib.champ_put_object_in_leader_hand.argtypes = [ctypes.c_uint16, ctypes.c_int]
    lib.input_find_command.restype = ctypes.c_uint16

    def hold_and_release(x, y, mode):
        assert request({"cmd": "press", "x": x, "y": y}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        assert ui.eye == 1 and ui.panel_mode == mode
        assert request({"cmd": "screenshot", "path": str(tmp_path / "held.png")}, path)["ok"]
        held, screen = read_png(tmp_path / "held.png"), screen_rgb(lib)
        assert [row[x:x + 16] for row in held[y:y + 16]] == [row[x:x + 16] for row in screen[y:y + 16]]  # no pointer
        assert request({"cmd": "release", "x": x, "y": y}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        assert ui.eye == 0 and ui.panel_mode != mode

    with pumped(lib):
        assert request({"cmd": "key", "key": "1"}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        origin_x, origin_y = ctypes.c_int16(), ctypes.c_int16()
        lib.zone_get_position(ZONE_PANEL_ORIGIN, ctypes.byref(origin_x), ctypes.byref(origin_y))
        x, y = arrow_center(lib, ZONE_EYE_CLICK)
        x, y = x + origin_x.value, y + origin_y.value
        assert lib.input_find_command(INPUT_MODE_INVENTORY, x, y, LEFT, 0, (ctypes.c_int16 * 4)()) == CMD_EYE
        assert request({"cmd": "click", "x": x, "y": y}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        assert ui.eye == 0 and ui.panel_mode != PANEL_MODE_SKILLS
        hold_and_release(x, y, PANEL_MODE_SKILLS)
        mouth_x, mouth_y = arrow_center(lib, ZONE_MOUTH_CLICK)
        mouth_x, mouth_y = mouth_x + origin_x.value, mouth_y + origin_y.value
        assert lib.input_find_command(INPUT_MODE_INVENTORY, mouth_x, mouth_y, LEFT, 0, (ctypes.c_int16 * 4)()) == CMD_MOUTH
        assert request({"cmd": "press", "x": mouth_x, "y": mouth_y}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        assert ui.mouth == 1  # the mouth with an empty hand holds like the eye
        assert request({"cmd": "release", "x": mouth_x, "y": mouth_y}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        assert ui.mouth == 0
    lib.champ_put_object_in_leader_hand(things_of(lib, THING_SCROLL, 1)[0], 0)
    with pumped(lib):
        hold_and_release(x, y, PANEL_MODE_SCROLL)


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
    assert changed and all(100 <= x < 116 and 100 <= y < 116 for x, y in changed)  # a 16 x 16 cursor, hot spot (0, 0)


def test_screenshot_path_with_non_ascii_characters(control, lib, tmp_path):
    path, *_ = control
    shot = tmp_path / "Bild \u00e4\u20ac\U0001f600.png"  # the client sends them as \\u escapes
    with pumped(lib, ticks=False):
        assert request({"cmd": "screenshot", "path": str(shot)}, path) == {"ok": True, "path": os.path.realpath(shot)}
    assert shot.exists()


def test_open_keeps_a_file_that_is_not_a_socket(lib):
    folder = tempfile.mkdtemp(prefix="dm2c", dir="/tmp")
    path = os.path.join(folder, "start.DAT")
    with open(path, "wb") as f:
        f.write(b"save")
    lib.control_open.argtypes = [ctypes.c_char_p]
    assert not lib.control_open(path.encode())
    assert open(path, "rb").read() == b"save"
    shutil.rmtree(folder)


def test_a_client_that_leaves_during_a_wait_frees_the_socket(control, lib):
    path, *_ = control
    with pumped(lib, ticks=False):  # the wait never ends by itself
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.connect(path)
            s.sendall(b'{"cmd": "wait", "ticks": 6000}\n')
            time.sleep(0.05)
        assert request({"cmd": "state"}, path, timeout=5)["ok"]


def test_a_second_client_gets_busy(control, lib):
    path, *_ = control
    with pumped(lib, ticks=False), socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as first:
        first.connect(path)
        first.sendall(b'{"cmd": "wait", "ticks": 6000}\n')
        time.sleep(0.05)
        assert request({"cmd": "state"}, path, timeout=5) == {"ok": False, "error": "busy"}


def test_long_line_and_nul_byte_get_one_error_each(control, lib):
    path, *_ = control
    data = b"x" * LONG_LINE + b"\n" + b'{"cmd": "state"\0}\n' + b'{"cmd": "state"}\n'
    with pumped(lib, ticks=False):
        long_line, nul, state = raw_lines(path, data, 3)
    assert long_line == {"ok": False, "error": "request line too long"}
    assert nul == {"ok": False, "error": "request line has a NUL byte"}
    assert state["ok"] and state["mode"] == "move"


def test_bad_requests_get_errors(control, lib):
    path, *_ = control
    bad = [({"x": 1}, "missing cmd"), ({"cmd": "jump"}, "unknown cmd"), ({"cmd": "key", "key": "nokey"}, "unknown key"),
           ({"cmd": "click", "x": 320, "y": 0}, "x must be 0-319 and y 0-199"),
           ({"cmd": "click", "x": 1, "y": 1, "button": "middle"}, "button must be left or right"),
           ({"cmd": "click", "x": TOO_BIG, "y": 0}, "x must be 0-319 and y 0-199"),
           ({"cmd": "command", "id": TOO_BIG}, "missing id"),
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


def test_bad_unicode_escapes_are_not_hex_digits(control, lib):
    path, *_ = control
    data = b'{"cmd": "key", "key": "\\u 041"}\n{"cmd": "key", "key": "\\u0x41"}\n'
    with pumped(lib, ticks=False):
        assert raw_lines(path, data, 2) == [{"ok": False, "error": "unknown key"}] * 2


def test_too_long_reply_is_one_error_line(control, lib, tmp_path):
    path, *_ = control
    folder = tmp_path
    for _ in range(4):  # 800 control characters, each 6 bytes as a JSON escape: more than a reply holds
        folder = folder / ("\x01" * 200)
    folder.mkdir(parents=True)
    raw_path = str(folder / "a.png").encode()  # raw bytes 1 in the request: it stays short
    data = b'{"cmd": "screenshot", "path": "' + raw_path + b'"}\n{"cmd": "state"}\n'
    with pumped(lib, ticks=False):
        too_long, state = raw_lines(path, data, 2)
    assert too_long == {"ok": False, "error": "reply too long"} and state["ok"]


def test_open_replaces_a_stale_socket(control, lib):
    path, *_ = control
    lib.control_close()
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as stale:
        stale.bind(path)
    assert os.path.exists(path)
    assert lib.control_open(path.encode())
    with pumped(lib, ticks=False):
        assert request({"cmd": "state"}, path, timeout=5)["ok"]
