"""Step 13: text drawing, the interface panels, and the message area against the original code in dm2tools.emu."""
import ctypes
import random
import struct

import pytest
from unicorn import UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_ESP

from conftest import ROOT
from test_save import GRAPHICS, START, Game
from test_view import OriginalView, u8_p

SCREEN_W, SCREEN_H = 320, 200
DATA_START, DATA_END = 0x70000, 0x828F0  # the data object of SKULL.EXE
VGA = 0xA0000
VIEW_W, VIEW_H = 224, 136
CHAMPION_SIZE = 263
GAME_CHAMPIONS = 252  # offsetof(Game, champions) in save.h

# Original functions.
GFX_DRAW_TEXT, TEXT_FORMAT, TEXT_MEASURE, TEXT_WRAP_LINE = 0x10E3F, 0x3A6AB, 0x554AB, 0x554FC
GFX_FILL_ZONE_SCREEN, UI_DRAW_MOVEMENT_ARROWS, CHAMP_INIT_PARTY_STATE = 0x13B4D, 0x42AE4, 0x49D46
CHAMP_DRAW_ALL_STATES, INV_TOGGLE, UI_UPDATE_ACTION_AREA = 0x4904F, 0x3A464, 0x3FE68
TEXT_PRINT_MESSAGE, TEXT_SET_MESSAGE_CURSOR, ZONE_GET_RECT, MONEY_INIT_COIN_TABLE = 0x5599F, 0x5570F, 0x19CFC, 0x154E0
# Hardware and input services that the drawing calls: no-ops here.
NO_OPS = (0x14C55, 0x14C68, 0x138D9, 0x13A87, 0x13B7C, 0x1C5FD, 0x20707, 0x206FF, 0x14889, 0x3BDDA)
# Original globals.
G_UI_COLORS, G_FONT_BITMAP, G_MESSAGE_LINE, G_MESSAGE_AREA_Y = 0x7F210, 0x80414, 0x80410, 0x80418
G_GAME_TIME, G_CHAMPIONS, G_INVENTORY, G_LEADER_HAND = 0x7F22C, 0x7FBD0, 0x7F972, 0x7FBB4
G_EYE, G_MOUTH, G_ACTION_CHAMPION, G_ACTION_HAND = 0x7F224, 0x7F21C, 0x7FB70, 0x7FB72
G_ACTION_DIRTY, G_ACTION_DELAY, G_ACTION_RESULT, G_ACTION_RESULT_FLAG, G_ACTION_RESULT_AMMO = (
    0x7FB2C, 0x7FB68, 0x7FB78, 0x7FB6E, 0x7FB74)
G_SKILL_RAISED, G_PARTY_DIR, G_MAP_GFX_SET = 0x7FFF8, 0x7F254, 0x75BFE


class UiState(ctypes.Structure):
    _fields_ = [("inventory", ctypes.c_int16), ("candidate", ctypes.c_uint16), ("screen_needs_refresh", ctypes.c_int16),
                ("buttons", ctypes.c_uint16), ("eye", ctypes.c_int16), ("mouth", ctypes.c_int16),
                ("push_mode", ctypes.c_int16), ("panel_mode", ctypes.c_int16)]


class ActionArea(ctypes.Structure):
    _fields_ = [("champion", ctypes.c_int16), ("hand", ctypes.c_int16), ("mode", ctypes.c_int16),
                ("dirty", ctypes.c_int16), ("redraw_delay", ctypes.c_int16), ("flags", ctypes.c_uint16),
                ("result", ctypes.c_int16), ("result_flag", ctypes.c_int16), ("result_ammo", ctypes.c_uint16),
                ("flash", ctypes.c_int16), ("flash_hand", ctypes.c_int16), ("map_item", ctypes.c_uint8),
                ("open_items", ctypes.c_uint16 * 8), ("open_container", ctypes.c_uint16),
                ("hand_items", ctypes.c_uint16 * 8)]


