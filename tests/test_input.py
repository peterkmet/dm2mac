"""The input tree of SKULL.EXE: clicks and keys give the same commands in the C port and in the original."""

import ctypes
import random
import struct

import pytest

from test_move import Scenario, gdat, start  # noqa: F401 (gdat and start are fixtures)

INPUT_INIT_TABLES, INPUT_FIND_CLICK_COMMAND, INPUT_FIND_KEY_COMMAND = 0x20847, 0x203A3, 0x2048A
MOUSE_AREA_ALLOC_NODE = 0x14B1F
G_INPUT_MODE_ROOTS, G_MATCH_RECT, G_MATCH_X, G_MATCH_Y = 0x72ED1, 0x7F46C, 0x7F520, 0x7F51E
MODES = range(10)
G_ACTION_AREA_MODE = 0x757B8


class ActionArea(ctypes.Structure):
    _fields_ = [("champion", ctypes.c_int16), ("hand", ctypes.c_int16), ("mode", ctypes.c_int16)]


@pytest.fixture(scope="module")
def keys():
    """Every key code of the key lists of SKULL.EXE."""
    from conftest import ROOT
    from dm2tools.le import load_image
    image = load_image(ROOT / "game" / "SKULL.EXE")
    data = image.data[0x729B8 - image.base:0x72B9C - image.base]
    return sorted({struct.unpack_from("<H", data, i + 2)[0] for i in range(0, len(data), 4)})


@pytest.fixture
def tree(lib, gdat, start, tmp_path):
    scenario = Scenario(lib, gdat, start, tmp_path)
    scenario.emu.stub(MOUSE_AREA_ALLOC_NODE, lambda *_: 0)
    scenario.emu.call(INPUT_INIT_TABLES)
    lib.input_init()
    lib.input_find_command.restype = ctypes.c_uint16
    # the action area state: the original after the load; the C port starts it with the screen
    mode = struct.unpack("<h", scenario.emu.read(G_ACTION_AREA_MODE, 2))[0]
    action = ActionArea.in_dll(lib, "action")
    action.champion, action.mode = 0, mode
    return scenario


def original_command(emu, mode, x, y, buttons, key):
    root = G_INPUT_MODE_ROOTS + 4 * mode
    if key:
        command = emu.call(INPUT_FIND_KEY_COMMAND, root, key) & 0xFFFF
    else:
        command = emu.call(INPUT_FIND_CLICK_COMMAND, root, x, y, buttons) & 0xFFFF
    return command, emu.read(G_MATCH_RECT, 8)


def test_clicks_and_keys_match_original(tree, lib, keys):
    # All modes: random clicks of both buttons on the whole screen, and every key of the key lists.
    rng = random.Random(3)
    rect = (ctypes.c_int16 * 4)()
    found = 0
    for mode in MODES:
        for _ in range(400):
            x, y, buttons = rng.randrange(320), rng.randrange(200), rng.choice((1, 2))
            mine = lib.input_find_command(mode, x, y, buttons, 0, rect)
            theirs, their_rect = original_command(tree.emu, mode, x, y, buttons, 0)
            assert mine == theirs, (mode, x, y, buttons)
            if mine:
                assert bytes(rect) == their_rect, (mode, x, y, buttons)
                found += 1
        for key in keys:
            mine = lib.input_find_command(mode, 0, 0, 0, key, rect)
            theirs, their_rect = original_command(tree.emu, mode, 0, 0, 0, key)
            assert mine == theirs, (mode, hex(key))
            if mine:
                assert bytes(rect) == their_rect, (mode, hex(key))
    assert found > 300


INPUT_REFINE_CHAMPION_CLICK = 0x2186C
CHAMPION_ACTION_DELAY, CHAMPION_HANDS = 0x2A, 0xC3


