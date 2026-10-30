"""Differential tests of the dialogs, the slot dialogs, the start menu picture, and the name editor against the original drawing in
dm2tools.emu (the harness of dm2test.interface)."""

import ctypes
import struct

import pytest
from unicorn import UC_PROT_ALL

from dm2test import constants as C
from dm2test.dialog import (
    G_DATA_PATH, G_DIALOG_SCREEN_MODE, G_DISK_PATH_KIND, G_SAVE_PATH, G_SCREEN_BITMAP, DialogState, InputMatch,
    main_loop_quit,
)
from dm2test.structs import GdatKey, Session
from dm2test.interface import (
    SCREEN_H, SCREEN_W, VIEW_W, c_screen, c_view, diff,
)
from dm2test.addresses import VGA

pytestmark = pytest.mark.usefixtures("program_start_after")


UI_DIALOG_DRAW, UI_DIALOG_PRESENT, UI_DIALOG_RUN, UI_VOLUME_DIALOG = 0x1A37D, 0x1A001, 0x1A0A4, 0x1AA75
SAVE_OPEN_SLOT_DIALOG, SAVE_DRAW_SLOT_SELECTION, SAVE_DRAW_NAME_ENTRY = 0x37AB0, 0x37D9F, 0x37C33
MENU_DRAW_TITLE = 0x3867B
WAIT_VBLANKS, FILE_OPEN, FILE_CLOSE, FILE_SEEK, KBD_KEY_AVAILABLE = 0x1C660, 0x10F7E, 0x10F9E, 0x10F5D, 0x114B8
INPUT_POP_MODE, IOP_SET_VIDEO_STATE, MEM_FREE_PICTURE, G_MOUSE_HIDE_COUNT = 0x2075D, 0x138C7, 0x3BCC6, 0x71558
G_SOUND_VOLUME, G_MUSIC_VOLUME, G_TITLE_PICTURE, G_TITLE_PALETTE, G_MENU_PICTURE = 0x7169C, 0x7169E, 0x7F950, 0x756F0, 0x7F94C
G_SAVE_SLOTS, G_INV_PANEL_MODE, G_FORMAT_BUFFERS = 0x7F8F0, 0x756F8, 0x7F978
G_GDAT_LOADED, G_IMAGE_CACHE_ENABLED = 0x7FA84, 0x7FA6C
DRIVE_PATH = b"GAME"
DIALOGS = [(C.DIALOG_SAVE_MENU, 0), (C.DIALOG_NOT_SAVED, 0), (C.DIALOG_SAVING, 0), (C.DIALOG_LOADING, 0), (C.DIALOG_THE_END, 0),
           (C.DIALOG_LOADED, 0), (C.DIALOG_ERROR, C.MESSAGE_SAVE_FAILED), (C.DIALOG_ERROR, C.MESSAGE_GAME_DAMAGED),
           (C.DIALOG_ERROR, 0x1C), (0x11, 0), (0x12, 0), (C.DIALOG_LOADING, C.MESSAGE_GAME_DAMAGED)]


@pytest.fixture()
def ui(port, original):
    """Both with a cleared screen and viewport, the drive path GAME, and the screen bitmap of the original at VGA."""
    emu = original.emu
    if not getattr(original, "dialog_stubs", False):  # once: a second stub of an address would return twice
        original.dialog_stubs = True
        emu.uc.mem_map(VGA - 0x1000, 0x1000, UC_PROT_ALL)
        for addr in (WAIT_VBLANKS, IOP_SET_VIDEO_STATE, MEM_FREE_PICTURE, FILE_CLOSE):
            emu.stub(addr, lambda *_: 0)
    stub(original, KBD_KEY_AVAILABLE, lambda *_: 0)
    emu.write(VGA - 6, struct.pack("<BBhh", 8, 0, SCREEN_W, SCREEN_H))
    emu.write_u32(G_SCREEN_BITMAP, VGA)
    emu.write(G_MOUSE_HIDE_COUNT, bytes(2))
    emu.write_u32(G_GDAT_LOADED, 1)  # the dialogs draw their GDAT pictures
    emu.write_u32(G_IMAGE_CACHE_ENABLED, 1)
    path = original.put(DRIVE_PATH + b"\0")
    emu.write_u32(G_DATA_PATH, path)
    emu.write_u32(G_SAVE_PATH, path)
    emu.write(G_DISK_PATH_KIND + 2, struct.pack("<H", 2))
    # the port gets a --data path; the dialogs show its last folder in capitals (the font has no lowercase letters)
    Session.in_dll(port, "g_session").data_dir = b"/some/dir/" + DRIVE_PATH.lower() + b"/"
    return port


