"""The text summary and the call log of dm2-play (tools/dm2tools/control_client.py)."""

from dm2tools import control_client as cc

CHAMPION = {"index": 0, "name": "TIGGY", "title": "THE FIGHTER", "position": "front_left", "alive": True,
            "health": [80, 100], "stamina": [50, 60], "mana": [10, 20], "food": 900, "water": 800,
            "poisoned": True}
STATE = {
    "time": 120, "game_mode": "turnbased", "mode": "move", "dialog": {"id": 3, "buttons": ["YES", "NO"]},
    "party": {"map": 0, "level": 2, "x": 5, "y": 7, "facing": "north", "darkness": 4, "can_move": True,
              "sleeping": False, "dead": False},
    "leader": 0, "leader_hand": {"name": "TORCH", "kind": "weapon"}, "candidate": None,
    "champions": [CHAMPION, {**CHAMPION, "index": 1, "name": "ZED", "alive": False, "poisoned": False}],
    "action_menu": {"champion": 0, "hand": "action_hand", "actions": ["SWING", "THRUST"]},
    "spell": None, "money_box": None, "minion_map": None, "messages": ["IT IS DARK"],
    "outdoor": True, "weather": {"clouds": "light", "rain": "none"}, "day_time": "day",
}
VIEW = {
    "facing": "north", "darkness": 4,
    "squares": [
        {"depth": 0, "side": 0, "x": 5, "y": 7, "element": "floor"},
        {"depth": 1, "side": 0, "x": 5, "y": 6, "element": "door", "door": "closed", "button": True},
        {"depth": 1, "side": -1, "x": 4, "y": 6, "element": "wall",
         "side_face": {"facing": "east", "mirror": True}},
        {"depth": 2, "side": 0, "x": 5, "y": 5, "element": "floor",
         "creatures": [{"type": 4, "count": 1}], "objects": [{"cell": "front_left", "name": "TORCH"}]},
        {"depth": 2, "side": -2, "x": 3, "y": 5, "element": "floor"},
    ],
    "piles": [{"cell": 2, "top": "Torch", "objects": ["Torch"]}], "alcove": None, "wall_ahead_clickable": False,
}


def test_brief_of_a_turn():
    text = cc.brief({"ok": True, "ticks": 3, "game_over": False, "bumped": True, "state": STATE, "view": VIEW})
    assert text.splitlines()[0] == "ticks 3 | game_over False | BUMPED"
    assert "ahead: door closed with button" in text and "piles: cell 2: Torch (top first)" in text
    assert "ahead 2: floor, CREATURES 1 4, objects TORCH (front_left)" in text
    assert "ahead left: wall, side face east: mirror" in text
    assert "ahead 2 far left: floor" in text
    assert "dialog 3: YES, NO" in text and "messages: IT IS DARK" in text
    assert "action menu of 0: SWING, THRUST" in text


def test_brief_of_the_state():
    lines = cc.brief({"ok": True, "state": STATE}).splitlines()
    assert lines[0] == "time 120 | level 2 x 5 y 7 facing north | darkness 4 | outdoor clouds light rain none day"
    assert lines[1] == "leader 0 | hand TORCH"
    assert lines[2].startswith("0 TIGGY (front_left): HP 80/100 ST 50/60 MA 10/20 food 900 water 800 poisoned")
    assert lines[3].endswith("DEAD")


def test_brief_of_the_stops_and_the_look():
    seen = {"explosions": [{"name": "fireball", "x": 1, "y": 1, "depth": 2, "side": "left"}],
            "creature_changes": [{"type": 4, "x": 1, "y": 1, "depth": 1, "side": "center", "before": 3, "after": 0}]}
    text = cc.brief({"ok": True, "ran": 2, "stopped": "damage", "error": "x", "look": {"name": "TORCH", "lines": ["A", "B"]},
                     "seen": seen})
    assert text.splitlines()[0] == "ran 2 | stopped damage | error x"
    assert "look: TORCH | A | B" in text
    assert "seen: fireball at ahead 2 left; a group of 3 4 at ahead was killed" in text


def test_side_words_tell_each_distance_apart():
    assert [cc.side_name(s) for s in (-3, -2, -1, 1, 2, 3)] == [
        "outer left", "far left", "left", "right", "far right", "outer right"]
    assert [cc.place_text({"depth": 1, "side": w}) for w in ("left_3", "left_2", "left", "right_3")] == [
        "ahead outer left", "ahead far left", "ahead left", "ahead outer right"]


def test_brief_of_an_error():
    assert cc.brief({"ok": False, "error": "game over"}) == "error: game over"


def test_brief_of_a_map_prints_the_grid():
    assert cc.brief({"ok": True, "map": {"grid": ["   0", " 0 @"]}}) == "   0\n 0 @"


def test_call_log_line(tmp_path):
    log = tmp_path / "calls.log"
    cc.log_call(str(log), {"cmd": "act", "action": "move_forward"}, {"ok": True, "state": STATE})
    cc.log_call(str(log), {"cmd": "get_map"}, {"ok": False, "error": "x"})
    first, second = [line.split("\t") for line in log.read_text().splitlines()]
    assert first[1:] == ["move_forward", "ok", "0", "5", "7", "120"]
    assert second[1:] == ["get_map", "fail", "", "", "", ""]


def test_act_forms_build_the_request(tmp_path, monkeypatch):
    sent = []
    monkeypatch.setattr(cc, "LOG_DIR", "none")
    monkeypatch.setattr(cc, "request", lambda payload, path=None: sent.append(payload) or {"ok": True})
    assert cc.main(["act", "put_in_slot", '{"slot": 5}']) == 0
    assert cc.main(["act_many", '[{"action": "turn_left"}]']) == 0
    assert sent == [{"cmd": "act", "action": "put_in_slot", "args": {"slot": 5}},
                    {"cmd": "act_many", "actions": [{"action": "turn_left"}]}]


def test_unreachable_game_is_logged_and_exits_1(tmp_path, monkeypatch):
    monkeypatch.setenv("DM2_CONTROL", str(tmp_path / "none.sock"))
    monkeypatch.setattr(cc, "LOG_DIR", str(tmp_path))
    assert cc.main(["act", "move_forward"]) == 1
    assert (tmp_path / "calls.log").read_text().count("\n") == 1
