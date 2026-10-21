"""Step 20 scenarios with the emulator oracle (dm2test.scenario.Scenario on start.DAT): a save through the save dialogs and its
load through the load dialog, the end of the game by the end sensor, and the end after the death of the party."""

import ctypes
import struct

import pytest

from test_dialog import DialogState, InputMatch, Session, program_start_after  # noqa: F401 (an autouse fixture)
from dm2test.scenario import Scenario, WALL_END_GAME, gdat, square_event, start  # noqa: F401 (gdat and start are fixtures)
from dm2tools.emu import EmuError
from dm2test.save import original_load, original_save
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

GAME_QUIT, IOP_SET_VIDEO_STATE, GAME_END, COMMAND_EXECUTE = 0x2005B, 0x138C7, 0x1FEF6, 0x21D6C
UI_DIALOG_DRAW, UI_DIALOG_RUN, SAVE_CHECK_DISK, WAIT_VBLANKS, GFX_FILL_ZONE_SCREEN = 0x1A37D, 0x1A0A4, 0x346D1, 0x1C660, 0x13B4D
G_GAME_COMPLETED, G_QUIT_EXIT_CODE, G_NEW_GAME, G_USE_DUNGENB, G_END_OFFER_RESTORE = 0x7F23C, 0x7F434, 0x7F280, 0x803F8, 0x803F0
INPUT_MODE_DIALOG = 4
GAME_SIZE = 252 + 4 * 263 + 2 + 6  # sizeof(Game) in save.h
WALK = [3, 3, 3, 2, 3, 3, 1, 1, 6, 5, 5, 4, 3, 3]
FRAME = ctypes.CFUNCTYPE(None)


class Player:
    """The frame hook of the C port: every few frames the next action of the script runs (clicks, keys)."""

    def __init__(self, lib, actions, every=30):
        self.lib, self.actions, self.every, self.frames = lib, list(actions), every, 0
        self.hook = FRAME(self.frame)

    def frame(self):
        self.frames += 1
        if self.frames % self.every == 0 and self.actions:
            self.actions.pop(0)(self.lib)
        if self.frames > 100 * self.every:  # a dialog that never closes: the program ends, the test fails
            main_loop_quit(self.lib).value = 1

    def __enter__(self):
        ctypes.c_void_p.in_dll(self.lib, "play_frame_hook").value = ctypes.cast(self.hook, ctypes.c_void_p).value
        return self

    def __exit__(self, *_):
        ctypes.c_void_p.in_dll(self.lib, "play_frame_hook").value = None


def find_point(lib, mode, test):
    """A screen point whose left click in input mode gives a match that test accepts."""
    rect = (ctypes.c_int16 * 4)()
    match = InputMatch.in_dll(lib, "input_match")
    lib.input_find_command.restype = ctypes.c_uint16
    for y in range(1, 200, 2):
        for x in range(1, 320, 2):
            if lib.input_find_command(mode, x, y, 2, 0, rect) and test(match):
                return x, y
    raise AssertionError("no such click point")


def click_choice(choice):
    """An action: a left click on the button with this place in the list of the open dialog."""
    def act(lib):
        x, y = find_point(lib, INPUT_MODE_DIALOG, lambda m: m.choice == choice)
        lib.input_mouse_click(x, y, 2)
    return act


def click_slot(slot):
    """An action: a left click on slot of the slot list (choice 3)."""
    def act(lib):
        x, y = find_point(lib, INPUT_MODE_DIALOG, lambda m: m.choice == 3)
        rect = (ctypes.c_int16 * 4)()
        assert lib.zone_get_rect(0x1C3, rect)
        vx, vy = ctypes.c_int16(), ctypes.c_int16()
        lib.zone_get_position(InputMatch.in_dll(lib, "input_match").origin, ctypes.byref(vx), ctypes.byref(vy))
        lib.input_mouse_click(x, rect[1] + vy.value + slot * 7 + 3, 2)
    return act


def key(code):
    def act(lib):
        lib.input_key_down(code)
    return act


