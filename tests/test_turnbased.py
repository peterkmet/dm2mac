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
LEAVE_WAIT_TICKS = 2000
CMD_VOLUME, CMD_DIALOG_BUTTON_FIRST = 0x8D, 0xDB
STILL_CREATURE = 27  # a creature type that map 0 of start.DAT takes and that stands still
GROUP_DISTANCE = 6  # the view shows 4 rows ahead: the group comes into view after the second step
NORTH = 0


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
    hook = ctypes.c_void_p.in_dll(lib, "play_frame_hook")
    hook.value = ctypes.cast(lib.control_poll, ctypes.c_void_p).value  # a dialog polls the socket, as main.c does
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
        while pending():  # a turn that the test left runs to its end: the next test starts without one
            frame()
        hook.value = None
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
    """A client sends wait 2000 and closes: the turn runs all its ticks without a reply (a quiet place: no news); a
    new client gets the get_state reply, not the wait reply."""
    path, game, _ = tb
    before = game.game_time
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect(path)
        s.sendall(b'{"cmd": "wait", "ticks": %d}\n' % LEAVE_WAIT_TICKS)
        deadline = time.monotonic() + 10
        while not lib.control_turn_pending() and time.monotonic() < deadline:
            time.sleep(0.001)
        assert lib.control_turn_pending()
    r = request({"cmd": "get_state"}, path, timeout=60)
    assert r["ok"] and "ticks" not in r and "state" in r and not lib.control_turn_pending()
    assert r["state"]["time"] == before + LEAVE_WAIT_TICKS


def act(path, action, **args):
    return request({"cmd": "act", "action": action, **args}, path, timeout=30)


def command_rect(lib, command):
    """The screen rect (x, y, w, h) of the zone of command in the current input mode."""
    rect = (ctypes.c_int16 * 4)()
    lib.input_command_rect.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_int16)]
    lib.input_command_rect.restype = ctypes.c_bool
    assert lib.input_command_rect(command, rect), hex(command)
    return tuple(rect)


def center(rect):
    x, y, w, h = rect
    return x + w // 2, y + h // 2


def test_act_many_stops_on_a_new_creature(tb, lib):
    """Five moves up the corridor of the start; a creature group 6 squares ahead comes into view after the second
    move: ran 2, stopped creature."""
    path, game, _ = tb
    assert (game.party_x, game.party_y, game.party_dir) == (1, 8, NORTH)
    lib.creature_create_group.restype = ctypes.c_uint16
    lib.dun_set_current_map(game.party_map)
    assert lib.creature_create_group(STILL_CREATURE, 0, NORTH, game.party_x, game.party_y - GROUP_DISTANCE) != 0xFFFF
    r = request({"cmd": "act_many", "actions": ["move_forward"] * 5}, path, timeout=30)
    assert r["ok"] and r["ran"] == 2 and r["stopped"] == "creature" and "error" not in r
    assert r["state"]["party"]["y"] == 6 and r["ticks"] >= 4


def test_act_many_parses_names_and_objects(tb, lib):
    """["turn_left", {"action": "turn_right"}] runs both: ran 2, stopped done, facing back to the start."""
    path, game, _ = tb
    facing = request({"cmd": "get_state"}, path)["state"]["party"]["facing"]
    r = request({"cmd": "act_many", "actions": ["turn_left", {"action": "turn_right", "args": {}}]}, path, timeout=30)
    assert r["ok"] and r["ran"] == 2 and r["stopped"] == "done" and r["state"]["party"]["facing"] == facing


def test_act_many_bad_item_fails_without_running_it(tb, lib):
    """["turn_left", 7]: ran 1, stopped failed, an error text; a bad first item runs nothing."""
    path, game, _ = tb
    r = request({"cmd": "act_many", "actions": ["turn_left", 7]}, path, timeout=30)
    assert r["ok"] and r["ran"] == 1 and r["stopped"] == "failed" and r["error"]
    before = game.game_time
    r = request({"cmd": "act_many", "actions": ["fly", "turn_left"]}, path, timeout=30)
    assert r["ok"] and r["ran"] == 0 and r["stopped"] == "failed" and r["error"] == "unknown action"
    assert game.game_time == before and r["ticks"] == 0
    assert request({"cmd": "act_many", "actions": "turn_left"}, path) == {"ok": False, "error": "actions must be a list"}


def test_act_many_stops_on_bump(tb, lib):
    """["move_forward", "move_forward"] facing a wall: ran 1, stopped bumped."""
    path, *_ = tb
    face(path, lambda square: square["element"] == "wall")
    r = request({"cmd": "act_many", "actions": ["move_forward", "move_forward"]}, path, timeout=30)
    assert r["ok"] and r["ran"] == 1 and r["stopped"] == "bumped" and r["bumped"] is True


