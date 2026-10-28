"""The original view drawing in the emulator, with the GDAT data of the C port."""

import ctypes
import struct

from dm2test import constants as C
from dm2test.gdat import GdatKey, image_info
from dm2test.markers import ROOT
from dm2test.save import Game


DUN_SET_PARTY_LOCATION, VIEW_LOAD_SQUARE = 0x1C82D, 0x52BF6
GDAT_GET_TEXT = 0x3A921
G_PARTY_MOVE_STEPS, G_VIEW_ARROWS_ENABLED, G_VIEW_PARTY_DIR = 0x7F258, 0x802CA, 0x802C4
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
ACTIVE_GROUP_SIZE, ACTIVE_GROUP_MAX = C.ACTIVE_GROUP_SIZE, 256
G_VIEW_HIT_OFFSET = 0x802D4  # int16 y, x: the place of the last view hit test, in percent
VIEW_SEED = 0x2F6B1D35  # the random numbers of sparkles, rain, and explosions start from it in each view
MOVE_THING, VIEW_SEED_ADDR = 0x4B108, 0x70518
HEAP, HEAP_SIZE = 0x01000000, 0x02000000
VIEW_W, VIEW_H = C.VIEWPORT_WIDTH, C.VIEWPORT_HEIGHT
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
        from dm2test.interface import G_FONT_BITMAP, G_UI_COLORS, MONEY_INIT_COIN_TABLE
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
        return max(value, 0) if type_ & 0xFF in (C.GDAT_TYPE_VALUE, C.GDAT_TYPE_VALUE2) else value & 0xFFFF

    def image_item(self, category, index, entry):
        item = self.lib.gdat_find(self.gdat, category & 0xFF, index & 0xFF, 1, entry & 0xFF)
        return item if item >= 0 else self.lib.gdat_find(self.gdat, 0x15, 0xFE, 1, 0xFE)

    def image(self, item):
        if item not in self.images:
            result, info = image_info(self.lib, self.gdat, item)
            assert result == 0
            pixels = (ctypes.c_uint8 * max(info.size, 1))()
            assert self.lib.gdat_decode_image(self.gdat, item, pixels, info.size) == 0
            self.images[item] = self.put(struct.pack("<BBhh", info.bpp, 0, info.width, info.height) + bytes(pixels)) + 6
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
        info = image_info(self.lib, self.gdat, self.image_item(category, index, entry))[1]
        self.emu.write(width, struct.pack("<H", info.width))
        self.emu.write(height, struct.pack("<H", info.height))

    def text(self, category, index, entry, buf):
        text = ctypes.create_string_buffer(128)
        self.lib.gdat_text(self.gdat, GdatKey(category & 0xFF, index & 0xFF, entry & 0xFF), text, 128)
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
        game = WeatherGame.in_dll(lib, "g_game")
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
            self.lib.gdat_text(self.gdat, GdatKey(0x17, gfx_set, entry), text, 128)
            if text.value:
                out += struct.pack("<BBhhhBB", entry, self.lib.gdat_text_int_field(text.value, b"fw") & 0xFF,
                                   self.lib.gdat_text_int_field(text.value, b"cd"), 0, 0, 0x40, 0x40)
        return out + b"\xff" + bytes(9)


def draw_both(sc):
    """The view of the party in both, each with its own light level and sky objects."""
    from dm2test import view as tv
    from dm2test.scenario import SKY_OBJECTS_SIZE, WEATHER
    lib, emu, game = sc.lib, sc.emu, sc.game
    light = struct.unpack("<h", emu.read(WEATHER["light_level"][0], 2))[0]
    sky = emu.read(G_SKY_OBJECTS, SKY_OBJECTS_SIZE)
    seed = ctypes.c_uint32.in_dll(lib, "g_random_seed")
    saved = seed.value
    kept = [(addr, emu.read(addr, 4)) for addr in (tv.G_PARTY_MOVE_STEPS, tv.G_PUSH_MODE, tv.G_PUSH_PENDING_GROUP)]
    steps, sc.party.move_steps = sc.party.move_steps, 0  # the view of the party square, not of a walk
    theirs = sc.view.draw(game.party_map, game.party_x, game.party_y, game.party_dir, light, sky)
    for addr, data in kept:
        emu.write(addr, data)
    seed.value = tv.VIEW_SEED
    viewport = tv.Bitmap.in_dll(lib, "g_view_viewport")
    ctypes.memset(viewport.pixels, 0, tv.VIEW_W * tv.VIEW_H)
    lib.view_draw_dungeon(game.party_dir, game.party_x, game.party_y)
    mine = ctypes.string_at(viewport.pixels, tv.VIEW_W * tv.VIEW_H)
    sc.party.move_steps = steps
    seed.value = saved
    emu.write_u32(VIEW_SEED_ADDR, saved)
    return mine, theirs
