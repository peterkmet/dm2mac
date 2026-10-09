"""The game modes (port/src/ui/advanced.c): vanilla is the original game; advanced adds the W A S D Q E keys and the quick
save (F5) and quick load (F9)."""

import ctypes
import os
import shutil
import subprocess
import tempfile
import time

import pytest

from conftest import ROOT
from dm2tools.control_client import request
from test_control import control, pumped  # noqa: F401 (control is a fixture)
from test_dialog import DialogState, Session
from test_input import run_ticks, session  # noqa: F401 (session is a fixture)
from test_move import gdat, start  # noqa: F401 (gdat and start are fixtures)

VANILLA, ADVANCED = 0, 1
SCAN_Q, SCAN_W, SCAN_E, SCAN_A, SCAN_S, SCAN_D = 0x10, 0x11, 0x12, 0x1E, 0x1F, 0x20
KEY_F5, KEY_F9, KEY_PAUSE = 0x3F, 0x43, 0x01
# the keypad key of each letter: turn left, forward, turn right, strafe left, back, strafe right
LETTER_KEYPAD = {SCAN_Q: 0x4B, SCAN_W: 0x4C, SCAN_E: 0x4D, SCAN_A: 0x4F, SCAN_S: 0x50, SCAN_D: 0x51}
INPUT_MODE_DIALOG, INPUT_MODE_MOVE, INPUT_MODE_INVENTORY, INPUT_MODE_NAME = 4, 5, 8, 9
SLOT_BUTTONS_SAVE = 6
ZONE_MESSAGE_AREA, TEXT_LINE_HEIGHT, SCREEN_W, UI_WHITE = 15, 7, 320, 15
STEP_TICKS = 40  # enough timer ticks for a key to give its command and the party to finish a step


def set_mode(lib, mode):
    ctypes.c_int.in_dll(lib, "game_mode").value = mode


@pytest.fixture
def advanced(lib, session, tmp_path):  # noqa: F811
    """start.DAT in the advanced mode, with the game folder in tmp_path; vanilla again after the test."""
    (tmp_path / "DATA").mkdir()
    state = Session.in_dll(lib, "session")
    state.data_dir = str(tmp_path).encode()
    state.slot, state.loaded, state.new_game = -1, 0, 0
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    lib.text_print_message.argtypes = [ctypes.c_int, ctypes.c_char_p]
    set_mode(lib, ADVANCED)
    yield session[0], tmp_path / "DATA"
    set_mode(lib, VANILLA)


def press(lib, key):
    lib.input_key_down(key)
    run_ticks(lib, STEP_TICKS)


def last_message_row(lib):
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(ZONE_MESSAGE_AREA, rect)
    fb = (ctypes.c_uint8 * (SCREEN_W * 200)).in_dll(lib, "fb")
    top = rect[1] + rect[3] - TEXT_LINE_HEIGHT
    return [bytes(fb[y * SCREEN_W + rect[0]:y * SCREEN_W + rect[0] + rect[2]]) for y in range(top, top + TEXT_LINE_HEIGHT)]


def shows(lib, text):
    """The last row of the message area shows text in white: printing it again gives the same row."""
    before = last_message_row(lib)
    lib.text_print_message(UI_WHITE, b"\n" + text)
    return before == last_message_row(lib) and len({p for row in before for p in row}) > 1


def key_command(lib, mode, key):
    lib.input_find_command.restype = ctypes.c_uint16
    rect = (ctypes.c_int16 * 4)()
    return lib.input_find_command(mode, 0, 0, 0, key, rect)


def test_vanilla_letter_keys_and_quick_keys_do_nothing(lib, session, tmp_path):  # noqa: F811
    # The library starts in vanilla, as the original: W does not move the party, F5 writes no file.
    game, _ = session
    (tmp_path / "DATA").mkdir()
    Session.in_dll(lib, "session").data_dir = str(tmp_path).encode()
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    assert ctypes.c_int.in_dll(lib, "game_mode").value == VANILLA
    position = (game.party_x, game.party_y, game.party_dir)
    for key in (SCAN_W, SCAN_Q, KEY_F5, KEY_F9):
        assert key_command(lib, INPUT_MODE_MOVE, key) == 0
        press(lib, key)
    assert (game.party_x, game.party_y, game.party_dir) == position
    assert not list((tmp_path / "DATA").iterdir())