def test_raw_click_runs_a_turn_in_a_dialog(tb, lib):
    """act click on the volume button of an open inventory opens the volume dialog (state.mode dialog); act click on
    its first button closes it (state.mode inventory)."""
    path, *_ = tb
    assert act(path, "open_inventory", champion=0)["state"]["mode"] == "inventory"
    x, y = center(command_rect(lib, CMD_VOLUME))
    r = act(path, "click", x=x, y=y)
    assert r["ok"] and r["state"]["mode"] == "dialog"
    x, y = center(command_rect(lib, CMD_DIALOG_BUTTON_FIRST))
    r = act(path, "click", x=x, y=y, button="left")
    assert r["ok"] and r["state"]["mode"] == "inventory"
    assert act(path, "click", x=-1, y=0) == {"ok": False, "error": "x must be 0-319 and y 0-199"}
    assert act(path, "key", key="nokey") == {"ok": False, "error": "unknown key"}


def test_recruit_join_adds_the_candidate(tb, lib):
    """The mirror of the start corridor: three steps north, a turn left, click_wall_ahead shows the candidate; another
    named action is refused; act recruit choice join adds the candidate to state.champions."""
    path, game, _ = tb
    assert act(path, "recruit", choice="join")["error"].startswith("no candidate")
    for name in ("move_forward", "move_forward", "move_forward", "turn_left"):
        assert act(path, name)["ok"]
    assert ahead_square(request({"cmd": "get_view"}, path)["view"])["front_face"]["mirror"] is True
    r = act(path, "click_wall_ahead")
    assert r["ok"] and r["state"]["candidate"] is not None and len(r["state"]["champions"]) == 1
    before = game.game_time
    assert act(path, "turn_right") == {"ok": False, "error": "a candidate is shown: answer with recruit first"}
    assert act(path, "recruit", choice="stay") == {"ok": False, "error": "choice must be join or cancel"}
    assert game.game_time == before
    r = act(path, "recruit", choice="join")
    assert r["ok"] and r["state"]["candidate"] is None and len(r["state"]["champions"]) == 2


def test_choose_action_with_both_hands(tb, lib):
    """act choose_action champion 0 hand ready index 0 runs the action: the reply has action_damage (>= 0 or a
    failure code); an index out of the menu is refused."""
    path, *_ = tb
    assert act(path, "choose_action", champion=0, hand="ready", index=9)["error"].startswith("index out of range")
    r = act(path, "choose_action", champion=0, hand="ready", index=0)
    assert r["ok"] and isinstance(r["action_damage"], int)
    r = request({"cmd": "act_many", "actions": [{"action": "choose_action",
                                                 "args": {"champion": 0, "hand": "action", "index": 0}}]}, path, timeout=30)
    assert r["ok"] and r["ran"] == 1 and len(r["action_damage"]) == 1


def test_take_coin_moves_a_coin_to_the_hand(tb, lib):
    """The first money box of start.DAT (it holds coins) in the ready hand; choose_action on that hand opens it: act
    take_coin on a slot with coins -> state.leader_hand is that coin, one coin less in the box."""
    from test_interface import things_of
    path, *_ = tb
    lib.item_is_moneybox.argtypes = [ctypes.c_uint16]
    lib.champ_add_object_to_slot.argtypes = [ctypes.c_int, ctypes.c_uint16, ctypes.c_int]
    lib.champ_remove_object_from_slot(0, 0)
    lib.champ_add_object_to_slot(0, things_of(lib, 9, 1, lib.item_is_moneybox)[0], 0)
    lib.area_update(0)  # the hand icons show the box, as after a click that puts it into the hand
    assert act(path, "take_coin", slot=0)["error"].startswith("open a money box first")
    r = act(path, "choose_action", champion=0, hand="ready")
    assert r["ok"] and r["state"]["money_box"], r["state"]
    pile = next(p for p in r["state"]["money_box"] if p["count"])
    assert act(path, "take_coin", slot=6) == {"ok": False, "error": "slot must be 0-5"}
    r = act(path, "take_coin", slot=pile["slot"])
    assert r["ok"] and r["state"]["leader_hand"]["name"] == pile["coin"]
    assert next(p for p in r["state"]["money_box"] if p["slot"] == pile["slot"])["count"] == pile["count"] - 1


def test_put_in_slot_needs_an_object_in_hand(tb, lib):
    """put_in_slot with an empty leader hand: ok false, nothing changes."""
    path, game, _ = tb
    before = request({"cmd": "get_state"}, path)["state"]
    assert act(path, "put_in_slot", champion=0, slot=1) == {"ok": False, "error": "the leader hand is empty"}
    assert request({"cmd": "get_state"}, path)["state"] == before