@pytest.fixture
def play(lib, gdat, start, tmp_path):
    """start.DAT after a walk in the C port and the original, with the fonts and the dialogs of the C port ready."""
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    sc = Scenario(lib, gdat, start, tmp_path)
    assert lib.text_init(gdat)
    lib.ui_reset()
    lib.input_init()
    for command in WALK:
        for _ in range(4):
            sc.step()
        mine, theirs = sc.command(command)
        assert mine == theirs
    sc.assert_equal("walk")
    (tmp_path / "DATA").mkdir()
    session = Session.in_dll(lib, "session")
    session.data_dir = str(tmp_path).encode()
    session.slot, session.loaded, session.new_game, session.restart, session.completed = -1, 0, 0, 0, 0
    session.load_time = session.save_time = 0
    main_loop_quit(lib).value = 0
    DialogState.in_dll(lib, "dialog").screen_mode = 0
    return sc


def main_loop_quit(lib):
    """MainLoop.quit (after tick_threshold, tick_count, timer_ticks, running, view_hidden, end)."""
    return ctypes.c_int.from_address(ctypes.addressof(ctypes.c_int.in_dll(lib, "main_loop")) + 24)


def test_save_and_load_through_the_dialogs(play, lib, gdat, tmp_path):
    # The save dialog: SAVE, slot 2 of the list (empty: the name GAME 2), the name field, two letters and Enter, OK.
    # The file is the file that the original save writes of the same game. The original load of it and its save give
    # the same file as the load and save of the C port. The load dialog (slot 2, LOAD) brings back the same game in the
    # C port, also from the backup file.
    sc = play
    with Player(lib, [click_choice(1), click_slot(2), click_choice(4), key(0x1E), key(0x30), key(0x1C), click_choice(2)]):
        lib.session_save()
    saved = (tmp_path / "DATA" / "SKSAVE2.DAT").read_bytes()
    assert saved[2:0x12].split(b"\0")[0] == b"GAME 2AB"
    header = saved[:0x2A]
    sc.emu.written = bytearray()
    theirs = original_save(sc.emu, header)
    assert saved == theirs
    # the port-written file loads in the original as in the port: both write the same file of it
    from dm2test.save import c_round_trip
    expected = original_save(original_load(lib, gdat, saved), header)
    assert c_round_trip(lib, gdat, saved, tmp_path) == expected
    session = Session.in_dll(lib, "session")
    assert (session.slot, session.save_time) == (2, sc.time())

    # the second load: the restore of the last slot (no dialog) when only the backup file is left
    for name in ("SKSAVE2.DAT", "SKSAVE2.BAK"):
        restore = name.endswith("BAK")
        if restore:
            (tmp_path / "DATA" / "SKSAVE2.DAT").rename(tmp_path / "DATA" / "SKSAVE2.BAK")
        session.loaded, session.new_game = restore, 0
        with Player(lib, [] if restore else [click_slot(2), click_choice(2)]):
            assert lib.session_load() == 1
        assert (session.loaded, session.slot) == (1, 2)
        again = tmp_path / "again.DAT"
        assert lib.save_write(str(again).encode(), str(tmp_path / "again.BAK").encode(), b"GAME 2AB", gdat) == 0
        assert again.read_bytes()[0x2A:] == expected[0x2A:], name  # a load and a save: as in the original
        assert (tmp_path / "DATA" / "SKSAVE2.DAT").exists()


def test_load_dialog_cancel_and_empty_slot(play, lib):
    # A click on an empty slot chooses nothing: LOAD then cancels the load; CANCEL too.
    session = Session.in_dll(lib, "session")
    for actions in ([click_slot(5), click_choice(2)], [click_choice(1)]):
        with Player(lib, actions):
            assert lib.session_load() == -1
        assert (session.slot, session.loaded) == (0, 0)


def test_hide_game_view_cancels_the_champion_drag(lib):
    # The original game_hide_view cancels a champion drag inside its guard (SKULL.c:54260).
    drag = ctypes.c_int.in_dll(lib, "input_drag_state")
    hidden = ctypes.c_int.from_address(ctypes.addressof(main_loop_quit(lib)) - 8)  # MainLoop.view_hidden
    DialogState.in_dll(lib, "dialog").screen_mode, hidden.value, drag.value = 0, 0, 1
    lib.play_hide_view()
    assert (hidden.value, drag.value) == (1, 0)
    drag.value = 1
    lib.play_hide_view()  # already hidden: the guard keeps the drag
    assert drag.value == 1
    drag.value = hidden.value = 0


