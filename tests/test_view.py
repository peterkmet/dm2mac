import ctypes
import random
import struct

import pytest

from conftest import ROOT
from dm2tools.emu import SCRATCH
from test_save import GRAPHICS, START, Game, original_load

DUN_SET_PARTY_LOCATION, VIEW_LOAD_SQUARE = 0x1C82D, 0x52BF6
VIEW_UNHIDE_LINKED_SQUARES, VIEW_MARK_HIDDEN_SQUARES = 0x54ABD, 0x5497E
GDAT_GET_TEXT = 0x3A921
G_VIEW_SQUARES, G_VIEW_CREATURE_GRID, G_VIEW_OBJECT_GRID = 0x80040, 0x80044, 0x8004C
G_VIEW_MISSILE_MASKS, G_VIEW_OBJECT_MASKS = 0x80034, 0x80048
G_PARTY_MOVE_STEPS, G_VIEW_ARROWS_ENABLED, G_VIEW_PARTY_DIR = 0x7F258, 0x802CA, 0x802C4
VIEW_BUFFERS = SCRATCH + 0xF0000
SQUARE_COUNT, SQUARE_SIZE = 23, 18


@pytest.fixture(scope="module")
def gdat(lib):
    if not GRAPHICS.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_text.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3 + [ctypes.c_char_p, ctypes.c_size_t]
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == 0
    yield g
    lib.gdat_free(g)


@pytest.fixture(scope="module")
def start():
    if not START.exists():
        pytest.skip("tests/saves/start.DAT missing")
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return START.read_bytes()


@pytest.fixture(scope="module")
def emu(lib, gdat, start):
    """The original game state after loading start.DAT, with the view buffers of view_draw_dungeon."""
    emu = original_load(lib, gdat, start)

    def get_text(category, index, entry, buf):
        text = ctypes.create_string_buffer(128)
        lib.gdat_text(gdat, category & 0xFF, index & 0xFF, entry & 0xFF, text, 128)
        emu.write(buf, text.value + b"\0")
        return buf

    emu.stub(GDAT_GET_TEXT, get_text)
    base = VIEW_BUFFERS
    for addr, size, fill in ((G_VIEW_SQUARES, 0x19E, 0), (G_VIEW_CREATURE_GRID, 0x166, 0), (G_VIEW_OBJECT_GRID, 0x166, 0xFF),
                             (G_VIEW_MISSILE_MASKS, 0x5C, 0), (G_VIEW_OBJECT_MASKS, 0x5C, 0)):
        emu.write(base, bytes([fill]) * size)
        emu.write_u32(addr, base)
        base += 0x200
    emu.write(G_PARTY_MOVE_STEPS, bytes(2))
    emu.write(G_VIEW_ARROWS_ENABLED, bytes(2))
    return emu


def original_view_squares(emu, x, y, direction, party):
    emu.call(DUN_SET_PARTY_LOCATION, *party)
    emu.write(G_VIEW_PARTY_DIR, struct.pack("<h", direction))
    for pos in range(SQUARE_COUNT - 1, -1, -1):
        emu.call(VIEW_LOAD_SQUARE, x, y, pos, direction)
    emu.call(VIEW_UNHIDE_LINKED_SQUARES)
    emu.call(VIEW_MARK_HIDDEN_SQUARES)
    return emu.read(emu.read_u32(G_VIEW_SQUARES), SQUARE_COUNT * SQUARE_SIZE)


def c_view_squares(lib, x, y, direction):
    for pos in range(SQUARE_COUNT - 1, -1, -1):
        lib.view_load_square(x, y, pos, direction)
    lib.view_unhide_linked_squares()
    lib.view_mark_hidden_squares()
    return bytes((ctypes.c_uint8 * (SQUARE_COUNT * SQUARE_SIZE)).in_dll(lib, "view_squares"))


@pytest.fixture(scope="module")
def loaded(lib, gdat, start, tmp_path_factory):
    path = tmp_path_factory.mktemp("view") / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    lib.draw_init(gdat)
    assert lib.text_init(gdat)  # the shops show item names and prices
    lib.money_init_coin_table()
    game = Game.in_dll(lib, "game")
    lib.view_enter_map(game.party_map)
    return game


def test_view_squares_at_start_match_original(lib, emu, loaded):
    game = loaded
    party = (game.party_x, game.party_y, game.party_map)
    original = original_view_squares(emu, game.party_x, game.party_y, game.party_dir, party)
    assert c_view_squares(lib, game.party_x, game.party_y, game.party_dir) == original


def test_view_squares_on_all_maps_match_original(lib, emu, loaded):
    # Every 7th square of every map, in the 4 directions: walls with ornaments and texts, doors, stairs, pits,
    # teleporters, and the map edges. The complete sweep (49016 views) matched too.
    views = 0
    for map_ in range(lib.dungeon_map_count()):
        lib.view_enter_map(map_)
        width, height = lib.dungeon_map_width(map_), lib.dungeon_map_height(map_)
        for square in range(0, width * height, 7):
            x, y = divmod(square, height)
            for direction in range(4):
                original = original_view_squares(emu, x, y, direction, (x, y, map_))
                assert c_view_squares(lib, x, y, direction) == original, (map_, x, y, direction)
                views += 1
    assert views > 5000
    lib.view_enter_map(loaded.party_map)


# ---- The whole view picture against the original view_draw_dungeon ----

MEM_ALLOC, GDAT_ENTRY_INDEX, WEATHER_INIT, READ_LEVEL_TABLE = 0x3BBBA, 0x3C5D3, 0x59F38, 0x367DA
LOAD_BEGIN, LOAD_END = 0x372D6, 0x37586
VIEW_DRAW_DUNGEON, ZONE_UNPACK_LAYOUT, ZONE_ALLOC_RECORD = 0x54B3F, 0x19D0E, 0x19F29
G_ZONE_LIST, G_COLOR_RAMPS, G_COLOR_RAMP_OF_COLOR, G_VIEWPORT_BITMAP = 0x71796, 0x7F208, 0x7F20C, 0x7F240
G_MAP_GFX_SET, G_MAP_WALL_COLOR_KEY, G_CUR_MAP_DOOR_TYPES, G_MAP_FLIP_FLAGS = 0x75BFE, 0x75BFC, 0x7F40A, 0x802D2
G_VIEW_CLEAR, G_SKY_OBJECTS, G_WEATHER_VISIBLE, G_LIGHT_LEVEL = 0x802D0, 0x80438, 0x8047B, 0x7F282
G_PARTY_X, G_PARTY_DIR, G_PUSH_MODE, G_PUSH_PENDING_GROUP = 0x7F26C, 0x7F254, 0x7F3A4, 0x72FFC
# The original functions that the C port does not have: the image cache, the GDAT index, and the memory manager are served from the C port and Python; the viewport arrow (Step 16) does nothing.
NO_OPS = (0x3F3C2, 0x3F406, 0x3F352, 0x3F422, 0x3F49D, 0x3BCA5, 0x3BC8E, 0x3BCBA, 0x210DC, 0x50BF4)
G_ACTIVE_GROUPS, G_RANDOM_SEED, G_SKY_STATE, G_RAIN_INTENSITY = 0x7F898, 0x70518, 0x8046E, 0x8047E
ACTIVE_GROUP_SIZE, ACTIVE_GROUP_MAX = 0x22, 256
G_VIEW_HIT_OFFSET = 0x802D4  # int16 y, x: the place of the last view hit test, in percent
VIEW_SEED = 0x2F6B1D35  # the random numbers of sparkles, rain, and explosions start from it in each view
HEAP, HEAP_SIZE = 0x01000000, 0x02000000
VIEW_W, VIEW_H = 224, 136
u8_p = ctypes.POINTER(ctypes.c_uint8)


