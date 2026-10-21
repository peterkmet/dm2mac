"""The turns of the turn-based mode (src/control/turn.c): act and wait run the game over the control socket until
the party can act again, or until news stops a wait."""

import ctypes
import os
import shutil
import socket
import tempfile
import threading
import time

import pytest

from dm2test import constants as C
from dm2test.markers import ROOT
from dm2tools.control_client import request
from dm2test.control import PALETTE_COLORS, RGB, GameWithChampions
from dm2test.dialog import Session
from dm2test.input import session  # noqa: F401 (a fixture)
from dm2test.scenario import gdat, start  # noqa: F401 (gdat and start are fixtures)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

VANILLA, TURNBASED = C.GAME_MODE_VANILLA, C.GAME_MODE_TURNBASED
TURN_WAIT_MAX_TICKS = 5000
POISON_AMOUNT = 200  # a poison that hurts now and again within the wait
FACINGS = C.DIR_COUNT
LEAVE_WAIT_TICKS = 2000
CMD_VOLUME, CMD_DIALOG_BUTTON_FIRST = C.CMD_VOLUME, C.CMD_DIALOG_BUTTON_FIRST
STILL_CREATURE = 27  # a creature type that map 0 of start.DAT takes and that stands still
FLOOR_HIDING_CREATURE = 1  # a static creature type whose floor takes no click
GROUP_CREATURE = C.GROUP_CREATURE  # the byte of the creature type in a group record
GROUP_DISTANCE = 6  # the view shows 4 rows ahead: the group comes into view after the second step
NORTH = C.DIR_NORTH