def stub(original, addr, fn):
    """Runs fn instead of the original function at addr; a second call replaces fn (a second hook would return
    twice)."""
    fns = original.__dict__.setdefault("dialog_fns", {})
    if addr not in fns:
        original.emu.stub(addr, lambda *args: fns[addr](*args))
    fns[addr] = fn


def set_screen_mode(lib, emu, mode):
    DialogState.in_dll(lib, "g_dialog").screen_mode = mode
    emu.write_u32(G_DIALOG_SCREEN_MODE, mode)


def assert_same(lib, original, what, screen=True):
    mine, theirs = c_view(lib), original.view()
    assert mine == theirs, (what, "view", diff(mine, theirs, VIEW_W))
    if screen:
        mine, theirs = c_screen(lib), original.screen()
        assert mine == theirs, (what, "screen", diff(mine, theirs, SCREEN_W))


def test_dialogs_match_original(ui, original):
    # Every dialog of the game with and without its error message, in both screen modes: the picture, the version,
    # the button texts in their zones, and the wrapped and centered message (with the drive path code).
    lib, emu = ui, original.emu
    for mode in (1, 0):
        for dialog, message in DIALOGS:
            set_screen_mode(lib, emu, mode)
            assert lib.dialog_draw(dialog, message) == emu.call(UI_DIALOG_DRAW, dialog, message) & 0xFF
            assert len(set(original.view())) > 2
            assert_same(lib, original, (mode, hex(dialog), hex(message)), screen=mode == 1)


def test_progress_bar_matches_original(ui, original):
    lib, emu = ui, original.emu
    set_screen_mode(lib, emu, 1)
    lib.dialog_draw(C.DIALOG_LOADING, 0)
    emu.call(UI_DIALOG_DRAW, C.DIALOG_LOADING, 0)
    DialogState.in_dll(lib, "g_dialog").progress_enabled = 1
    emu.write_u32(0x7F1FC, 1)
    for permille in (0, 1, 120, 500, 999, 1000):
        lib.dialog_draw_progress_bar(permille)
        emu.call(0x1AA09, permille)
        assert_same(lib, original, permille)


def test_volume_dialog_matches_original(ui, original):
    # The volume dialog with each pair of settings; the dialog closes at once (the OK button).
    lib, emu = ui, original.emu
    run = emu.stub(UI_DIALOG_RUN, lambda *_: C.CMD_DIALOG_BUTTON_FIRST)
    frame = ctypes.CFUNCTYPE(None)(lambda: close_dialog(lib))
    ctypes.c_void_p.in_dll(lib, "g_play_frame_hook").value = ctypes.cast(frame, ctypes.c_void_p).value
    try:
        from dm2test.structs import Game
        game = Game.in_dll(lib, "g_game")
        for sound, music in ((7, 7), (0, 3), (5, 0)):
            set_screen_mode(lib, emu, 1)
            game.sound_volume, game.music_volume = sound, music
            emu.write(G_SOUND_VOLUME, struct.pack("<hh", sound, music))
            lib.dialog_volume()
            emu.call(UI_VOLUME_DIALOG)
            assert_same(lib, original, (sound, music))
    finally:
        ctypes.c_void_p.in_dll(lib, "g_play_frame_hook").value = None
        emu.uc.hook_del(run)


def close_dialog(lib):
    """A frame hook: the player clicks the first button of the open dialog."""
    match = InputMatch.in_dll(lib, "g_input_match")
    match.choice, match.value = 1, C.CMD_DIALOG_BUTTON_FIRST