class Bitmap(ctypes.Structure):
    _fields_ = [("pixels", u8_p), ("width", ctypes.c_int16), ("height", ctypes.c_int16), ("bpp", ctypes.c_uint8)]


class WeatherGame(ctypes.Structure):
    _fields_ = Game._fields_ + [("leader", ctypes.c_uint16), ("cooldown", ctypes.c_uint16), ("time_a", ctypes.c_uint32),
                                ("time_b", ctypes.c_uint32), ("countdown", ctypes.c_uint16), ("unknown_dir", ctypes.c_uint16),
                                ("sound_volume", ctypes.c_uint8), ("music_volume", ctypes.c_uint8),
                                ("weather_visible", ctypes.c_uint8), ("heavy_rain", ctypes.c_uint8), ("wind_dir", ctypes.c_uint8),
                                ("rain_intensity", ctypes.c_uint8), ("sky_darkness", ctypes.c_uint8), ("cloud_cover", ctypes.c_uint8)]


class OriginalView:
    """The original view drawing of start.DAT in the emulator, with the GDAT data of the C port."""

    def __init__(self, lib, gdat, data):
        from dm2tools.emu import Emu
        from dm2tools.le import load_image
        self.lib, self.gdat = lib, gdat
        self.emu = emu = Emu(load_image(ROOT / "game" / "SKULL.EXE"), max_insns=100_000_000)
        emu.uc.mem_map(HEAP, HEAP_SIZE)
        self.top = HEAP
        self.images, self.palettes, self.data, self.cache = {}, {}, {}, {}
        emu.serve_file(data)
        self.alloc_hook = emu.stub(MEM_ALLOC, lambda size, *_: self.alloc(size))
        emu.stub(GDAT_ENTRY_INDEX, self.entry_index)
        self.weather_init_stub = emu.stub(WEATHER_INIT, lambda *_: 0)  # the load: the test gives the C values
        emu.stub(READ_LEVEL_TABLE, lambda *_: 0)
        emu.run(LOAD_BEGIN, LOAD_END)
        emu.stub(0x3EC2B, lambda c, i, e, *_: self.image(self.image_item(c, i, e)))  # gdat_get_image
        emu.stub(0x3EE1D, self.image_palette)
        emu.stub(0x3EED3, lambda c, i, e, *_: lib.gdat_image_offset(gdat, c & 0xFF, i & 0xFF, e & 0xFF) & 0xFFFF)
        emu.stub(0x3C92E, lambda c, i, t, e: lib.gdat_exists(gdat, c & 0xFF, i & 0xFF, t & 0xFF, e & 0xFF))
        emu.stub(GDAT_GET_TEXT, self.text)
        emu.stub(0x3ECC7, self.entry_data)
        # gdat_get_entry_data_or_null, gdat_entry_size: the map graphics load has all entries of the map ornaments
        emu.stub(0x3ED8A, lambda c, i, t, e: self.entry_data(c, i, t, e) if self.entry_index(c, i, t, e) != 0xFFFF else 0)
        emu.stub(0x3F573, lambda c, i, t, e: len(self.item_bytes(self.entry_index(c, i, t, e))))
        emu.stub(0x3F5B0, self.image_size, stack_args=1)
        emu.stub(0x3F186, self.cache_lookup)  # image_cache_lookup_or_add: always a new entry
        self.cache_alloc_hook = emu.stub(0x3F279, lambda index, size, *_: self.cache_put(index, self.alloc(size)))
        emu.stub(0x3F2F0, self.cache_alloc_image)
        emu.stub(0x3F337, lambda index, *_: self.cache[index & 0xFFFF])
        emu.stub(0x3F3EE, lambda *_: self.new_index())
        self.no_ops = {addr: emu.stub(addr, lambda *_: 0) for addr in NO_OPS}
        # No active groups exist after a load; like the port (Step 16), the creatures show their first frame.
        emu.write_u32(G_ACTIVE_GROUPS, self.put(bytes(ACTIVE_GROUP_SIZE * ACTIVE_GROUP_MAX)))
        layout = self.item_bytes(lib.gdat_find(gdat, 1, 0, 4, 0))
        emu.call(ZONE_UNPACK_LAYOUT, self.put(layout), len(layout), G_ZONE_LIST, ZONE_ALLOC_RECORD)
        ramps = self.item_bytes(lib.gdat_find(gdat, 1, 0, 7, 2))
        base = self.put(ramps)
        count, lengths = ramps[0], ramps[1:1 + ramps[0]]
        brightness = base + 1 + count
        colors = brightness + sum(lengths)
        records = b"".join(bytes([n]) + struct.pack("<II", brightness + sum(lengths[:i]), colors + sum(lengths[:i]))
                           for i, n in enumerate(lengths))
        emu.write_u32(G_COLOR_RAMPS, self.put(records))
        emu.write_u32(G_COLOR_RAMP_OF_COLOR, colors + sum(lengths))
        self.viewport = self.alloc(VIEW_W * VIEW_H + 6) + 6
        emu.write(self.viewport - 6, struct.pack("<BBhh", 8, 0, VIEW_W, VIEW_H))
        emu.write_u32(G_VIEWPORT_BITMAP, self.viewport)
        self.next_index = 1
        from test_interface import G_FONT_BITMAP, G_UI_COLORS, MONEY_INIT_COIN_TABLE
        emu.write_u32(G_UI_COLORS, self.entry_data(1, 0, 0x0D, 0xFE))  # the shops show item names and prices
        emu.write_u32(G_FONT_BITMAP, self.put(self.item_bytes(lib.gdat_find(gdat, 1, 0, 7, 0))[:0x300] + bytes(0x80)))
        emu.call(MONEY_INIT_COIN_TABLE)

    def alloc(self, size):
        self.top += size + 15 & ~15
        return self.top - (size + 15 & ~15)

    def put(self, data):
        addr = self.alloc(len(data))
        self.emu.write(addr, data)
        return addr

    def item_bytes(self, item):
        size = ctypes.c_uint32()
        return ctypes.string_at(self.lib.gdat_item(self.gdat, item, ctypes.byref(size)), size.value)

    def entry_index(self, category, index, type_, entry):
        value = self.lib.gdat_find(self.gdat, category & 0xFF, index & 0xFF, type_ & 0xFF, entry & 0xFF)
        return max(value, 0) if type_ & 0xFF in (0x0B, 0x0C) else value & 0xFFFF

    def image_item(self, category, index, entry):
        item = self.lib.gdat_find(self.gdat, category & 0xFF, index & 0xFF, 1, entry & 0xFF)
        return item if item >= 0 else self.lib.gdat_find(self.gdat, 0x15, 0xFE, 1, 0xFE)

    def image(self, item):
        if item not in self.images:
            w, h, bpp = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
            size = self.lib.gdat_image_size(self.gdat, item, ctypes.byref(w), ctypes.byref(h), ctypes.byref(bpp))
            pixels = (ctypes.c_uint8 * max(size, 1))()
            assert self.lib.gdat_decode_image(self.gdat, item, pixels, size) == 0
            self.images[item] = self.put(struct.pack("<BBhh", bpp.value, 0, w.value, h.value) + bytes(pixels)) + 6
        return self.images[item]

    def image_palette(self, category, index, entry, *_):
        item = self.image_item(category, index, entry)
        colors = self.lib.gdat_image_colormap(self.gdat, item)
        if not colors:
            return 0
        if item not in self.palettes:
            self.palettes[item] = self.put(ctypes.string_at(colors, 16))
        return self.palettes[item]

    def image_size(self, category, index, entry, width, height):
        w, h, bpp = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
        self.lib.gdat_image_size(self.gdat, self.image_item(category, index, entry), ctypes.byref(w), ctypes.byref(h),
                                 ctypes.byref(bpp))
        self.emu.write(width, struct.pack("<H", w.value))
        self.emu.write(height, struct.pack("<H", h.value))

    def text(self, category, index, entry, buf):
        text = ctypes.create_string_buffer(128)
        self.lib.gdat_text(self.gdat, category & 0xFF, index & 0xFF, entry & 0xFF, text, 128)
        self.emu.write(buf, text.value + b"\0")
        return buf

    def entry_data(self, category, index, type_, entry):
        item = self.lib.gdat_find(self.gdat, category & 0xFF, index & 0xFF, type_ & 0xFF, entry & 0xFF)
        if item not in self.data:
            self.data[item] = self.put(self.item_bytes(item))
        return self.data[item]

    def new_index(self):
        self.next_index += 1
        return self.next_index

    def cache_lookup(self, key, index, *_):
        self.emu.write(index, struct.pack("<H", self.new_index()))
        return 0

    def cache_put(self, index, addr):
        self.cache[index & 0xFFFF] = addr
        return addr

    def cache_alloc_image(self, index, width, height, bpp):
        width, height, bpp = width & 0xFFFF, height & 0xFFFF, bpp & 0xFFFF
        size = ((width + 1) // 2 if bpp == 4 else width) * height
        return self.cache_put(index, self.put(struct.pack("<BBhh", bpp, 0, width, height) + bytes(size)) + 6)

    def draw(self, map_, x, y, direction, light, sky, walking=0):
        """The view of the party at (x, y) of map_ looking to direction, with the map graphics of view_enter_map."""
        lib, emu = self.lib, self.emu
        emu.call(DUN_SET_PARTY_LOCATION, x, y, map_)
        emu.write(G_PARTY_X, struct.pack("<hh", x, y))
        emu.write(G_PARTY_DIR, struct.pack("<h", direction))
        graphics, flags = lib.dungeon_map_graphics(map_), lib.dungeon_map_flags(map_)
        gfx_set = graphics >> 4 & 0xF
        emu.write(G_MAP_GFX_SET, bytes([gfx_set]))
        emu.write(G_CUR_MAP_DOOR_TYPES, bytes([graphics >> 8 & 0xF if flags & 0x80 else 0xFF,
                                               graphics >> 12 & 0xF if flags & 0x100 else 0xFF]))
        emu.write(G_MAP_WALL_COLOR_KEY, struct.pack("<H", self.entry_index(8, gfx_set, 0x0B, 0x64)))
        emu.write(G_MAP_FLIP_FLAGS, struct.pack("<H", self.entry_index(8, gfx_set, 0x0B, 0x65)))
        emu.write(G_VIEW_CLEAR, bytes(2))
        emu.write(G_SKY_OBJECTS, sky)
        emu.write(G_LIGHT_LEVEL, struct.pack("<h", light))
        emu.write(G_PARTY_MOVE_STEPS, struct.pack("<h", walking))
        emu.write(G_PUSH_MODE, bytes(4))
        emu.write(G_PUSH_PENDING_GROUP, struct.pack("<h", -1))
        emu.write(G_SKY_STATE, struct.pack("<H", self.entry_index(8, gfx_set, 0x0B, 0x66)))
        game = WeatherGame.in_dll(lib, "game")
        emu.write(G_RAIN_INTENSITY, bytes([game.rain_intensity, game.wind_dir]))
        emu.write_u32(G_RANDOM_SEED, VIEW_SEED)
        emu.write(self.viewport, bytes(VIEW_W * VIEW_H))
        emu.call(VIEW_DRAW_DUNGEON, direction, x, y)
        return emu.read(self.viewport, VIEW_W * VIEW_H)

    def sky_objects(self, gfx_set, darkness, clouds):
        """The rain and clouds of a weather state, as world_update_day_night builds them."""
        text = ctypes.create_string_buffer(128)
        out = b""
        # (amount, lowest amount shown, limits of the light and medium pictures, the 3 pictures)
        for amount, low, limits, entries in ((darkness, 0x10, (0x40, 0x80), (0x67, 0x68, 0x69)),
                                             (clouds, 0x40, (0x80, 0xC0), (0x6A, 0x6B, 0x6C))):
            if not self.lib.gdat_exists(self.gdat, 0x17, gfx_set, 1, entries[0]) or amount < low:
                continue
            entry = entries[sum(amount >= limit for limit in limits)]
            self.lib.gdat_text(self.gdat, 0x17, gfx_set, entry, text, 128)
            if text.value:
                out += struct.pack("<BBhhhBB", entry, self.lib.gdat_text_int_field(text.value, b"fw") & 0xFF,
                                   self.lib.gdat_text_int_field(text.value, b"cd"), 0, 0, 0x40, 0x40)
        return out + b"\xff" + bytes(9)


@pytest.fixture(scope="module")
def original_view(lib, gdat, start):
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = u8_p
    lib.gdat_image_colormap.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.gdat_image_colormap.restype = u8_p
    lib.gdat_decode_image.argtypes = [ctypes.c_void_p, ctypes.c_int, u8_p, ctypes.c_size_t]
    lib.gdat_image_size.argtypes = [ctypes.c_void_p, ctypes.c_int] + [ctypes.POINTER(ctypes.c_int)] * 3
    lib.gdat_image_offset.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3
    lib.gdat_exists.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_text_int_field.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    assert lib.zone_load(gdat) == 1
    assert lib.pal_load_color_ramps(gdat) == 1
    return OriginalView(lib, gdat, start)


class PartyHead(ctypes.Structure):
    """The first fields of the C movement state (move.h, Party)."""
    _fields_ = [(name, ctypes.c_int16) for name in ("cur_map", "square_x", "square_y", "square_map", "ref_dir",
                                                     "move_steps")]


def c_view(lib, map_, x, y, direction, light, sky=None, walking=0):
    game = WeatherGame.in_dll(lib, "game")
    game.party_x, game.party_y, game.party_dir, game.party_map = x, y, direction, map_
    party = PartyHead.in_dll(lib, "party")
    party.cur_map, party.move_steps = -1, walking  # 0: no walk; the current map of the dungeon reads becomes map_
    lib.dun_set_party_location(x, y, map_)
    lib.view_enter_map(map_)
    if sky is not None:  # the weather sky objects that world_update_day_night builds
        lib.weather_sky_objects.restype = ctypes.c_void_p
        ctypes.memmove(lib.weather_sky_objects(), sky, len(sky))
    ctypes.c_int.in_dll(lib, "view_light_level").value = light
    ctypes.c_uint32.in_dll(lib, "random_seed").value = VIEW_SEED
    viewport = Bitmap.in_dll(lib, "view_viewport")
    ctypes.memset(viewport.pixels, 0, VIEW_W * VIEW_H)
    lib.view_draw_dungeon(direction, x, y)
    return ctypes.string_at(viewport.pixels, VIEW_W * VIEW_H)


def test_view_picture_matches_original(lib, loaded, original_view):
    # The start view, then random squares of all maps in random directions and light levels: walls, ornaments,
    # wall texts, doors, stairs, pits, landmarks of the outdoor maps. A sweep of 1500 views matched too.
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible)
    original_view.emu.write(G_WEATHER_VISIBLE, b"\0")
    game.weather_visible = 0
    rng = random.Random(7)
    views = [(game.party_map, game.party_x, game.party_y, game.party_dir, 0)]
    while len(views) < 120:
        map_ = rng.randrange(lib.dungeon_map_count())
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        if lib.dungeon_square(map_, x, y) >> 5:  # not inside a wall
            views.append((map_, x, y, rng.randrange(4), rng.choice([0, 2, 5])))
    pictures = 0
    for map_, x, y, direction, light in views:
        original = original_view.draw(map_, x, y, direction, light, b"\xff" + bytes(9))
        pictures += len(set(original)) > 2  # not an empty viewport (a few maps have black graphics)
        assert c_view(lib, map_, x, y, direction, light) == original, (map_, x, y, direction, light)
    assert pictures > 100
    game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible = saved
    lib.view_enter_map(game.party_map)


