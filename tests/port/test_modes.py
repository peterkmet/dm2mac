"""The game modes (src/ui/advanced.c): vanilla is the original game; advanced adds the W A S D Q E keys and the quick
save (F5) and quick load (F9)."""

import ctypes
import os
import shutil
import subprocess
import tempfile
import time

import pytest

from dm2test import constants as C
from dm2test.markers import ROOT
from dm2tools.control_client import request
from dm2test.control import control, pumped  # noqa: F401 (control is a fixture)
from dm2test.modes import (  # noqa: F401 (advanced is a fixture)
    ADVANCED, INPUT_MODE_DIALOG, INPUT_MODE_INVENTORY, INPUT_MODE_MOVE, INPUT_MODE_NAME, KEY_F5, KEY_F9, KEY_PAUSE, SCAN_A,
    SCAN_D, SCAN_E, SCAN_Q, SCAN_S, SCAN_W, SCREEN_W, STEP_TICKS, TEXT_LINE_HEIGHT, UI_WHITE, VANILLA, ZONE_MESSAGE_AREA,
    advanced, key_command, press, set_mode,
)
from dm2test.dialog import DialogState, Session
from dm2test.input import run_ticks, session  # noqa: F401 (session is a fixture)
from dm2test.scenario import gdat, start  # noqa: F401 (gdat and start are fixtures)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

# the keypad key of each letter: turn left, forward, turn right, strafe left, back, strafe right
LETTER_KEYPAD = {SCAN_Q: C.KEYPAD_TURN_LEFT, SCAN_W: C.KEYPAD_FORWARD, SCAN_E: C.KEYPAD_TURN_RIGHT, SCAN_A: C.KEYPAD_LEFT,
                 SCAN_S: C.KEYPAD_BACK, SCAN_D: C.KEYPAD_RIGHT}
SLOT_BUTTONS_SAVE = 6


def last_message_row(lib):
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(ZONE_MESSAGE_AREA, rect)
    fb = (ctypes.c_uint8 * (SCREEN_W * C.SCREEN_H)).in_dll(lib, "g_video_fb")
    top = rect[1] + rect[3] - TEXT_LINE_HEIGHT
    return [bytes(fb[y * SCREEN_W + rect[0]:y * SCREEN_W + rect[0] + rect[2]]) for y in range(top, top + TEXT_LINE_HEIGHT)]


def shows(lib, text):
    """The last row of the message area shows text in white: printing it again gives the same row."""
    before = last_message_row(lib)
    lib.text_print_message(UI_WHITE, b"\n" + text)
    return before == last_message_row(lib) and len({p for row in before for p in row}) > 1


def test_vanilla_letter_keys_and_quick_keys_do_nothing(lib, session, tmp_path):  # noqa: F811
    # The library starts in vanilla, as the original: W does not move the party, F5 writes no file.
    game, _ = session
    (tmp_path / "DATA").mkdir()
    Session.in_dll(lib, "g_session").data_dir = str(tmp_path).encode()
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    assert ctypes.c_int.in_dll(lib, "g_game_mode").value == VANILLA
    position = (game.party_x, game.party_y, game.party_dir)
    for key in (SCAN_W, SCAN_Q, KEY_F5, KEY_F9):
        assert key_command(lib, INPUT_MODE_MOVE, key) == 0
        press(lib, key)
    assert (game.party_x, game.party_y, game.party_dir) == position
    assert not list((tmp_path / "DATA").iterdir())


