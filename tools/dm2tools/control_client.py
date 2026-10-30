"""dm2-play: sends one request to the control socket of a running build/dm2 --control and prints the reply.

    uv run dm2-play <cmd> [json-fields]      for example: uv run dm2-play key '{"key": "up"}'
    uv run dm2-play act <action> [json-args]       turn-based mode: one action, then the turn runs
    uv run dm2-play act_many '<json-list>'         turn-based mode: a list of actions in one request
    uv run dm2-play --brief <cmd> ...              a short text summary instead of the JSON

The reply is printed as compact JSON on one line. The exit code is 1 when the reply has "ok": false. The socket path
comes from --socket, else from DM2_CONTROL, else ~/.dm2/control.sock (docs/llm/control-socket.md).
DM2_PLAY_LOG_DIR (default playthroughs/current/coordinator; "none" turns it off) names the folder of calls.log: one
tab-separated line per call, written when the folder exists.
"""

import argparse
import json
import os
import socket
import sys
import time

DEFAULT_SOCKET = "~/.dm2/control.sock"
TIMEOUT = 150.0  # a wait of 6000 ticks takes 100 seconds
LOG_DIR = os.environ.get("DM2_PLAY_LOG_DIR", "playthroughs/current/coordinator")
LANE = ["here", "ahead", "ahead 2", "ahead 3", "ahead 4"]
FAR_SIDE = 2  # the squares at this distance from the lane and beyond show only when they are not a wall
SIDE_WORDS = {1: "", 2: "far ", 3: "outer "}  # the word of each distance from the lane
WALL_FEATURES = ("mirror", "switch", "lock", "alcove", "fountain", "shop", "ornament")


def socket_path(path: str | None = None) -> str:
    return os.path.expanduser(path or os.environ.get("DM2_CONTROL", DEFAULT_SOCKET))