def test_outdoor_sky_matches_original(lib, loaded, original_view):
    # Outdoor maps with rain and clouds of the saved weather, and the landmarks.
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible, game.sky_darkness,
             game.cloud_cover)
    outdoor = [m for m in range(lib.dungeon_map_count())
               if lib.gdat_exists(original_view.gdat, 0x17, lib.dungeon_map_graphics(m) >> 4 & 0xF, 1, 0x6A)]
    assert outdoor
    original_view.emu.write(G_WEATHER_VISIBLE, b"\1")
    rng = random.Random(8)
    views = 0
    while views < 40:
        map_ = rng.choice(outdoor)
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        if not lib.dungeon_square(map_, x, y) >> 5:
            continue
        game.weather_visible, game.sky_darkness, game.cloud_cover = 1, rng.choice([0, 0x20, 0x50, 0x90]), rng.choice([0, 0x50, 0x90, 0xD0])
        direction = rng.randrange(4)
        sky = original_view.sky_objects(lib.dungeon_map_graphics(map_) >> 4 & 0xF, game.sky_darkness, game.cloud_cover)
        mine = c_view(lib, map_, x, y, direction, 0, sky)
        assert mine == original_view.draw(map_, x, y, direction, 0, sky), (map_, x, y, direction)
        views += 1
    original_view.emu.write(G_WEATHER_VISIBLE, b"\0")
    (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible, game.sky_darkness,
     game.cloud_cover) = saved
    lib.view_enter_map(game.party_map)