def test_slot_dialogs_match_original(ui, original, tmp_path):
    # The load and save slot dialogs: the slot list (slots 0 and 3 used, the others "n UNUSED") with each slot chosen,
    # and the name field with and without the cursor.
    lib, emu = ui, original.emu
    # the original reads the slot headers in order from the served file (start.DAT from byte 0): give the C port the
    # same headers
    from dm2test.save import START
    start = START.read_bytes()
    (tmp_path / "DATA").mkdir()
    for n, slot in enumerate((0, 3)):
        (tmp_path / "DATA" / f"SKSAVE{slot}.DAT").write_bytes(start[C.SAVE_HEADER_SIZE * n:C.SAVE_HEADER_SIZE * (n + 1)])
    Session.in_dll(lib, "g_session").data_dir = str(tmp_path).encode()
    for i in range(2):  # the file names go through the 2 format buffers of gtext_format
        emu.write_u32(G_FORMAT_BUFFERS + 4 * i, original.alloc(128))
    names = []

    def file_open(name, *_):
        text = emu.read(name, 32).split(b"\0")[0]
        names.append(text)
        return 5 if text[-5:-4] in (b"0", b"3") else -1

    stub(original, FILE_OPEN, file_open)
    for dialog, buttons in ((C.DIALOG_LOAD_SLOTS, 5), (C.DIALOG_SAVE_SLOTS, 6)):
        for chosen in (-1, 0, 3, 7, 9):
            set_screen_mode(lib, emu, 1)
            emu.call(FILE_SEEK, 0, 0)
            lib.session_open_slot_dialog(dialog, buttons)
            emu.call(SAVE_OPEN_SLOT_DIALOG, dialog, buttons)
            lib.session_draw_slot_selection(chosen)
            emu.call(SAVE_DRAW_SLOT_SELECTION, chosen)
            lib.dialog_present()
            emu.call(UI_DIALOG_PRESENT)
            assert_same(lib, original, (dialog, chosen))
            lib.session_close_slot_dialog()
            emu.call(INPUT_POP_MODE)
    assert len(names) == 100
    for name, cursor in ((b"GAME 4", 0), (b"ABC", 1), (b"", 1), (b"A LONG NAME OF THIRTY ONE CHARS", 1)):
        buf = original.put(name + b"\0")
        lib.session_draw_name_entry(name, cursor)
        emu.call(SAVE_DRAW_NAME_ENTRY, buf, cursor)
        assert_same(lib, original, (name, cursor))


def test_menu_pictures_match_original(ui, original):
    # The title of the start menu and the picture of command 0xda: GDAT (5, 0, 1, 4) and (5, 0, 1, 1) with their
    # colors on the screen.
    lib, emu = ui, original.emu
    title = original.image(original.image_item(C.CAT_MENU, 0, 4))
    emu.write_u32(G_TITLE_PICTURE, title)
    emu.write_u32(G_TITLE_PALETTE, original.image_palette(C.CAT_MENU, 0, 4))
    lib.menu_draw_title()
    emu.call(MENU_DRAW_TITLE)
    mine, theirs = c_screen(lib), original.screen()
    assert len(set(theirs)) > 8
    assert mine == theirs, diff(mine, theirs, SCREEN_W)


CHAMP_EDIT_NAME, COMMAND_PROCESS_QUEUE, CHAMP_DRAW_STATE, UI_HIDE_GAME_VIEW, UI_SHOW_GAME_VIEW = (
    0x496E5, 0x224A9, 0x48890, 0x54E41, 0x54EAC)
KBD_KEY_TO_ASCII = 0x114C5
G_INVENTORY_ORDINAL, G_DIALOG_VALUE, G_DIALOG_KEY, INPUT_PUSH_MODE = 0x7F972, 0x7F526, 0x7F51A, 0x2073F
NAME_FIELDS_SIZE = 28  # the name (8 bytes) and the title (20 bytes) at the start of a champion record
KEY_ESCAPE = 0x01  # the cancel key of the name editor
# Typed keys: X, backspace, Q, 1 (no name character), space, Enter (to the title), 3 backspaces, Z, Tab (back to the
# name), Enter (to the title), Enter (done).
NAME_KEYS = [0x2D, C.KEY_BACKSPACE, 0x10, 0x02, 0x39, C.KEY_ENTER, C.KEY_BACKSPACE, C.KEY_BACKSPACE, C.KEY_BACKSPACE, 0x2C, C.KEY_TAB,
             C.KEY_ENTER, C.KEY_ENTER]