def test_end_sensor_completes_the_game(play, lib):
    # The wall sensor of the end (kind 0x12, map 38) in both: the game is complete and the program ends (exit code 1).
    sc = play
    emu = sc.emu
    emu.uc.hook_del(sc.skips.pop(WALL_END_GAME))
    quits = []
    emu.stub(GAME_QUIT, lambda *_: quits.append(emu.read_u32(G_GAME_COMPLETED)))
    emu.stub(IOP_SET_VIDEO_STATE, lambda *_: 0)
    session = Session.in_dll(lib, "session")
    for effect in (0, 1, 2):
        square_event(sc, 38, 17, 2, 3, effect, 1)
        for _ in range(3):
            sc.step()
        sc.assert_same(effect)
        if quits:
            break
    assert quits == [1]
    assert (session.completed, session.exit_code, main_loop_quit(lib).value) == (1, 1, 1)


def test_party_death_ends_the_game_like_original(play, lib):
    # session_end after the death of the party: the scream of the first champion, the end dialog, QUIT GAME: the program
    # ends with the exit code of a dead party in both.
    sc = play
    emu = sc.emu
    dialogs, quits = [], []
    emu.stub(UI_DIALOG_DRAW, lambda d, m, *_: dialogs.append((d & 0xFF, m & 0xFF)) or d & 0xFF)
    emu.stub(UI_DIALOG_RUN, lambda *_: 1)  # the text entry of QUIT GAME
    for addr in (SAVE_CHECK_DISK, WAIT_VBLANKS, GFX_FILL_ZONE_SCREEN, IOP_SET_VIDEO_STATE):
        emu.stub(addr, lambda *_: 0)

    def quit_(*_):
        quits.append(emu.read(G_QUIT_EXIT_CODE, 2))
        raise RuntimeError("session_quit")  # the original never returns from session_quit

    emu.stub(GAME_QUIT, quit_)
    emu.write_u32(G_END_OFFER_RESTORE, 1)
    Session.in_dll(lib, "session").offer_restore = 1
    with Player(lib, [click_choice(2)], every=300):
        lib.session_end(1)
    with pytest.raises(EmuError, match="session_quit"):
        emu.call(GAME_END, 1)
    assert dialogs == [(0x10, 0)]
    assert quits == [b"\x01\x00"]
    session = Session.in_dll(lib, "session")
    assert (session.quit_code, session.exit_code, main_loop_quit(lib).value) == (1, 2, 1)
    sc.flush_sounds()
    sc.assert_same_sounds("scream")


def test_party_death_restores_the_last_save(play, lib, gdat, tmp_path):
    # After a save, the end dialog's RESTART GAME loads the last slot again without the load dialog.
    with Player(lib, [click_choice(1), click_slot(4), click_choice(2)]):
        lib.session_save()
    from dm2test.scenario import GameRecord
    game = GameRecord.in_dll(lib, "game")
    saved_state = (game.game_time, game.party_x, game.party_y, game.party_dir, game.party_map)
    for _ in range(20):
        play.step()
    session = Session.in_dll(lib, "session")
    session.offer_restore = 1
    with Player(lib, [click_choice(1), click_choice(1)], every=300):  # RESTART GAME, then OK of "GAME LOADED"
        lib.session_end(1)
    assert main_loop_quit(lib).value == 0
    assert (session.loaded, session.slot) == (0, 4)
    # the game of the save, and the first game step of the new session
    assert (game.game_time - 1, game.party_x, game.party_y, game.party_dir, game.party_map) == saved_state



GAME_SAVE, G_LOAD_TIME, G_SAVE_TIME, G_DISK_NO_SWAP = 0x3502B, 0x7F218, 0x7F8F4, 0x7054A
# session_save calls that this test leaves out: the action area, the game view, the free list of the timeline, and the
# disk check (the scenario stubs the mouse)
GAME_SAVE_STUBS = (0x3FD17, 0x3FE68, 0x54E41, 0x54EAC, 0x56030, SAVE_CHECK_DISK)