def test_missing_save_exits_1(lib, game_dir, tmp_path):
    import subprocess
    result = subprocess.run([ROOT / "build" / "dm2", "--save", tmp_path / "none.DAT"], capture_output=True, timeout=10)
    assert result.returncode == 1
    assert b"cannot load the save" in result.stderr


G_MAP_COLUMNS = 0x7F3C8


def test_door_states_match_original(lib, loaded, original_view):
    # start.DAT has no half open or broken door: set the door state of 10 doors in front of the party in both.
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    lib.dungeon_square_byte.restype = u8_p
    emu = original_view.emu
    doors = []
    for map_ in range(lib.dungeon_map_count()):
        lib.view_enter_map(map_)
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                for direction in range(4):
                    if len(doors) == 10 or not lib.dungeon_square(map_, x, y) >> 5:
                        continue
                    c_view_squares(lib, x, y, direction)
                    door = (ctypes.c_uint8 * (SQUARE_COUNT * SQUARE_SIZE)).in_dll(lib, "view_squares")[3 * SQUARE_SIZE:]
                    if struct.unpack_from("<H", bytes(door), 2)[0] == 0x11:  # a door front ahead
                        doors.append((map_, x, y, direction, door[0], door[1]))
    assert len(doors) == 10
    for map_, x, y, direction, door_x, door_y in doors:
        square = lib.dungeon_square_byte(map_, door_x, door_y)
        columns = emu.read_u32(emu.read_u32(G_MAP_COLUMNS) + 4 * map_)
        original_square = emu.read_u32(columns + 4 * door_x) + door_y
        saved_square = square[0]
        for state in (1, 2, 3, 5):  # three half open states, broken
            square[0] = saved_square & ~7 | state
            emu.write(original_square, bytes([square[0]]))
            mine = c_view(lib, map_, x, y, direction, 0)
            assert mine == original_view.draw(map_, x, y, direction, 0, b"\xff" + bytes(9)), (map_, x, y, direction, state)
        square[0] = saved_square
        emu.write(original_square, bytes([saved_square]))
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


# ---- Things in the view (Step 12b) ----

VIEW_GRID5_CELL_TO_XY, VIEW_ROTATE_GRID5_CELL, VIEW_OFFSET_CREATURE = 0x18E7F, 0x19B0B, 0x52A88
OUT_X, OUT_Y = SCRATCH, SCRATCH + 4
GRID_SIZE, MASKS_SIZE = 0x166, 4 * SQUARE_COUNT
i16_p = ctypes.POINTER(ctypes.c_int16)


@pytest.fixture(scope="module")
def image_emu():
    from dm2tools.emu import Emu
    from dm2tools.le import load_image
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return Emu(load_image(ROOT / "game" / "SKULL.EXE"))


def read_xy(emu):
    return struct.unpack("<h", emu.read(OUT_X, 2))[0], struct.unpack("<h", emu.read(OUT_Y, 2))[0]


def test_grid5_cell_to_xy_matches_original(lib, image_emu):
    lib.aspect_cell_to_xy.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint16, i16_p, i16_p]
    rng = random.Random(21)
    x, y = ctypes.c_int16(), ctypes.c_int16()
    for _ in range(1000):
        base_x, base_y = rng.randrange(-40, 40), rng.randrange(-40, 40)
        cell = rng.randrange(25) if rng.random() < 0.8 else rng.randrange(0x10000)
        image_emu.call(VIEW_GRID5_CELL_TO_XY, base_x, base_y, cell, OUT_X, OUT_Y)
        lib.aspect_cell_to_xy(base_x, base_y, cell, ctypes.byref(x), ctypes.byref(y))
        assert (x.value, y.value) == read_xy(image_emu), (base_x, base_y, cell)


def test_rotate_grid5_cell_matches_original(lib, image_emu):
    for cell in range(25):
        for direction in range(4):
            original = struct.unpack("<h", struct.pack("<H", image_emu.call(VIEW_ROTATE_GRID5_CELL, cell, direction) & 0xFFFF))[0]
            assert lib.aspect_rotate_cell(cell, direction) == original, (cell, direction)


def test_offset_creature_toward_party_matches_original(lib, image_emu):
    lib.view_offset_creature_toward_party.argtypes = [ctypes.c_int16] * 3 + [i16_p, i16_p]
    rng = random.Random(22)
    x, y = ctypes.c_int16(), ctypes.c_int16()
    cases = [(px, py, 8, 8, size) for px in range(5, 12) for py in range(5, 12) for size in range(5)]
    cases += [(rng.randrange(-30, 90), rng.randrange(-30, 90), rng.randrange(-30, 90), rng.randrange(-30, 90),
               rng.randrange(6)) for _ in range(1000)]
    for party_x, party_y, cx, cy, size in cases:
        image_emu.write(OUT_X, struct.pack("<h", cx))
        image_emu.write(OUT_Y, struct.pack("<h", cy))
        image_emu.call(VIEW_OFFSET_CREATURE, party_x, party_y, size, OUT_X, OUT_Y)
        x.value, y.value = cx, cy
        lib.view_offset_creature_toward_party(party_x, party_y, size, ctypes.byref(x), ctypes.byref(y))
        assert (x.value, y.value) == read_xy(image_emu), (party_x, party_y, cx, cy, size)