@pytest.fixture
def tb(lib, gdat, session):  # noqa: F811
    """start.DAT in the turn-based mode with the control socket open and a thread that runs control_frame as the turn
    loop of main.c does; yields the socket path, the game record, the main loop."""
    poll, frame, pending = lib.transport_poll, lib.control_frame, lib.control_is_turn_pending
    palette = (ctypes.c_uint8 * (PALETTE_COLORS * RGB)).in_dll(lib, "palette")
    for c in range(PALETTE_COLORS):
        assert lib.gdat_palette_color(gdat, c, ctypes.byref(palette, RGB * c)) == 0
    mode = ctypes.c_int.in_dll(lib, "game_mode")
    mode.value = TURNBASED
    hook = ctypes.c_void_p.in_dll(lib, "play_frame_hook")
    hook.value = ctypes.cast(lib.transport_poll, ctypes.c_void_p).value  # a dialog polls the socket, as main.c does
    folder = tempfile.mkdtemp(prefix="dm2t", dir="/tmp")  # sun_path holds about 100 bytes
    path = os.path.join(folder, "control.sock")
    lib.transport_open.argtypes = [ctypes.c_char_p]
    lib.transport_open.restype = ctypes.c_bool
    assert lib.transport_open(path.encode())
    stop = threading.Event()

    def pump():
        while not stop.is_set():
            poll()
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
            poll()
            frame()
        hook.value = None
        loop.end = 0
        lib.transport_close()
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
        while not lib.control_is_turn_pending() and time.monotonic() < deadline:
            time.sleep(0.001)
        assert lib.control_is_turn_pending()
    r = request({"cmd": "get_state"}, path, timeout=60)
    assert r["ok"] and "ticks" not in r and "state" in r and not lib.control_is_turn_pending()
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
    failure code) and the ready hand is busy; an index out of the menu is refused."""
    path, *_ = tb
    assert act(path, "choose_action", champion=0, hand="ready", index=9)["error"].startswith("index out of range")
    assert request({"cmd": "get_state"}, path)["state"]["champions"][0]["busy"][0] is False
    r = act(path, "choose_action", champion=0, hand="ready", index=0)
    assert r["ok"] and isinstance(r["action_damage"], int)
    assert r["state"]["champions"][0]["busy"][0] is True
    r = request({"cmd": "act_many", "actions": [{"action": "choose_action",
                                                 "args": {"champion": 0, "hand": "action", "index": 0}}]}, path, timeout=30)
    assert r["ok"] and r["ran"] == 1 and len(r["action_damage"]) == 1


def test_take_coin_moves_a_coin_to_the_hand(tb, lib):
    """The first money box of start.DAT (it holds coins) in the ready hand; choose_action on that hand opens it: act
    take_coin on a slot with coins -> state.leader_hand is that coin, one coin less in the box."""
    from dm2test.interface import things_of
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


def test_drop_object_ahead_of_a_floor_hiding_creature_is_refused(tb, lib):
    """A static creature whose floor takes no click stands ahead: drop_object cell 2 is refused and the object stays
    in the leader hand (a click there throws it)."""
    from dm2test.interface import things_of
    path, game, _ = tb
    assert (game.party_x, game.party_y, game.party_dir) == (1, 8, NORTH)
    lib.creature_create_group.restype = ctypes.c_uint16
    lib.dun_set_current_map(game.party_map)
    group = lib.creature_create_group(STILL_CREATURE, 0, NORTH, game.party_x, game.party_y - 1)
    assert group != 0xFFFF
    lib.dungeon_thing_record.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.dungeon_thing_record.argtypes = [ctypes.c_uint16]
    lib.dungeon_thing_record(group)[GROUP_CREATURE] = FLOOR_HIDING_CREATURE  # map 0 takes no static creature type
    lib.item_is_moneybox.argtypes = [ctypes.c_uint16]
    lib.champ_put_object_in_leader_hand.argtypes = [ctypes.c_uint16, ctypes.c_int]
    thing = things_of(lib, 9, 1, lib.item_is_moneybox)[0]
    lib.champ_put_object_in_leader_hand(thing, 0)
    before = game.game_time
    assert act(path, "drop_object", cell=2) == {"ok": False, "error": "no floor to drop on ahead"}
    after = request({"cmd": "get_state"}, path)["state"]
    assert after["leader_hand"] is not None and game.game_time == before


NO_GAME = {"ok": False, "error": "no game is running: use load or new_game"}
CLOSE_DIALOG = {"ok": False, "error": "close the dialog first (act click or act key)"}
DAMAGED_SAVE = b"\0" * 300
LOST_UMASK = 0o477  # QUICK.TMP gets no read rights: the restore of the running game fails
SESSION_START_STEPS = 1  # the start of a loaded game runs one game step (play_start_session)


@pytest.fixture
def saves(tb, lib, tmp_path):
    """The save folder of the turn-based mode in tmp_path and the game folder for new_game; both go back after the
    test, with the session flags that new_game and a completed game set."""
    state = Session.in_dll(lib, "session")
    data_dir, save_dir = state.data_dir, state.save_dir
    folder = tmp_path / "saves"  # tmp_path holds the SKSAVE0.DAT of the session fixture
    folder.mkdir()
    state.data_dir, state.save_dir = str(ROOT / "game").encode(), str(folder).encode()
    try:
        yield folder
    finally:
        state.data_dir, state.save_dir = data_dir, save_dir
        state.new_game = state.completed = 0


def place(r, steps=0):
    """The party square and the game time of a reply, the time less steps."""
    return r["state"]["party"]["x"], r["state"]["party"]["y"], r["state"]["time"] - steps


def lose_the_game(path, folder):
    """A damaged QUICK.DAT and a failed restore of the running game: load replies the damage and no game is left."""
    (folder / "QUICK.DAT").write_bytes(DAMAGED_SAVE)
    old = os.umask(LOST_UMASK)
    try:
        assert request({"cmd": "load"}, path, timeout=30) == {"ok": False, "error": "the saved game is damaged"}
    finally:
        os.umask(old)
        (folder / "QUICK.TMP").chmod(0o644)


def test_save_move_load_returns_to_the_save(tb, lib, saves):
    """save, act move_forward, load: the party square and the game time are those of the save (and the game step of
    the start of the loaded game)."""
    path, *_ = tb
    saved = place(request({"cmd": "get_state"}, path))
    assert request({"cmd": "save"}, path, timeout=30) == {"ok": True}
    assert (saves / "QUICK.DAT").exists()
    face(path, open_floor)
    assert place(act(path, "move_forward"))[:2] != saved[:2]
    r = request({"cmd": "load"}, path, timeout=30)
    assert r["ok"] and place(r, SESSION_START_STEPS) == saved


def test_load_without_a_save(tb, lib, saves):
    """No QUICK.DAT: error "no saved game (use save first)", nothing changes."""
    path, *_ = tb
    before = request({"cmd": "get_state"}, path)["state"]
    assert request({"cmd": "load"}, path) == {"ok": False, "error": "no saved game (use save first)"}
    assert place({"state": before}) == place(request({"cmd": "get_state"}, path))
    assert list(saves.iterdir()) == []


def test_load_of_a_damaged_save_keeps_the_game(tb, lib, saves):
    """A damaged QUICK.DAT: error "the saved game is damaged", the square stays and the game time too (but for the
    game step of the start of the restored game)."""
    path, *_ = tb
    face(path, open_floor)
    before = place(act(path, "move_forward"))
    (saves / "QUICK.DAT").write_bytes(DAMAGED_SAVE)
    assert request({"cmd": "load"}, path, timeout=30) == {"ok": False, "error": "the saved game is damaged"}
    assert place(request({"cmd": "get_state"}, path), SESSION_START_STEPS) == before
    assert act(path, "turn_left")["ok"]


def test_lost_game_allows_only_load_and_new_game(tb, lib, saves):
    """A damaged QUICK.DAT and a failed restore: get_state, act, save reply "no game is running: use load or
    new_game"; new_game starts a game and get_state works again; the program does not quit."""
    path, _, loop = tb
    lose_the_game(path, saves)
    for cmd in ({"cmd": "get_state"}, {"cmd": "get_view"}, {"cmd": "state"}, {"cmd": "act", "action": "turn_left"},
                {"cmd": "act_many", "actions": ["turn_left"]}, {"cmd": "wait", "ticks": 5}, {"cmd": "save"},
                {"cmd": "key", "key": "up"}):
        assert request(cmd, path) == NO_GAME, cmd
    assert loop.quit == 0
    r = request({"cmd": "new_game"}, path, timeout=30)
    assert r["ok"] and len(r["state"]["champions"]) == 1
    assert request({"cmd": "get_state"}, path)["ok"] and act(path, "turn_left")["ok"]
    assert loop.quit == 0