@pytest.mark.parametrize("since_load, since_save", [(100, 101), (101, 100), (101, 101)])
def test_quit_asks_when_not_saved_like_original(play, lib, since_load, since_save):
    # QUIT of the save menu: dialog 0x0c asks first only when the last load and the last save are both more than 100
    # ticks ago. CANCEL of the question goes back to the save menu, then RESUME; without the question the game ends.
    emu = play.emu
    now = play.time()
    emu.write_u32(G_LOAD_TIME, now - since_load)
    emu.write_u32(G_SAVE_TIME, now - since_save)
    emu.write(G_DISK_NO_SWAP, struct.pack("<H", 1))
    dialogs, ends = [], []
    emu.stub(UI_DIALOG_DRAW, lambda d, *_: dialogs.append(d & 0xFF) or d & 0xFF)
    emu.stub(UI_DIALOG_RUN, lambda d, *_: 3 if d & 0xFF == 0x0C or dialogs.count(0x1B) > 1 else 1)  # QUIT, CANCEL, RESUME
    emu.stub(GAME_END, lambda *_: ends.append(1))
    for addr in GAME_SAVE_STUBS:
        emu.stub(addr, lambda *_: 0)
    emu.call(GAME_SAVE)
    asked = 0x0C in dialogs
    assert asked == (since_load > 100 and since_save > 100)
    assert dialogs == ([0x1B, 0x0C, 0x1B] if asked else [0x1B]) and ends == ([] if asked else [1])

    session = Session.in_dll(lib, "session")
    session.load_time, session.save_time = now - since_load, now - since_save
    pick = lambda lib: setattr(InputMatch.in_dll(lib, "input_match"), "value", 0xEF)  # a click on the end picture
    actions = [click_choice(2), click_choice(2), click_choice(4)] if asked else [click_choice(2), pick]
    with Player(lib, actions) as player:
        lib.session_save()
    assert not player.actions and player.frames < 100 * player.every
    assert main_loop_quit(lib).value == (0 if asked else 1)


def test_menu_commands_match_original(play, lib):
    # The commands of the start menu: new game, new game from DUNGENB.DAT, load.
    from dm2test.scenario import COMMAND_RECORD, Command
    sc = play
    session = Session.in_dll(lib, "session")
    for command in (0xD7, 0xD9, 0xD8):
        c = Command(0, 0, command)
        sc.emu.write(COMMAND_RECORD, bytes(c))
        lib.command_execute(ctypes.byref(c))
        sc.emu.call(COMMAND_EXECUTE, COMMAND_RECORD)
        theirs = struct.unpack("<HH", sc.emu.read(G_NEW_GAME, 2) + sc.emu.read(G_USE_DUNGENB, 2))
        assert (session.new_game, session.use_dungenb) == theirs, hex(command)


DUN_GET_UNUSED_THING, G_LAST_DISCARDED_MAP = 0x1DDD7, 0x7F422
THING_SIZES = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)


# The "do not discard" bit by thing type: (byte offset, bit). start.DAT sets it on all its weapons, clothes, and potions.
KEEP_BITS = {4: (0x0F, 0x04), 5: (2, 0x80), 6: (2, 0x80), 8: (3, 0x80), 10: (2, 0x80)}


def fill_table(sc, lib, kind):
    """Marks every free record of kind as used (a link to the end of a list) and clears the "do not discard" bit of
    every other third record, in both."""
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    base = sc.emu.read_u32(0x7F288 + 4 * kind)
    offset, bit = KEEP_BITS[kind]
    for i in range(lib.dungeon_thing_count(kind)):
        rec = lib.dungeon_thing_record(kind << 10 | i)
        link = ctypes.c_uint16.from_address(rec)
        if link.value == 0xFFFF:
            link.value = 0xFFFE
        elif i % 3:
            ctypes.c_uint8.from_address(rec + offset).value &= ~bit
        sc.emu.write(base + THING_SIZES[kind] * i, ctypes.string_at(rec, THING_SIZES[kind]))


@pytest.mark.parametrize("place", [None, (5, 10, 12, 0), (13, 7, 9, 2)])
def test_discarded_things_match_original(play, lib, place):
    # Full thing tables: the next new weapon, armour, misc item, potion, and creature group takes the record of a thing
    # that the original discards (searched map by map, the party map last, things near the party and on protected
    # squares stay), 4 times each, from the party at the start and on other maps.
    sc = play
    if place:
        sc.place(*place)
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    last = (ctypes.c_uint8 * 16).in_dll(lib, "last_discarded_map")
    ctypes.memmove(last, sc.emu.read(G_LAST_DISCARDED_MAP, 16), 16)  # both from the program start
    found = {}
    for kind in (5, 6, 10, 8, 4):
        fill_table(sc, lib, kind)
        for n in range(4):
            mine = lib.dungeon_unused_thing(kind)
            theirs = sc.emu.call(DUN_GET_UNUSED_THING, kind) & 0xFFFF
            assert mine == theirs, (kind, n)
            assert bytes(last) == sc.emu.read(G_LAST_DISCARDED_MAP, 16), (kind, n)
            found[kind] = found.get(kind, 0) + (mine != 0xFFFF)
            sc.assert_same((kind, n))
    assert all(found.values()), found