def original_thing_grids(original_view, map_, x, y, direction):
    """The view squares, thing grids, and masks of the original view_load_square for the party at (x, y) of map_."""
    emu = original_view.emu
    if not hasattr(original_view, "grid_buffers"):
        original_view.grid_buffers = [original_view.alloc(0x200) for _ in range(5)]
    squares, creatures, objects, missile_masks, object_masks = original_view.grid_buffers
    for addr, buffer, size, fill in ((G_VIEW_SQUARES, squares, SQUARE_COUNT * SQUARE_SIZE, 0),
                                     (G_VIEW_CREATURE_GRID, creatures, GRID_SIZE, 0), (G_VIEW_OBJECT_GRID, objects, GRID_SIZE, 0xFF),
                                     (G_VIEW_MISSILE_MASKS, missile_masks, MASKS_SIZE, 0),
                                     (G_VIEW_OBJECT_MASKS, object_masks, MASKS_SIZE, 0)):
        emu.write(buffer, bytes([fill]) * size)
        emu.write_u32(addr, buffer)
    emu.call(DUN_SET_PARTY_LOCATION, x, y, map_)
    emu.write(G_PARTY_X, struct.pack("<hh", x, y))
    emu.write(G_PARTY_DIR, struct.pack("<h", direction))
    emu.write(G_VIEW_PARTY_DIR, struct.pack("<h", direction))
    for pos in range(SQUARE_COUNT - 1, -1, -1):
        emu.call(VIEW_LOAD_SQUARE, x, y, pos, direction)
    emu.call(VIEW_UNHIDE_LINKED_SQUARES)
    emu.call(VIEW_MARK_HIDDEN_SQUARES)
    return (emu.read(squares, SQUARE_COUNT * SQUARE_SIZE), emu.read(creatures, GRID_SIZE), emu.read(objects, GRID_SIZE),
            emu.read(missile_masks, MASKS_SIZE), emu.read(object_masks, MASKS_SIZE))


def c_thing_grids(lib, map_, x, y, direction):
    lib.view_enter_map(map_)
    grids = [(ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "view_creature_grid"), (ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "view_object_grid")]
    ctypes.memset(grids[0], 0, GRID_SIZE)
    ctypes.memset(grids[1], 0xFF, GRID_SIZE)
    squares = c_view_squares(lib, x, y, direction)
    masks = [bytes((ctypes.c_uint8 * MASKS_SIZE).in_dll(lib, name)) for name in ("view_missile_masks", "view_object_masks")]
    return (squares, bytes(grids[0]), bytes(grids[1]), *masks)


def views_with_things(lib, count, seed):
    """Random views of all maps whose squares hold objects or creatures."""
    rng = random.Random(seed)
    views = []
    while len(views) < count:
        map_ = rng.randrange(lib.dungeon_map_count())
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        direction = rng.randrange(4)
        if not lib.dungeon_square(map_, x, y) >> 5:
            continue
        lib.view_enter_map(map_)
        c_view_squares(lib, x, y, direction)
        if any((ctypes.c_uint32 * SQUARE_COUNT).in_dll(lib, "view_object_masks")) or \
                any(v != 0xFF for v in (ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "view_object_grid")):
            views.append((map_, x, y, direction))
    return views


def test_thing_grids_match_original(lib, loaded, original_view):
    # The creature cells, the creatures in the object grid, and the cells with objects and missiles.
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    ctypes.memset((ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "view_object_grid"), 0xFF, GRID_SIZE)
    for map_, x, y, direction in views_with_things(lib, 300, 23):
        game.party_x, game.party_y, game.party_dir, game.party_map = x, y, direction, map_
        assert c_thing_grids(lib, map_, x, y, direction) == original_thing_grids(original_view, map_, x, y, direction), \
            (map_, x, y, direction)
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


DUN_GET_THING_DATA, G_TIMELINE_EVENTS = 0x1D2DA, 0x760E8
THING_END, CAT_WEAPON = 0xFFFE, 0x10
THING_SCALE = [0x13, 0x60, 0x57, 0x4E, 0x47, 0x40, 0x3A, 0x34, 0x2F, 0x2B, 0x27, 0x23, 0x1F, 0x1C, 0x1A, 0x17, 0x15, 0x13, 0x11]


def thing_list(lib, first):
    things, thing = [], first
    while thing != THING_END:
        things.append(thing)
        thing = lib.thing_word(thing, 0) & 0xFFFF
    return things


def test_one_object_draws_in_its_cell_rectangle(lib, gdat, loaded):
    # A floor square in front of the party with one weapon in its near left cell: the pixels that change lie in
    # the rectangle of the weapon picture, scaled for its row, at the zone of its cell.
    lib.thing_word.restype = ctypes.c_uint16
    lib.gdat_entry_index.restype = ctypes.c_uint16
    lib.gdat_entry_index.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_image_offset.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3
    lib.gfx_scale_64.restype = ctypes.c_int16
    lib.gfx_scale_64.argtypes = [ctypes.c_int16, ctypes.c_int16]
    lib.zone_compute_rect.argtypes = [ctypes.POINTER(Bitmap), ctypes.POINTER(ctypes.c_int16), ctypes.c_int, i16_p, i16_p,
                                      ctypes.c_int]
    lib.zone_compute_rect.restype = ctypes.c_void_p
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    assert lib.zone_load(gdat) == 1
    assert lib.pal_load_color_ramps(gdat) == 1
    g = gdat
    index = next(i for i in range(128) if lib.gdat_find(g, CAT_WEAPON, i, 1, 0) >= 0)
    map_ = game.party_map
    lib.view_enter_map(map_)
    view = None
    for x in range(lib.dungeon_map_width(map_)):
        for y in range(lib.dungeon_map_height(map_)):
            if view or not lib.dungeon_square(map_, x, y) >> 5:
                continue
            grid = (ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "view_object_grid")
            ctypes.memset(grid, 0xFF, GRID_SIZE)
            squares = c_view_squares(lib, x, y, 0)
            front = struct.unpack_from("<bbHHH", squares, 3 * SQUARE_SIZE)
            if front[2] == 1 and front[4] == THING_END and all(v == 0xFF for v in grid):
                view = (x, y, front[0], front[1])
    assert view
    x, y, front_x, front_y = view
    thing = lib.dungeon_unused_thing(5) & 0xFFFF | 3 << 14  # south west: the near left cell for a party looking north
    lib.set_thing_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    lib.set_thing_word(thing, 2, index)
    assert lib.dungeon_link_thing_to_square(thing, map_, front_x, front_y)
    with_object = c_view(lib, map_, x, y, 0, 0)
    lib.dungeon_unlink_thing_from_square(thing, map_, front_x, front_y)
    without = c_view(lib, map_, x, y, 0, 0)
    changed = [divmod(i, VIEW_W) for i in range(VIEW_W * VIEW_H) if with_object[i] != without[i]]
    assert changed
    # cell 16 of the square at depth 1: row 1, scale THING_SCALE[1 * 4 + 1 + 2]; offset of ordinal 0 is (2, -3)
    scale = THING_SCALE[7]
    w, h, bpp = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
    lib.gdat_image_size(g, lib.gdat_find(g, CAT_WEAPON, index, 1, 0), ctypes.byref(w), ctypes.byref(h), ctypes.byref(bpp))
    picture = Bitmap(None, lib.gfx_scale_64(w.value, scale), lib.gfx_scale_64(h.value, scale), bpp.value)
    dx, dy = 2, -3
    for packed in (lib.gdat_entry_index(g, CAT_WEAPON, index, 0x0C, 0xFE), lib.gdat_image_offset(g, CAT_WEAPON, index, 0),
                   lib.gdat_entry_index(g, CAT_WEAPON, 0xFE, 0x0C, 0)):
        dx += struct.unpack("b", bytes([packed >> 8 & 0xFF]))[0]
        dy += struct.unpack("b", bytes([packed & 0xFF]))[0]
    sx, sy = ctypes.c_int16(lib.gfx_scale_64(dx, scale)), ctypes.c_int16(lib.gfx_scale_64(dy, scale))
    rect = (ctypes.c_int16 * 4)()
    zone = 5000 + 3 * 25 + 16 | 0x8000
    assert lib.zone_compute_rect(ctypes.byref(picture), rect, zone, ctypes.byref(sx), ctypes.byref(sy), 0)
    rx, ry, rw, rh = rect
    assert all(rx <= col < rx + rw and ry <= row < ry + rh for row, col in changed), (rect[:], changed[:5])
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


