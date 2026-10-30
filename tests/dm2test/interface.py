"""The interface harness: start.DAT in the C port and in the original interface drawing, and their champions,
hands, and action area."""

import ctypes
import struct

import pytest
from unicorn import UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_ESP

from dm2test import constants as C
from dm2test.markers import ROOT
from dm2test.save import GRAPHICS, START
from dm2test.structs import ActionArea, Game, GdatKey, ImageInfo
from dm2test.view import OriginalView, u8_p


SCREEN_W, SCREEN_H = C.SCREEN_W, C.SCREEN_H
DATA_START, DATA_END = 0x70000, 0x828F0  # the data object of SKULL.EXE
VGA = 0xA0000
VIEW_W, VIEW_H = C.VIEWPORT_WIDTH, C.VIEWPORT_HEIGHT
CHAMPION_SIZE = C.CHAMPION_SIZE
GAME_CHAMPIONS = Game.champions.offset

# Original functions.
GFX_DRAW_TEXT, TEXT_FORMAT, TEXT_MEASURE, TEXT_WRAP_LINE = 0x10E3F, 0x3A6AB, 0x554AB, 0x554FC
SCREEN_PRESENT_VIEWPORT = 0x138D9
GFX_FILL_ZONE_SCREEN, UI_DRAW_MOVEMENT_ARROWS, CHAMP_INIT_PARTY_STATE = 0x13B4D, 0x42AE4, 0x49D46
CHAMP_DRAW_ALL_STATES, INV_TOGGLE, UI_UPDATE_ACTION_AREA = 0x4904F, 0x3A464, 0x3FE68
TEXT_PRINT_MESSAGE, TEXT_SET_MESSAGE_CURSOR, ZONE_GET_RECT, MONEY_INIT_COIN_TABLE = 0x5599F, 0x5570F, 0x19CFC, 0x154E0
# Hardware and input services that the drawing calls: no-ops here.
NO_OPS = (0x14C55, 0x14C68, 0x13A87, 0x13B7C, 0x1C5FD, 0x20707, 0x206FF, 0x14889, 0x3BDDA)
# Original globals.
G_UI_COLORS, G_FONT_BITMAP, G_MESSAGE_LINE, G_MESSAGE_AREA_Y = 0x7F210, 0x80414, 0x80410, 0x80418
G_GAME_TIME, G_CHAMPIONS, G_LEADER_HAND = 0x7F22C, 0x7FBD0, 0x7FBB4
G_EYE, G_MOUTH, G_ACTION_CHAMPION, G_ACTION_HAND = 0x7F224, 0x7F21C, 0x7FB70, 0x7FB72
G_ACTION_DIRTY, G_ACTION_DELAY, G_ACTION_RESULT, G_ACTION_RESULT_FLAG, G_ACTION_RESULT_AMMO = (
    0x7FB2C, 0x7FB68, 0x7FB78, 0x7FB6E, 0x7FB74)
G_SKILL_RAISED, G_MAP_GFX_SET = 0x7FFF8, 0x75BFE