def test_load_after_a_lost_game_plays_again(tb, lib, saves):
    """A lost game, then a valid QUICK.DAT: load replies ok and get_state works again."""
    path, *_ = tb
    saved = place(request({"cmd": "get_state"}, path))
    assert request({"cmd": "save"}, path, timeout=30) == {"ok": True}
    good = (saves / "QUICK.DAT").read_bytes()
    lose_the_game(path, saves)
    assert request({"cmd": "get_state"}, path) == NO_GAME
    (saves / "QUICK.DAT").write_bytes(good)
    r = request({"cmd": "load"}, path, timeout=30)
    assert r["ok"] and place(r, SESSION_START_STEPS) == saved
    assert request({"cmd": "get_state"}, path)["ok"] and act(path, "turn_left")["ok"]


def test_load_after_game_over_plays_again(tb, lib, saves):
    """save; set main_loop.end = 1; act -> "game over"; load -> ok, game_over false, the start of the loaded game
    runs its full game step; act turn_left works."""
    path, _, loop = tb
    saved = place(request({"cmd": "get_state"}, path))
    assert request({"cmd": "save"}, path, timeout=30) == {"ok": True}
    loop.end = 1
    assert act(path, "turn_left") == {"ok": False, "error": "game over"}
    r = request({"cmd": "load"}, path, timeout=30)
    assert r["ok"] and r["state"]["party"]["dead"] is False and loop.end == 0
    assert place(r, SESSION_START_STEPS) == saved
    r = act(path, "turn_left")
    assert r["ok"] and r["game_over"] is False


def test_new_game_after_game_over_plays_again(tb, lib, saves):
    """main_loop.end = 1, then new_game: a new game with one champion; act turn_left works."""
    path, _, loop = tb
    loop.end = 1
    r = request({"cmd": "new_game"}, path, timeout=30)
    assert r["ok"] and r["state"]["party"]["dead"] is False and len(r["state"]["champions"]) == 1
    r = act(path, "turn_left")
    assert r["ok"] and r["game_over"] is False


def test_turn_in_a_dialog_ends_and_the_dialog_stays(tb, lib, saves):
    """A turn that opens a dialog (act click on the volume button) ends; state.mode is dialog; save, load, and
    new_game reply "close the dialog first (act click or act key)"; act key enter closes it."""
    path, *_ = tb  # the tb fixture sets play_frame_hook to transport_poll: the dialog serves the requests
    assert act(path, "open_inventory", champion=0)["state"]["mode"] == "inventory"
    x, y = center(command_rect(lib, CMD_VOLUME))
    r = act(path, "click", x=x, y=y)
    assert r["ok"] and r["state"]["mode"] == "dialog"
    try:
        for cmd in ("save", "load", "new_game"):
            assert request({"cmd": cmd}, path) == CLOSE_DIALOG, cmd
        assert list(saves.iterdir()) == []
        r = act(path, "key", key="enter")
        assert r["ok"] and r["state"]["mode"] == "inventory"
    finally:  # the pump thread runs the dialog loop until the dialog closes
        if request({"cmd": "get_state"}, path)["state"]["mode"] == "dialog":
            x, y = center(command_rect(lib, CMD_DIALOG_BUTTON_FIRST))
            act(path, "click", x=x, y=y)


def test_completed_game_is_game_over_and_the_program_stays(tb, lib, saves):
    """session_complete during a wait in the turn-based mode: the turn ends with game_over true, state.party.dead
    false, main_loop.quit 0; act replies "game over"."""
    path, _, loop = tb
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect(path)
        s.sendall(b'{"cmd": "wait", "ticks": %d}\n' % TURN_WAIT_MAX_TICKS)
        deadline = time.monotonic() + 10
        while not lib.control_is_turn_pending() and time.monotonic() < deadline:
            time.sleep(0.001)
        lib.session_complete()
        reply = s.makefile().readline()
    r = __import__("json").loads(reply)
    assert r["ok"] and r["game_over"] is True and r["ticks"] < TURN_WAIT_MAX_TICKS
    assert r["state"]["party"]["dead"] is False and loop.quit == 0
    assert act(path, "turn_left") == {"ok": False, "error": "game over"}