def test_champion_clicks_match_original(tree, lib):
    # Clicks on the champion hands (status boxes) and on the champion icons (party icons): 1 to 4 champions in
    # random cells and party directions, busy hands and spells, empty hands, random points in the icon.
    from test_interface import CHAMPION_SIZE, champion_bytes, champion_records, sync_champions
    from test_move import CHAMPION_CELL, COMMAND_RECORD, G_CHAMPION_COUNT, Command
    rng = random.Random(16)
    first = bytes(champion_records(lib)[:CHAMPION_SIZE])
    results = set()
    for n in range(600):
        count = rng.randrange(1, 5)
        tree.game.champion_count = count
        tree.emu.write(G_CHAMPION_COUNT, struct.pack("<H", count))
        for champ, cell in enumerate(rng.sample(range(4), count)):
            champion_bytes(lib, champ, 0, first)
            champion_bytes(lib, champ, CHAMPION_CELL, [cell])
            champion_bytes(lib, champ, CHAMPION_ACTION_DELAY, [rng.choice((0, 0, 3)) for _ in range(3)])
            if rng.random() < 0.3:
                champion_bytes(lib, champ, CHAMPION_HANDS + 2 * rng.randrange(2), b"\xff\xff")
        sync_champions(lib, tree)
        tree.game.party_dir = rng.randrange(4)
        tree.emu.write(0x7F254, struct.pack("<h", tree.game.party_dir))
        command = rng.choice(list(range(0x5F, 0x63)) + list(range(0x74, 0x7C)))
        w, h = rng.randrange(8, 40), rng.randrange(8, 30)
        rect = (rng.randrange(200), rng.randrange(150), w, h)
        c = Command(rect[0] + rng.randrange(w), rect[1] + rng.randrange(h), command, (ctypes.c_int16 * 4)(*rect))
        tree.emu.write(COMMAND_RECORD, bytes(c))
        lib.input_refine_champion_click(ctypes.byref(c))
        tree.emu.call(INPUT_REFINE_CHAMPION_CLICK, COMMAND_RECORD)
        theirs = struct.unpack("<H", tree.emu.read(COMMAND_RECORD + 4, 2))[0]
        assert c.command == theirs, (n, hex(command))
        results.add((command, theirs))
    assert len(results) > 30
    refined = {theirs for _, theirs in results}
    assert refined & set(range(0x74, 0x7C)) and refined & set(range(0x10, 0x14)) and refined & set(range(0x5F, 0x63))


class MainLoop(ctypes.Structure):
    _fields_ = [("tick_threshold", ctypes.c_int), ("tick_count", ctypes.c_int), ("timer_ticks", ctypes.c_uint32),
                ("running", ctypes.c_int), ("view_hidden", ctypes.c_int), ("end", ctypes.c_int)]


@pytest.fixture
def session(lib, gdat, start, tmp_path):
    """start.DAT ready to play, as build/dm2 --save starts it."""
    from test_move import GameRecord
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.draw_init(gdat)
    game = GameRecord.in_dll(lib, "game")
    party = __import__("test_move").Party.in_dll(lib, "party")
    party.cur_map, party.new_map, party.move_steps, party.push_pending_group = -1, -1, 0, 0xFFFF
    lib.view_enter_map(game.party_map)
    lib.ui_reset()
    lib.money_init_coin_table()
    lib.ui_draw_game_screen()
    lib.input_init()
    lib.game_start_session()
    return game, MainLoop.in_dll(lib, "main_loop")


def run_ticks(lib, n):
    for _ in range(n):
        lib.game_tick()


KEY_FORWARD, KEY_TURN_RIGHT, KEY_PAUSE = 0x4C, 0x4D, 0x01


def test_keys_walk_and_turn_in_the_main_loop(session, lib):
    # A game step every 8 timer ticks; the forward key moves the party (a slow walk ends a step later), the right
    # key turns it, the pause key stops the game time until it is pressed again.
    game, loop = session
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    time, x, y, direction = game.game_time, game.party_x, game.party_y, game.party_dir
    run_ticks(lib, 16)
    assert game.game_time == time + 2 and loop.tick_threshold == 8
    lib.input_key_down(KEY_FORWARD)
    run_ticks(lib, 40)
    dx, dy = ((0, -1), (1, 0), (0, 1), (-1, 0))[direction]
    assert (game.party_x, game.party_y) == (x + dx, y + dy)
    lib.input_key_down(KEY_TURN_RIGHT)
    run_ticks(lib, 16)
    assert game.party_dir == (direction + 1) % 4
    lib.input_key_down(KEY_PAUSE)
    run_ticks(lib, 8)
    paused = game.game_time
    run_ticks(lib, 40)
    assert game.game_time == paused and not loop.running
    lib.input_key_down(KEY_PAUSE)
    run_ticks(lib, 24)
    assert loop.running and game.game_time > paused


def test_clicks_on_the_movement_arrows(session, lib):
    # Left clicks on the turn left, turn right, and forward arrows (layout zones 0x28, 0x29, and 0x2a).
    game, loop = session
    rect = (ctypes.c_int16 * 4)()
    direction = game.party_dir
    assert lib.zone_get_rect(0x28, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, 2)
    run_ticks(lib, 16)
    assert game.party_dir == (direction + 3) % 4
    assert lib.zone_get_rect(0x29, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, 2)
    run_ticks(lib, 16)
    assert game.party_dir == direction
    x, y = game.party_x, game.party_y
    assert lib.zone_get_rect(0x2A, rect)
    lib.input_mouse_click(rect[0] + rect[2] // 2, rect[1] + rect[3] // 2, 2)
    run_ticks(lib, 40)
    assert (game.party_x, game.party_y) != (x, y)
