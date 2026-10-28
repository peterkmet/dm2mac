"""Step 13: text drawing, the interface panels, and the message area against the original code in dm2tools.emu."""
import ctypes
import random
import struct

import pytest
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP

from dm2test import constants as C
from dm2test.interface import (  # noqa: F401 (gdat, original, and port are fixtures)
    CHAMP_DRAW_ALL_STATES, CHAMPION_SIZE, GAME_CHAMPIONS, GFX_DRAW_TEXT, G_CHAMP_DRAW_STATE, G_EYE, G_GAME_TIME,
    G_IOP_BUFFER, G_LEADER_HAND, G_MOUTH, G_PARTY_SLEEPING, G_SKILL_RAISED, INV_TOGGLE, IOP_SCROLL_CALL, IOP_SCROLL_NEXT,
    SCREEN_W, TEXT_FORMAT, TEXT_MEASURE, TEXT_PRINT_MESSAGE, TEXT_SET_MESSAGE_CURSOR, TEXT_WRAP_LINE, VGA, VIEW_W,
    ZONE_GET_RECT, ActionArea, UiState, c_screen, c_view, champion_bytes, champion_records, diff, draw_game_screen, gdat, open_hand, original, port, set_action, set_leader_hand, set_magic,
    sync_champions, things_of, update_action_area,
)
from dm2test.save import Game
from dm2test.view import Bitmap, u8_p
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()


def test_game_screen_matches_original(port, original):
    # The screen after loading start.DAT: background, movement arrows, the status box of Torham, the action area.
    draw_game_screen(port, original)
    mine, theirs = c_screen(port), original.screen()
    assert len(set(theirs)) > 4
    assert mine == theirs, diff(mine, theirs, SCREEN_W)


def set_eye(lib, original, eye, mouth=0):
    ui = UiState.in_dll(lib, "g_ui")
    ui.eye, ui.mouth = eye, mouth
    original.emu.write(G_EYE, struct.pack("<I", eye))
    original.emu.write(G_MOUTH, struct.pack("<I", mouth))


def assert_same_screen_and_view(lib, original, what):
    mine, theirs = c_screen(lib), original.screen()
    assert mine == theirs, (what, "screen", diff(mine, theirs, SCREEN_W))
    mine, theirs = c_view(lib), original.view()
    assert mine == theirs, (what, "view", diff(mine, theirs, VIEW_W))


def test_inventory_matches_original(port, original):
    # Torham's inventory: portrait, values, load, all 30 slots, the food and water panel.
    draw_game_screen(port, original)
    original.emu.call(INV_TOGGLE, 0)
    port.ui_toggle_inventory(0)
    assert len(set(original.view())) > 8
    assert_same_screen_and_view(port, original, "inventory")


def test_inventory_panels_match_original(port, original):
    # The eye shows the skills without a leader hand object, else the object: weapons, clothing, scrolls,
    # potions, containers, money boxes, and miscellaneous items of the dungeon.
    draw_game_screen(port, original)
    original.emu.call(INV_TOGGLE, 0)
    port.ui_toggle_inventory(0)
    lib = port
    lib.item_is_chest.argtypes = lib.item_is_moneybox.argtypes = [ctypes.c_uint16]
    samples = [C.THING_NONE]
    for kind in (C.THING_WEAPON, C.THING_ARMOUR, C.THING_SCROLL, C.THING_POTION, C.THING_MISC):
        samples += things_of(lib, kind, 6)
    samples += things_of(lib, C.THING_CONTAINER, 3, lib.item_is_chest) + things_of(lib, C.THING_CONTAINER, 3, lib.item_is_moneybox)
    assert len(samples) > 25
    pictures = set()
    for thing in samples:
        set_eye(lib, original, 1)
        set_leader_hand(lib, original, thing)
        original.emu.call(G_CHAMP_DRAW_STATE, 0)
        lib.ui_draw_champion_state(0)
        assert_same_screen_and_view(lib, original, f"eye {thing:#x}")
        pictures.add(original.view())
    assert len(pictures) > 20  # each object draws its own panel
    set_eye(lib, original, 0)
    set_leader_hand(lib, original, C.THING_NONE)


INV_RENDER_HAND_ICON = 0x38392
HAND_ICON_SIZE = C.HAND_ICON_SIZE