def test_letter_keys_give_the_keypad_commands_only_where_the_letters_have_none(lib, session):  # noqa: F811
    # In the advanced mode a letter without a command of its own gives the command of its keypad key (the movement
    # mode); in the text modes (slot name, champion name) it still types its letter.
    DialogState.in_dll(lib, "g_dialog").button_count = SLOT_BUTTONS_SAVE
    for mode in range(10):
        for letter, keypad in LETTER_KEYPAD.items():
            vanilla = key_command(lib, mode, letter)
            set_mode(lib, ADVANCED)
            mine = key_command(lib, mode, letter)
            set_mode(lib, VANILLA)
            assert mine == (vanilla or key_command(lib, mode, keypad)), (mode, hex(letter))
            if mode == INPUT_MODE_MOVE:
                assert mine and not vanilla, hex(letter)
            if mode in (INPUT_MODE_DIALOG, INPUT_MODE_NAME):
                assert mine == vanilla != 0, (mode, hex(letter))
    DialogState.in_dll(lib, "g_dialog").button_count = 0


def test_letter_keys_move_and_turn(lib, advanced):
    game, _ = advanced
    direction = game.party_dir
    press(lib, SCAN_E)
    assert game.party_dir == (direction + 1) % 4
    press(lib, SCAN_Q)
    press(lib, SCAN_Q)
    assert game.party_dir == (direction + 3) % 4
    press(lib, SCAN_E)
    opposite = {SCAN_W: SCAN_S, SCAN_S: SCAN_W, SCAN_A: SCAN_D, SCAN_D: SCAN_A}
    moved = 0
    for letter in (SCAN_W, SCAN_D, SCAN_S, SCAN_A):  # each letter steps where its keypad key steps (or a wall stops it)
        here = (game.party_x, game.party_y)
        press(lib, LETTER_KEYPAD[letter])
        there = (game.party_x, game.party_y)
        if there != here:
            press(lib, LETTER_KEYPAD[opposite[letter]])
        press(lib, letter)
        assert (game.party_x, game.party_y) == there, hex(letter)
        if there != here:
            moved += 1
            press(lib, opposite[letter])
            assert (game.party_x, game.party_y) == here
    assert moved and game.party_dir == direction


def test_quick_save_and_quick_load(lib, advanced):
    # F5 writes QUICK.DAT (no backup) and keeps the QUIT question state; F9 brings the party back to the saved square.
    game, data = advanced
    state = Session.in_dll(lib, "g_session")
    state.load_time, state.save_time, state.new_game = 5, 7, 1
    press(lib, KEY_F5)
    assert shows(lib, b"GAME SAVED")
    assert sorted(p.name for p in data.iterdir()) == ["QUICK.DAT"]
    assert (state.load_time, state.save_time, state.slot, state.new_game) == (5, 7, -1, 1)
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    press(lib, SCAN_E)
    press(lib, SCAN_W)
    press(lib, SCAN_W)
    assert (game.party_x, game.party_y, game.party_dir, game.party_map) != saved
    press(lib, KEY_F9)
    assert shows(lib, b"GAME LOADED")
    assert (game.party_x, game.party_y, game.party_dir, game.party_map) == saved
    assert sorted(p.name for p in data.iterdir()) == ["QUICK.DAT"]
    assert lib.input_get_mode() == INPUT_MODE_MOVE and state.loaded == 0 and state.slot == -1
    assert (state.load_time, state.save_time, state.new_game) == (0, 0, 1)
    position = (game.party_x, game.party_y)
    press(lib, SCAN_W)
    press(lib, SCAN_W)
    assert (game.party_x, game.party_y) != position, "the game goes on after the load"


def sound_level(lib):
    """The mixed level of a sample of 100 at full volume: the master volume of the sounds."""
    from dm2test.audio import UPSAMPLE, Sound, mix
    lib.sound_play.argtypes = [ctypes.POINTER(Sound), ctypes.c_uint8, ctypes.c_int8, ctypes.c_int8]
    lib.sound_play.restype = ctypes.c_bool
    lib.sound_mix.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
    lib.sound_stop_all()
    pcm = (ctypes.c_int8 * 1)(100)
    assert lib.sound_play(ctypes.byref(Sound(ctypes.cast(pcm, ctypes.POINTER(ctypes.c_int8)), 1)), 255, 0, 0)
    level = mix(lib, UPSAMPLE)[0]
    lib.sound_stop_all()
    return level