@pytest.mark.parametrize("distance", [5, 6])
def test_discard_keeps_things_within_5_squares(play, lib, distance):
    # One weapon that may go, on a square without sensors and texts; the party on its map, 5 or 6 squares beside it.
    # At 5 squares the weapon stays and no record comes free; at 6 squares it goes, in both.
    from test_session import square_things
    sc = play
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    lib.dungeon_unused_thing.restype = ctypes.c_uint16

    def loose_weapons():
        for map_ in range(lib.dungeon_map_count()):
            width = lib.dungeon_map_width(map_)
            for x in range(width):
                for y in range(lib.dungeon_map_height(map_)):
                    things = square_things(lib, map_, x, y)
                    if (x >= 6 or x + 6 < width) and not any(2 <= t >> 10 <= 3 for t in things):
                        yield from ((t, map_, x, y) for t in things if t >> 10 == 5)

    target, map_, x, y = next(loose_weapons())
    base = sc.emu.read_u32(0x7F288 + 4 * 5)
    offset, bit = KEEP_BITS[5]
    for i in range(lib.dungeon_thing_count(5)):
        rec = lib.dungeon_thing_record(5 << 10 | i)
        link = ctypes.c_uint16.from_address(rec)
        if link.value == 0xFFFF:
            link.value = 0xFFFE
        keep = ctypes.c_uint8.from_address(rec + offset)
        keep.value = keep.value & ~bit if 5 << 10 | i == target else keep.value | bit
        sc.emu.write(base + 4 * i, ctypes.string_at(rec, 4))
    last = (ctypes.c_uint8 * 16).in_dll(lib, "last_discarded_map")
    ctypes.memmove(last, sc.emu.read(G_LAST_DISCARDED_MAP, 16), 16)
    sc.place(map_, x - distance if x >= 6 else x + distance, y, 0)
    mine = lib.dungeon_unused_thing(5)
    assert mine == sc.emu.call(DUN_GET_UNUSED_THING, 5) & 0xFFFF
    assert mine == (target if distance == 6 else 0xFFFF)
    sc.assert_same(distance)


def click_command(mode, command):
    """An action: a left click on the zone of command in input mode."""
    def act(lib):
        x, y = find_point(lib, mode, lambda m: m.value == command)
        lib.input_mouse_click(x, y, 2)
    return act


def test_start_menu_new_game_and_load(lib, gdat, start, tmp_path):
    # The start of a run in the C port: the menu, NEW GAME: DUNGEON.DAT with the party on its start square; then the
    # menu again, LOAD, slot 0 (start.DAT), LOAD, and OK of "GAME LOADED": the game of start.DAT.
    from dm2test.scenario import GameRecord
    from dm2test.markers import ROOT
    data = tmp_path / "game"
    (data / "DATA").mkdir(parents=True)
    (data / "DATA" / "DUNGEON.DAT").write_bytes((ROOT / "game" / "DATA" / "DUNGEON.DAT").read_bytes())
    (data / "DATA" / "SKSAVE0.DAT").write_bytes(start)
    lib.draw_init(gdat)
    assert lib.zone_load(gdat) and lib.pal_load_color_ramps(gdat) and lib.text_init(gdat)
    lib.input_init()
    session = Session.in_dll(lib, "session")
    session.data_dir = str(data).encode()
    session.slot, session.loaded, session.new_game, session.restart, session.completed = -1, 0, 0, 0, 0
    main_loop_quit(lib).value = 0
    game = GameRecord.in_dll(lib, "game")
    # the state of the program start (the tests before leave theirs): no game, the start values
    from dm2test.scenario import Party, WorldGame
    ctypes.memset(ctypes.addressof(game), 0, GAME_SIZE)
    world = WorldGame.in_dll(lib, "game")
    world.leader, world.sound_volume, world.music_volume = 0xFFFF, 7, 7
    ctypes.c_uint32.in_dll(lib, "random_seed").value = 0
    party = Party.in_dll(lib, "party")
    ctypes.memset(ctypes.addressof(party), 0, ctypes.sizeof(party))
    party.cur_map, party.new_map, party.push_pending_group = -1, -1, 0xFFFF
    with Player(lib, [click_command(0, 0xD7)]):
        assert lib.session_init() == 1
    assert (session.new_game, session.loaded) == (1, 0)
    party_start = lib.dungeon_party_start()
    assert (game.party_map, game.party_x, game.party_y) == (0, party_start & 0x1F, party_start >> 5 & 0x1F)
    assert DialogState.in_dll(lib, "dialog").screen_mode == 0

    session.new_game = 0
    DialogState.in_dll(lib, "dialog").screen_mode = 1  # the program start state, reset on the load path
    with Player(lib, [click_command(0, 0xD9), click_slot(0), click_choice(2), click_choice(1)]):
        assert lib.session_init() == 1
    assert (session.new_game, session.loaded, session.slot) == (0, 1, 0)
    assert DialogState.in_dll(lib, "dialog").screen_mode == 0  # the game after "GAME LOADED"
    mine = (game.game_time - 1, game.party_map, game.party_x, game.party_y)
    path = tmp_path / "direct.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    assert mine == (game.game_time, game.party_map, game.party_x, game.party_y)  # a direct load of the same file


