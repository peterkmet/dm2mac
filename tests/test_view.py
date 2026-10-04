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
# The original functions that Step 12a does not port: things, creatures, rain, click zones (12b, 14, 15) do nothing;
# the image cache, the GDAT index, and the memory manager are served from the C port and Python.
NO_OPS = (0x52518, 0x51203, 0x5142D, 0x509E6, 0x4E79C, 0x5288F, 0x528C5, 0x4E6C3, 0x15A02,  # 12b
          0x3F3C2, 0x3F406, 0x3F352, 0x3F422, 0x3F49D, 0x3BCA5, 0x3BC8E, 0x3BCBA, 0x210DC, 0x50BF4)
SHOP_DRAW_DISPLAY, VIEW_DRAW_WET_IMAGE = 0x4EC19, 0x4E930
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
        emu.stub(MEM_ALLOC, lambda size, *_: self.alloc(size))
        emu.stub(GDAT_ENTRY_INDEX, self.entry_index)
        emu.stub(WEATHER_INIT, lambda *_: 0)
        emu.stub(READ_LEVEL_TABLE, lambda *_: 0)
        emu.run(LOAD_BEGIN, LOAD_END)
        emu.stub(0x3EC2B, lambda c, i, e, *_: self.image(self.image_item(c, i, e)))  # gdat_get_image
        emu.stub(0x3EE1D, self.image_palette)
        emu.stub(0x3EED3, lambda c, i, e, *_: lib.gdat_image_offset(gdat, c & 0xFF, i & 0xFF, e & 0xFF) & 0xFFFF)
        emu.stub(0x3C92E, lambda c, i, t, e: lib.gdat_exists(gdat, c & 0xFF, i & 0xFF, t & 0xFF, e & 0xFF))
        emu.stub(GDAT_GET_TEXT, self.text)
        emu.stub(0x3ECC7, self.entry_data)
        emu.stub(0x3F5B0, self.image_size, stack_args=1)
        emu.stub(0x3F186, self.cache_lookup)  # image_cache_lookup_or_add: always a new entry
        emu.stub(0x3F279, lambda index, size, *_: self.cache_put(index, self.alloc(size)))
        emu.stub(0x3F2F0, self.cache_alloc_image)
        emu.stub(0x3F337, lambda index, *_: self.cache[index & 0xFFFF])
        emu.stub(0x3F3EE, lambda *_: self.new_index())
        for addr in NO_OPS:
            emu.stub(addr, lambda *_: 0)
        emu.stub(SHOP_DRAW_DISPLAY, lambda *_: 0, stack_args=5)
        emu.stub(VIEW_DRAW_WET_IMAGE, self.wet_image)  # like the 12a port: the offscreen bitmap starts clear
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

    def wet_image(self, ctx, index, key, *_):
        emu = self.emu
        if struct.unpack("<h", emu.read(ctx + 0x38, 2))[0] > 0:
            key = emu.read(ctx + 0x3A + (key & 0xFF), 1)[0]
        addr = self.cache[index & 0xFFFF]
        bpp, width, height = emu.read(addr - 6, 1)[0], *struct.unpack("<hh", emu.read(addr - 4, 4))
        emu.write(addr, bytes([key & 0xFF]) * (((width + 1) // 2 if bpp == 4 else width) * height))

    def draw(self, map_, x, y, direction, light, sky):
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
        emu.write(G_PARTY_MOVE_STEPS, bytes(2))
        emu.write(G_PUSH_MODE, bytes(4))
        emu.write(G_PUSH_PENDING_GROUP, struct.pack("<h", -1))
        emu.write(self.viewport, bytes(VIEW_W * VIEW_H))
        emu.call(VIEW_DRAW_DUNGEON, direction, x, y)
        return emu.read(self.viewport, VIEW_W * VIEW_H)

    def sky_objects(self, gfx_set, darkness, clouds):
        """The rain and clouds of a weather state, as the C port builds them (Step 17 builds them in the weather)."""
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
                out += struct.pack("<BBhhhBB", entry, self.lib.text_find_int_field(text.value, b"fw") & 0xFF,
                                   self.lib.text_find_int_field(text.value, b"cd"), 0, 0, 0x40, 0x40)
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
    lib.text_find_int_field.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    assert lib.zone_load(gdat) == 1
    assert lib.pal_load_color_ramps(gdat) == 1
    return OriginalView(lib, gdat, start)


def c_view(lib, map_, x, y, direction, light):
    game = WeatherGame.in_dll(lib, "game")
    game.party_x, game.party_y, game.party_dir, game.party_map = x, y, direction, map_
    lib.view_enter_map(map_)
    ctypes.c_int.in_dll(lib, "view_light_level").value = light
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
        mine = c_view(lib, map_, x, y, direction, 0)
        sky = original_view.sky_objects(lib.dungeon_map_graphics(map_) >> 4 & 0xF, game.sky_darkness, game.cloud_cover)
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