def test_quick_load_sets_the_loaded_volumes(lib, advanced):
    from dm2test.scenario import WorldGame
    from dm2test.audio import Music
    game = WorldGame.in_dll(lib, "g_game")
    music = Music.in_dll(lib, "g_music")
    game.sound_volume, game.music_volume = 5, 3
    press(lib, KEY_F5)
    lib.sound_set_volume(5)
    expected = sound_level(lib)
    game.sound_volume, game.music_volume = 1, 6
    lib.sound_set_volume(1)
    lib.music_set_volume(6)
    assert sound_level(lib) != expected
    press(lib, KEY_F9)
    assert shows(lib, b"GAME LOADED")
    assert (game.sound_volume, game.music_volume, music.volume) == (5, 3, 3) and sound_level(lib) == expected
    lib.sound_set_volume(7)


def test_failed_quick_save_keeps_the_old_quick_save(lib, advanced):
    # QUICK.DAT and the folder are read-only: the write fails, the old bytes stay, no QUICK.BAK is left.
    _, data = advanced
    (data / "QUICK.DAT").write_bytes(b"old quick save")
    (data / "QUICK.DAT").chmod(0o444)
    data.chmod(0o555)
    try:
        press(lib, KEY_F5)
        assert shows(lib, b"UNABLE TO SAVE")
    finally:
        data.chmod(0o755)
        (data / "QUICK.DAT").chmod(0o644)
    assert sorted(p.name for p in data.iterdir()) == ["QUICK.DAT"]
    assert (data / "QUICK.DAT").read_bytes() == b"old quick save"


def test_quick_save_without_data_folder_writes_nothing(lib, advanced, tmp_path):
    _, data = advanced
    state = Session.in_dll(lib, "g_session")
    state.data_dir = str(tmp_path / "missing").encode()
    press(lib, KEY_F5)
    assert shows(lib, b"UNABLE TO SAVE") and not (tmp_path / "missing").exists() and not list(data.iterdir())


def test_quick_load_without_quick_tmp_keeps_the_game(lib, advanced):
    # QUICK.TMP is a folder with a file: the copy of the running game cannot be written, so the load does not run.
    game, data = advanced
    press(lib, KEY_F5)
    press(lib, SCAN_E)
    (data / "QUICK.TMP").mkdir()
    (data / "QUICK.TMP" / "file").write_bytes(b"")
    position = (game.party_x, game.party_y, game.party_dir)
    press(lib, KEY_F9)
    assert shows(lib, b"UNABLE TO SAVE")
    assert (game.party_x, game.party_y, game.party_dir) == position
    assert sorted(p.name for p in data.iterdir()) == ["QUICK.DAT", "QUICK.TMP"]


def test_no_quick_save_keeps_the_game(lib, advanced):
    game, data = advanced
    position, time = (game.party_x, game.party_y, game.party_dir), game.game_time
    press(lib, KEY_F9)
    assert shows(lib, b"NO QUICK SAVE")
    assert (game.party_x, game.party_y, game.party_dir) == position and game.game_time > time
    assert not list(data.iterdir())


def test_damaged_quick_save_keeps_the_game(lib, advanced):
    game, data = advanced
    (data / "QUICK.DAT").write_bytes(b"\0" * 300)
    position, time, count = (game.party_x, game.party_y, game.party_dir), game.game_time, game.champion_count
    press(lib, KEY_F9)
    assert shows(lib, b"QUICK SAVE DAMAGED")
    assert (game.party_x, game.party_y, game.party_dir) == position and game.champion_count == count
    assert game.game_time >= time and sorted(p.name for p in data.iterdir()) == ["QUICK.DAT"]
    press(lib, SCAN_E)
    assert game.party_dir == (position[2] + 1) % 4, "the game goes on"