class OriginalUi(OriginalView):
    """The original interface drawing on start.DAT: screen at 0xA0000, the view picture, texts formatted by the
    original text_format."""

    def __init__(self, lib, gdat, data):
        super().__init__(lib, gdat, data)
        emu = self.emu
        emu.uc.mem_map(VGA, 0x10000, UC_PROT_ALL)
        for addr in NO_OPS:
            emu.stub(addr, lambda *_: 0)
        emu.write_u32(G_UI_COLORS, self.entry_data(1, 0, 0x0D, 0xFE))
        font = self.item_bytes(lib.gdat_find(gdat, 1, 0, 7, 0))[:0x300]
        emu.write_u32(G_FONT_BITMAP, self.put(font + bytes(0x80)))
        self.message_line = self.alloc(320 * 7 + 6) + 6
        emu.write(self.message_line - 6, struct.pack("<BBhh", 8, 0, 320, 7))
        emu.write_u32(G_MESSAGE_LINE, self.message_line)
        rect = self.alloc(8)
        emu.call(ZONE_GET_RECT, 15, rect)
        height = struct.unpack("<h", emu.read(rect + 6, 2))[0]
        emu.write(G_MESSAGE_AREA_Y, struct.pack("<h", (height - 20) >> 1))
        emu.call(TEXT_SET_MESSAGE_CURSOR, 0, 2)
        emu.call(MONEY_INIT_COIN_TABLE)
        self.data_snapshot = emu.read(DATA_START, DATA_END - DATA_START)

    def restore(self):
        """Puts the globals back to the state after the load, before each test."""
        self.emu.write(DATA_START, self.data_snapshot)

    def text(self, category, index, entry, buf):
        """gdat_get_text: the decrypted text goes through the original text_format (a jump with the caller's return)."""
        raw = ctypes.create_string_buffer(512)
        self.lib.gdat_text(self.gdat, category & 0xFF, index & 0xFF, entry & 0xFF, raw, 512)
        raw_addr = self.put(raw.value + b"\0")
        emu = self.emu
        sp = emu.uc.reg_read(UC_X86_REG_ESP)
        tramp = self.alloc(32)
        code = (b"\xb8" + struct.pack("<I", raw_addr) + b"\xba" + struct.pack("<I", buf) + b"\x68"
                + struct.pack("<I", emu.read_u32(sp)) + b"\xe9" + struct.pack("<i", TEXT_FORMAT - (tramp + 20)))
        emu.write(tramp, code)
        emu.write_u32(sp, tramp)
        return buf

    def screen(self):
        return self.emu.read(VGA, SCREEN_W * SCREEN_H)

    def view(self):
        return self.emu.read(self.viewport, VIEW_W * VIEW_H)


@pytest.fixture(scope="module")
def gdat(lib):
    if not GRAPHICS.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    if not START.exists():
        pytest.skip("tests/saves/start.DAT missing")
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_text.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3 + [ctypes.c_char_p, ctypes.c_size_t]
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = u8_p
    lib.gdat_image_colormap.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.gdat_image_colormap.restype = u8_p
    lib.gdat_decode_image.argtypes = [ctypes.c_void_p, ctypes.c_int, u8_p, ctypes.c_size_t]
    lib.gdat_image_size.argtypes = [ctypes.c_void_p, ctypes.c_int] + [ctypes.POINTER(ctypes.c_int)] * 3
    lib.gdat_image_offset.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3
    lib.gdat_exists.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.text_find_int_field.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == 0
    yield g
    lib.gdat_free(g)


@pytest.fixture(scope="module")
def original(lib, gdat):
    return OriginalUi(lib, gdat, START.read_bytes())


@pytest.fixture()
def port(lib, gdat, original, tmp_path):
    """The C port with start.DAT loaded and a cleared screen; the original gets the same champions back."""
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(START.read_bytes())
    assert lib.save_load(str(path).encode(), gdat) == 0
    lib.draw_init(gdat)
    assert lib.zone_load(gdat) == 1
    assert lib.pal_load_color_ramps(gdat) == 1
    assert lib.text_init(gdat) == 1
    game = Game.in_dll(lib, "game")
    lib.view_enter_map(game.party_map)
    lib.money_init_coin_table()
    lib.ui_reset()
    ctypes.memset(ctypes.addressof(ctypes.c_uint8.in_dll(lib, "fb")), 0, SCREEN_W * SCREEN_H)
    viewport = ctypes.cast(ctypes.c_void_p.in_dll(lib, "view_viewport"), u8_p)
    ctypes.memset(viewport, 0, VIEW_W * VIEW_H)
    emu = original.emu
    original.restore()
    emu.write(G_CHAMPIONS, bytes(champion_records(lib)))
    emu.write(VGA, bytes(SCREEN_W * SCREEN_H))
    emu.write(original.viewport, bytes(VIEW_W * VIEW_H))
    emu.write(G_MAP_GFX_SET, bytes([lib.dungeon_map_graphics(game.party_map) >> 4 & 0xF]))
    return lib


def champion_records(lib):
    """The 4 champion records of the C game state."""
    return (ctypes.c_uint8 * (4 * CHAMPION_SIZE)).from_address(ctypes.addressof(Game.in_dll(lib, "game")) + GAME_CHAMPIONS)


def c_screen(lib):
    return ctypes.string_at(ctypes.addressof(ctypes.c_uint8.in_dll(lib, "fb")), SCREEN_W * SCREEN_H)