def run_name_editor(lib, original, keys, fields=None):
    """Runs the name editor of champion 0 in both with the typed keys (fields: the 28 bytes of name and title to start
    with). Returns the views after each key of both, the record bytes before, and the record bytes after of both."""
    from dm2test.addresses import G_CHAMPIONS
    from dm2test.scenario import champion_records
    from dm2test.structs import UiState
    emu = original.emu
    lib.input_init()
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    lib.input_key_command.restype = ctypes.c_uint16
    rect = (ctypes.c_int16 * 4)()
    commands = [lib.input_key_command(C.INPUT_MODE_NAME, k, rect) for k in keys]
    assert all(commands), commands
    if fields is not None:
        champion_records(lib)[:NAME_FIELDS_SIZE] = list(fields)
        emu.write(G_CHAMPIONS, fields)
    UiState.in_dll(lib, "g_ui").inventory = 1
    emu.write(G_INVENTORY_ORDINAL, struct.pack("<h", 1))
    queue = list(keys)
    theirs, mine = [], []

    def process(*_):
        theirs.append(original.view())
        if queue:
            emu.write(G_DIALOG_VALUE, struct.pack("<H", commands[len(keys) - len(queue)]))
            emu.write(G_DIALOG_KEY, struct.pack("<H", queue.pop(0)))

    for addr, fn in ((COMMAND_PROCESS_QUEUE, process), (INPUT_PUSH_MODE, None), (INPUT_POP_MODE, None),
                     (UI_HIDE_GAME_VIEW, None), (UI_SHOW_GAME_VIEW, None), (CHAMP_DRAW_STATE, None),
                     (KBD_KEY_TO_ASCII, lambda k, *_: lib.kbd_key_to_ascii(k & 0xFFFF) & 0xFF)):  # IBMIOP service 0x1b
        stub(original, addr, fn or (lambda *_: 0))
    emu.call(CHAMP_EDIT_NAME, G_CHAMPIONS)
    port_keys = list(keys)

    def frame():
        mine.append(c_view(lib))
        if port_keys:
            lib.input_key_down(port_keys.pop(0))

    hook = ctypes.CFUNCTYPE(None)(frame)
    ctypes.c_void_p.in_dll(lib, "g_play_frame_hook").value = ctypes.cast(hook, ctypes.c_void_p).value
    try:
        before = bytes(champion_records(lib))[:NAME_FIELDS_SIZE]
        lib.session_edit_champion_name.argtypes = [ctypes.c_int16]
        lib.session_edit_champion_name(0)
    finally:
        ctypes.c_void_p.in_dll(lib, "g_play_frame_hook").value = None
    after = bytes(champion_records(lib))[:NAME_FIELDS_SIZE]
    return mine, theirs, before, after, emu.read(G_CHAMPIONS, NAME_FIELDS_SIZE)


def test_champion_name_editor_matches_original(ui, original):
    # The name editor of the inventory: the name and title fields with the cursor after each key, and the new name and
    # title of the champion.
    mine, theirs, before, after, original_after = run_name_editor(ui, original, NAME_KEYS)
    assert after == original_after
    assert after != before
    assert len(mine) == len(theirs) == len(NAME_KEYS)
    for i, (a, b) in enumerate(zip(mine, theirs)):
        assert a == b, (i, diff(a, b, VIEW_W))


# A name and a title with bytes after their end marks; typed keys: D, E, F, G, Enter (to the title), 2 backspaces, Z,
# Escape (cancel).
CANCEL_FIELDS = b"ABC\0WXYZ" + b"KNIGHT\0QRSTUVWXYZABC"
CANCEL_KEYS = [0x20, 0x12, 0x21, 0x22, C.KEY_ENTER, C.KEY_BACKSPACE, C.KEY_BACKSPACE, 0x2C, KEY_ESCAPE]


def test_champion_name_cancel_restores_all_bytes(ui, original):
    # Regression: a cancel of the name editor gives back the whole name and title fields, the bytes after the end
    # marks too, as the original does.
    mine, theirs, before, after, original_after = run_name_editor(ui, original, CANCEL_KEYS, CANCEL_FIELDS)
    assert before == CANCEL_FIELDS
    assert original_after == CANCEL_FIELDS
    assert after == original_after


GAME_END, GAME_QUIT, SAVE_CHECK_DISK = 0x1FEF6, 0x2005B, 0x346D1


def test_quit_picture_matches_original(ui, original):
    # The end after the QUIT button of the save menu: the picture GDAT (6, 0, 1, 1) until a click, then the end.
    lib, emu = ui, original.emu
    quits = []

    def quit_(*_):
        quits.append(1)
        raise RuntimeError("session_quit")

    for addr, fn in ((SAVE_CHECK_DISK, None), (GAME_QUIT, quit_),
                     (COMMAND_PROCESS_QUEUE, lambda *_: emu.write(G_DIALOG_VALUE, struct.pack("<H", C.CMD_PICTURE_CLICK)))):
        stub(original, addr, fn or (lambda *_: 0))
    from dm2tools.emu import EmuError
    with pytest.raises(EmuError, match="session_quit"):
        emu.call(GAME_END, 0)
    hook = ctypes.CFUNCTYPE(None)(lambda: setattr(InputMatch.in_dll(lib, "g_input_match"), "value", C.CMD_PICTURE_CLICK))
    ctypes.c_void_p.in_dll(lib, "g_play_frame_hook").value = ctypes.cast(hook, ctypes.c_void_p).value
    session = Session.in_dll(lib, "g_session")
    session.restart = session.quit_code = session.completed = 0
    try:
        lib.session_end(0)
    finally:
        ctypes.c_void_p.in_dll(lib, "g_play_frame_hook").value = None
    assert quits == [1] and main_loop_quit(lib).value == 1 and session.exit_code == 0
    main_loop_quit(lib).value = 0
    mine, theirs = c_screen(lib), original.screen()
    assert len(set(theirs)) > 8
    assert mine == theirs, diff(mine, theirs, SCREEN_W)