def test_quick_keys_work_only_in_the_dungeon_view(lib, advanced):
    # Paused, in the inventory, or in a dialog the quick keys do nothing.
    game, data = advanced
    for mode in range(10):
        if mode != INPUT_MODE_MOVE:
            assert key_command(lib, mode, KEY_F5) == key_command(lib, mode, KEY_F9) == 0, mode
    lib.ui_toggle_inventory(0)
    assert lib.input_get_mode() == INPUT_MODE_INVENTORY
    press(lib, KEY_F5)
    lib.ui_toggle_inventory(0)
    press(lib, KEY_PAUSE)
    press(lib, KEY_F5)
    press(lib, KEY_PAUSE)
    assert not list(data.iterdir())
    press(lib, KEY_F5)
    assert [p.name for p in data.iterdir()] == ["QUICK.DAT"]


def test_state_tells_the_game_mode(control, lib):  # noqa: F811
    path, *_ = control
    for mode, name in ((ADVANCED, "advanced"), (VANILLA, "vanilla")):
        set_mode(lib, mode)
        with pumped(lib, ticks=False):
            assert request({"cmd": "state"}, path)["game_mode"] == name


def test_state_tells_the_automap_and_the_cheats(control, lib):  # noqa: F811
    path, *_ = control
    lib.game_set_cheats.argtypes = [ctypes.c_uint8]
    cheats = {"cheat_light": C.CHEAT_LIGHT, "cheat_immortal": C.CHEAT_IMMORTAL, "cheat_food": C.CHEAT_FOOD,
              "cheat_kill": C.CHEAT_KILL, "cheat_walls": C.CHEAT_WALLS, "cheat_map": C.CHEAT_MAP}
    automap = ctypes.c_int.in_dll(lib, "g_advanced_screen")
    try:
        for name, cheat in [("automap", 0), *cheats.items()]:
            automap.value = name == "automap"
            lib.game_set_cheats(cheat)
            with pumped(lib, ticks=False):
                state = request({"cmd": "state"}, path)
            assert {f: state[f] for f in ["automap", *cheats]} == {f: int(f == name) for f in ["automap", *cheats]}
    finally:
        automap.value = 0
        lib.game_set_cheats(0)


