"""Step 13: text drawing, the interface panels, and the message area against the original code in dm2tools.emu."""
import ctypes
import random
import struct

import pytest
from unicorn import UC_HOOK_CODE, UC_PROT_ALL
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

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
G_GAME_TIME, G_CHAMPIONS, G_LEADER_HAND = 0x7F22C, 0x7FBD0, 0x7FBB4
G_EYE, G_MOUTH, G_ACTION_CHAMPION, G_ACTION_HAND = 0x7F224, 0x7F21C, 0x7FB70, 0x7FB72
G_ACTION_DIRTY, G_ACTION_DELAY, G_ACTION_RESULT, G_ACTION_RESULT_FLAG, G_ACTION_RESULT_AMMO = (
    0x7FB2C, 0x7FB68, 0x7FB78, 0x7FB6E, 0x7FB74)
G_SKILL_RAISED, G_MAP_GFX_SET = 0x7FFF8, 0x75BFE


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


INV_RENDER_HAND_ICON = 0x38392
HAND_ICON_SIZE = 16


class LeaderHand(ctypes.Structure):
    _fields_ = [("attr", ctypes.c_uint16), ("weight", ctypes.c_uint16), ("icon", ctypes.c_uint8),
                ("cursor", ctypes.c_uint8 * HAND_ICON_SIZE ** 2), ("palette", ctypes.c_uint8 * 16)]