UI_DIALOG_DRAW_BUTTON, G_MATCH_RECT, G_MATCH_ORIGIN, G_BUTTON_PICTURE = 0x1A270, 0x7F46C, 0x7F524, 0x7F202


def test_pressed_buttons_match_original(ui, original):
    # The click feedback of a dialog button (opcodes 0x0f and 0x10: GDAT (0x1a, 0xfe, 1, 0 or 1)) over each button of
    # the save menu and of the end dialog.
    from dm2test.dialog import find_point
    lib, emu = ui, original.emu
    lib.input_init()
    lib.dialog_draw_button.argtypes = [ctypes.c_bool, GdatKey]
    match = InputMatch.in_dll(lib, "g_input_match")
    state = DialogState.in_dll(lib, "g_dialog")
    for dialog, buttons in ((C.DIALOG_SAVE_MENU, 4), (C.DIALOG_THE_END, 2)):
        for choice in range(1, buttons + 1):
            for entry in (0, 1):
                set_screen_mode(lib, emu, 1)
                lib.dialog_draw(dialog, 0)
                emu.call(UI_DIALOG_DRAW, dialog, 0)
                state.button_count = buttons
                find_point(lib, C.INPUT_MODE_DIALOG, lambda m: m.choice == choice)
                emu.write(G_MATCH_RECT, bytes(match.rect))
                emu.write(G_MATCH_ORIGIN, struct.pack("<h", match.origin))
                emu.write(G_BUTTON_PICTURE, bytes([C.CAT_DIALOG, 0xFE, entry]))
                lib.dialog_draw_button(True, GdatKey(C.CAT_DIALOG, 0xFE, entry))
                emu.call(UI_DIALOG_DRAW_BUTTON, match.zone, 1)
                assert_same(lib, original, (dialog, choice, entry), screen=False)


G_DIALOG_BUTTON_COUNT, G_CLICK_FEEDBACK_SCRIPTS = 0x7F200, 0x72EF9
INPUT_GET_CLICK_FEEDBACK_SCRIPT, COMMAND_RECORD_SIZE = 0x20EE0, 14


def feedback_scripts(lib, original, command):
    """The click feedback script of command in both: the port pointer and the offset of the original in its table."""
    from dm2test.scenario import Command
    lib.input_click_feedback_script.restype = ctypes.c_void_p
    lib.input_click_feedback_script.argtypes = [ctypes.POINTER(Command)]
    record = original.alloc(COMMAND_RECORD_SIZE)
    original.emu.write(record, bytes(Command(0, 0, command)))
    return lib.input_click_feedback_script(Command(0, 0, command)), original.emu.call(INPUT_GET_CLICK_FEEDBACK_SCRIPT, record) - G_CLICK_FEEDBACK_SCRIPTS


def test_dialog_button_scripts_match_original(port, original):
    # The click feedback script of each dialog button command for each button count 0 to 8: the offset in the
    # scripts table equals the one of the original (0xe3, 0xdc, or the execute script).
    lib, emu = port, original.emu
    state = DialogState.in_dll(lib, "g_dialog")
    base, queue = feedback_scripts(lib, original, C.CMD_PAUSE)  # the queue script: offset 0 in both
    assert queue == 0
    seen = set()
    try:
        for count in range(9):
            state.button_count = count
            emu.write(G_DIALOG_BUTTON_COUNT, struct.pack("<h", count))
            for command in range(C.CMD_DIALOG_BUTTON_FIRST, C.CMD_DIALOG_BUTTON_LAST + 1):
                mine, theirs = feedback_scripts(lib, original, command)
                assert mine - base == theirs, (count, hex(command), hex(mine - base), hex(theirs))
                seen.add(theirs)
    finally:
        state.button_count = 0
        emu.write(G_DIALOG_BUTTON_COUNT, bytes(2))
    assert seen == {1, 0xDC, 0xE3}