def test_control_click_on_the_open_automap_ends_the_drag(control, lib):  # noqa: F811
    # A control click gives the press and the release: moving the pointer afterwards does not scroll the map.
    path, game, _ = control
    lib.seen_mark(game.party_map, game.party_x, game.party_y, game.party_dir)
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(7, rect)  # the viewport
    x, y, w, h = rect
    middle = (y + (h - 8) // 2) * SCREEN_W + x + (w - 8) // 2  # the top left pixel of the party frame
    fb = (ctypes.c_uint8 * (SCREEN_W * C.SCREEN_H)).in_dll(lib, "g_video_fb")
    set_mode(lib, ADVANCED)
    try:
        with pumped(lib):
            for req in ({"cmd": "key", "key": "tab"}, {"cmd": "wait", "ticks": 4},
                        {"cmd": "click", "x": x + w // 2, "y": y + h // 2}, {"cmd": "wait", "ticks": 4},
                        {"cmd": "move", "x": x + w // 2 + 40, "y": y + h // 2}, {"cmd": "wait", "ticks": 4}):
                assert request(req, path)["ok"]
            framed = fb[middle]
            assert request({"cmd": "key", "key": "esc"}, path)["ok"]
            assert request({"cmd": "wait", "ticks": 4}, path)["ok"]
        assert framed == ctypes.POINTER(ctypes.c_uint8).in_dll(lib, "g_ui_colors")[8]  # red: the map did not move
    finally:
        set_mode(lib, VANILLA)
        ctypes.c_int.in_dll(lib, "g_advanced_screen").value = 0


def test_failed_restore_ends_the_game_without_game_steps(lib, advanced):
    # QUICK.DAT is damaged, and QUICK.TMP is written without read rights (umask), so the running game cannot come
    # back: the program quits with EXIT_GAME_LOST and no command or game step runs on the empty dungeon.
    from dm2test.input import MainLoop
    game, data = advanced
    loop, state = MainLoop.in_dll(lib, "g_main_loop"), Session.in_dll(lib, "g_session")
    (data / "QUICK.DAT").write_bytes(b"\0" * 300)
    old = os.umask(0o477)
    try:
        lib.input_key_down(KEY_F9)
        lib.input_key_down(SCAN_W)
        for _ in range(STEP_TICKS):
            loop.tick_count = loop.tick_threshold  # each tick is due for a game step
            lib.play_tick()
            if loop.quit:
                break
        due = loop.tick_count >= loop.tick_threshold  # a game step resets the tick count
        timer = loop.timer_ticks
        run_ticks(lib, STEP_TICKS)
    finally:
        os.umask(old)
    try:
        assert loop.quit == 1 and state.exit_code == 4
        assert loop.timer_ticks == timer, "no tick after the quit"
        assert due, "no game step after the quit"
        assert (data / "QUICK.TMP").exists()
    finally:
        (data / "QUICK.TMP").chmod(0o644)
        loop.quit, state.exit_code = 0, 0


TURNBASED = C.GAME_MODE_TURNBASED


def test_quick_save_goes_to_the_save_folder(lib, advanced, tmp_path):
    """--saves: QUICK.DAT goes to the save folder, not to <data>/DATA."""
    saves = tmp_path / "saves"
    saves.mkdir()
    state = Session.in_dll(lib, "g_session")
    state.save_dir = str(saves).encode()
    try:
        assert lib.advanced_quick_save() == 1
        assert (saves / "QUICK.DAT").exists() and not (tmp_path / "DATA" / "QUICK.DAT").exists()
    finally:
        state.save_dir = b""


@pytest.mark.parametrize("case, result", [("missing", 1), ("damaged", 2), ("blocked", 3)])
def test_quick_load_results(lib, advanced, case, result):
    """No QUICK.DAT gives QUICK_MISSING, a damaged one QUICK_DAMAGED, an unwritable QUICK.TMP (a folder of that name)
    QUICK_BLOCKED; the game goes on in all three (a restore may add one game step)."""
    game, data = advanced
    if case == "damaged":
        (data / "QUICK.DAT").write_bytes(b"\0" * 300)
    elif case == "blocked":
        (data / "QUICK.DAT").write_bytes(b"\0" * 300)
        (data / "QUICK.TMP").mkdir()
        (data / "QUICK.TMP" / "file").write_bytes(b"")
    time = game.game_time
    assert lib.advanced_quick_load() == result
    assert time <= game.game_time <= time + 1


def test_lost_game_stays_in_the_turnbased_mode(lib, advanced):
    """A damaged quick save whose restore also fails: QUICK_LOST; the turn-based mode does not quit."""
    from dm2test.input import MainLoop
    _, data = advanced
    loop, state = MainLoop.in_dll(lib, "g_main_loop"), Session.in_dll(lib, "g_session")
    (data / "QUICK.DAT").write_bytes(b"\0" * 300)
    set_mode(lib, TURNBASED)
    old = os.umask(0o477)
    try:
        result = lib.advanced_quick_load()
    finally:
        os.umask(old)
        (data / "QUICK.TMP").chmod(0o644)
    try:
        assert result == 4 and loop.quit == 0 and state.exit_code == 0
        assert lib.advanced_quick_load() == 4, "a lost game stays lost"
    finally:
        lib.advanced_reset()
        loop.quit, state.exit_code = 0, 0


def test_bad_mode_stops_the_program():
    result = subprocess.run([str(ROOT / "build" / "dm2"), "--mode", "turbo"], capture_output=True, text=True, timeout=10)
    assert result.returncode == 2 and "unknown mode turbo" in result.stderr


@pytest.mark.parametrize("args, name", [([], "advanced"), (["--mode", "vanilla"], "vanilla")])
def test_program_starts_in_advanced_unless_vanilla(game_dir, tmp_path, args, name):
    # build/dm2 without --mode plays the advanced mode: W walks and F5 writes DATA/QUICK.DAT; --mode vanilla does not.
    (tmp_path / "DATA").mkdir()
    for f in ("GRAPHICS.DAT", "DUNGEON.DAT"):
        (tmp_path / "DATA" / f).symlink_to(game_dir / "DATA" / f)
    folder = tempfile.mkdtemp(prefix="dm2m", dir="/tmp")  # sun_path holds about 100 bytes
    path = os.path.join(folder, "control.sock")
    env = dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy")
    program = subprocess.Popen([str(ROOT / "build/dm2"), "--data", str(tmp_path), "--control", path, *args, "--save",
                                str(ROOT / "tests/saves/start.DAT")], env=env, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    try:
        for _ in range(200):
            if os.path.exists(path):
                break
            time.sleep(0.05)
        state = request({"cmd": "state"}, path, timeout=10)
        assert state["game_mode"] == name
        for key in ("w", "f5"):
            assert request({"cmd": "key", "key": key}, path, timeout=10)["ok"]
            assert request({"cmd": "wait", "ticks": 60}, path, timeout=10)["ok"]
        moved = request({"cmd": "state"}, path, timeout=10)["party"]
        assert (moved != state["party"]) == (name == "advanced")
        assert (tmp_path / "DATA" / "QUICK.DAT").exists() == (name == "advanced")
        assert request({"cmd": "quit"}, path, timeout=10) == {"ok": True}
        assert program.wait(timeout=10) == 0
    finally:
        program.kill()
        shutil.rmtree(folder)


def test_tab_closes_the_inventory_in_the_advanced_mode_only(lib, session):  # noqa: F811
    assert key_command(lib, INPUT_MODE_INVENTORY, C.KEY_AUTOMAP) == 0
    name_editor = key_command(lib, INPUT_MODE_NAME, C.KEY_AUTOMAP)
    set_mode(lib, ADVANCED)
    assert key_command(lib, INPUT_MODE_INVENTORY, C.KEY_AUTOMAP) == C.CMD_INVENTORY_LEADER
    assert key_command(lib, INPUT_MODE_NAME, C.KEY_AUTOMAP) == name_editor, "the name editor keeps Tab"
    set_mode(lib, VANILLA)


def test_damage_of_a_hit_stays_one_extra_step_in_the_advanced_mode(lib, advanced):  # noqa: F811
    from dm2test.interface import ActionArea
    action = ActionArea.in_dll(lib, "g_action")
    rect = (ctypes.c_int16 * 4)()
    lib.zone_get_rect(C.ZONE_ACTION_AREA, rect)
    lib.area_end_damage_hold.argtypes = [ctypes.c_int16, ctypes.c_int16]

    def hit(mode):
        set_mode(lib, mode)
        action.champion, action.mode, action.redraw_delay, action.result = 0, C.ACTION_MODE_HANDS, 1, 7
        lib.area_update(1)
        assert action.mode == C.ACTION_MODE_RESULT and action.dirty

    hit(VANILLA)
    lib.area_update(1)
    assert not action.dirty, "the vanilla mode redraws one step later"
    hit(ADVANCED)
    lib.area_update(1)
    assert action.dirty, "the damage stays one more step"
    lib.area_update(1)
    assert not action.dirty
    hit(ADVANCED)
    lib.area_update(1)
    assert not lib.area_end_damage_hold(rect[0] - 1, rect[1]), "a click outside the area keeps the hold"
    assert lib.area_end_damage_hold(rect[0], rect[1])
    assert not action.dirty, "a click in the area ends the hold and draws the icons"
    set_mode(lib, VANILLA)
