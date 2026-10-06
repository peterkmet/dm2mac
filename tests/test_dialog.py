"""Step 20: the dialogs, the slot dialogs, the start menu picture, and the name editor against the original drawing in
dm2tools.emu (the harness of test_interface.py)."""

import ctypes
import struct

import pytest
from unicorn import UC_PROT_ALL

from test_interface import (  # noqa: F401 (gdat, original, and port are fixtures)
    SCREEN_H, SCREEN_W, VGA, VIEW_W, c_screen, c_view, diff, gdat, original, port,
)

UI_DIALOG_DRAW, UI_DIALOG_PRESENT, UI_DIALOG_RUN, UI_VOLUME_DIALOG = 0x1A37D, 0x1A001, 0x1A0A4, 0x1AA75
SAVE_OPEN_SLOT_DIALOG, SAVE_DRAW_SLOT_SELECTION, SAVE_DRAW_NAME_ENTRY = 0x37AB0, 0x37D9F, 0x37C33
MENU_DRAW_TITLE = 0x3867B
WAIT_VBLANKS, FILE_OPEN, FILE_CLOSE, FILE_SEEK, KBD_KEY_AVAILABLE = 0x1C660, 0x10F7E, 0x10F9E, 0x10F5D, 0x114B8
INPUT_POP_MODE, IOP_SET_VIDEO_STATE, MEM_FREE_PICTURE, G_MOUSE_HIDE_COUNT = 0x2075D, 0x138C7, 0x3BCC6, 0x71558
G_DIALOG_SCREEN_MODE, G_SCREEN_BITMAP, G_DISK_PATH_KIND, G_DATA_PATH, G_SAVE_PATH = 0x717BC, 0x71550, 0x7F98C, 0x70536, 0x7053A
G_SOUND_VOLUME, G_MUSIC_VOLUME, G_TITLE_PICTURE, G_TITLE_PALETTE, G_MENU_PICTURE = 0x7169C, 0x7169E, 0x7F950, 0x756F0, 0x7F94C
G_SAVE_SLOTS, G_INV_PANEL_MODE, G_FORMAT_BUFFERS = 0x7F8F0, 0x756F8, 0x7F978
G_GDAT_LOADED, G_IMAGE_CACHE_ENABLED = 0x7FA84, 0x7FA6C
DRIVE_PATH = b"GAME"
DIALOGS = [(0x1B, 0), (0x0C, 0), (0x0D, 0), (0x0E, 0), (0x10, 0), (0x06, 0), (0x00, 0x1B), (0x00, 0x1F), (0x00, 0x1C),
           (0x11, 0), (0x12, 0), (0x0E, 0x1F)]


class DialogState(ctypes.Structure):
    _fields_ = [("screen_mode", ctypes.c_int), ("button_count", ctypes.c_int), ("button_category", ctypes.c_uint8),
                ("button_index", ctypes.c_uint8), ("button_entry", ctypes.c_uint8), ("progress_enabled", ctypes.c_int),
                ("progress_width", ctypes.c_int16)]


class Session(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ("new_game", "use_dungenb", "loaded", "completed", "restart", "offer_restore",
                                            "quit_code", "slot")] + [
        ("load_time", ctypes.c_uint32), ("save_time", ctypes.c_uint32), ("exit_code", ctypes.c_int),
        ("save_name", ctypes.c_char * 1024), ("data_dir", ctypes.c_char * 1024)]


@pytest.fixture()
def ui(port, original):
    """Both with a cleared screen and viewport, the drive path GAME, and the screen bitmap of the original at VGA."""
    emu = original.emu
    if not getattr(original, "dialog_stubs", False):  # once: a second stub of an address would return twice
        original.dialog_stubs = True
        emu.uc.mem_map(VGA - 0x1000, 0x1000, UC_PROT_ALL)
        for addr in (WAIT_VBLANKS, KBD_KEY_AVAILABLE, IOP_SET_VIDEO_STATE, MEM_FREE_PICTURE, FILE_CLOSE):
            emu.stub(addr, lambda *_: 0)
    emu.write(VGA - 6, struct.pack("<BBhh", 8, 0, SCREEN_W, SCREEN_H))
    emu.write_u32(G_SCREEN_BITMAP, VGA)
    emu.write(G_MOUSE_HIDE_COUNT, bytes(2))
    emu.write_u32(G_GDAT_LOADED, 1)  # the dialogs draw their GDAT pictures
    emu.write_u32(G_IMAGE_CACHE_ENABLED, 1)
    path = original.put(DRIVE_PATH + b"\0")
    emu.write_u32(G_DATA_PATH, path)
    emu.write_u32(G_SAVE_PATH, path)
    emu.write(G_DISK_PATH_KIND + 2, struct.pack("<H", 2))
    ctypes.memmove(ctypes.addressof(ctypes.c_char.in_dll(port, "text_drive_path")), DRIVE_PATH + b"\0", len(DRIVE_PATH) + 1)
    return port


def set_screen_mode(lib, emu, mode):
    DialogState.in_dll(lib, "dialog").screen_mode = mode
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
            assert lib.ui_dialog_draw(dialog, message) == emu.call(UI_DIALOG_DRAW, dialog, message) & 0xFF
            assert len(set(original.view())) > 2
            assert_same(lib, original, (mode, hex(dialog), hex(message)), screen=mode == 1)