def dialog_button_count(lib, ui_gdat, dialog):
    """The buttons of dialog: its text entries, as dialog_run counts them."""
    text = ctypes.create_string_buffer(128)
    lib.gdat_text.argtypes = [ctypes.c_void_p, GdatKey, ctypes.c_char_p, ctypes.c_size_t]
    return sum(1 for e in range(C.DIALOG_TEXT_COUNT) if lib.gdat_text(ui_gdat, GdatKey(C.CAT_DIALOG, dialog, e), text, 128) and text.value)


def test_pressed_button_feedback_matches_original(ui, original, ui_gdat):
    # A click on each button of each dialog with buttons: the click feedback script of the button count and the
    # command draws the pressed button picture; the screens are equal after each tick of the script.
    from dm2test.input import click_feedback, find_click
    lib, emu = ui, original.emu
    lib.input_init()
    match = InputMatch.in_dll(lib, "g_input_match")
    state = DialogState.in_dll(lib, "g_dialog")
    counts = {}
    try:
        for dialog in dict.fromkeys(d for d, _ in DIALOGS):
            count = counts[dialog] = dialog_button_count(lib, ui_gdat, dialog)
            for command in range(C.CMD_DIALOG_BUTTON_FIRST, C.CMD_DIALOG_BUTTON_FIRST + count):
                set_screen_mode(lib, emu, 1)
                lib.dialog_draw(dialog, 0)
                emu.call(UI_DIALOG_DRAW, dialog, 0)
                state.button_count = count
                emu.write(G_DIALOG_BUTTON_COUNT, struct.pack("<h", count))
                click = find_click(lib, C.INPUT_MODE_DIALOG, command)  # it leaves the match of the click
                emu.write(G_MATCH_RECT, bytes(match.rect))
                emu.write(G_MATCH_ORIGIN, struct.pack("<h", match.origin))
                click_feedback(lib, original, command, (hex(dialog), count), C.INPUT_MODE_DIALOG, {command: click})
    finally:
        emu.write(G_DIALOG_BUTTON_COUNT, bytes(2))
    assert {1, 2, 3, 4} <= set(counts.values()), counts


KBD_READ_KEY, INPUT_SIMULATE_COMMAND_CLICK, G_DIALOG_CHOICE = 0x11413, 0x207F1, 0x7F51C


def test_enter_presses_first_button_like_original(ui, original):
    # Enter in dialog 0x0c (OK, CANCEL): the original clicks command 0xdb, the first button; both return its value.
    lib, emu = ui, original.emu
    keys, clicks = [C.KEY_ENTER], []
    stub(original, KBD_KEY_AVAILABLE, lambda *_: int(bool(keys)))
    stub(original, KBD_READ_KEY, lambda *_: keys.pop(0))
    stub(original, INPUT_SIMULATE_COMMAND_CLICK, lambda command, *_: clicks.append(command & 0xFFFF))
    # the command queue: the dialog button command 0xdb + n chooses button n + 1
    stub(original, COMMAND_PROCESS_QUEUE,
         lambda *_: clicks and emu.write(G_DIALOG_CHOICE, struct.pack("<H", clicks[-1] - (C.CMD_DIALOG_BUTTON_FIRST - 1))))
    for addr in (INPUT_PUSH_MODE, INPUT_POP_MODE):
        stub(original, addr, lambda *_: 0)
    set_screen_mode(lib, emu, 1)
    theirs = emu.call(UI_DIALOG_RUN, emu.call(UI_DIALOG_DRAW, C.DIALOG_NOT_SAVED, 0) & 0xFF) & 0xFFFF
    assert clicks == [C.CMD_DIALOG_BUTTON_FIRST]

    lib.input_init()
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    frames = []

    def frame():
        frames.append(1)
        if len(frames) == 1:
            lib.input_key_down(C.KEY_ENTER)
        if len(frames) > 100:  # no choice: the program ends, the test fails
            main_loop_quit(lib).value = 1

    hook = ctypes.CFUNCTYPE(None)(frame)
    ctypes.c_void_p.in_dll(lib, "g_play_frame_hook").value = ctypes.cast(hook, ctypes.c_void_p).value
    set_screen_mode(lib, emu, 1)
    assert lib.dialog_run(lib.dialog_draw(C.DIALOG_NOT_SAVED, 0)) == theirs == 1
    assert len(frames) <= 100