SPELL_MISSILES = (0, 2, 3, 4, 6, 7, 9, 10, 13, 49)
EXPLOSION_TYPES = (0, 2, 3, 4, 6, 7, 9, 10, 13, 14, 40, 48, 49)
THING_GROUP, THING_PROJECTILE, THING_EXPLOSION, EVENT_SIZE = 4, 14, 15, 12
HIDDEN_POSSESSION_TYPES = (19, 26, 37)  # creature infos 7, 10, 39: static, possessions not shown


class CreatureInfo(ctypes.Structure):
    _fields_ = [("flags", ctypes.c_uint16), ("possessions", ctypes.c_uint16), ("size", ctypes.c_uint8)]


class Things:
    """Writes thing records and event data the same way into the C port and into the original."""

    def __init__(self, lib, emu):
        self.lib, self.emu = lib, emu
        lib.dungeon_thing_record.restype = u8_p
        lib.dungeon_thing_record.argtypes = [ctypes.c_uint16]
        self.events = ctypes.cast(ctypes.c_void_p.in_dll(lib, "timeline"), u8_p)

    def write(self, thing, offset, data):
        record = self.lib.dungeon_thing_record(thing)
        for i, b in enumerate(data):
            record[offset + i] = b
        self.emu.write(self.emu.call(DUN_GET_THING_DATA, thing) + offset, data)

    def write_event(self, index, offset, data):
        for i, b in enumerate(data):
            self.events[index * EVENT_SIZE + offset + i] = b
        self.emu.write(self.emu.read_u32(G_TIMELINE_EVENTS) + index * EVENT_SIZE + offset, data)

    def read_event(self, index):
        return bytes(self.events[index * EVENT_SIZE:(index + 1) * EVENT_SIZE])


def test_added_missiles_explosions_creatures_match_original(lib, loaded, original_view):
    # start.DAT has no missiles or explosions: add one at the end of the thing list of a square in view, with a
    # random cell, payload, power, flight direction, or explosion type, or a creature of any type with or without
    # a possession (also creatures that hide it, and static creatures in other animation frames), then compare the
    # whole view picture.
    lib.thing_word.restype = ctypes.c_uint16
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    things = Things(lib, original_view.emu)
    weapons = [t for t in range(lib.dungeon_thing_count(5)) if lib.dungeon_thing_valid(5 << 10 | t)]
    saved_event = things.read_event(0)
    free_groups = [t for t in range(lib.dungeon_thing_count(THING_GROUP)) if lib.thing_word(THING_GROUP << 10 | t, 0) == 0xFFFF]
    free_weapons = [t for t in range(lib.dungeon_thing_count(5)) if lib.thing_word(5 << 10 | t, 0) == 0xFFFF]
    creature_types = [t for t in range(256) if lib.gdat_exists(original_view.gdat, 0x0F, t, 7, 0xFD)]
    assert free_groups and free_weapons and creature_types
    lib.creature_info.restype = ctypes.POINTER(CreatureInfo)
    lib.creature_info.argtypes = [ctypes.c_void_p, ctypes.c_int]
    size = ctypes.c_uint32()

    def frame_data(creature):
        item = lib.gdat_find(original_view.gdat, 0x0F, creature, 7, 0xFD)
        return ctypes.string_at(lib.gdat_item(original_view.gdat, item, ctypes.byref(size)), size.value)

    def frame_count(creature):
        return len(frame_data(creature)) // 8

    hidden_cases = shifted_cases = 0
    shifted_types = [t for t in creature_types if frame_count(t) and frame_data(t)[6]]  # moving in their first frame
    rng = random.Random(24)
    views = 0
    for map_, x, y, direction in views_with_things(lib, 700, 25):
        lib.view_enter_map(map_)
        squares = c_view_squares(lib, x, y, direction)
        positions = [p for p in (0, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15)
                     if struct.unpack_from("<H", squares, p * SQUARE_SIZE + 6)[0] != THING_END]
        if not positions:
            continue
        last = thing_list(lib, struct.unpack_from("<H", squares, rng.choice(positions) * SQUARE_SIZE + 6)[0])[-1]
        cell = rng.randrange(4) << 14
        kind = rng.random()
        if kind < 0.4:
            thing = THING_PROJECTILE << 10 | rng.randrange(lib.dungeon_thing_count(THING_PROJECTILE)) | cell
            payload = 0xFF80 + rng.choice(SPELL_MISSILES) if rng.random() < 0.7 else 5 << 10 | rng.choice(weapons)
            power = rng.choice([0xFF, rng.randrange(256)])
            things.write(thing, 0, struct.pack("<HHBBH", THING_END, payload, power, 0, 0))
            things.write_event(0, 8, struct.pack("<H", rng.randrange(4) << 10))
        elif kind < 0.75:
            thing = THING_GROUP << 10 | rng.choice(free_groups) | cell
            hidden = rng.random() < 0.3  # creatures that show no possessions
            shifted = rng.random() < 0.2
            creature = rng.choice(HIDDEN_POSSESSION_TYPES if hidden else shifted_types if shifted else creature_types)
            possessions = THING_END
            if hidden or rng.random() < 0.7:
                possessions = 5 << 10 | rng.choice(free_weapons) | rng.randrange(4) << 14
                things.write(possessions, 0, struct.pack("<HH", THING_END, rng.randrange(40)))
            sequence = state = 0
            flags = lib.creature_info(original_view.gdat, creature).contents.flags
            frames = frame_count(creature)
            if flags & 1 and frames:  # a static creature keeps its animation in the record: a still frame
                sequence = rng.randrange(frames)
                state = rng.randrange(min(64, frames - sequence))
            creature_dir = rng.randrange(4)
            record = struct.pack("<HHBBHHHHH", THING_END, possessions, creature, 0xFF, 100, sequence, state, 0,
                                 creature_dir << 8)
            things.write(thing, 0, record)
            hidden_cases += hidden
            side = 2 if flags & 4 else (direction - creature_dir) & 3
            shifted_cases += frame_data(creature)[(sequence + state) * 8 + 6] != 0 and side in (0, 2)
        else:
            thing = THING_EXPLOSION << 10 | rng.randrange(lib.dungeon_thing_count(THING_EXPLOSION)) | cell
            data = rng.choice(EXPLOSION_TYPES) | rng.choice([0, 0x80]) | rng.randrange(256) << 8
            things.write(thing, 0, struct.pack("<HH", THING_END, data))
        things.write(last, 0, struct.pack("<H", thing))
        mine = c_view(lib, map_, x, y, direction, rng.choice([0, 3]))
        light = ctypes.c_int.in_dll(lib, "view_light_level").value
        original = original_view.draw(map_, x, y, direction, light, b"\xff" + bytes(9))
        things.write(last, 0, struct.pack("<H", THING_END))
        assert mine == original, (map_, x, y, direction, hex(thing), hex(last))
        views += 1
    things.write_event(0, 0, saved_event)
    assert views > 200 and hidden_cases > 10 and shifted_cases > 0
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


