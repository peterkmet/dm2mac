"""The turns of the turn-based mode (port/src/platform/turn.c): act and wait run the game over the control socket until
the party can act again, or until news stops a wait."""

import ctypes
import os
import shutil
import socket
import tempfile
import threading
import time

import pytest

from dm2tools.control_client import request
from test_control import PALETTE_COLORS, RGB, GameWithChampions
from test_input import session  # noqa: F401 (a fixture)
from test_move import gdat, start  # noqa: F401 (gdat and start are fixtures)

VANILLA, TURNBASED = 0, 2
TURN_WAIT_MAX_TICKS = 5000
POISON_AMOUNT = 200  # a poison that hurts now and again within the wait
FACINGS = 4


@pytest.fixture
def tb(lib, gdat, session):  # noqa: F811
    """start.DAT in the turn-based mode with the control socket open and a thread that runs control_frame as the turn
    loop of main.c does; yields the socket path, the game record, the main loop."""
    frame, pending = lib.control_frame, lib.control_turn_pending
    palette = (ctypes.c_uint8 * (PALETTE_COLORS * RGB)).in_dll(lib, "palette")
    for c in range(PALETTE_COLORS):
        assert lib.gdat_palette_color(gdat, c, ctypes.byref(palette, RGB * c)) == 0
    mode = ctypes.c_int.in_dll(lib, "game_mode")
    mode.value = TURNBASED
    folder = tempfile.mkdtemp(prefix="dm2t", dir="/tmp")  # sun_path holds about 100 bytes
    path = os.path.join(folder, "control.sock")
    lib.control_open.argtypes = [ctypes.c_char_p]
    assert lib.control_open(path.encode())
    stop = threading.Event()

    def pump():
        while not stop.is_set():
            frame()
            if not pending():
                time.sleep(0.0005)

    thread = threading.Thread(target=pump)
    thread.start()
    game, loop = session
    loop.end = 0  # a game over of another test ends with the test
    try:
        yield path, GameWithChampions.from_address(ctypes.addressof(game)), loop
    finally:
        stop.set()
        thread.join()
        loop.end = 0
        lib.control_close()
        mode.value = VANILLA
        shutil.rmtree(folder)


def ahead_square(view):
    return next(q for q in view["squares"] if q["depth"] == 1 and q["side"] == 0)


def face(path, wanted):
    """Turns the party with act turn_right until the square ahead is wanted (a predicate of the view square); returns
    the square."""
    view = request({"cmd": "get_view"}, path)["view"]
    for _ in range(FACINGS):
        square = ahead_square(view)
        if wanted(square):
            return square
        reply = request({"cmd": "act", "action": "turn_right"}, path, timeout=30)
        assert reply["ok"], reply
        view = reply["view"]
    pytest.fail("no facing has the wanted square ahead")


def open_floor(square):
    return square["element"] == "floor" and not square.get("creatures")


def test_act_move_forward_moves_one_square(tb, lib):
    """A step on an open square: the turn runs until the party can move again; the reply has ticks, game_over, state,
    view; the party stands one square further."""
    path, *_ = tb
    square = face(path, open_floor)
    r = request({"cmd": "act", "action": "move_forward"}, path, timeout=30)
    assert r["ok"] and r["ticks"] >= 2 and r["game_over"] is False and "state" in r and "view" in r
    assert "bumped" not in r and "stopped" not in r
    assert (r["state"]["party"]["x"], r["state"]["party"]["y"]) == (square["x"], square["y"])
    assert r["state"]["party"]["can_move"]


def test_bump_keeps_the_square(tb, lib):
    """move_forward into a wall: "bumped": true and the square stays."""
    path, game, _ = tb
    face(path, lambda square: square["element"] == "wall")
    x, y = game.party_x, game.party_y
    r = request({"cmd": "act", "action": "move_forward"}, path, timeout=30)
    assert r["ok"] and r["bumped"] is True
    assert (r["state"]["party"]["x"], r["state"]["party"]["y"]) == (x, y)


def test_turn_does_not_tick_between_turns(tb, lib):
    """Between turns the game time stands still: two get_state 0.2 s apart have the same time."""
    path, *_ = tb
    first = request({"cmd": "get_state"}, path)
    time.sleep(0.2)
    second = request({"cmd": "get_state"}, path)
    assert first["ok"] and first["state"]["time"] == second["state"]["time"]