class LeaderHand(ctypes.Structure):
    _fields_ = [("attr", ctypes.c_uint16), ("weight", ctypes.c_uint16), ("icon", ctypes.c_uint8),
                ("cursor", ctypes.c_uint8 * HAND_ICON_SIZE ** 2), ("palette", ctypes.c_uint8 * C.HAND_ICON_COLORS)]


def test_hand_icon_matches_original(port, original):
    # The cursor icon of objects in the leader hand: the 4 bpp color numbers of the original inv_render_hand_icon
    # (high nibble first) and its 16 palette colors.
    lib, emu = port, original.emu
    hand = LeaderHand.in_dll(lib, "g_leader_hand")
    bitmap = original.alloc(4 + HAND_ICON_SIZE ** 2 // 2) + 4
    emu.write(bitmap - 4, struct.pack("<hh", HAND_ICON_SIZE, HAND_ICON_SIZE))
    icon = original.alloc(6 + 16)
    samples = [t for kind in (C.THING_WEAPON, C.THING_ARMOUR, C.THING_SCROLL, C.THING_POTION, C.THING_MISC)
               for t in things_of(lib, kind, 4)] + things_of(lib, C.THING_CONTAINER, 2)
    pictures = set()
    for thing in samples:
        lib.champ_put_in_leader_hand(thing, 0)
        lib.champ_take_from_leader_hand()
        emu.write(icon, struct.pack("<HI", thing, bitmap))
        emu.call(INV_RENDER_HAND_ICON, icon)
        packed = emu.read(bitmap, HAND_ICON_SIZE ** 2 // 2)
        theirs = bytes(v for b in packed for v in (b >> 4, b & 0xF))
        assert bytes(hand.cursor) == theirs, hex(thing)
        assert bytes(hand.palette) == emu.read(icon + 6, C.HAND_ICON_COLORS), hex(thing)
        pictures.add(theirs)
    assert len(pictures) > 10


class CursorImage(ctypes.Structure):
    _fields_ = [("bits", u8_p), ("palette", u8_p)] + [
        (name, ctypes.c_int) for name in ("bpp", "width", "height", "hot_x", "hot_y", "key")]


MOUSE_SET_CURSOR_IMAGE, INPUT_INIT, IOP_INSTALL_INPUT_ISR, MOUSE_SET_DEFAULT_CURSOR = 0x14743, 0x147D1, 0x5A8F3, 0x14846
CHAMP_DRAG_CLICK, MEM_ALLOC_IMAGE, G_CHAMP_DRAG_STATE = 0x149BA, 0x3BD64, 0x7F0B4
# mouse_update_hand_cursor (a no-op of OriginalUi) without its register saves, up to mouse_set_default_cursor
HAND_CURSOR_BODY, HAND_CURSOR_END, G_HAND_ICON_BITMAP = 0x1488D, 0x148FC, G_LEADER_HAND + 2
CURSOR_ARROW, CURSOR_HAND, CURSOR_OBJECT, CURSOR_CHAMPION = C.CURSOR_ARROW, C.CURSOR_HAND, C.CURSOR_OBJECT, C.CURSOR_CHAMPION


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
    saved_icon = emu.read(G_HAND_ICON_BITMAP, 4)
    try:
        emu.call(INPUT_INIT, allow_io=True)
        assert port_image(CURSOR_ARROW) == images[0]
        assert port_image(CURSOR_HAND) == images[1]
        emu.write(G_HAND_ICON_BITMAP, struct.pack("<I", alloc_image(HAND_ICON_SIZE, HAND_ICON_SIZE, 0, 4)))
        objects = set()
        for thing in [t for kind in (C.THING_WEAPON, C.THING_ARMOUR, C.THING_POTION, C.THING_MISC) for t in things_of(lib, kind, 2)]:
            emu.write(G_LEADER_HAND, struct.pack("<H", thing))
            emu.call(INV_RENDER_HAND_ICON, G_LEADER_HAND)
            images.pop(2, None)
            emu.run(HAND_CURSOR_BODY, HAND_CURSOR_END)
            assert 2 in images, hex(thing)
            lib.champ_put_in_leader_hand(thing, 0)
            assert port_image(CURSOR_OBJECT) == images[2], hex(thing)
            lib.champ_take_from_leader_hand()
            objects.add(images[2])
        assert len(objects) > 4
        dragged = 0
        for cell in range(4):
            images.pop(3, None)
            emu.write(G_CHAMP_DRAG_STATE, b"\0\0")
            ctypes.c_int.in_dll(lib, "g_input_drag_state").value = 0
            emu.call(CHAMP_DRAG_CLICK, cell, allow_io=True)
            lib.champ_drag_click(cell)
            if 3 in images:
                assert port_image(CURSOR_CHAMPION) == images[3], cell
                dragged += 1
        assert dragged
    finally:
        for hook in hooks:
            emu.uc.hook_del(hook)
        ctypes.c_int.in_dll(lib, "g_input_drag_state").value = 0
        emu.write(G_LEADER_HAND, b"\xff\xff")
        emu.write(G_HAND_ICON_BITMAP, saved_icon)


def test_draw_cursor_puts_the_image_at_the_hot_spot(port):
    # input_draw_cursor: the image pixels except the clear color, in palette colors, clipped at the screen edges.
    lib = port
    lib.input_cursor_image.restype = CursorImage
    thing = things_of(lib, 5, 1)[0]
    lib.input_init()
    Game.in_dll(lib, "g_game").leader = 0
    lib.champ_put_in_leader_hand(thing, 0)
    try:
        lib.input_set_mode(C.INPUT_MODE_MOVE)
        zone = [(x, y) for y in range(C.SCREEN_H) for x in range(C.SCREEN_W) if lib.input_cursor_at(x, y) == CURSOR_OBJECT]
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
                if n != c.key and 0 <= sx < C.SCREEN_W and 0 <= sy < C.SCREEN_H:
                    expected[sy * C.SCREEN_W + sx] = c.palette[n] if c.palette else n
            assert bytes(fb) == bytes(expected), (x, y)
            assert bytes(fb) != screen, (x, y)
    finally:
        lib.champ_take_from_leader_hand()


CH_RUNE_COUNT, CH_RUNES, CH_ACTION_DELAY, CH_WOUNDS, CH_HEALTH, CH_SLOTS, CH_AURA = 0x1E, 0x22, 0x2A, 0x34, 0x36, 0xC3, 0x102
CH_DIR, CH_CELL, CH_POISONED, CH_FOOD, CH_DAMAGE_EVENT, CH_REDRAW = 0x1C, 0x1D, 0x1F, 0x44, 0x2E, 0x32


def test_drag_cancel_of_a_dead_champion_changes_nothing(port):
    # Regression: the dragged champion in relative cell 1-3 dies (kill_champion cancels the drag). The cancel ends
    # the drag and writes no champion record (a champion -1 write went into g_game).
    lib = port
    game = Game.in_dll(lib, "g_game")
    drag = ctypes.c_int.in_dll(lib, "g_input_drag_state")
    size = GAME_CHAMPIONS + 4 * CHAMPION_SIZE
    try:
        for cell in range(1, C.DIR_COUNT):
            champion_bytes(lib, 0, CH_CELL, [(cell + game.party_dir) % C.DIR_COUNT])
            champion_bytes(lib, 0, CH_HEALTH, [0, 0])
            before = ctypes.string_at(ctypes.addressof(game), size)
            drag.value = cell + 1
            lib.champ_drag_cancel()
            assert drag.value == 0, cell
            assert ctypes.string_at(ctypes.addressof(game), size) == before, cell
            records = champion_records(lib)
            assert all(records[c * CHAMPION_SIZE + CH_CELL] < C.DIR_COUNT for c in range(game.champion_count)), cell
    finally:
        drag.value = 0


def set_sleeping(lib, original, value):
    ctypes.c_bool.in_dll(lib, "g_party_sleeping").value = value
    original.emu.write(G_PARTY_SLEEPING, struct.pack("<I", value))


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
    for result, flag, ammo in ((3, 0, 0), (15, 0, 0), (16, 0, 0), (40, 0, 0), (41, 0, 0), (123, 0, 0), (C.RESULT_NOTHING, 0, 0),
                               (C.RESULT_NO_AMMO, 0, 0x01), (C.RESULT_NO_AMMO, 1, 0x02), (C.RESULT_NO_AMMO, 0, 0x10),
                               (C.RESULT_UNKNOWN_SPELL, 0, 0), (C.RESULT_WIZARD_FAILED, 0, 0), (C.RESULT_PRIEST_FAILED, 0, 0),
                               (C.RESULT_CANT, 0, 0)):
        set_action(lib, original, result=result, result_flag=flag, result_ammo=ammo)
        update_action_area(lib, original)
        assert_same_screen_and_view(lib, original, f"result {result}")
        pictures.add(original.screen())
    assert len(pictures) >= 12


def test_containers_in_hand_match_original(port, original):
    # A money box and a chest in the ready hand open their panels in the action area.
    lib = port
    lib.item_is_chest.argtypes = lib.item_is_moneybox.argtypes = [ctypes.c_uint16]
    boxes = things_of(lib, C.THING_CONTAINER, 2, lib.item_is_moneybox) + things_of(lib, C.THING_CONTAINER, 2, lib.item_is_chest)
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
        modes.add(ActionArea.in_dll(lib, "g_action").mode)
        update_action_area(lib, original, tick=1)
        assert_same_screen_and_view(lib, original, f"box {box:#x} tick")
    assert modes == {C.ACTION_MODE_MONEY_BOX, C.ACTION_MODE_CONTAINER}  # money box and container panels


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
        champion_bytes(lib, 0, CH_REDRAW, struct.pack("<H", C.REDRAW_ALL))
        sync_champions(lib, original)
        set_action(lib, original, dirty=1)
        original.emu.call(CHAMP_DRAW_ALL_STATES, 1)
        lib.ui_draw_all_champion_states(1)
        assert_same_screen_and_view(lib, original, name)
        pictures.add(original.screen())
    # visible and awake go back to a former picture; hungry and poisoned show in the inventory; the busy hands count
    # down on each tick, so the states after busy differ
    assert len(pictures) == 11
    original.emu.call(INV_TOGGLE, 0)
    lib.ui_toggle_inventory(0)
    assert_same_screen_and_view(lib, original, "inventory")
    champion_bytes(lib, 0, CH_HEALTH, struct.pack("<h", 0))
    champion_bytes(lib, 0, CH_REDRAW, struct.pack("<H", C.REDRAW_ALL))
    sync_champions(lib, original)
    original.emu.call(CHAMP_DRAW_ALL_STATES, 1)
    lib.ui_draw_all_champion_states(1)
    assert_same_screen_and_view(lib, original, "dead")


# ---- Text ----

def random_text(rng, length):
    return bytes(rng.randrange(1, 128) for _ in range(length))


class TextColors(ctypes.Structure):
    _fields_ = [("color", ctypes.c_int), ("bg", ctypes.c_int)]


def test_draw_text_matches_original(port, original):
    # Random texts in 8 bpp and 4 bpp bitmaps, with and without background, at random positions.
    lib = port
    emu = original.emu
    lib.text_draw.argtypes = [ctypes.POINTER(Bitmap), ctypes.c_int16, ctypes.c_int16, TextColors, ctypes.c_char_p]
    rng = random.Random(13)
    width, height = 96, 40
    src, text = original.alloc(width * height), original.alloc(64)
    for case in range(300):
        bpp = rng.choice([C.IMAGE_8BPP, C.IMAGE_4BPP])
        size = width * height if bpp == C.IMAGE_8BPP else width // 2 * height
        fill = bytes(rng.randrange(256) for _ in range(size))
        s = random_text(rng, rng.randrange(0, 12))
        x, y = rng.randrange(0, 40), rng.randrange(4, height - 1)  # inside the bitmap
        color, bg = rng.randrange(256 if bpp == C.IMAGE_8BPP else 16), rng.randrange(256 if bpp == C.IMAGE_8BPP else 16)
        if rng.random() < 0.5:
            bg |= 0x4000
        emu.write(src, fill)
        emu.write(text, s + b"\0")
        emu.call(GFX_DRAW_TEXT, src, width, x, y, color, bg, text, bpp)
        mine = (ctypes.c_uint8 * size).from_buffer_copy(fill)
        dst = Bitmap(ctypes.cast(mine, u8_p), width, height, bpp)
        lib.text_draw(ctypes.byref(dst), x, y, TextColors(color, bg), s)
        assert bytes(mine) == emu.read(src, size), (case, s, x, y, color, bg, bpp)


class TextSize(ctypes.Structure):
    _fields_ = [("w", ctypes.c_int16), ("h", ctypes.c_int16)]


def test_measure_and_wrap_match_original(port, original):
    lib = port
    emu = original.emu
    lib.text_wrap_line.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
    lib.text_measure.argtypes = [ctypes.c_char_p]
    lib.text_measure.restype = TextSize
    rng = random.Random(14)
    text, line, out = original.alloc(256), original.alloc(256), original.alloc(16)
    for case in range(300):
        words = [bytes(rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ.") for _ in range(rng.randrange(0, 9)))
                 for _ in range(rng.randrange(1, 12))]
        s = b" ".join(words)
        if rng.random() < 0.3:
            s = s.replace(b" ", b"\n", 1)
        emu.write(text, s + b"\0")
        theirs = emu.call(TEXT_MEASURE, text, out, out + 2)
        size = lib.text_measure(s)
        assert (size.w != 0) == bool(theirs)
        if theirs:
            assert (size.w, size.h) == struct.unpack("<hh", emu.read(out, 4))
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

    args = Args.in_dll(lib, "g_text_args")
    lib.gtext_format.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
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
        lib.gtext_format(fmt, mine)
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
    expiry = (ctypes.c_uint32 * C.MESSAGE_ROWS).in_dll(lib, "g_text_message_expiry")
    assert list(expiry) == list(struct.unpack("<3I", emu.read(0x80404, 12)))
    time = expiry[1]
    emu.write(G_GAME_TIME, struct.pack("<I", time))
    Game.in_dll(lib, "g_game").game_time = time
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
    lib.zone_get_rect.argtypes = [ctypes.c_int16, ctypes.POINTER(ctypes.c_int16)]
    r = (ctypes.c_int16 * 4)()
    lib.zone_get_rect(C.ZONE_MESSAGE_AREA, r)
    x, y, w, h = r
    lib.text_set_message_cursor(0, 0)
    lib.text_print_message(15, b"ONE\nTWO\nTHREE")
    before = c_screen(lib)
    lib.text_print_message(15, b"\nFOUR")
    after = c_screen(lib)
    rows = lambda screen, top, count: [screen[(top + i) * SCREEN_W + x:(top + i) * SCREEN_W + x + w] for i in range(count)]
    assert rows(after, y, h - 7) == rows(before, y + 7, h - 7)
    assert rows(after, y + h - 6, 6) != rows(before, y + h - 6, 6)


MESSAGE_SHOW_TIME = C.MESSAGE_SHOW_TIME


def test_message_rows_follow_print_scroll_and_expiry(port):
    """Four printed lines: the first scrolls out; each row clears at its expiry time."""
    lib = port
    lib.text_print_message.argtypes = [ctypes.c_int, ctypes.c_char_p]
    lib.text_message_row.restype = ctypes.c_char_p
    game = Game.in_dll(lib, "g_game")
    start = 1000
    for step, message in enumerate([b"\nONE", b"\nTWO", b"\nTHREE", b"\nFOUR"]):
        game.game_time = start + step
        lib.text_print_message(15, message)
    assert [lib.text_message_row(i) for i in range(3)] == [b"TWO", b"THREE", b"FOUR"]
    game.game_time = start + 1 + MESSAGE_SHOW_TIME
    lib.text_clear_expired_rows()
    assert [lib.text_message_row(i) for i in range(3)] == [b"", b"THREE", b"FOUR"]
    game.game_time = start + 3 + MESSAGE_SHOW_TIME
    lib.text_clear_expired_rows()
    assert [lib.text_message_row(i) for i in range(3)] == [b"", b"", b""]


def test_layout_zone_helpers_match_original(port, original):
    # The icon rectangle and the position of every zone of the screen layout.
    lib = port
    emu = original.emu
    lib.zone_get_rect.argtypes = [ctypes.c_int16, ctypes.POINTER(ctypes.c_int16)]
    lib.zone_get_icon_rect.argtypes = [ctypes.c_int16, ctypes.POINTER(ctypes.c_int16)]
    lib.zone_get_position.argtypes = [ctypes.c_int16] + [ctypes.POINTER(ctypes.c_int16)] * 2
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
    lib.ui_toggle_inventory(0)
    set_eye(lib, original, 1)
    original.emu.call(G_CHAMP_DRAW_STATE, 0)
    lib.ui_draw_champion_state(0)
    plain = original.view()
    raised = (ctypes.c_uint8 * 16).in_dll(lib, "g_champ_skill_raised")
    counts = [2, 1, 3, 1]
    for i, n in enumerate(counts):
        raised[i] = n
    emu.write(G_SKILL_RAISED, bytes(counts) + bytes(12))
    champion_bytes(lib, 0, CH_REDRAW, struct.pack("<H", C.REDRAW_TITLE_ALL))
    sync_champions(lib, original)
    original.emu.call(G_CHAMP_DRAW_STATE, 0)
    lib.ui_draw_champion_state(0)
    assert_same_screen_and_view(lib, original, "raised")
    assert original.view() != plain  # a raised skill shows green
    set_eye(lib, original, 0)
    original.emu.call(INV_TOGGLE, 0)
    lib.ui_toggle_inventory(0)
    assert bytes(raised) == emu.read(G_SKILL_RAISED, 16)
    assert bytes(raised) != bytes(counts) + bytes(12)


TEXT_PRINT_NUMBER = 0x55AD8


def test_message_area_pending_rows_match_original(port, original):
    # Messages past the bottom row: the new row waits in the row bitmap, then the area moves up and the bottom
    # 6 rows come from the bitmap. The IBMIOP move (service 0x105) is stubbed in the original like the port does it.
    lib = port
    emu = original.emu
    emu.write_u32(G_IOP_BUFFER, original.alloc(0x400))
    rect = original.alloc(8)
    emu.call(ZONE_GET_RECT, C.ZONE_MESSAGE_AREA, rect)
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


ITEM_GET_NAME, DUN_GET_THING_DATA, G_OBJECT_NAME = 0x37E7A, 0x1D2DA, 0x7F92C


def test_item_names_match_original(port, original):
    # The name of every item index of weapons, armour, scrolls, potions, containers, and misc items. Misc 0x3E (a
    # shell) has the GDAT name "f m" in all languages: the original shows the same 2 font glyphs. A token of no champion
    # shows stale bytes in the original.
    lib, emu = port, original.emu
    lib.item_name.restype = ctypes.c_char_p
    lib.item_name.argtypes = [ctypes.c_uint16]
    lib.thing_word.restype = ctypes.c_uint16
    lib.thing_word.argtypes = [ctypes.c_uint16, ctypes.c_int]
    lib.thing_set_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]

    def with_index(kind, word, index):
        if kind == C.THING_SCROLL:  # a scroll has only index 0
            return word
        if kind == C.THING_CONTAINER:  # container word 4: the index bits 0-2 in bits 13-15, bits 3-4 in bits 1-2
            return word & ~0xE006 | (index & 7) << 13 | (index >> 3) << 1
        shift = C.POTION_TYPE_SHIFT if kind == C.THING_POTION else 0
        return word & 0x3FFF & ~(0x7F << shift) | index << shift  # a champion token of champion 0

    names = set()
    for kind, offset, count in ((C.THING_WEAPON, 2, 128), (C.THING_ARMOUR, 2, 128), (C.THING_SCROLL, 2, 1), (C.THING_POTION, 2, 128),
                                (C.THING_CONTAINER, 4, 32), (C.THING_MISC, 2, 128)):
        thing = things_of(lib, kind, 1)[0]
        record = emu.call(DUN_GET_THING_DATA, thing)
        word = lib.thing_word(thing, offset)
        for index in range(count):
            value = with_index(kind, word, index)
            lib.thing_set_word(thing, offset, value)
            emu.write(record + offset, struct.pack("<H", value))
            emu.call(ITEM_GET_NAME, thing)
            theirs = emu.read(G_OBJECT_NAME, 64).split(b"\0")[0]
            assert lib.item_name(thing) == theirs, (hex(thing), hex(index))
            names.add(theirs)
        emu.write(record + offset, struct.pack("<H", word))
    assert b"f m" in names and len(names) > 100