def test_progress_bar_matches_original(ui, original):
    lib, emu = ui, original.emu
    set_screen_mode(lib, emu, 1)
    lib.ui_dialog_draw(0x0E, 0)
    emu.call(UI_DIALOG_DRAW, 0x0E, 0)
    DialogState.in_dll(lib, "dialog").progress_enabled = 1
    emu.write_u32(0x7F1FC, 1)
    for permille in (0, 1, 120, 500, 999, 1000):
        lib.ui_draw_progress_bar(permille)
        emu.call(0x1AA09, permille)
        assert_same(lib, original, permille)


def test_volume_dialog_matches_original(ui, original):
    # The volume dialog with each pair of settings; the dialog closes at once (the OK button).
    lib, emu = ui, original.emu
    emu.stub(UI_DIALOG_RUN, lambda *_: 0xDB)
    frame = ctypes.CFUNCTYPE(None)(lambda: close_dialog(lib))
    ctypes.c_void_p.in_dll(lib, "game_frame_hook").value = ctypes.cast(frame, ctypes.c_void_p).value
    try:
        from test_move import WorldGame
        game = WorldGame.in_dll(lib, "game")
        for sound, music in ((7, 7), (0, 3), (5, 0)):
            set_screen_mode(lib, emu, 1)
            game.sound_volume, game.music_volume = sound, music
            emu.write(G_SOUND_VOLUME, struct.pack("<hh", sound, music))
            lib.ui_volume_dialog()
            emu.call(UI_VOLUME_DIALOG)
            assert_same(lib, original, (sound, music))
    finally:
        ctypes.c_void_p.in_dll(lib, "game_frame_hook").value = None


def close_dialog(lib):
    """A frame hook: the player clicks the first button of the open dialog."""
    import test_dialog
    match = test_dialog.InputMatch.in_dll(lib, "input_match")
    match.choice, match.value = 1, 0xDB


class InputMatch(ctypes.Structure):
    _fields_ = [("zone", ctypes.c_uint16), ("origin", ctypes.c_int16), ("key", ctypes.c_uint16), ("choice", ctypes.c_int16),
                ("x", ctypes.c_int16), ("y", ctypes.c_int16), ("value", ctypes.c_uint16), ("rect", ctypes.c_int16 * 4)]


def test_slot_dialogs_match_original(ui, original, tmp_path):
    # The load and save slot dialogs: the slot list (slots 0 and 3 used, the others "n UNUSED") with each slot chosen,
    # and the name field with and without the cursor.
    lib, emu = ui, original.emu
    # the original reads the slot headers in order from the served file (start.DAT from byte 0): give the C port the
    # same headers
    from test_save import START
    start = START.read_bytes()
    (tmp_path / "DATA").mkdir()
    for n, slot in enumerate((0, 3)):
        (tmp_path / "DATA" / f"SKSAVE{slot}.DAT").write_bytes(start[0x2A * n:0x2A * (n + 1)])
    Session.in_dll(lib, "session").data_dir = str(tmp_path).encode()
    for i in range(2):  # the file names go through the 2 format buffers of text_format
        emu.write_u32(G_FORMAT_BUFFERS + 4 * i, original.alloc(128))
    names = []

    def file_open(name, *_):
        text = emu.read(name, 32).split(b"\0")[0]
        names.append(text)
        return 5 if text[-5:-4] in (b"0", b"3") else -1

    emu.stub(FILE_OPEN, file_open)
    for dialog, buttons in ((0x80, 5), (0x81, 6)):
        for chosen in (-1, 0, 3, 7, 9):
            set_screen_mode(lib, emu, 1)
            emu.call(FILE_SEEK, 0, 0)
            lib.save_open_slot_dialog(dialog, buttons)
            emu.call(SAVE_OPEN_SLOT_DIALOG, dialog, buttons)
            lib.save_draw_slot_selection(chosen)
            emu.call(SAVE_DRAW_SLOT_SELECTION, chosen)
            lib.ui_dialog_present()
            emu.call(UI_DIALOG_PRESENT)
            assert_same(lib, original, (dialog, chosen))
            lib.save_close_slot_dialog()
            emu.call(INPUT_POP_MODE)
    assert len(names) == 100
    for name, cursor in ((b"GAME 4", 0), (b"ABC", 1), (b"", 1), (b"A LONG NAME OF THIRTY ONE CHARS", 1)):
        buf = original.put(name + b"\0")
        lib.save_draw_name_entry(name, cursor)
        emu.call(SAVE_DRAW_NAME_ENTRY, buf, cursor)
        assert_same(lib, original, (name, cursor))


def test_menu_pictures_match_original(ui, original):
    # The title of the start menu and the picture of command 0xda: GDAT (5, 0, 1, 4) and (5, 0, 1, 1) with their
    # colors on the screen.
    lib, emu = ui, original.emu
    title = original.image(original.image_item(5, 0, 4))
    emu.write_u32(G_TITLE_PICTURE, title)
    emu.write_u32(G_TITLE_PALETTE, original.image_palette(5, 0, 4))
    lib.menu_draw_title()
    emu.call(MENU_DRAW_TITLE)
    mine, theirs = c_screen(lib), original.screen()
    assert len(set(theirs)) > 8
    assert mine == theirs, diff(mine, theirs, SCREEN_W)