VIEW_DRAW_WET_IMAGE, G_MAP_GFX_SET_BYTE, G_VIEW_SHADE = 0x4E930, 0x75BFE, 0x802CE
WET_INDEX, CTX_SIZE = 0x7000, 0x13A
CTX_W, CTX_RECT, CTX_PALETTE_COUNT, CTX_PALETTE = 0x12, 0x24, 0x38, 0x3A


class DrawCtx(ctypes.Structure):
    _fields_ = [("image", ctypes.POINTER(Bitmap)), ("x", ctypes.c_int16), ("y", ctypes.c_int16), ("w", ctypes.c_int16),
                ("h", ctypes.c_int16), ("category", ctypes.c_uint8), ("index", ctypes.c_uint8), ("entry", ctypes.c_uint8),
                ("item", ctypes.c_int16), ("zone", ctypes.c_int16), ("anchor", ctypes.c_int16), ("dx", ctypes.c_int16),
                ("dy", ctypes.c_int16), ("src_x", ctypes.c_int16), ("src_y", ctypes.c_int16),
                ("rect", ctypes.c_int16 * 4), ("dst", ctypes.POINTER(Bitmap)), ("transparent", ctypes.c_int16),
                ("flip", ctypes.c_int16), ("scale_x", ctypes.c_int16), ("scale_y", ctypes.c_int16),
                ("palette_count", ctypes.c_int16), ("palette", ctypes.c_uint8 * 256)]


def test_shop_displays_match_original(lib, gdat, loaded, original_view):
    # Each shop of start.DAT seen from the front: the display steps to the next item (the shop flag 4 of the save),
    # then stays; a click below the center steps back, a click above steps forward, a click at the center steps back. The item, its name, and the prices
    # in coins, in 3 light levels.
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    emu = original_view.emu
    lib.thing_word.restype = ctypes.c_uint16
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.dungeon_map_wall_ornaments.restype = ctypes.POINTER(ctypes.c_uint8)
    shops = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                t = lib.dungeon_square_first_thing(map_, x, y)
                while t != 0xFFFE:
                    flags = lib.thing_word(t, 4)
                    if t >> 10 & 15 == 3 and lib.thing_word(t, 2) & 0x7F == 0x3F and flags >> 12:
                        ornament = lib.dungeon_map_wall_ornaments(map_)[(flags >> 12) - 1]
                        if lib.gdat_entry_index(gdat, 9, ornament, 0x0B, 0x0A) == 2:
                            shops.append((map_, x, y, t >> 14, t))
                    t = lib.dungeon_next_thing(t)
    assert len(shops) == 8
    for map_, x, y, cell, shop in shops:
        party_x, party_y = x + (0, 1, 0, -1)[cell], y + (-1, 0, 1, 0)[cell]
        target = lib.thing_word(shop, 6)
        display = lib.dungeon_square_first_thing(map_, target >> 6 & 31, target >> 11)
        items = []
        for step, (light, offset) in enumerate(((0, None), (3, None), (2, 50), (0, -50), (5, 0))):
            if offset is not None:  # a click with an empty hand below or above the center clears the shown flag
                Things(lib, emu).write(shop, 4, struct.pack("<H", lib.thing_word(shop, 4) & ~1))
                ctypes.c_int16.in_dll(lib, "view_hit_offset_y").value = offset
                emu.write(G_VIEW_HIT_OFFSET, struct.pack("<h", offset))
            original = original_view.draw(map_, party_x, party_y, (cell + 2) & 3, light, b"\xff" + bytes(9))
            assert c_view(lib, map_, party_x, party_y, (cell + 2) & 3, light) == original, (map_, x, y, step)
            items.append(lib.thing_word(display, 2) >> 7)
            assert struct.unpack("<H", emu.read(emu.call(DUN_GET_THING_DATA, display) + 2, 2))[0] >> 7 == items[-1]
        assert items[0] == items[1] == items[3] != items[2] == items[4], (map_, x, y)
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


def test_wet_image_matches_original(lib, loaded, original_view):
    # The rain background of wet ornaments of all widths, with small widths where the tile row of the rain bitmap
    # (rect width x rain height - 16 - 0 to 31 pixels) is shorter than its random start. There the original tiles
    # from past the end of the rain bitmap; the port fills the color key.
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.rain_intensity, game.wind_dir)
    gdat, emu = original_view.gdat, original_view.emu
    map_ = next(m for m in range(lib.dungeon_map_count())
                if lib.gdat_exists(gdat, 0x17, lib.dungeon_map_graphics(m) >> 4 & 0xF, 1, 0x71))
    x, y = next((x, y) for x in range(lib.dungeon_map_width(map_)) for y in range(lib.dungeon_map_height(map_))
                if lib.dungeon_square(map_, x, y) >> 5)
    lib.view_draw_wet_image.argtypes = [ctypes.POINTER(DrawCtx), ctypes.POINTER(Bitmap), ctypes.c_int]
    lib.rand_below.restype = ctypes.c_uint16
    lib.rand_below.argtypes = [ctypes.c_uint16]
    lib.rand_next.restype = ctypes.c_uint32
    seed = ctypes.c_uint32.in_dll(lib, "random_seed")
    rng = random.Random(26)
    ctx_addr = original_view.alloc(CTX_SIZE)
    drawn = filled = small_drawn = 0
    for case in range(400):
        direction, light = rng.randrange(4), rng.choice([0, 3])
        game.rain_intensity, game.wind_dir = rng.choice([5, 0x20, 0x50, 0x90]), rng.randrange(4)
        c_view(lib, map_, x, y, direction, light)
        emu.write(G_MAP_GFX_SET_BYTE, bytes([lib.dungeon_map_graphics(map_) >> 4 & 0xF]))
        emu.write(G_VIEW_PARTY_DIR, struct.pack("<h", direction))
        emu.write(G_VIEW_SHADE, bytes([light * 10]))
        emu.write(G_RAIN_INTENSITY, bytes([game.rain_intensity, game.wind_dir]))
        w = rng.choice([1, 2, 3, 3, 3, 4, 5, 8, 13, 30])
        h = rng.choice([1, 4, 20])
        key = rng.randrange(16)
        palette = bytes(rng.randrange(256) for _ in range(16)) if rng.random() < 0.5 else None
        ctx = DrawCtx()
        ctx.w, ctx.h = w, h
        ctx.rect[:] = [0, 0, w, h]
        ctx.palette_count = 16 if palette else -1
        if palette:
            ctx.palette[:16] = palette
        pixels = (ctypes.c_uint8 * (w * h))()
        offscreen = Bitmap(ctypes.cast(pixels, u8_p), w, h, 8)
        emu.write(ctx_addr, bytes(CTX_SIZE))
        emu.write(ctx_addr + CTX_W, struct.pack("<h", w))
        emu.write(ctx_addr + CTX_RECT, struct.pack("<4h", 0, 0, w, h))
        emu.write(ctx_addr + CTX_PALETTE_COUNT, struct.pack("<h", ctx.palette_count))
        if palette:
            emu.write(ctx_addr + CTX_PALETTE, palette)
        original_pixels = original_view.cache_alloc_image(WET_INDEX, w, h, 8)
        case_seed = rng.getrandbits(32)
        # the tile row and its start, from the same random numbers
        seed.value = case_seed
        area = struct.unpack("<h", struct.pack("<H", w * 20 & 0xFFFF))[0]
        start = lib.rand_below((area - 0x3C) & 0xFFFF)
        row = struct.unpack("<h", struct.pack("<H", area - (lib.rand_next() & 0x1F) - 0x10 & 0xFFFF))[0]
        seed.value = case_seed
        lib.view_draw_wet_image(ctypes.byref(ctx), ctypes.byref(offscreen), key)
        mine = bytes(pixels)
        if row <= start:
            assert mine == bytes([palette[key] if palette else key]) * (w * h), (case, w, h)
            filled += 1
            continue
        emu.write_u32(G_RANDOM_SEED, case_seed)
        emu.call(VIEW_DRAW_WET_IMAGE, ctx_addr, WET_INDEX, key)
        assert mine == emu.read(original_pixels, w * h), (case, w, h, key)
        drawn += 1
        small_drawn += area <= 0x3C
    assert drawn > 100 and filled > 10 and small_drawn > 5
    (game.party_x, game.party_y, game.party_dir, game.party_map, game.rain_intensity, game.wind_dir) = saved
    lib.view_enter_map(game.party_map)