CHAMP_INIT_PARTY_STATE, DUN_LINK_THING_TO_SQUARE = 0x49D46, 0x1D3DB
G_LEADER, G_LEADER_HAND_ICON, G_CHAMPION_COUNT, G_CANDIDATE, G_INVENTORY = 0x759A4, 0x7FBCA, 0x7F276, 0x7F284, 0x7F972
G_INPUT_MODE, G_MESSAGE_EXPIRY = 0x72FED, 0x80404
CH_SLOTS = 0xC3


def test_new_game_champion_joins_like_original(lib, gdat, start, tmp_path):
    # champ_init_party_state of a new game: the champion of the mirror (sensor 0x7e) on square (0, 0) of map 0 joins at
    # once with the objects on that square, faces the party, and leads; no inventory, no mouth, no message. start.DAT
    # is the game after this join: both take the champion out of the party and put its objects back on the square.
    from test_champions import LeaderHand
    from test_input import Loop
    from test_interface import CHAMPION_SIZE, G_CHAMPIONS, champion_records, UiState
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    loop = Loop(lib, gdat, start, tmp_path)
    emu, game = loop.emu, loop.game
    assert (game.party_map, game.champion_count) == (0, 1)
    records = champion_records(lib)
    slots = [t for t in struct.unpack_from("<30H", bytes(records), CH_SLOTS) if t != 0xFFFF]
    assert slots
    for t in slots:
        assert lib.dungeon_link_thing_to_square(t | 2 << 14, 0, 0, 0) == 1
        emu.call(DUN_LINK_THING_TO_SQUARE, t | 2 << 14, 0, 0, 0)
    ctypes.memset(records, 0, 4 * CHAMPION_SIZE)
    emu.write(G_CHAMPIONS, bytes(4 * CHAMPION_SIZE))
    game.champion_count, game.leader = 0, 0xFFFF
    emu.write(G_CHAMPION_COUNT, bytes(2))
    emu.write(G_LEADER, struct.pack("<h", -1))
    Session.in_dll(lib, "session").new_game = 1
    emu.write(G_NEW_GAME, struct.pack("<H", 1))
    mode = lib.input_get_mode()
    assert mode == struct.unpack("<h", emu.read(G_INPUT_MODE, 2))[0]
    rows = lambda: (list((ctypes.c_uint32 * 3).in_dll(lib, "message_expiry")), emu.read(G_MESSAGE_EXPIRY, 12))
    before = rows()
    lib.champ_init_party_state()
    emu.call(CHAMP_INIT_PARTY_STATE)
    # the silent join keeps the input mode, requests no mouth sound, and prints no "joined" message
    assert lib.input_get_mode() == struct.unpack("<h", emu.read(G_INPUT_MODE, 2))[0] == mode
    assert rows() == before  # the message rows of each side (the original leaves them at 0 in the emulator)
    loop.flush_sounds()
    loop.assert_same_sounds("new game")
    ui = UiState.in_dll(lib, "ui")
    mine = (game.champion_count, game.leader, ui.candidate, ui.inventory)
    theirs = tuple(struct.unpack("<H", emu.read(addr, 2))[0] for addr in (G_CHAMPION_COUNT, G_LEADER, G_CANDIDATE,
                                                                         G_INVENTORY))
    assert mine == theirs == (1, 0, 0, 0)
    assert LeaderHand.in_dll(lib, "leader_hand").icon == emu.read(G_LEADER_HAND_ICON, 1)[0] == 0xFF
    assert sorted(t for t in struct.unpack_from("<30H", bytes(records), CH_SLOTS) if t != 0xFFFF) == sorted(slots)
    loop.assert_loop("new game")


