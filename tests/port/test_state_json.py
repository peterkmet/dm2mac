"""The game state as JSON (src/control/state_json.c): what the turn-based mode tells the player."""

import ctypes
import json
import struct

import pytest

from dm2test import constants as C
from dm2test.markers import ROOT
from dm2test.structs import ActionArea, CurrentMap, Game, MainLoop, Party, ViewSquare
from dm2test.scenario import Position
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.seen_visible.argtypes = [Position, ctypes.c_int16, ctypes.POINTER(SeenSquare)]
    lib.seen_visible.restype = ctypes.c_int
    lib.seen_mark.argtypes = [Position, ctypes.c_int16]
    lib.seen_is_seen.argtypes = [Position]
    lib.seen_is_seen.restype = ctypes.c_bool
    return lib

STATE, VIEW, MAP = C.STATE_JSON_STATE, C.STATE_JSON_VIEW, C.STATE_JSON_MAP
VANILLA, TURNBASED = C.GAME_MODE_VANILLA, C.GAME_MODE_TURNBASED
OUTSIDE = ROOT / "tests" / "saves" / "outside.DAT"
DIRECTIONS = ("north", "east", "south", "west")
CH_NAME, CH_HEALTH, CH_STAMINA, CH_MANA = 0x00, 0x36, 0x3A, 0x3E
STAMINA_DISPLAY_UNIT = C.STAMINA_DISPLAY_UNIT
ACTION_MODE_MENU, SLOT_READY_HAND = C.ACTION_MODE_MENU, C.SLOT_READY_HAND
CHAMPION_ACTION_MAX, CHAMPION_ACTION_SIZE = C.CHAMPION_ACTION_MAX, 3
CH_AURA, AURA_STRENGTH, AURA_VALUE = 0x102, 3, 50  # an aura that raises the strength


def part(lib, which):
    lib.state_json_string.restype = ctypes.c_void_p
    p = lib.state_json_string(which)
    text = ctypes.string_at(p).decode()
    ctypes.CDLL(None).free(ctypes.c_void_p(p))
    return json.loads(text)


@pytest.fixture(scope="module", autouse=True)
def turnbased(lib):
    """The turn-based game mode for this module; vanilla again at the end."""
    mode = ctypes.c_int.in_dll(lib, "g_game_mode")
    mode.value = TURNBASED
    yield
    mode.value = VANILLA


def load(lib, gdat, data, tmp_path):
    """A save ready to play, as build/dm2 --save starts it (dm2test.input.session); returns the game record."""
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(data)
    assert lib.save_load(str(path).encode(), gdat) == 0
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    game = Game.in_dll(lib, "g_game")
    party = Party.in_dll(lib, "g_party")
    CurrentMap.in_dll(lib, "g_current_map").map = -1
    party.new_map, party.move_steps, party.push_pending_group = -1, 0, 0xFFFF
    lib.view_enter_map(game.party_map)
    lib.ui_reset()
    lib.money_init_coin_table()
    lib.ui_draw_game_screen()
    lib.input_init()
    lib.play_start_session()
    MainLoop.in_dll(lib, "g_main_loop").end = 0
    return game


@pytest.fixture
def game(lib, gdat, start, tmp_path):
    return load(lib, gdat, start, tmp_path)


def test_state_has_the_fields_of_the_playbook(lib, game):
    state = part(lib, STATE)
    assert set(state) >= {"time", "game_mode", "mode", "dialog", "party", "leader", "leader_hand", "candidate",
                          "champions", "action_menu", "spell", "inventory", "money_box", "minion_map", "messages",
                          "outdoor"}
    assert state["game_mode"] == "turnbased" and state["mode"] == "move"
    assert set(state["party"]) == {"map", "level", "x", "y", "facing", "darkness", "can_move", "sleeping", "dead"}