def test_hand_icon_matches_original(port, original):
    # The cursor icon of objects in the leader hand: the 4 bpp color numbers of the original inv_render_hand_icon
    # (high nibble first) and its 16 palette colors.
    lib, emu = port, original.emu
    hand = LeaderHand.in_dll(lib, "leader_hand")
    bitmap = original.alloc(4 + HAND_ICON_SIZE ** 2 // 2) + 4
    emu.write(bitmap - 4, struct.pack("<hh", HAND_ICON_SIZE, HAND_ICON_SIZE))
    icon = original.alloc(6 + 16)
    samples = [t for kind in (5, 6, 7, 8, 10) for t in things_of(lib, kind, 4)] + things_of(lib, 9, 2)
    pictures = set()
    for thing in samples:
        lib.champ_put_object_in_leader_hand(thing, 0)
        lib.champ_remove_object_from_leader_hand()
        emu.write(icon, struct.pack("<HI", thing, bitmap))
        emu.call(INV_RENDER_HAND_ICON, icon)
        packed = emu.read(bitmap, HAND_ICON_SIZE ** 2 // 2)
        theirs = bytes(v for b in packed for v in (b >> 4, b & 0xF))
        assert bytes(hand.cursor) == theirs, hex(thing)
        assert bytes(hand.palette) == emu.read(icon + 6, 16), hex(thing)
        pictures.add(theirs)
    assert len(pictures) > 10


class CursorImage(ctypes.Structure):
    _fields_ = [("bits", u8_p), ("palette", u8_p)] + [
        (name, ctypes.c_int) for name in ("bpp", "width", "height", "hot_x", "hot_y", "key")]


MOUSE_SET_CURSOR_IMAGE, INPUT_INIT, IOP_INSTALL_INPUT_ISR, MOUSE_SET_DEFAULT_CURSOR = 0x14743, 0x147D1, 0x5A8F3, 0x14846
CHAMP_DRAG_CLICK, MEM_ALLOC_IMAGE, G_CHAMP_DRAG_STATE = 0x149BA, 0x3BD64, 0x7F0B4
# mouse_update_hand_cursor (a no-op of OriginalUi) without its register saves, up to mouse_set_default_cursor
HAND_CURSOR_BODY, HAND_CURSOR_END, G_HAND_ICON_BITMAP = 0x1488D, 0x148FC, G_LEADER_HAND + 2
CURSOR_ARROW, CURSOR_HAND, CURSOR_OBJECT, CURSOR_CHAMPION = 0, 1, 2, 3


def test_cursor_images_match_original(port, original):
    # The arrow and the hand of input_init, the held object of mouse_update_hand_cursor, and the dragged champion of
    # champ_drag_click: the arguments that the original passes to mouse_set_cursor_image (bpp, size, hot spot, clear
    # color, pixels, palette colors).
    lib, emu = port, original.emu
    lib.input_cursor_image.restype = CursorImage
    images = {}

    def capture(kind, bits, hot_x, hot_y, w, h, bpp, palette, key):
        w, h, bpp = w & 0xFFFF, h & 0xFFFF, bpp & 0xFFFF
        images[kind] = (bpp, w, h, hot_x & 0xFFFF, hot_y & 0xFFFF, key & 0xFF, emu.read(bits, w * h * bpp // 8),
                        palette and emu.read(palette, 16))

    def alloc_image(w, h, flags, bpp):
        w, h = w & 0xFFFF, h & 0xFFFF
        image = original.alloc(6 + w * h) + 6
        emu.write(image - 6, struct.pack("<BBhh", bpp & 0xFF, 0, w, h))
        return image

    def port_image(cursor):
        c = lib.input_cursor_image(cursor)
        palette = bytes(c.palette[:16]) if c.palette else 0
        return c.bpp, c.width, c.height, c.hot_x, c.hot_y, c.key, bytes(c.bits[:c.width * c.height * c.bpp // 8]), palette

    hooks = [emu.stub(MOUSE_SET_CURSOR_IMAGE, capture, stack_args=5), emu.stub(IOP_INSTALL_INPUT_ISR, lambda *_: 0),
             emu.stub(MOUSE_SET_DEFAULT_CURSOR, lambda *_: 0), emu.stub(MEM_ALLOC_IMAGE, alloc_image)]
    saved_icon = None
    try:
        emu.call(INPUT_INIT, allow_io=True)
        assert port_image(CURSOR_ARROW) == images[0]
        assert port_image(CURSOR_HAND) == images[1]
        saved_icon = emu.read(G_HAND_ICON_BITMAP, 4)
        emu.write(G_HAND_ICON_BITMAP, struct.pack("<I", alloc_image(HAND_ICON_SIZE, HAND_ICON_SIZE, 0, 4)))
        objects = set()
        for thing in [t for kind in (5, 6, 8, 10) for t in things_of(lib, kind, 2)]:
            emu.write(G_LEADER_HAND, struct.pack("<H", thing))
            emu.call(INV_RENDER_HAND_ICON, G_LEADER_HAND)
            images.pop(2, None)
            emu.run(HAND_CURSOR_BODY, HAND_CURSOR_END)
            assert 2 in images, hex(thing)
            lib.champ_put_object_in_leader_hand(thing, 0)
            assert port_image(CURSOR_OBJECT) == images[2], hex(thing)
            lib.champ_remove_object_from_leader_hand()
            objects.add(images[2])
        assert len(objects) > 4
        dragged = 0
        for cell in range(4):
            images.pop(3, None)
            emu.write(G_CHAMP_DRAG_STATE, b"\0\0")
            ctypes.c_int.in_dll(lib, "input_drag_state").value = 0
            emu.call(CHAMP_DRAG_CLICK, cell, allow_io=True)
            lib.champ_drag_click(cell)
            if 3 in images:
                assert port_image(CURSOR_CHAMPION) == images[3], cell
                dragged += 1
        assert dragged
    finally:
        for hook in hooks:
            emu.uc.hook_del(hook)
        ctypes.c_int.in_dll(lib, "input_drag_state").value = 0
        emu.write(G_LEADER_HAND, b"\xff\xff")
        emu.write(G_HAND_ICON_BITMAP, saved_icon)


def test_draw_cursor_puts_the_image_at_the_hot_spot(port):
    # input_draw_cursor: the image pixels except the clear color, in palette colors, clipped at the screen edges.
    lib = port
    lib.input_cursor_image.restype = CursorImage
    thing = things_of(lib, 5, 1)[0]
    lib.input_init()
    Game.in_dll(lib, "game").leader = 0
    lib.champ_put_object_in_leader_hand(thing, 0)
    lib.input_set_mode(5)
    zone = [(x, y) for y in range(200) for x in range(320) if lib.input_cursor_at(x, y) == CURSOR_OBJECT]
    assert zone
    near_left = min(zone, key=lambda p: p[0])
    assert near_left[0] < 8  # the left clip runs
    middle = zone[len(zone) // 2]
    for x, y in ((0, 0), (319, 199), (160, 100), (0, near_left[1]), near_left, middle):
        kind = lib.input_cursor_at(x, y)
        if (x, y) in (near_left, middle):
            assert kind == CURSOR_OBJECT, (x, y)
        c = lib.input_cursor_image(kind)
        screen = bytearray(range(256)) * 250
        fb = (ctypes.c_uint8 * len(screen)).from_buffer_copy(screen)
        lib.input_draw_cursor(fb, x, y)
        expected = bytearray(screen)
        for i in range(c.width * c.height):
            n = c.bits[i // 2] >> (0 if i & 1 else 4) & 0xF if c.bpp == 4 else c.bits[i]
            sx, sy = x - c.hot_x + i % c.width, y - c.hot_y + i // c.width
            if n != c.key and 0 <= sx < 320 and 0 <= sy < 200:
                expected[sy * 320 + sx] = c.palette[n] if c.palette else n
        assert bytes(fb) == bytes(expected), (x, y)
        assert bytes(fb) != screen, (x, y)
    lib.champ_remove_object_from_leader_hand()


G_ACTION_FLASH, G_ACTION_FLASH_HAND, G_ACTION_MODE, G_ACTION_FLAGS, G_PARTY_SLEEPING = (
    0x7FB50, 0x7FB4E, 0x757B8, 0x7FB5E, 0x7F234)
GAME_MAGIC = GAME_LEADER_HAND + 2
CH_RUNE_COUNT, CH_RUNES, CH_ACTION_DELAY, CH_WOUNDS, CH_HEALTH, CH_SLOTS, CH_AURA = 0x1E, 0x22, 0x2A, 0x34, 0x36, 0xC3, 0x102
CH_DIR, CH_CELL, CH_POISONED, CH_FOOD, CH_DAMAGE_EVENT, CH_REDRAW = 0x1C, 0x1D, 0x1F, 0x44, 0x2E, 0x32


def champion_bytes(lib, champ, offset, data):
    """Writes data into champion champ of the C game state."""
    records = champion_records(lib)
    for i, b in enumerate(data):
        records[champ * CHAMPION_SIZE + offset + i] = b


def sync_champions(lib, original):
    original.emu.write(G_CHAMPIONS, bytes(champion_records(lib)))


def set_magic(lib, original, invisible, haste):
    magic = (ctypes.c_uint8 * 6).from_address(ctypes.addressof(Game.in_dll(lib, "game")) + GAME_MAGIC)
    magic[2], magic[4] = invisible, haste
    original.emu.write(0x7FFEE, bytes([invisible]))
    original.emu.write(0x7FFF0, bytes([haste]))


def set_sleeping(lib, original, value):
    ctypes.c_int.in_dll(lib, "party_sleeping").value = value
    original.emu.write(G_PARTY_SLEEPING, struct.pack("<I", value))


def set_action(lib, original, **fields):
    """Sets fields of the action area state in the port and in the original."""
    addresses = {"dirty": G_ACTION_DIRTY, "champion": G_ACTION_CHAMPION, "hand": G_ACTION_HAND, "mode": G_ACTION_MODE, "redraw_delay": G_ACTION_DELAY,
                 "flags": G_ACTION_FLAGS, "result": G_ACTION_RESULT, "result_flag": G_ACTION_RESULT_FLAG,
                 "result_ammo": G_ACTION_RESULT_AMMO, "flash": G_ACTION_FLASH, "flash_hand": G_ACTION_FLASH_HAND}
    action = ActionArea.in_dll(lib, "action")
    for name, value in fields.items():
        setattr(action, name, value)
        original.emu.write(addresses[name], struct.pack("<H", value & 0xFFFF))


def update_action_area(lib, original, tick=0):
    original.emu.call(UI_UPDATE_ACTION_AREA, tick)
    lib.ui_update_action_area(tick)


def open_hand(lib, original, hand):
    # Like ui_action_area_open_hand and ui_action_area_open_spells: the area of champion 0 opens on hand.
    set_action(lib, original, champion=1, hand=hand, redraw_delay=1, flags=0, flash=1, flash_hand=hand)
    update_action_area(lib, original)


def test_action_area_panels_match_original(port, original):
    # The action menu of the weapon in the ready hand, the own actions of the empty action hand, and the spell
    # panel with each rune set and entered runes.
    lib = port
    draw_game_screen(lib, original)
    pictures = set()
    for hand in (0, 1):
        open_hand(lib, original, hand)
        assert_same_screen_and_view(lib, original, f"hand {hand}")
        pictures.add(original.screen())
    for set_, runes in ((0, b""), (1, b"`"), (2, b"`f"), (3, b"`fl"), (4, b"`flr")):
        champion_bytes(lib, 0, CH_RUNE_COUNT, [set_])
        champion_bytes(lib, 0, CH_RUNES, runes.ljust(4, b"\0"))
        sync_champions(lib, original)
        open_hand(lib, original, 2)
        assert_same_screen_and_view(lib, original, f"spells {set_}")
        pictures.add(original.screen())
    assert len(pictures) == 7


def test_action_results_match_original(port, original):
    # Damage numbers in the small and the large star, and the failure pictures; -2 shows the missing ammunition.
    lib = port
    draw_game_screen(lib, original)
    pictures = set()
    for result, flag, ammo in ((3, 0, 0), (15, 0, 0), (16, 0, 0), (40, 0, 0), (41, 0, 0), (123, 0, 0), (-1, 0, 0),
                               (-2, 0, 0x01), (-2, 1, 0x02), (-2, 0, 0x10), (-3, 0, 0), (-4, 0, 0), (-5, 0, 0), (-6, 0, 0)):
        set_action(lib, original, result=result, result_flag=flag, result_ammo=ammo)
        update_action_area(lib, original)
        assert_same_screen_and_view(lib, original, f"result {result}")
        pictures.add(original.screen())
    assert len(pictures) >= 12


def test_containers_in_hand_match_original(port, original):
    # A money box and a chest in the ready hand open their panels in the action area.
    lib = port
    lib.item_is_chest.argtypes = lib.item_is_moneybox.argtypes = [ctypes.c_uint16]
    boxes = things_of(lib, 9, 2, lib.item_is_moneybox) + things_of(lib, 9, 2, lib.item_is_chest)
    assert len(boxes) == 4
    draw_game_screen(lib, original)
    modes = set()
    for box in boxes:
        champion_bytes(lib, 0, CH_SLOTS, struct.pack("<H", box))
        sync_champions(lib, original)
        set_action(lib, original, champion=0)
        update_action_area(lib, original)  # the hand icons take the box
        open_hand(lib, original, 0)
        assert_same_screen_and_view(lib, original, f"box {box:#x}")
        modes.add(ActionArea.in_dll(lib, "action").mode)
        update_action_area(lib, original, tick=1)
        assert_same_screen_and_view(lib, original, f"box {box:#x} tick")
    assert modes == {4, 5}  # money box and container panels


def test_party_states_match_original(port, original):
    # The status box and the hands of the party in other states: turned, invisible, wounded, busy, asleep,
    # with an aura, damaged, hungry, poisoned, and dead; then the inventory in the last living state.
    lib = port
    draw_game_screen(lib, original)
    states = [
        ("turned", lambda: champion_bytes(lib, 0, CH_DIR, [1])),
        ("cell", lambda: champion_bytes(lib, 0, CH_CELL, [2])),
        ("invisible", lambda: set_magic(lib, original, 1, 0)),
        ("visible", lambda: set_magic(lib, original, 0, 0)),
        ("wounded", lambda: champion_bytes(lib, 0, CH_WOUNDS, struct.pack("<H", 0x13))),
        ("asleep", lambda: set_sleeping(lib, original, 1)),
        ("awake", lambda: set_sleeping(lib, original, 0)),
        ("busy", lambda: champion_bytes(lib, 0, CH_ACTION_DELAY, [3, 4, 5])),
        ("aura", lambda: champion_bytes(lib, 0, CH_AURA, [4, 30, 0])),
        ("haste", lambda: set_magic(lib, original, 0, 1)),
        ("damage", lambda: champion_bytes(lib, 0, CH_DAMAGE_EVENT, struct.pack("<HH", 5, 37))),
        ("hungry", lambda: champion_bytes(lib, 0, CH_FOOD, struct.pack("<hh", -300, -700))),
        ("poisoned", lambda: champion_bytes(lib, 0, CH_POISONED, [1])),
    ]
    pictures = set()
    for name, change in states:
        change()
        champion_bytes(lib, 0, CH_REDRAW, struct.pack("<H", 0xFC00))
        sync_champions(lib, original)
        set_action(lib, original, dirty=1)
        original.emu.call(CHAMP_DRAW_ALL_STATES, 1)
        lib.champ_draw_all_states(1)
        assert_same_screen_and_view(lib, original, name)
        pictures.add(original.screen())
    # visible and awake go back to a former picture; hungry and poisoned show in the inventory; the busy hands count
    # down on each tick, so the states after busy differ
    assert len(pictures) == 11
    original.emu.call(INV_TOGGLE, 0)
    lib.inv_toggle(0)
    assert_same_screen_and_view(lib, original, "inventory")
    champion_bytes(lib, 0, CH_HEALTH, struct.pack("<h", 0))
    champion_bytes(lib, 0, CH_REDRAW, struct.pack("<H", 0xFC00))
    sync_champions(lib, original)
    original.emu.call(CHAMP_DRAW_ALL_STATES, 1)
    lib.champ_draw_all_states(1)
    assert_same_screen_and_view(lib, original, "dead")


# ---- Text ----

def random_text(rng, length):
    return bytes(rng.randrange(1, 128) for _ in range(length))


def test_draw_text_matches_original(port, original):
    # Random texts in 8 bpp and 4 bpp bitmaps, with and without background, at random positions.
    lib = port
    emu = original.emu
    lib.gfx_draw_text.argtypes = [u8_p] + [ctypes.c_int] * 5 + [ctypes.c_char_p, ctypes.c_int]
    rng = random.Random(13)
    width, height = 96, 40
    src, text = original.alloc(width * height), original.alloc(64)
    for case in range(300):
        bpp = rng.choice([8, 4])
        size = width * height if bpp == 8 else width // 2 * height
        fill = bytes(rng.randrange(256) for _ in range(size))
        s = random_text(rng, rng.randrange(0, 12))
        x, y = rng.randrange(0, 40), rng.randrange(4, height - 1)  # inside the bitmap
        color, bg = rng.randrange(256 if bpp == 8 else 16), rng.randrange(256 if bpp == 8 else 16)
        if rng.random() < 0.5:
            bg |= 0x4000
        emu.write(src, fill)
        emu.write(text, s + b"\0")
        emu.call(GFX_DRAW_TEXT, src, width, x, y, color, bg, text, bpp)
        mine = (ctypes.c_uint8 * size).from_buffer_copy(fill)
        lib.gfx_draw_text(mine, width, x, y, color, bg, s, bpp)
        assert bytes(mine) == emu.read(src, size), (case, s, x, y, color, bg, bpp)


def test_measure_and_wrap_match_original(port, original):
    lib = port
    emu = original.emu
    lib.text_wrap_line.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
    lib.text_measure.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int16), ctypes.POINTER(ctypes.c_int16)]
    rng = random.Random(14)
    text, line, out = original.alloc(256), original.alloc(256), original.alloc(16)
    for case in range(300):
        words = [bytes(rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ.") for _ in range(rng.randrange(0, 9)))
                 for _ in range(rng.randrange(1, 12))]
        s = b" ".join(words)
        if rng.random() < 0.3:
            s = s.replace(b" ", b"\n", 1)
        emu.write(text, s + b"\0")
        w, h = ctypes.c_int16(), ctypes.c_int16()
        theirs = emu.call(TEXT_MEASURE, text, out, out + 2)
        assert lib.text_measure(s, ctypes.byref(w), ctypes.byref(h)) == theirs
        if theirs:
            assert (w.value, h.value) == struct.unpack("<hh", emu.read(out, 4))
        max_width = rng.randrange(1, 200)
        pos = ctypes.c_int16(0)
        emu.write(out, bytes(2))
        while True:
            start = pos.value
            mine_line = ctypes.create_string_buffer(256)
            width = lib.text_wrap_line(s, mine_line, ctypes.byref(pos), max_width)
            their_width = emu.call(TEXT_WRAP_LINE, text, line, out, max_width) & 0xFFFF
            assert (width & 0xFFFF, pos.value) == (their_width, struct.unpack("<h", emu.read(out, 2))[0]), (case, s, start)
            assert mine_line.value == emu.read(line, 256).split(b"\0")[0]
            if pos.value >= len(s) or pos.value == start:
                break
            if s[pos.value:pos.value + 1] == b"\n":
                pos.value += 1
                emu.write(out, struct.pack("<h", pos.value))


def test_text_format_matches_original(port, original):
    # Format codes: champion names, numbers, and inserted texts, also nested.
    lib = port
    emu = original.emu

    class Args(ctypes.Structure):
        _fields_ = [("champion", ctypes.c_int16), ("number", ctypes.c_int16 * 5), ("entry", ctypes.c_uint8)]

    args = Args.in_dll(lib, "text_args")
    lib.text_format.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    rng = random.Random(15)
    number_addresses = (0x7F996, 0x7F98A, 0x7F994, 0x7F992, 0x7F986)
    src, dst = original.alloc(256), original.alloc(512)
    for case in range(100):
        args.champion = rng.choice([0, 0, -1])
        emu.write(0x7F988, struct.pack("<h", args.champion))
        for i, addr in enumerate(number_addresses):
            args.number[i] = rng.randrange(-500, 30000)
            emu.write(addr, struct.pack("<h", args.number[i]))
        args.entry = rng.randrange(4)  # the skill names
        emu.write(0x7F990, bytes([args.entry]))
        parts = []
        for _ in range(rng.randrange(1, 6)):
            code = rng.choice([7, 10, 11, 12, 13, 14, 17])
            parts.append(rng.choice([b"AB ", b"", b"X"]) + (b".Z%03d" % code if rng.random() < 0.5 else bytes([1, code + 32])))
        if args.champion < 0:
            parts = [p for p in parts if b"007" not in p and bytes([1, 39]) not in p]  # the original shows stale bytes
        fmt = b"".join(parts) + b" END"
        emu.write(src, fmt + b"\0")
        emu.call(TEXT_FORMAT, src, dst)
        mine = ctypes.create_string_buffer(512)
        lib.text_format(fmt, mine)
        assert mine.value == emu.read(dst, 512).split(b"\0")[0], (case, fmt)


def test_message_area_matches_original(port, original):
    # Messages in the 3 rows of the message area: colors, wrapped lines, new lines, then the rows that expire.
    lib = port
    emu = original.emu
    lib.text_print_message.argtypes = [ctypes.c_int, ctypes.c_char_p]
    text = original.alloc(256)
    emu.call(TEXT_SET_MESSAGE_CURSOR, 0, 0)
    lib.text_set_message_cursor(0, 0)
    messages = [(4, b"TORHAM ZED"), (7, b" SAYS HELLO."), (15, b"\nTHIS IS A LONG MESSAGE THAT DOES NOT FIT IN ONE ROW OF THE AREA"),
                (9, b" OK")]
    for color, message in messages:
        emu.write(text, message + b"\0")
        emu.call(TEXT_PRINT_MESSAGE, color, text, allow_io=True)
        lib.text_print_message(color, message)
        mine, theirs = c_screen(lib), original.screen()
        assert mine == theirs, (message, diff(mine, theirs, SCREEN_W))
    expiry = (ctypes.c_uint32 * 3).in_dll(lib, "message_expiry")
    assert list(expiry) == list(struct.unpack("<3I", emu.read(0x80404, 12)))
    time = expiry[1]
    emu.write(G_GAME_TIME, struct.pack("<I", time))
    Game.in_dll(lib, "game").game_time = time
    emu.call(0x557B5, allow_io=True)
    lib.text_clear_expired_rows()
    mine, theirs = c_screen(lib), original.screen()
    assert mine == theirs, ("expired", diff(mine, theirs, SCREEN_W))
    assert list(expiry) == list(struct.unpack("<3I", emu.read(0x80404, 12)))


def test_message_area_scrolls(port):
    # A fourth row moves the area up by one row; the new row appears at the bottom (IBMIOP service 0x105 is not
    # in SKULL.EXE: the port moves the rows).
    lib = port
    lib.text_print_message.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.zone_get_rect.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int16)]
    r = (ctypes.c_int16 * 4)()
    lib.zone_get_rect(15, r)
    x, y, w, h = r
    lib.text_set_message_cursor(0, 0)
    lib.text_print_message(15, b"ONE\nTWO\nTHREE")
    before = c_screen(lib)
    lib.text_print_message(15, b"\nFOUR")
    after = c_screen(lib)
    rows = lambda screen, top, count: [screen[(top + i) * SCREEN_W + x:(top + i) * SCREEN_W + x + w] for i in range(count)]
    assert rows(after, y, h - 7) == rows(before, y + 7, h - 7)
    assert rows(after, y + h - 6, 6) != rows(before, y + h - 6, 6)


def test_layout_zone_helpers_match_original(port, original):
    # The icon rectangle and the position of every zone of the screen layout.
    lib = port
    emu = original.emu
    lib.zone_get_rect.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int16)]
    lib.zone_get_icon_rect.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int16)]
    lib.zone_get_position.argtypes = [ctypes.c_int] + [ctypes.POINTER(ctypes.c_int16)] * 2
    out = original.alloc(16)
    zones = 0
    for zone in range(1, 0x300):
        r = (ctypes.c_int16 * 4)()
        if not lib.zone_get_rect(zone, r):
            continue
        zones += 1
        lib.zone_get_icon_rect(zone, r)
        emu.call(0x37F3A, zone, out)
        assert tuple(r) == struct.unpack("<4h", emu.read(out, 8)), zone
        x, y = ctypes.c_int16(), ctypes.c_int16()
        lib.zone_get_position(zone, ctypes.byref(x), ctypes.byref(y))
        emu.call(0x19BAE, zone, out, out + 2)
        assert (x.value, y.value) == struct.unpack("<2h", emu.read(out, 4)), zone
    assert zones > 250


