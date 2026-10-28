"""The control socket (src/control/control.c): requests over a Unix domain socket give input to the game and read its
state and screen; the program starts it with --control."""

import ctypes
import json
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import zlib

import pytest

from dm2tools.control_client import request
from dm2test import constants as C
from dm2test.control import (  # noqa: F401 (control is a fixture)
    NAME_SIZE, PALETTE_COLORS, RGB, SCAN_QUOTE, SCREEN_H, SCREEN_W, GameWithChampions, control, pumped,
)
from dm2test.input import session  # noqa: F401 (a fixture)
from dm2test.markers import ROOT
from dm2test.scenario import gdat, start  # noqa: F401 (gdat and start are fixtures)
from dm2test.interface import UiState, things_of
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

KEY_SHIFTED, KEY_ALT = C.KEY_SHIFTED, C.KEY_ALT
SCAN_A, SCAN_ENTER = 0x1E, 0x1C
ZONE_TURN_RIGHT = 0x29
ZONE_EYE_CLICK, ZONE_VIEWPORT, CMD_EYE, INPUT_MODE_INVENTORY = 0x222, 7, C.CMD_EYE, C.INPUT_MODE_INVENTORY  # the eye zone is in the panel
PANEL_MODE_SKILLS, PANEL_MODE_SCROLL, THING_SCROLL = C.PANEL_MODE_SKILLS, C.PANEL_MODE_SCROLL, C.THING_SCROLL
ZONE_MOUTH_CLICK, CMD_MOUTH = 0x221, C.CMD_MOUTH
INPUT_MODE_MOVE, LEFT = C.INPUT_MODE_MOVE, C.INPUT_BUTTON_LEFT
DIRECTIONS = ((0, -1), (1, 0), (0, 1), (-1, 0))
PNG_ROW = 1 + RGB * SCREEN_W  # the filter byte and the pixels of a row
LONG_LINE = 5000  # more than the request buffer of 4096 bytes
TOO_BIG = 2**32 + 1  # 1 after a cast to int


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
    fb = (ctypes.c_uint8 * (SCREEN_W * SCREEN_H)).in_dll(lib, "g_video_fb")
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
    assert [lib.input_read_key() for _ in range(6)] == [SCAN_A | KEY_ALT, SCAN_A | KEY_SHIFTED, 0x51, SCAN_ENTER, SCAN_ENTER,
                                                         SCAN_QUOTE | KEY_SHIFTED]


def test_key_click_and_command_move_the_party(control, lib):
    path, game, _ = control
    direction, x, y = game.party_dir, game.party_x, game.party_y
    lib.input_click_command.restype = ctypes.c_uint16
    turn_right = lib.input_click_command(INPUT_MODE_MOVE, *arrow_center(lib, ZONE_TURN_RIGHT), LEFT, (ctypes.c_int16 * 4)())
    with pumped(lib):
        assert request({"cmd": "key", "key": "up"}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 40}, path)["ok"]
        dx, dy = DIRECTIONS[direction]
        assert (game.party_x, game.party_y) == (x + dx, y + dy)
        cx, cy = arrow_center(lib, ZONE_TURN_RIGHT)
        assert request({"cmd": "click", "x": cx, "y": cy}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 16}, path)["ok"]
        assert game.party_dir == (direction + 1) % 4
        assert ctypes.c_int.in_dll(lib, "g_input_mouse_x").value == cx
        assert request({"cmd": "command", "id": turn_right}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 16}, path)["ok"]
        assert game.party_dir == (direction + 2) % 4
        assert request({"cmd": "click", "x": 10, "y": 100, "button": "right"}, path)["ok"]