CHAMP_APPLY_PENDING_DAMAGE, G_GAME_END_FLAG, G_PENDING_DAMAGE = 0x47113, 0x7F24C, 0x7FBAC


def test_dead_party_ends_the_game_like_original(play, lib, request):
    # The damage of a step kills the last champion: champ_kill sets the end flag in both (main_loop.end and
    # g_game_end_flag), drops the objects and the bones. Then session_end with that flag, as main.c and the original main
    # call it: the scream of the dead champion, the end dialog, QUIT GAME: the exit code of a dead party.
    sc = play
    emu = sc.emu
    from test_input import MainLoop
    loop = MainLoop.in_dll(lib, "main_loop")
    request.addfinalizer(lambda: setattr(loop, "end", 0))  # the next tests start with a running loop
    assert sc.game.champion_count == 1 and not loop.end
    damage = (ctypes.c_int16 * 4).in_dll(lib, "champ_pending_damage")
    damage[0] = 999
    emu.write(G_PENDING_DAMAGE, struct.pack("<h", 999))
    lib.champ_apply_pending_damage()
    emu.call(CHAMP_APPLY_PENDING_DAMAGE)
    assert loop.end == struct.unpack("<h", emu.read(G_GAME_END_FLAG, 2))[0] == 1
    sc.assert_same("death")
    sc.assert_equal("death")
    dialogs, quits = [], []
    emu.stub(UI_DIALOG_DRAW, lambda d, m, *_: dialogs.append((d & 0xFF, m & 0xFF)) or d & 0xFF)
    emu.stub(UI_DIALOG_RUN, lambda *_: 1)  # the text entry of QUIT GAME
    for addr in (SAVE_CHECK_DISK, WAIT_VBLANKS, GFX_FILL_ZONE_SCREEN, IOP_SET_VIDEO_STATE):
        emu.stub(addr, lambda *_: 0)

    def quit_(*_):
        quits.append(emu.read(G_QUIT_EXIT_CODE, 2))
        raise RuntimeError("session_quit")

    emu.stub(GAME_QUIT, quit_)
    emu.write_u32(G_END_OFFER_RESTORE, 1)
    Session.in_dll(lib, "session").offer_restore = 1
    state = DialogState.in_dll(lib, "dialog")
    state.id = -1
    with Player(lib, [click_choice(2)], every=300):
        lib.session_end(loop.end)
    assert state.id == 0x10
    with pytest.raises(EmuError, match="session_quit"):
        emu.call(GAME_END, struct.unpack("<h", emu.read(G_GAME_END_FLAG, 2))[0])
    assert dialogs == [(0x10, 0)] and quits == [b"\x01\x00"]
    assert (Session.in_dll(lib, "session").exit_code, main_loop_quit(lib).value) == (2, 1)
    sc.flush_sounds()
    sc.assert_same_sounds("scream")


G_LEADER_HAND, G_LEADER_HAND_WEIGHT, G_MAGIC_LIGHT = 0x7FBB4, 0x7FBCE, 0x7FFEC
HAND_OFFSET, MAGIC_LIGHT_OFFSET = 252 + 4 * 263, 252 + 4 * 263 + 2  # offsetof(Game, ...) in save.h