def test_skill_raised_acknowledged_like_original(port, original):
    # Raised skills show green in the skills panel; closing the inventory takes back the counts that it showed.
    lib = port
    emu = original.emu
    draw_game_screen(lib, original)
    original.emu.call(INV_TOGGLE, 0)
    lib.inv_toggle(0)
    set_eye(lib, original, 1)
    original.emu.call(G_CHAMP_DRAW_STATE, 0)
    lib.champ_draw_state(0)
    plain = original.view()
    raised = (ctypes.c_uint8 * 16).in_dll(lib, "champ_skill_raised")
    counts = [2, 1, 3, 1]
    for i, n in enumerate(counts):
        raised[i] = n
    emu.write(G_SKILL_RAISED, bytes(counts) + bytes(12))
    champion_bytes(lib, 0, CH_REDRAW, struct.pack("<H", 0x2000))
    sync_champions(lib, original)
    original.emu.call(G_CHAMP_DRAW_STATE, 0)
    lib.champ_draw_state(0)
    assert_same_screen_and_view(lib, original, "raised")
    assert original.view() != plain  # a raised skill shows green
    set_eye(lib, original, 0)
    original.emu.call(INV_TOGGLE, 0)
    lib.inv_toggle(0)
    assert bytes(raised) == emu.read(G_SKILL_RAISED, 16)
    assert bytes(raised) != bytes(counts) + bytes(12)