def test_eye_and_mouth_hold_while_the_left_button_is_down(control, lib, tmp_path):
    # The original eye command 0x47 (input_hold_right_button) sets the eye only while the left button is down and hides
    # the pointer; the release of the button (event 4, input_cancel_mouse_capture) ends it. The eye shows the skills
    # with an empty leader hand, else the leader hand object.
    path, *_ = control
    ui = UiState.in_dll(lib, "g_ui")
    lib.champ_put_in_leader_hand.argtypes = [ctypes.c_uint16]
    lib.input_click_command.restype = ctypes.c_uint16

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
        lib.zone_get_position(ZONE_VIEWPORT, ctypes.byref(origin_x), ctypes.byref(origin_y))
        x, y = arrow_center(lib, ZONE_EYE_CLICK)
        x, y = x + origin_x.value, y + origin_y.value
        assert lib.input_click_command(INPUT_MODE_INVENTORY, x, y, LEFT, (ctypes.c_int16 * 4)()) == CMD_EYE
        assert request({"cmd": "click", "x": x, "y": y}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        assert ui.eye == 0 and ui.panel_mode != PANEL_MODE_SKILLS
        hold_and_release(x, y, PANEL_MODE_SKILLS)
        mouth_x, mouth_y = arrow_center(lib, ZONE_MOUTH_CLICK)
        mouth_x, mouth_y = mouth_x + origin_x.value, mouth_y + origin_y.value
        assert lib.input_click_command(INPUT_MODE_INVENTORY, mouth_x, mouth_y, LEFT, (ctypes.c_int16 * 4)()) == CMD_MOUTH
        assert request({"cmd": "press", "x": mouth_x, "y": mouth_y}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        assert ui.mouth == 1  # the mouth with an empty hand holds like the eye
        assert request({"cmd": "release", "x": mouth_x, "y": mouth_y}, path)["ok"]
        assert request({"cmd": "wait", "ticks": 20}, path)["ok"]
        assert ui.mouth == 0
    lib.champ_put_in_leader_hand(things_of(lib, THING_SCROLL, 1)[0])
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
    ctypes.c_int.in_dll(lib, "g_input_mouse_x").value = -1
    with pumped(lib, ticks=False):
        reply = request({"cmd": "screenshot", "path": str(tmp_path / "a.png")}, path)
        assert reply == {"ok": True, "path": os.path.realpath(tmp_path / "a.png")}
        assert request({"cmd": "move", "x": 100, "y": 100}, path) == {"ok": True}
        assert request({"cmd": "screenshot", "path": str(tmp_path / "b.png")}, path)["ok"]
    screen = screen_rgb(lib)
    assert read_png(tmp_path / "a.png") == screen and len({rgb for row in screen for rgb in row}) > 16
    pointed = read_png(tmp_path / "b.png")
    changed = {(x, y) for y in range(C.SCREEN_H) for x in range(C.SCREEN_W) if pointed[y][x] != screen[y][x]}
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
    lib.transport_open.argtypes = [ctypes.c_char_p]
    lib.transport_open.restype = ctypes.c_bool
    assert not lib.transport_open(path.encode())
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
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as first:
        first.connect(path)
        first.sendall(b'{"cmd": "wait", "ticks": 6000}\n')
        lib.transport_poll()  # accepts the first client and starts its wait (no game ticks: it never ends)
        lib.transport_poll()
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as second:
            second.settimeout(5)
            second.connect(path)  # sends nothing: the server sends busy and closes before it reads
            lib.transport_poll()
            assert json.loads(second.makefile().readline()) == {"ok": False, "error": "busy"}


def test_a_client_that_reconnects_after_a_reply_is_not_busy(control, lib):
    path, *_ = control
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as first, socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as second:
        first.connect(path)
        first.sendall(b'{"cmd": "state"}\n')
        first.shutdown(socket.SHUT_WR)
        lib.transport_poll()  # accepts the first client
        second.connect(path)
        second.sendall(b'{"cmd": "state"}\n')
        lib.transport_poll()  # answers the first client, then accepts the second
        assert json.loads(first.makefile().readline())["ok"]
        lib.transport_poll()
        assert json.loads(second.makefile().readline())["ok"]


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
        assert len(read_png(shot["path"])) == C.SCREEN_H
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


def test_a_long_reply_arrives_whole(lib):
    """The JSON writer grows past 4096 bytes and keeps valid JSON (no reply size limit)."""
    class Json(ctypes.Structure):
        _fields_ = [("s", ctypes.c_void_p), ("len", ctypes.c_size_t), ("cap", ctypes.c_size_t)]
    j = Json()
    lib.json_open(ctypes.byref(j), ord("["))
    for _ in range(1000):
        lib.json_str(ctypes.byref(j), "caf\u00e9 \"q\"\n".encode())
    lib.json_close(ctypes.byref(j), ord("]"))
    text = ctypes.string_at(j.s, j.len).decode()
    assert len(text) > 4096 and json.loads(text) == ["caf\u00e9 \"q\"\n"] * 1000
    ctypes.CDLL(None).free(ctypes.c_void_p(j.s))


def test_open_replaces_a_stale_socket(control, lib):
    path, *_ = control
    lib.transport_close()
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as stale:
        stale.bind(path)
    assert os.path.exists(path)
    assert lib.transport_open(path.encode())
    with pumped(lib, ticks=False):
        assert request({"cmd": "state"}, path, timeout=5)["ok"]