G_VIEW_CLICK_ZONES, G_VIEW_CLICK_ZONE_COUNT, CLICK_ZONE_SIZE = 0x7F2EC, 0x7F400, 12


def used_click_zones(raw):
    """The click zones without the old rects and cells of the unused fixed zones (kind 0): the view keeps them."""
    zones = [raw[i:i + CLICK_ZONE_SIZE] for i in range(0, len(raw), CLICK_ZONE_SIZE)]
    return [z if z[11] else z[8:10] + z[11:] for z in zones]


def test_view_click_zones_match_original(lib, loaded, original_view):
    # The click zones that the view drawing records (objects, alcoves, wall ornaments, door buttons, possessions)
    # for random places of all maps, as in test_view_picture_matches_original.
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible)
    original_view.emu.write(G_WEATHER_VISIBLE, b"\0")
    game.weather_visible = 0
    emu = original_view.emu
    count = ctypes.c_int.in_dll(lib, "view_click_zone_count")
    zones = (ctypes.c_uint8 * (32 * CLICK_ZONE_SIZE)).in_dll(lib, "view_click_zones")
    rng = random.Random(21)
    views = with_zones = 0
    while views < 300:
        map_ = rng.randrange(lib.dungeon_map_count())
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        if not lib.dungeon_square(map_, x, y) >> 5:
            continue
        direction = rng.randrange(4)
        c_view(lib, map_, x, y, direction, 0)
        original_view.draw(map_, x, y, direction, 0, b"\xff" + bytes(9))
        n = struct.unpack("<h", emu.read(G_VIEW_CLICK_ZONE_COUNT, 2))[0]
        assert count.value == n, (map_, x, y, direction)
        mine = used_click_zones(bytes(zones)[:n * CLICK_ZONE_SIZE])
        assert mine == used_click_zones(emu.read(G_VIEW_CLICK_ZONES, n * CLICK_ZONE_SIZE)), (map_, x, y, direction)
        with_zones += any(emu.read(G_VIEW_CLICK_ZONES + i * 12 + 11, 1)[0] for i in range(n))
        views += 1
    assert with_zones > 10
    game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible = saved
    lib.view_enter_map(game.party_map)


def test_walking_view_matches_original(lib, loaded, original_view):
    # While the party walks, only the walk rectangle of the view is drawn.
    game = WeatherGame.in_dll(lib, "game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible)
    original_view.emu.write(G_WEATHER_VISIBLE, b"\0")
    game.weather_visible = 0
    rng = random.Random(11)
    views = [(game.party_map, game.party_x, game.party_y, game.party_dir, 0)]
    while len(views) < 60:
        map_ = rng.randrange(lib.dungeon_map_count())
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        if lib.dungeon_square(map_, x, y) >> 5:
            views.append((map_, x, y, rng.randrange(4), rng.choice([0, 2, 5])))
    pictures = 0
    for map_, x, y, direction, light in views:
        original = original_view.draw(map_, x, y, direction, light, b"\xff" + bytes(9), walking=1)
        pictures += len(set(original)) > 2
        assert c_view(lib, map_, x, y, direction, light, walking=1) == original, (map_, x, y, direction, light)
    assert pictures > 40
    game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible = saved
    lib.view_enter_map(game.party_map)


def present_viewport(lib, gdat, walking):
    """The screen viewport rect after game_present_viewport of a view whose walk rectangle holds the values 2 up."""
    walk = (ctypes.c_int16 * 4).in_dll(lib, "walk_view_rect")
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_load(gdat) == 1
    PartyHead.in_dll(lib, "party").move_steps = 0
    lib.zone_get_rect.restype = ctypes.c_void_p
    assert lib.zone_get_rect(7, rect)  # ZONE_VIEWPORT
    wx, wy, ww, wh = walk
    view = bytearray([1]) * (VIEW_W * VIEW_H)  # 1: outside the walk rectangle
    for y in range(wh):
        for x in range(ww):
            view[(wy + y) * VIEW_W + wx + x] = 2 + (x * 7 + y * 13) % 250
    viewport = Bitmap.in_dll(lib, "view_viewport")
    ctypes.memmove(viewport.pixels, bytes(view), len(view))
    PartyHead.in_dll(lib, "party").move_steps = walking
    lib.game_present_viewport()
    PartyHead.in_dll(lib, "party").move_steps = 0
    screen = Bitmap.in_dll(lib, "draw_screen")
    shown = [ctypes.string_at(ctypes.addressof(screen.pixels.contents) + (rect[1] + y) * screen.width + rect[0], VIEW_W) for y in range(VIEW_H)]
    return view, shown, (wx, wy, ww, wh)


def test_present_viewport_stretches_the_walking_view(lib, gdat, loaded):
    view, shown, (wx, wy, ww, wh) = present_viewport(lib, gdat, 1)
    assert all(1 not in row for row in shown)
    corner = lambda x, y: view[(wy + y) * VIEW_W + wx + x]
    assert (shown[0][0], shown[0][-1]) == (corner(0, 0), corner(ww - 1, 0))
    assert (shown[-1][0], shown[-1][-1]) == (corner(0, wh - 1), corner(ww - 1, wh - 1))


def test_present_viewport_copies_the_still_view(lib, gdat, loaded):
    view, shown, _ = present_viewport(lib, gdat, 0)
    assert b"".join(shown) == bytes(view)