def test_state_matches_the_game_record(lib, game):
    """time, party square, and each champion's bars come from the game record of start.DAT."""
    state = part(lib, STATE)
    assert state["time"] == game.game_time
    party = state["party"]
    assert (party["map"], party["x"], party["y"]) == (game.party_map, game.party_x, game.party_y)
    assert party["facing"] == DIRECTIONS[game.party_dir] and not party["dead"]
    assert len(state["champions"]) == game.champion_count and state["candidate"] is None
    assert state["leader"] == ctypes.c_int16(game.leader).value
    for i, champion in enumerate(state["champions"]):
        record = bytes(game.champions[i])
        health, max_health = struct.unpack_from("<hh", record, CH_HEALTH)
        stamina, max_stamina = struct.unpack_from("<hh", record, CH_STAMINA)
        mana, max_mana = struct.unpack_from("<hh", record, CH_MANA)
        assert champion["index"] == i and champion["name"] == record[CH_NAME:CH_NAME + 8].split(b"\0")[0].decode()
        assert champion["health"] == [health, max_health] and champion["mana"] == [mana, max_mana]
        assert champion["stamina"] == [stamina // STAMINA_DISPLAY_UNIT, max_stamina // STAMINA_DISPLAY_UNIT]
    names = [slot["object"]["name"] for slot in state["champions"][0]["slots"]]
    assert names and all(name == name.rstrip() for name in names)


def test_reading_the_state_keeps_the_random_seed(lib, game):
    """A stat aura makes champ_get_stat draw a random number; reading the state must not change the later game."""
    struct.pack_into("<Bh", game.champions[0], CH_AURA, AURA_STRENGTH, AURA_VALUE)
    seed = ctypes.c_uint32.in_dll(lib, "g_random_seed")
    before = seed.value
    part(lib, STATE)
    part(lib, STATE)
    assert seed.value == before


def test_hand_menus_do_not_change_the_open_menu(lib, game):
    """Listing the action menus of both hands for the state keeps the open menu of the action area."""
    lib.action_area_open_hand.argtypes = [ctypes.c_int16, ctypes.c_int16]
    lib.action_area_open_hand(0, SLOT_READY_HAND)
    lib.area_update(0)
    action = ActionArea.in_dll(lib, "g_action")
    assert action.mode == ACTION_MODE_MENU
    menu = (ctypes.c_uint8 * (CHAMPION_ACTION_MAX * CHAMPION_ACTION_SIZE)).in_dll(lib, "g_champ_actions")
    count = ctypes.c_int.in_dll(lib, "g_champ_action_count")
    before = (bytes(menu), count.value)
    state = part(lib, STATE)
    assert (bytes(menu), count.value) == before
    assert state["action_menu"]["champion"] == 0 and state["action_menu"]["hand"] == "ready"
    assert state["action_menu"]["actions"] == state["champions"][0]["hands"]["ready"]
    assert len(state["action_menu"]["actions"]) == count.value > 0


def test_outdoor_fields_only_outside(lib, gdat, game, tmp_path):
    """weather and day_time are present only while outdoor is true (outside.DAT)."""
    state = part(lib, STATE)
    assert state["outdoor"] is False and "weather" not in state and "day_time" not in state
    if not OUTSIDE.exists():
        pytest.skip("tests/saves/outside.DAT missing")
    load(lib, gdat, OUTSIDE.read_bytes(), tmp_path)
    state = part(lib, STATE)
    assert state["outdoor"] is True
    assert {"clouds", "rain"} <= set(state["weather"]) <= {"clouds", "rain", "wind"}
    assert ("wind" in state["weather"]) == lib.view_shows_rain()
    assert state["day_time"] in ("day", "dawn_dusk", "night")


class SeenSquare(ctypes.Structure):
    _fields_ = [(name, ctypes.c_int16) for name in ("depth", "side", "x", "y")]


SEEN_VISIBLE_MAX = C.SEEN_VISIBLE_MAX
SQUARE_TYPE_SHIFT = C.SQUARE_TYPE_SHIFT
SQUARE_WALL, SQUARE_FLOOR, SQUARE_PIT, SQUARE_STAIRS, SQUARE_DOOR, SQUARE_TELEPORTER, SQUARE_FAKE_WALL = (
    C.SQUARE_WALL, C.SQUARE_FLOOR, C.SQUARE_PIT, C.SQUARE_STAIRS, C.SQUARE_DOOR, C.SQUARE_TELEPORTER, C.SQUARE_FAKE_WALL)
PIT_OPEN, STAIRS_UP, DOOR_STATE = C.SQUARE_PIT_OPEN, C.SQUARE_STAIRS_UP, C.SQUARE_DOOR_STATE
TELEPORTER_ON, TELEPORTER_VISIBLE, FAKE_WALL_OPEN = C.SQUARE_TELEPORTER_ON, C.SQUARE_TELEPORTER_VISIBLE, C.SQUARE_FAKE_WALL_OPEN
DOOR_STATE_OPEN, DOOR_STATE_BROKEN = C.DOOR_STATE_OPEN, C.DOOR_STATE_BROKEN
SIDE_FLOOR_ORNAMENT, ORNAMENT_MASK = C.SIDE_FLOOR_ORNAMENT, C.ORNAMENT_INDEX_MASK


def visible(lib, game):
    """The squares of the seen rule for the party (seen_visible) as {(depth, side, x, y)}."""
    out = (SeenSquare * SEEN_VISIBLE_MAX)()
    count = lib.seen_visible(Position(game.party_map, game.party_x, game.party_y), game.party_dir, out)
    return {(s.depth, s.side, s.x, s.y) for s in out[:count]}


def test_view_shows_only_the_seen_squares(lib, game):
    """The view squares are exactly the squares of the seen rule from the party square (seen_visible)."""
    view = part(lib, VIEW)
    squares = {(s["depth"], s["side"], s["x"], s["y"]) for s in view["squares"]}
    assert squares == visible(lib, game) and len(view["squares"]) == len(squares)
    assert view["facing"] == DIRECTIONS[game.party_dir]


def test_view_square_fields(lib, game):
    view = part(lib, VIEW)
    assert set(view) >= {"facing", "darkness", "squares", "piles", "wall_ahead_clickable"}
    for s in view["squares"]:
        assert 0 <= s["depth"] <= 4 and -3 <= s["side"] <= 3
        assert s["element"] in {"wall", "floor", "pit", "stairs_up", "stairs_down", "door", "teleporter"}
        if s["element"] == "door":
            assert s["door"] in {"open", "partly_open", "closed", "broken"}
        if s["element"] == "wall":
            assert not {"creatures", "objects", "flying", "floor_ornament"} & set(s)
        else:
            assert isinstance(s["creatures"], list) and isinstance(s["objects"], list)
            assert isinstance(s["flying"], list)
            assert all(o["cell"] in {"front_left", "front_right", "back_right", "back_left"} for o in s["objects"])
            assert all(o["name"] == o["name"].rstrip() for o in s["objects"])
    for pile in view["piles"]:
        assert 0 <= pile["cell"] <= 3 and pile["objects"][0] == pile["top"]


def mark_whole_map(lib, game):
    """Marks as seen every square that the view shows from any open square of the party map, in every direction."""
    lib.dungeon_square.restype = ctypes.c_uint8
    m = game.party_map
    for x in range(lib.dungeon_map_width(m)):
        for y in range(lib.dungeon_map_height(m)):
            if lib.dungeon_square(m, x, y) >> SQUARE_TYPE_SHIFT != SQUARE_WALL:
                for d in range(4):
                    lib.seen_mark(Position(m, x, y), d)


def expected_glyph(lib, m, x, y):
    """The glyph of a square by the get_map legend."""
    square = lib.dungeon_square(m, x, y)
    kind = square >> SQUARE_TYPE_SHIFT
    if kind == SQUARE_WALL or (kind == SQUARE_FAKE_WALL and not square & FAKE_WALL_OPEN):
        return "#"
    if kind == SQUARE_PIT and square & PIT_OPEN:
        return "O"
    if kind == SQUARE_STAIRS:
        return "<" if square & STAIRS_UP else ">"
    if kind == SQUARE_DOOR:
        return "d" if square & DOOR_STATE in (DOOR_STATE_OPEN, DOOR_STATE_BROKEN) else "D"
    if kind == SQUARE_TELEPORTER and square & TELEPORTER_ON and square & TELEPORTER_VISIBLE:
        return "T"
    s = ViewSquare()
    lib.mmap_shows_floor_ornament.restype = ctypes.c_bool
    lib.aspect_get(ctypes.byref(s), m, 0, x, y)
    return "_" if lib.mmap_shows_floor_ornament(s.side[SIDE_FLOOR_ORNAMENT] & ORNAMENT_MASK) else "."


def test_map_rows_show_only_seen_squares(lib, game):
    """get_map: a square that seen_is_seen does not know and that is not in view is a space; @ is the party."""
    m = part(lib, MAP)
    assert (m["map"], m["width"], m["height"]) == (game.party_map, lib.dungeon_map_width(game.party_map),
                                                   lib.dungeon_map_height(game.party_map))
    assert m["party"] == [game.party_x, game.party_y] and m["facing"] == DIRECTIONS[game.party_dir]
    in_view = {(x, y) for _, _, x, y in visible(lib, game)}
    assert len(m["rows"]) == m["height"] and len(m["grid"]) == m["height"] + 1
    for y, row in enumerate(m["rows"]):
        assert len(row) == m["width"] and m["grid"][y + 1].endswith(row)
        for x, char in enumerate(row):
            if (x, y) == (game.party_x, game.party_y):
                assert char == "@"
            else:
                assert (char != " ") == (bool(lib.seen_is_seen(Position(game.party_map, x, y))) or (x, y) in in_view)


def test_map_glyphs(lib, game):
    """Each glyph follows the square type: # wall, . floor, D closed door, d open door, < > stairs, O open pit,
    T visible teleporter, _ a floor with a floor ornament that the automap draws; a closed fake wall is #, an open one
    is ."""
    mark_whole_map(lib, game)
    m = part(lib, MAP)
    shown = set()
    for y, row in enumerate(m["rows"]):
        for x, char in enumerate(row):
            if char not in " @":
                assert char == expected_glyph(lib, game.party_map, x, y), (x, y)
                shown.add(char)
    assert {"#", "."} <= shown


THING_GROUP, THING_MISC, THING_NONE = C.THING_GROUP, C.THING_MISC, C.THING_NONE
STAIRS_DOWN_SQUARE = SQUARE_STAIRS << SQUARE_TYPE_SHIFT
DOOR_ALONG_X, DOOR_STATE_CLOSED, SQUARE_HAS_THINGS = C.SQUARE_DOOR_ALONG_X, C.DOOR_STATE_CLOSED, C.SQUARE_HAS_THINGS
DOOR_FRONT_SQUARE = SQUARE_DOOR << SQUARE_TYPE_SHIFT | DOOR_ALONG_X | DOOR_STATE_CLOSED  # seen from the front facing north


def view_square(lib, depth, side):
    return next(s for s in part(lib, VIEW)["squares"] if (s["depth"], s["side"]) == (depth, side))


def test_view_lists_only_the_things_that_the_view_draws(lib, game):
    """The view draws no creature on stairs in the far row and nothing on a door seen from the front beside the
    party (door_front_shown); the same things on a floor are in the view."""
    assert game.party_dir == 0
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.dungeon_square_byte.restype = ctypes.POINTER(ctypes.c_uint8)
    far, near = view_square(lib, 4, 0), view_square(lib, 0, 1)
    assert far["element"] == "floor" and near["element"] == "floor"
    group, item = lib.dungeon_unused_thing(THING_GROUP), lib.dungeon_unused_thing(THING_MISC)
    assert THING_NONE not in (group, item)
    assert lib.dungeon_link_thing_to_square(group, game.party_map, far["x"], far["y"])
    assert lib.dungeon_link_thing_to_square(item, game.party_map, near["x"], near["y"])
    assert len(view_square(lib, 4, 0)["creatures"]) == 1 and len(view_square(lib, 0, 1)["objects"]) == 1
    for square, kind in ((far, STAIRS_DOWN_SQUARE), (near, DOOR_FRONT_SQUARE)):
        byte = lib.dungeon_square_byte(game.party_map, square["x"], square["y"])
        byte[0] = byte[0] & SQUARE_HAS_THINGS | kind
    far, near = view_square(lib, 4, 0), view_square(lib, 0, 1)
    assert far["element"] == "stairs_down" and far["creatures"] == []
    assert near["element"] == "door" and near["objects"] == []