class OriginalUi(OriginalView):
    """The original interface drawing on start.DAT: screen at 0xA0000, the view picture, texts formatted by the
    original gtext_format."""

    def __init__(self, lib, gdat, data):
        super().__init__(lib, gdat, data)
        emu = self.emu
        emu.uc.mem_map(VGA, 0x10000, UC_PROT_ALL)
        for addr in NO_OPS:
            emu.stub(addr, lambda *_: 0)
        emu.write_u32(G_UI_COLORS, self.entry_data(1, 0, 0x0D, 0xFE))
        font = self.item_bytes(lib.gdat_find(gdat, 1, 0, 7, 0))[:0x300]
        emu.write_u32(G_FONT_BITMAP, self.put(font + bytes(0x80)))
        self.message_line = self.alloc(C.SCREEN_W * C.TEXT_LINE_HEIGHT + 6) + 6
        emu.write(self.message_line - 6, struct.pack("<BBhh", 8, 0, C.SCREEN_W, C.TEXT_LINE_HEIGHT))
        emu.write_u32(G_MESSAGE_LINE, self.message_line)
        rect = self.alloc(8)
        emu.call(ZONE_GET_RECT, 15, rect)
        height = struct.unpack("<h", emu.read(rect + 6, 2))[0]
        emu.write(G_MESSAGE_AREA_Y, struct.pack("<h", (height - 20) >> 1))
        emu.call(ZONE_GET_RECT, 7, rect)
        x, y = struct.unpack("<hh", emu.read(rect, 4))
        emu.stub(SCREEN_PRESENT_VIEWPORT, lambda *_: self.present_viewport(x, y))
        emu.call(TEXT_SET_MESSAGE_CURSOR, 0, 2)
        emu.call(MONEY_INIT_COIN_TABLE)
        self.data_snapshot = emu.read(DATA_START, DATA_END - DATA_START)

    def restore(self):
        """Puts the globals back to the state after the load, before each test."""
        self.emu.write(DATA_START, self.data_snapshot)

    def text(self, category, index, entry, buf):
        """gdat_get_text: the decrypted text goes through the original gtext_format (a jump with the caller's return)."""
        raw = ctypes.create_string_buffer(512)
        self.lib.gdat_text(self.gdat, GdatKey(category & 0xFF, index & 0xFF, entry & 0xFF), raw, 512)
        raw_addr = self.put(raw.value + b"\0")
        emu = self.emu
        sp = emu.uc.reg_read(UC_X86_REG_ESP)
        tramp = self.alloc(32)
        code = (b"\xb8" + struct.pack("<I", raw_addr) + b"\xba" + struct.pack("<I", buf) + b"\x68"
                + struct.pack("<I", emu.read_u32(sp)) + b"\xe9" + struct.pack("<i", TEXT_FORMAT - (tramp + 20)))
        emu.write(tramp, code)
        emu.write_u32(sp, tramp)
        return buf

    def present_viewport(self, x, y):
        """play_present_viewport without the walk scaling: the view picture goes to the viewport zone."""
        view = self.view()
        for row in range(VIEW_H):
            self.emu.write(VGA + (y + row) * SCREEN_W + x, view[row * VIEW_W:(row + 1) * VIEW_W])
        return 0

    def screen(self):
        return self.emu.read(VGA, SCREEN_W * SCREEN_H)

    def view(self):
        return self.emu.read(self.viewport, VIEW_W * VIEW_H)


@pytest.fixture(scope="module")
def ui_gdat(lib):
    if not GRAPHICS.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    if not START.exists():
        pytest.skip("tests/saves/start.DAT missing")
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_text.argtypes = [ctypes.c_void_p, GdatKey, ctypes.c_char_p, ctypes.c_size_t]
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = u8_p
    lib.gdat_image_colormap.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.gdat_image_colormap.restype = u8_p
    lib.gdat_decode_image.argtypes = [ctypes.c_void_p, ctypes.c_int, u8_p, ctypes.c_size_t]
    lib.gdat_image_info.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ImageInfo)]
    lib.gdat_image_offset.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3
    lib.gdat_exists.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_exists.restype = ctypes.c_bool
    lib.gdat_text_int_field.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == 0
    yield g
    lib.gdat_free(g)


@pytest.fixture(scope="module")
def original(lib, ui_gdat):
    return OriginalUi(lib, ui_gdat, START.read_bytes())


@pytest.fixture()
def port(lib, ui_gdat, original, tmp_path):
    """The C port with start.DAT loaded and a cleared screen; the original gets the same champions back."""
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(START.read_bytes())
    assert lib.save_load(str(path).encode(), ui_gdat) == 0
    lib.draw_init(ui_gdat)
    assert lib.zone_load(ui_gdat) == 1
    assert lib.pal_load_color_ramps(ui_gdat) == 1
    assert lib.text_init(ui_gdat) == 1
    game = Game.in_dll(lib, "g_game")
    lib.view_enter_map(game.party_map)
    lib.money_init_coin_table()
    lib.ui_reset()
    ctypes.memset(ctypes.addressof(ctypes.c_uint8.in_dll(lib, "g_video_fb")), 0, SCREEN_W * SCREEN_H)
    viewport = ctypes.cast(ctypes.c_void_p.in_dll(lib, "g_view_viewport"), u8_p)
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
    return (ctypes.c_uint8 * (4 * CHAMPION_SIZE)).from_address(ctypes.addressof(Game.in_dll(lib, "g_game")) + GAME_CHAMPIONS)