def c_view(lib):
    viewport = ctypes.cast(ctypes.c_void_p.in_dll(lib, "view_viewport"), u8_p)
    return ctypes.string_at(viewport, VIEW_W * VIEW_H)


def diff(a, b, width):
    """The first differing pixels, for a readable failure."""
    import os
    if os.environ.get("DM2_DUMP"):
        open(os.environ["DM2_DUMP"] + ".mine", "wb").write(a)
        open(os.environ["DM2_DUMP"] + ".theirs", "wb").write(b)
    out = [(i % width, i // width, a[i], b[i]) for i in range(len(a)) if a[i] != b[i]]
    return f"{len(out)} pixels differ, first {out[:8]}"


def draw_game_screen(lib, original):
    emu = original.emu
    emu.call(GFX_FILL_ZONE_SCREEN, 2, 0)
    emu.call(UI_DRAW_MOVEMENT_ARROWS)
    emu.call(CHAMP_INIT_PARTY_STATE)
    emu.call(CHAMP_DRAW_ALL_STATES, 1)
    lib.ui_draw_game_screen()


def test_game_screen_matches_original(port, original):
    # The screen after loading start.DAT: background, movement arrows, the status box of Torham, the action area.
    draw_game_screen(port, original)
    mine, theirs = c_screen(port), original.screen()
    assert len(set(theirs)) > 4
    assert mine == theirs, diff(mine, theirs, SCREEN_W)


GAME_LEADER_HAND = GAME_CHAMPIONS + 4 * CHAMPION_SIZE
G_LEADER_HAND_WEIGHT, G_CHAMP_DRAW_STATE = 0x7FBCE, 0x48890


def set_leader_hand(lib, original, thing):
    ctypes.c_uint16.from_address(ctypes.addressof(Game.in_dll(lib, "game")) + GAME_LEADER_HAND).value = thing
    original.emu.write(G_LEADER_HAND, struct.pack("<H", thing))
    original.emu.write(G_LEADER_HAND_WEIGHT, struct.pack("<H", lib.item_weight(thing) & 0xFFFF if thing != 0xFFFF else 0))


def set_eye(lib, original, eye, mouth=0):
    ui = UiState.in_dll(lib, "ui")
    ui.eye, ui.mouth = eye, mouth
    original.emu.write(G_EYE, struct.pack("<I", eye))
    original.emu.write(G_MOUTH, struct.pack("<I", mouth))


def things_of(lib, kind, limit, test=None):
    """Up to limit things of thing type kind (that pass test) in the loaded dungeon."""
    lib.dungeon_thing_valid.argtypes = [ctypes.c_uint16]
    out = []
    for index in range(lib.dungeon_thing_count(kind)):
        thing = kind << 10 | index
        if lib.dungeon_thing_valid(thing) and (test is None or test(thing)) and len(out) < limit:
            out.append(thing)
    return out


def assert_same_screen_and_view(lib, original, what):
    mine, theirs = c_screen(lib), original.screen()
    assert mine == theirs, (what, "screen", diff(mine, theirs, SCREEN_W))
    mine, theirs = c_view(lib), original.view()
    assert mine == theirs, (what, "view", diff(mine, theirs, VIEW_W))


def test_inventory_matches_original(port, original):
    # Torham's inventory: portrait, values, load, all 30 slots, the food and water panel.
    draw_game_screen(port, original)
    original.emu.call(INV_TOGGLE, 0)
    port.inv_toggle(0)
    assert len(set(original.view())) > 8
    assert_same_screen_and_view(port, original, "inventory")


def test_inventory_panels_match_original(port, original):
    # The eye shows the skills without a leader hand object, else the object: weapons, clothing, scrolls,
    # potions, containers, money boxes, and miscellaneous items of the dungeon.
    draw_game_screen(port, original)
    original.emu.call(INV_TOGGLE, 0)
    port.inv_toggle(0)
    lib = port
    lib.item_is_chest.argtypes = lib.item_is_moneybox.argtypes = [ctypes.c_uint16]
    samples = [0xFFFF]
    for kind in (5, 6, 7, 8, 10):
        samples += things_of(lib, kind, 6)
    samples += things_of(lib, 9, 3, lib.item_is_chest) + things_of(lib, 9, 3, lib.item_is_moneybox)
    assert len(samples) > 25
    pictures = set()
    for thing in samples:
        set_eye(lib, original, 1)
        set_leader_hand(lib, original, thing)
        original.emu.call(G_CHAMP_DRAW_STATE, 0)
        lib.champ_draw_state(0)
        assert_same_screen_and_view(lib, original, f"eye {thing:#x}")
        pictures.add(original.view())
    assert len(pictures) > 20  # each object draws its own panel
    set_eye(lib, original, 0)
    set_leader_hand(lib, original, 0xFFFF)