def test_letter_keys_give_the_keypad_commands_only_where_the_letters_have_none(lib, session):  # noqa: F811
    # In the advanced mode a letter without a command of its own gives the command of its keypad key (the movement
    # mode); in the text modes (slot name, champion name) it still types its letter.
    DialogState.in_dll(lib, "dialog").button_count = SLOT_BUTTONS_SAVE
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
    DialogState.in_dll(lib, "dialog").button_count = 0


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
    state = Session.in_dll(lib, "session")
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
    from test_sound import UPSAMPLE, Sound, mix
    lib.sound_play.argtypes = [ctypes.POINTER(Sound), ctypes.c_uint8, ctypes.c_int8, ctypes.c_int8]
    lib.sound_mix.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
    lib.sound_stop_all()
    pcm = (ctypes.c_int8 * 1)(100)
    assert lib.sound_play(ctypes.byref(Sound(ctypes.cast(pcm, ctypes.POINTER(ctypes.c_int8)), 1)), 255, 0, 0)
    level = mix(lib, UPSAMPLE)[0]
    lib.sound_stop_all()
    return level


def test_quick_load_sets_the_loaded_volumes(lib, advanced):
    from test_move import WorldGame
    from test_music import Music
    game = WorldGame.in_dll(lib, "game")
    music = Music.in_dll(lib, "music")
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
    state = Session.in_dll(lib, "session")
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
    names = ("advanced_automap_open", "cheat_immortal", "cheat_food", "cheat_light")
    fields = ("automap", "cheat_immortal", "cheat_food", "cheat_light")
    try:
        for value in (0, 1):
            for name in names:
                ctypes.c_int.in_dll(lib, name).value = value
            with pumped(lib, ticks=False):
                state = request({"cmd": "state"}, path)
            assert [state[f] for f in fields] == [value] * 4
    finally:
        for name in names:
            ctypes.c_int.in_dll(lib, name).value = 0


def test_control_click_on_the_open_automap_ends_the_drag(control, lib):  # noqa: F811
    # A control click gives the press and the release: moving the pointer afterwards does not scroll the map.
    path, game, _ = control
    lib.seen_mark(game.party_map, game.party_x, game.party_y, game.party_dir)
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(7, rect)  # the viewport
    x, y, w, h = rect
    middle = (y + (h - 8) // 2) * SCREEN_W + x + (w - 8) // 2  # the top left pixel of the party frame
    fb = (ctypes.c_uint8 * (SCREEN_W * 200)).in_dll(lib, "fb")
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
        assert framed == ctypes.POINTER(ctypes.c_uint8).in_dll(lib, "ui_colors")[8]  # red: the map did not move
    finally:
        set_mode(lib, VANILLA)
        ctypes.c_int.in_dll(lib, "advanced_automap_open").value = 0


def test_failed_restore_ends_the_game_without_game_steps(lib, advanced):
    # QUICK.DAT is damaged, and QUICK.TMP is written without read rights (umask), so the running game cannot come
    # back: the program quits with EXIT_GAME_LOST and no command or game step runs on the empty dungeon.
    from test_input import MainLoop
    game, data = advanced
    loop, state = MainLoop.in_dll(lib, "main_loop"), Session.in_dll(lib, "session")
    (data / "QUICK.DAT").write_bytes(b"\0" * 300)
    old = os.umask(0o477)
    try:
        lib.input_key_down(KEY_F9)
        lib.input_key_down(SCAN_W)
        for _ in range(STEP_TICKS):
            loop.tick_count = loop.tick_threshold  # each tick is due for a game step
            lib.game_tick()
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