def c_screen(lib):
    return ctypes.string_at(ctypes.addressof(ctypes.c_uint8.in_dll(lib, "g_video_fb")), SCREEN_W * SCREEN_H)


def c_view(lib):
    viewport = ctypes.cast(ctypes.c_void_p.in_dll(lib, "g_view_viewport"), u8_p)
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


GAME_LEADER_HAND = Game.leader_hand.offset
G_LEADER_HAND_WEIGHT, G_CHAMP_DRAW_STATE = 0x7FBCE, 0x48890


def set_leader_hand(lib, original, thing):
    ctypes.c_uint16.from_address(ctypes.addressof(Game.in_dll(lib, "g_game")) + GAME_LEADER_HAND).value = thing
    original.emu.write(G_LEADER_HAND, struct.pack("<H", thing))
    original.emu.write(G_LEADER_HAND_WEIGHT, struct.pack("<H", lib.item_weight(thing) & 0xFFFF if thing != 0xFFFF else 0))


def things_of(lib, kind, limit, test=None):
    """Up to limit things of thing type kind (that pass test) in the loaded dungeon."""
    lib.dungeon_thing_valid.argtypes = [ctypes.c_uint16]
    out = []
    for index in range(lib.dungeon_thing_count(kind)):
        thing = kind << 10 | index
        if lib.dungeon_thing_valid(thing) and (test is None or test(thing)) and len(out) < limit:
            out.append(thing)
    return out


G_ACTION_FLASH, G_ACTION_FLASH_HAND, G_ACTION_MODE, G_ACTION_FLAGS, G_PARTY_SLEEPING = (
    0x7FB50, 0x7FB4E, 0x757B8, 0x7FB5E, 0x7F234)
GAME_MAGIC = Game.magic_light.offset


def champion_bytes(lib, champ, offset, data):
    """Writes data into champion champ of the C game state."""
    records = champion_records(lib)
    for i, b in enumerate(data):
        records[champ * CHAMPION_SIZE + offset + i] = b


def sync_champions(lib, original):
    original.emu.write(G_CHAMPIONS, bytes(champion_records(lib)))


def set_magic(lib, original, invisible, haste):
    magic = (ctypes.c_uint8 * 6).from_address(ctypes.addressof(Game.in_dll(lib, "g_game")) + GAME_MAGIC)
    magic[2], magic[4] = invisible, haste
    original.emu.write(0x7FFEE, bytes([invisible]))
    original.emu.write(0x7FFF0, bytes([haste]))


def set_action(lib, original, **fields):
    """Sets fields of the action area state in the port and in the original."""
    addresses = {"dirty": G_ACTION_DIRTY, "champion": G_ACTION_CHAMPION, "hand": G_ACTION_HAND, "mode": G_ACTION_MODE, "redraw_delay": G_ACTION_DELAY,
                 "flags": G_ACTION_FLAGS, "result": G_ACTION_RESULT, "result_flag": G_ACTION_RESULT_FLAG,
                 "result_ammo": G_ACTION_RESULT_AMMO, "flash": G_ACTION_FLASH, "flash_hand": G_ACTION_FLASH_HAND}
    action = ActionArea.in_dll(lib, "g_action")
    for name, value in fields.items():
        setattr(action, name, value)
        original.emu.write(addresses[name], struct.pack("<H", value & 0xFFFF))


def update_action_area(lib, original, tick=0):
    original.emu.call(UI_UPDATE_ACTION_AREA, tick)
    lib.area_update(tick)


def open_hand(lib, original, hand):
    # Like action_area_open_hand and action_area_open_spells: the area of champion 0 opens on hand.
    set_action(lib, original, champion=1, hand=hand, redraw_delay=1, flags=0, flash=1, flash_hand=hand)
    update_action_area(lib, original)


G_IOP_BUFFER, IOP_SCROLL_CALL, IOP_SCROLL_NEXT = 0x8048C, 0x13A28, 0x13A2A