def test_load_restores_hand_and_timed_effects_like_original(lib, gdat, start, tmp_path):
    # A save with an object in the leader hand and timed effects (magic light, invisibility, aura, poison, an eaten
    # item): the load and the start of its session in both (save_load and play_start_session, as session_load and
    # session_init run them) give the same light, champions, and leader hand (object, weight, icon).
    from test_champions import LeaderHand
    from test_input import Loop
    from dm2test.save import Event, Game
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    path, bak = tmp_path / "SKSAVE0.DAT", tmp_path / "SKSAVE0.BAK"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    lib.draw_init(gdat)
    game = Game.in_dll(lib, "game")
    lib.champ_remove_object_from_slot.restype = ctypes.c_uint16
    hand = next(t for t in (lib.champ_remove_object_from_slot(0, s) for s in range(30)) if t != 0xFFFF)
    ctypes.c_uint16.from_address(ctypes.addressof(game) + HAND_OFFSET).value = hand
    events = [(0x46, 0, struct.pack("<h", 3)), (0x46, 0, struct.pack("<h", -5)), (0x47, 0, b""),
              (0x48, 1, struct.pack("<h", 7)), (0x4B, 0, struct.pack("<h", 30)), (0x0E, 0, bytes([10, 0, 34]))]
    for n, (type_, priority, data) in enumerate(events):
        event = Event(game.game_time + 50 + n | game.party_map << 24, type_, priority,
                      (ctypes.c_uint8 * 6)(*data.ljust(6, b"\0")))
        lib.timeline_add_event(ctypes.byref(event))
    assert lib.save_write(str(path).encode(), str(bak).encode(), b"GAME 0", gdat) == 0
    loop = Loop(lib, gdat, path.read_bytes(), tmp_path)
    emu = loop.emu
    light = ctypes.string_at(ctypes.addressof(game) + MAGIC_LIGHT_OFFSET, 6)
    assert light == emu.read(G_MAGIC_LIGHT, 6) and light[:2] != bytes(2)
    # the hand icon is not compared: only inv_update_hand_cursor sets it, for an object with the icon flag; the load
    # of the port clears it (champ_reset_step_state), the original keeps the value of the program start
    mine = (ctypes.c_uint16.from_address(ctypes.addressof(game) + HAND_OFFSET).value,
            LeaderHand.in_dll(lib, "leader_hand").weight)
    theirs = (struct.unpack("<H", emu.read(G_LEADER_HAND, 2))[0], struct.unpack("<H", emu.read(G_LEADER_HAND_WEIGHT, 2))[0])
    assert mine == theirs and mine[0] != 0xFFFF and mine[1]
    loop.assert_loop("load")


KEY_SAVE_MENU = 0x81F  # Alt+S


@pytest.mark.parametrize("actions", [[click_choice(4)], [click_choice(1), click_slot(2), click_choice(4), key(0x1E),
                                                        key(0x1C), click_choice(2)]], ids=["resume", "save"])
def test_save_menu_restores_the_game_screen(lib, gdat, start, tmp_path, actions):
    # build/dm2 --save start.DAT: Alt+S opens the save menu over the game; RESUME, or SAVE into slot 2, closes it. The
    # game screen outside the viewport is as before the menu (no dialog parts, the status boxes), and the forward key
    # moves the party.
    from test_input import KEY_FORWARD, run_ticks, session
    from test_view import Bitmap
    game, _ = session.__wrapped__(lib, gdat, start, tmp_path)
    (tmp_path / "DATA").mkdir()
    Session.in_dll(lib, "session").data_dir = str(tmp_path).encode()
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    screen = Bitmap.in_dll(lib, "draw_screen")
    view = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(7, view)

    def outside_viewport():
        rows = ctypes.string_at(screen.pixels, screen.width * screen.height)
        return [rows[y * screen.width:(y + 1) * screen.width] if not view[1] <= y < view[1] + view[3]
                else rows[y * screen.width:y * screen.width + view[0]] + rows[y * screen.width + view[0] + view[2]:(y + 1) * screen.width]
                for y in range(screen.height)]

    run_ticks(lib, 16)
    before = outside_viewport()
    with Player(lib, actions) as player:
        lib.input_key_down(KEY_SAVE_MENU)
        run_ticks(lib, 16)
    assert not player.actions
    assert (tmp_path / "DATA" / "SKSAVE2.DAT").exists() == (len(actions) > 1)
    run_ticks(lib, 16)
    assert outside_viewport() == before
    x, y = game.party_x, game.party_y
    lib.input_key_down(KEY_FORWARD)
    run_ticks(lib, 40)
    assert (game.party_x, game.party_y) != (x, y)


@pytest.mark.parametrize("held", ["eye", "mouth"])
def test_session_commands_wait_for_the_eye_and_the_mouth(lib, gdat, start, tmp_path, held):
    # The original runs sleep (0x8e) and the save menu (0x8c, Alt+S) only while neither the eye (0x7f224) nor the
    # mouth (0x7f21c) is held.
    from test_input import run_ticks, session
    from test_interface import UiState
    from dm2test.scenario import Command
    session.__wrapped__(lib, gdat, start, tmp_path)
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    run_ticks(lib, 16)
    setattr(UiState.in_dll(lib, "ui"), held, 1)
    with Player(lib, []) as player:
        lib.input_key_down(KEY_SAVE_MENU)
        run_ticks(lib, 16)
        lib.command_execute(ctypes.byref(Command(0, 0, 0x8E)))
    assert player.frames < player.every
    assert ctypes.c_int.in_dll(lib, "party_sleeping").value == 0