def test_unknown_and_bad_actions_change_nothing(tb, lib):
    path, game, _ = tb
    before = game.game_time
    assert request({"cmd": "act"}, path) == {"ok": False, "error": "missing action"}
    assert request({"cmd": "act", "action": "fly"}, path) == {"ok": False, "error": "unknown action"}
    assert request({"cmd": "wait", "ticks": 0}, path) == {"ok": False, "error": "ticks must be 1-5000"}
    assert request({"cmd": "wait", "ticks": TURN_WAIT_MAX_TICKS + 1}, path)["error"] == "ticks must be 1-5000"
    assert game.game_time == before


def test_wait_stops_on_damage(tb, lib):
    """wait 5000 with a poisoned champion: the poison hurts again -> stopped damage, ticks < 5000, health_lost."""
    path, *_ = tb
    lib.champ_poison(0, POISON_AMOUNT)  # no turn runs: the pump thread only polls the socket
    r = request({"cmd": "wait", "ticks": TURN_WAIT_MAX_TICKS}, path, timeout=30)
    assert r["ok"] and r["stopped"] == "damage" and r["ticks"] < TURN_WAIT_MAX_TICKS
    assert r["health_lost"][0] > 0


def test_wait_runs_its_ticks_without_news(tb, lib):
    """wait 50 in a quiet place: ticks == 50, stopped done."""
    path, *_ = tb
    r = request({"cmd": "wait", "ticks": 50}, path, timeout=30)
    assert r["ok"] and r["ticks"] == 50 and r["stopped"] == "done" and r["game_over"] is False
    assert "health_lost" not in r and "seen" not in r


def test_direct_commands_are_refused(tb, lib):
    path, game, _ = tb
    x, y, direction = game.party_x, game.party_y, game.party_dir
    for cmd in ({"cmd": "key", "key": "up"}, {"cmd": "click", "x": 1, "y": 1}, {"cmd": "press", "x": 1, "y": 1},
                {"cmd": "release", "x": 1, "y": 1}, {"cmd": "command", "id": 3}, {"cmd": "move", "x": 1, "y": 1}):
        assert request(cmd, path) == {"ok": False, "error": "use act in turnbased mode"}
    request({"cmd": "wait", "ticks": 20}, path, timeout=30)
    assert (game.party_x, game.party_y, game.party_dir) == (x, y, direction)


def test_game_over_refuses_turns(tb, lib):
    """Set main_loop.end = 1 (the party died): act, act_many, wait, save reply "game over"; get_state works and
    state.party.dead is true."""
    path, _, loop = tb
    loop.end = 1
    for cmd in ({"cmd": "act", "action": "move_forward"}, {"cmd": "act_many", "actions": ["move_forward"]},
                {"cmd": "wait", "ticks": 5}, {"cmd": "save"}):
        assert request(cmd, path) == {"ok": False, "error": "game over"}
    r = request({"cmd": "get_state"}, path)
    assert r["ok"] and r["state"]["party"]["dead"] is True
    assert request({"cmd": "get_view"}, path)["ok"] and request({"cmd": "get_map"}, path)["ok"]


def test_move_during_cooldown_runs_in_the_same_turn(tb, lib):
    """Set game.party_move_cooldown to 3 before act move_forward: the step happens in the same turn."""
    path, game, _ = tb
    square = face(path, open_floor)
    game.party_move_cooldown = 3
    r = request({"cmd": "act", "action": "move_forward"}, path, timeout=30)
    assert r["ok"] and (r["state"]["party"]["x"], r["state"]["party"]["y"]) == (square["x"], square["y"])
    assert r["state"]["party"]["can_move"]


def test_client_that_leaves_during_a_turn_frees_the_turn(tb, lib):
    """A client sends wait 2000 and closes: the turn ends without a reply; a new client gets get_state."""
    path, game, _ = tb
    before = game.game_time
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect(path)
        s.sendall(b'{"cmd": "wait", "ticks": 2000}\n')
        deadline = time.monotonic() + 10
        while not lib.control_turn_pending() and time.monotonic() < deadline:
            time.sleep(0.001)
        assert lib.control_turn_pending()
    r = request({"cmd": "get_state"}, path, timeout=60)
    assert r["ok"] and not lib.control_turn_pending() and r["state"]["time"] > before
