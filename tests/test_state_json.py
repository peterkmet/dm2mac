"""The game state as JSON (port/src/platform/state_json.c): what the turn-based mode tells the player."""

import ctypes
import json
import struct

import pytest

from conftest import ROOT
from test_control import GameWithChampions
from test_input import MainLoop
from test_interface import ActionArea
from test_move import Party, gdat, start  # noqa: F401 (gdat and start are fixtures)

STATE, VIEW, MAP = 0, 1, 2
VANILLA, TURNBASED = 0, 2
OUTSIDE = ROOT / "tests" / "saves" / "outside.DAT"
DIRECTIONS = ("north", "east", "south", "west")
CH_NAME, CH_HEALTH, CH_STAMINA, CH_MANA = 0x00, 0x36, 0x3A, 0x3E
STAMINA_DISPLAY_UNIT = 10
ACTION_MODE_MENU, SLOT_READY_HAND = 1, 0
CHAMPION_ACTION_MAX, CHAMPION_ACTION_SIZE = 3, 3


def part(lib, which):
    lib.state_json_string.restype = ctypes.c_void_p
    p = lib.state_json_string(which)
    text = ctypes.string_at(p).decode()
    ctypes.CDLL(None).free(ctypes.c_void_p(p))
    return json.loads(text)


@pytest.fixture(scope="module", autouse=True)
def turnbased(lib):
    """The turn-based game mode for this module; vanilla again at the end."""
    mode = ctypes.c_int.in_dll(lib, "game_mode")
    mode.value = TURNBASED
    yield
    mode.value = VANILLA


def load(lib, gdat, data, tmp_path):  # noqa: F811
    """A save ready to play, as build/dm2 --save starts it (test_input.session); returns the game record."""
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(data)
    assert lib.save_load(str(path).encode(), gdat) == 0
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    game = GameWithChampions.in_dll(lib, "game")
    party = Party.in_dll(lib, "party")
    party.cur_map, party.new_map, party.move_steps, party.push_pending_group = -1, -1, 0, 0xFFFF
    lib.view_enter_map(game.party_map)
    lib.ui_reset()
    lib.money_init_coin_table()
    lib.ui_draw_game_screen()
    lib.input_init()
    lib.play_start_session()
    MainLoop.in_dll(lib, "main_loop").end = 0
    return game


@pytest.fixture
def game(lib, gdat, start, tmp_path):  # noqa: F811
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


def test_hand_menus_do_not_change_the_open_menu(lib, game):
    """Listing the action menus of both hands for the state keeps the open menu of the action area."""
    lib.action_area_open_hand(0, SLOT_READY_HAND)
    lib.area_update(0)
    action = ActionArea.in_dll(lib, "action")
    assert action.mode == ACTION_MODE_MENU
    menu = (ctypes.c_uint8 * (CHAMPION_ACTION_MAX * CHAMPION_ACTION_SIZE)).in_dll(lib, "champ_actions")
    count = ctypes.c_int.in_dll(lib, "champ_action_count")
    before = (bytes(menu), count.value)
    state = part(lib, STATE)
    assert (bytes(menu), count.value) == before
    assert state["action_menu"]["champion"] == 0 and state["action_menu"]["hand"] == "ready"
    assert state["action_menu"]["actions"] == state["champions"][0]["hands"]["ready"]
    assert len(state["action_menu"]["actions"]) == count.value > 0


def test_outdoor_fields_only_outside(lib, gdat, game, tmp_path):  # noqa: F811
    """weather and day_time are present only while outdoor is true (outside.DAT)."""
    state = part(lib, STATE)
    assert state["outdoor"] is False and "weather" not in state and "day_time" not in state
    if not OUTSIDE.exists():
        pytest.skip("tests/saves/outside.DAT missing")
    load(lib, gdat, OUTSIDE.read_bytes(), tmp_path)
    state = part(lib, STATE)
    assert state["outdoor"] is True
    assert set(state["weather"]) == {"clouds", "rain", "wind"}
    assert state["day_time"] in ("day", "dawn_dusk", "night")