G_IOP_BUFFER, IOP_SCROLL_CALL, IOP_SCROLL_NEXT = 0x8048C, 0x13A28, 0x13A2A
TEXT_PRINT_NUMBER = 0x55AD8


def test_message_area_pending_rows_match_original(port, original):
    # Messages past the bottom row: the new row waits in the row bitmap, then the area moves up and the bottom
    # 6 rows come from the bitmap. The IBMIOP move (service 0x105) is stubbed in the original like the port does it.
    lib = port
    emu = original.emu
    emu.write_u32(G_IOP_BUFFER, original.alloc(0x400))
    rect = original.alloc(8)
    emu.call(ZONE_GET_RECT, 15, rect)
    x, y, w, h = struct.unpack("<4h", emu.read(rect, 8))

    def iop_scroll(uc, *_):
        for row in range(y, y + h - 7):
            emu.write(VGA + row * SCREEN_W + x, emu.read(VGA + (row + 7) * SCREEN_W + x, w))
        uc.reg_write(UC_X86_REG_EIP, IOP_SCROLL_NEXT)

    hook = emu.uc.hook_add(UC_HOOK_CODE, iop_scroll, begin=IOP_SCROLL_CALL, end=IOP_SCROLL_CALL)
    lib.text_print_message.argtypes = [ctypes.c_int, ctypes.c_char_p]
    text = original.alloc(256)
    messages = [(4, b"\nFIRST"), (7, b"\nSECOND MESSAGE"), (15, b"\nTHIRD"), (9, b"\nFOURTH ROW SCROLLS THE AREA"),
                (11, b"\nA LONG FIFTH MESSAGE THAT WRAPS INTO A SECOND ROW AND SCROLLS TWICE"), (4, b" MORE")]
    try:
        for color, message in messages:
            emu.write(text, message + b"\0")
            emu.call(TEXT_PRINT_MESSAGE, color, text, allow_io=True)
            lib.text_print_message(color, message)
            mine, theirs = c_screen(lib), original.screen()
            assert mine == theirs, (message, diff(mine, theirs, SCREEN_W))
    finally:
        emu.uc.hook_del(hook)


def test_print_number_matches_original(port, original):
    lib = port
    emu = original.emu
    lib.text_print_number.argtypes = [ctypes.c_int, ctypes.c_int32]
    emu.call(TEXT_SET_MESSAGE_CURSOR, 0, 0)
    lib.text_set_message_cursor(0, 0)
    for color, value in ((4, 0), (7, 12345), (15, -77), (9, 2147483647), (11, -2147483647)):
        emu.call(TEXT_PRINT_NUMBER, color, value, allow_io=True)
        lib.text_print_number(color, value)
        mine, theirs = c_screen(lib), original.screen()
        assert mine == theirs, (value, diff(mine, theirs, SCREEN_W))