def request(payload: dict, path: str | None = None, timeout: float = TIMEOUT) -> dict:
    """Sends one request line to the game and returns the reply object."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(timeout)
        s.connect(socket_path(path))
        s.sendall(json.dumps(payload).encode() + b"\n")
        data = b""
        while not data.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    return json.loads(data)


def face_text(name: str, face: dict) -> str:
    """One wall face in a few words: its facing and what it holds."""
    features = []
    for key in WALL_FEATURES:
        value = face.get(key)
        if value:
            features.append(key if value is True else f"{key} {value}")
    if face.get("text"):
        features.append(f'text "{face["text"]}"')
    return f"{name.replace('_', ' ')} {face['facing']}: " + " ".join(features)


def square_text(square: dict) -> str:
    """One square of the view in a few words: element, door, floor ornament, wall faces, creatures, objects, flying."""
    words = [square["element"]] if "door" not in square else []
    if square.get("floor_ornament") is not None:
        words.append(f"floor ornament {square['floor_ornament']}")
    if "door" in square:
        words.append(f"door {square['door']}" + (" with button" if square.get("button") else ""))
    for name in ("front_face", "side_face"):
        if square.get(name):
            words.append(face_text(name, square[name]))
    for group in square.get("creatures", []):
        words.append(f"CREATURES {group['count']} {group['type']}")
    if square.get("objects"):
        words.append("objects " + ", ".join(f"{o['name']} ({o['cell']})" for o in square["objects"]))
    if square.get("flying"):
        words.append("flying " + ", ".join(str(f.get("name", f["kind"])) for f in square["flying"]))
    return ", ".join(words)


def side_name(side: int) -> str:
    return SIDE_WORDS[abs(side)] + ("left" if side < 0 else "right")


def view_lines(view: dict) -> list[str]:
    """The lane ahead, then the side squares that hold something, then the piles."""
    squares = {(s["depth"], s["side"]): s for s in view["squares"]}
    lines = []
    for depth, name in enumerate(LANE):
        if (depth, 0) in squares:
            lines.append(f"{name}: {square_text(squares[depth, 0])}")
        for side in sorted(side for d, side in squares if d == depth and side):
            square = squares[depth, side]
            interesting = any(key in square for key in ("door", "side_face", "front_face", "floor_ornament", "creatures",
                                                        "objects", "flying"))
            opening = depth > 0 and square["element"] != "wall"  # a side passage the player sees
            if (opening or interesting or square["element"] == "pit") and (abs(side) < FAR_SIDE or square["element"] != "wall"):
                lines.append(f"{name} {side_name(side)}: {square_text(square)}")
    if view.get("piles"):
        lines.append("piles: " + "; ".join(f"cell {p['cell']}: {', '.join(p['objects'])}" for p in view["piles"]) + " (top first)")
    if view.get("alcove"):
        lines.append("alcove: " + ", ".join(view["alcove"]["objects"]) + " (top first)")
    if view.get("wall_ahead_clickable"):
        lines.append("wall ahead: clickable")
    return lines


def place_text(at: dict) -> str:
    """A seen place: the lane name and the side word of the reply (left_2 is far left, left_3 outer left)."""
    if at["side"] == "center":
        return LANE[at["depth"]]
    word, _, distance = at["side"].partition("_")
    side = int(distance or 1)
    return f"{LANE[at['depth']]} {side_name(-side if word == 'left' else side)}"


def seen_text(seen: dict) -> str:
    """What the party saw during the turn, also when it was gone at the end."""
    parts = [f"{e['name']} at {place_text(e)}" for e in seen.get("explosions", [])]
    for c in seen.get("creature_changes", []):
        lost = "was killed" if c["after"] == 0 else f"lost {c['before'] - c['after']}"
        parts.append(f"a group of {c['before']} {c['type']} at {place_text(c)} {lost}")
    return "seen: " + "; ".join(parts)


def champion_text(c: dict) -> str:
    bars = " ".join(f"{label} {c[key][0]}/{c[key][1]}" for label, key in (("HP", "health"), ("ST", "stamina"), ("MA", "mana")))
    return f"{bars} food {c['food']} water {c['water']}"


def sky_text(state: dict) -> str:
    weather = state["weather"]
    wind = f" wind {weather['wind']}" if "wind" in weather else ""
    return f"outdoor clouds {weather['clouds']} rain {weather['rain']}{wind} {state['day_time']}"


def panel_lines(state: dict) -> list[str]:
    """The open action menu, spell panel, money box, minion map, and dialog."""
    lines = []
    if state.get("action_menu"):
        menu = state["action_menu"]
        lines.append(f"action menu of {menu['champion']}: " + ", ".join(menu["actions"]))
    if spell := state.get("spell"):
        runes = ", ".join(f"{r['rune']} {r['name']} ({r['mana']} mana)" for r in spell["available"])
        lines.append(f"spell of {spell['caster']}: chosen {' '.join(spell['chosen']) or '-'} | available {runes}")
    if state.get("money_box"):
        lines.append("money box: " + ", ".join(f"slot {c['slot']} {c['coin']} x{c['count']}" for c in state["money_box"]))
    if minion := state.get("minion_map"):
        options = ", ".join(f"{o['option']} {o['name']} {'on' if o['on'] else 'off'}" for o in minion["options"])
        lines.append(f"minion map: {'powered' if minion['powered'] else 'not powered'}" + (f" | {options}" if options else ""))
    if state.get("dialog"):
        lines.append(f"dialog {state['dialog']['id']}: " + ", ".join(state["dialog"]["buttons"]))
    return lines


def state_lines(state: dict) -> list[str]:
    party = state["party"]
    flags = [flag for flag in ("sleeping", "dead") if party[flag]] + ([] if party["can_move"] else ["cannot move yet"])
    sky = " | " + sky_text(state) if state.get("outdoor") else ""
    hand = state["leader_hand"]
    leader = "-" if state["leader"] is None else state["leader"]
    lines = [f"time {state['time']} | level {party['level']} x {party['x']} y {party['y']} facing {party['facing']} | "
             f"darkness {party['darkness']}{sky}" + "".join(f" | {flag}" for flag in flags),
             f"leader {leader} | hand {hand['name'] if hand else '-'}"]
    if candidate := state["candidate"]:
        lines.append(f"CANDIDATE (not recruited) {candidate['name']} {candidate['title']}: " + champion_text(candidate) + " | act recruit")
    for c in state["champions"]:
        notes = (["poisoned"] if c["poisoned"] else []) + ([] if c["alive"] else ["DEAD"])
        lines.append(f"{c['index']} {c['name']} ({c['position']}): {champion_text(c)}" + "".join(f" {n}" for n in notes))
    lines += panel_lines(state)
    if state["messages"]:
        lines.append("messages: " + " / ".join(state["messages"]))
    return lines


def brief(response: dict) -> str:
    """A few lines for the player: the turn, the party, the champions, what is ahead, the messages."""
    if not response.get("ok"):
        return f"error: {response.get('error')}"
    keys = ("ticks", "ran", "stopped", "error", "game_over", "action_damage", "health_lost", "path")
    head = " | ".join(f"{key} {response[key]}" for key in keys if key in response)
    head += " | BUMPED" if response.get("bumped") else ""
    lines = [head] if head else []
    if look := response.get("look"):
        lines.append(f"look: {look['name']}" + "".join(f" | {line}" for line in look["lines"]))
    if seen := response.get("seen"):
        lines.append(seen_text(seen))
    if "state" in response:
        lines += state_lines(response["state"])
    if "view" in response:
        lines += view_lines(response["view"])
    if "map" in response:
        lines += response["map"]["grid"]
    return "\n".join(lines) or "ok"


def log_call(path: str, payload: dict, response: dict) -> None:
    """Appends one line per call: time, action or command, result, map, position, game time."""
    state = response.get("state") or {}
    party = state.get("party") or {}
    line = [time.strftime("%Y-%m-%dT%H:%M:%S"), payload.get("action") or payload["cmd"], "ok" if response.get("ok") else "fail",
            party.get("map", ""), party.get("x", ""), party.get("y", ""), state.get("time", "")]
    with open(path, "a") as log:
        log.write("\t".join(str(v) for v in line) + "\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="dm2-play", description="Sends one request to the DM2 control socket.")
    parser.add_argument("--socket", help="socket path (default: $DM2_CONTROL or ~/.dm2/control.sock)")
    parser.add_argument("--brief", action="store_true", help="print a short summary instead of the JSON")
    parser.add_argument("cmd", help="state, key, click, move, command, wait, screenshot, quit; turn-based: get_state, "
                        "get_view, get_map, act, act_many, wait, save, load, new_game")
    parser.add_argument("rest", nargs="*", help="act: the action, then the args as JSON; act_many: the actions as a JSON "
                        "list; other commands: the fields as a JSON object")
    options = parser.parse_args(argv)
    payload = {"cmd": options.cmd}
    rest = options.rest
    if options.cmd == "act":
        if not rest:
            parser.error("act needs an action")
        payload["action"], rest = rest[0], rest[1:]
        payload["args"] = json.loads(rest[0]) if rest else {}
    elif options.cmd == "act_many":
        if len(rest) != 1:
            parser.error("act_many needs the actions as one JSON list")
        payload["actions"] = json.loads(rest[0])
    elif rest:
        payload.update(json.loads(rest[0]))
    if "path" in payload:
        payload["path"] = os.path.abspath(payload["path"])  # the game runs in another directory
    try:
        reply = request(payload, options.socket)
    except (OSError, ValueError) as error:
        reply = {"ok": False, "error": f"cannot reach the game at {socket_path(options.socket)}: {error}"}
    if LOG_DIR != "none" and os.path.isdir(LOG_DIR):
        log_call(os.path.join(LOG_DIR, "calls.log"), payload, reply)
    print(brief(reply) if options.brief else json.dumps(reply, separators=(",", ":")))
    return 0 if reply.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
