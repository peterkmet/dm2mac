import ctypes
import random
import struct

import pytest

from dm2test import constants as C
from dm2test.gdat import GdatKey, ImageInfo, image_info
from dm2test.markers import ROOT
from dm2tools.emu import SCRATCH
from dm2test.save import GRAPHICS, START, Game, original_load
from dm2test.scenario import Location, Position
from dm2test.view import (
    DUN_SET_PARTY_LOCATION, GDAT_GET_TEXT, G_PARTY_DIR, G_PARTY_MOVE_STEPS, G_PARTY_X, G_RAIN_INTENSITY, G_RANDOM_SEED,
    G_VIEW_ARROWS_ENABLED, G_VIEW_HIT_OFFSET, G_VIEW_PARTY_DIR, G_WEATHER_VISIBLE, VIEW_H, VIEW_LOAD_SQUARE, VIEW_SEED, VIEW_W,
    Bitmap, OriginalView, ScreenSize, WeatherGame, u8_p,
)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.view_load_square.argtypes = [Location, ctypes.c_int, ctypes.c_int16]
    lib.view_draw_dungeon.argtypes = [Location, ctypes.c_int16]
    return lib

VIEW_UNHIDE_LINKED_SQUARES, VIEW_MARK_HIDDEN_SQUARES = 0x54ABD, 0x5497E
G_VIEW_SQUARES, G_VIEW_CREATURE_GRID, G_VIEW_OBJECT_GRID = 0x80040, 0x80044, 0x8004C
G_VIEW_MISSILE_MASKS, G_VIEW_OBJECT_MASKS = 0x80034, 0x80048
VIEW_BUFFERS = SCRATCH + 0xF0000
SQUARE_COUNT, SQUARE_SIZE = C.VIEW_SQUARE_COUNT, C.VIEW_SQUARE_RECORD_SIZE


@pytest.fixture(scope="module")
def gdat(lib):
    if not GRAPHICS.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_text.argtypes = [ctypes.c_void_p, GdatKey, ctypes.c_char_p, ctypes.c_size_t]
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
        lib.gdat_text(gdat, GdatKey(category & 0xFF, index & 0xFF, entry & 0xFF), text, 128)
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
        lib.view_load_square(Location(x, y), pos, direction)
    lib.view_unhide_linked_squares()
    lib.view_mark_hidden_squares()
    return bytes((ctypes.c_uint8 * (SQUARE_COUNT * SQUARE_SIZE)).in_dll(lib, "g_view_squares"))


@pytest.fixture(scope="module")
def loaded(lib, gdat, start, tmp_path_factory):
    path = tmp_path_factory.mktemp("view") / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    lib.draw_init(gdat)
    assert lib.text_init(gdat)  # the shops show item names and prices
    lib.money_init_coin_table()
    game = Game.in_dll(lib, "g_game")
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
            for direction in range(C.DIR_COUNT):
                original = original_view_squares(emu, x, y, direction, (x, y, map_))
                assert c_view_squares(lib, x, y, direction) == original, (map_, x, y, direction)
                views += 1
    assert views > 5000
    lib.view_enter_map(loaded.party_map)


# ---- The whole view picture against the original view_draw_dungeon ----


@pytest.fixture(scope="module")
def original_view(lib, gdat, start):
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
    assert lib.zone_load(gdat) == 1
    assert lib.pal_load_color_ramps(gdat) == 1
    return OriginalView(lib, gdat, start)


class PartyHead(ctypes.Structure):
    """The first fields of the C movement state (move.h, Party)."""
    _fields_ = [(name, ctypes.c_int16) for name in ("cur_map", "square_x", "square_y", "square_map", "ref_dir",
                                                     "move_steps")]


def c_view(lib, map_, x, y, direction, light, sky=None, walking=0):
    game = WeatherGame.in_dll(lib, "g_game")
    game.party_x, game.party_y, game.party_dir, game.party_map = x, y, direction, map_
    party = PartyHead.in_dll(lib, "g_party")
    party.cur_map, party.move_steps = -1, walking  # 0: no walk; the current map of the dungeon reads becomes map_
    lib.dun_set_party_position(Position(map_, x, y))
    lib.view_enter_map(map_)
    if sky is not None:  # the weather sky objects that world_update_day_night builds
        lib.weather_sky_objects.restype = ctypes.c_void_p
        ctypes.memmove(lib.weather_sky_objects(), sky, len(sky))
    ctypes.c_int.in_dll(lib, "g_view_light_level").value = light
    ctypes.c_uint32.in_dll(lib, "g_random_seed").value = VIEW_SEED
    viewport = Bitmap.in_dll(lib, "g_view_viewport")
    ctypes.memset(viewport.pixels, 0, VIEW_W * VIEW_H)
    lib.view_draw_dungeon(Location(x, y), direction)
    return ctypes.string_at(viewport.pixels, VIEW_W * VIEW_H)


def test_view_picture_matches_original(lib, loaded, original_view):
    # The start view, then random squares of all maps in random directions and light levels: walls, ornaments,
    # wall texts, doors, stairs, pits, landmarks of the outdoor maps. A sweep of 1500 views matched too.
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible)
    original_view.emu.write(G_WEATHER_VISIBLE, b"\0")
    game.weather_visible = 0
    rng = random.Random(7)
    views = [(game.party_map, game.party_x, game.party_y, game.party_dir, 0)]
    while len(views) < 120:
        map_ = rng.randrange(lib.dungeon_map_count())
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        if lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT:  # not inside a wall
            views.append((map_, x, y, rng.randrange(C.DIR_COUNT), rng.choice([0, 2, 5])))
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
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible, game.sky_darkness,
             game.cloud_cover)
    outdoor = [m for m in range(lib.dungeon_map_count())
               if lib.gdat_exists(original_view.gdat, C.CAT_SKY, lib.dungeon_map_graphics(m) >> 4 & 0xF, C.GDAT_TYPE_IMAGE, 0x6A)]
    assert outdoor
    original_view.emu.write(G_WEATHER_VISIBLE, b"\1")
    rng = random.Random(8)
    views = 0
    while views < 40:
        map_ = rng.choice(outdoor)
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        if not lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT:
            continue
        game.weather_visible, game.sky_darkness, game.cloud_cover = 1, rng.choice([0, 0x20, 0x50, 0x90]), rng.choice([0, 0x50, 0x90, 0xD0])
        direction = rng.randrange(C.DIR_COUNT)
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
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    lib.dungeon_square_byte.restype = u8_p
    emu = original_view.emu
    doors = []
    for map_ in range(lib.dungeon_map_count()):
        lib.view_enter_map(map_)
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                for direction in range(C.DIR_COUNT):
                    if len(doors) == 10 or not lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT:
                        continue
                    c_view_squares(lib, x, y, direction)
                    door = (ctypes.c_uint8 * (SQUARE_COUNT * SQUARE_SIZE)).in_dll(lib, "g_view_squares")[3 * SQUARE_SIZE:]
                    if struct.unpack_from("<H", bytes(door), 2)[0] == C.ASPECT_DOOR_FRONT:  # a door front ahead
                        doors.append((map_, x, y, direction, door[0], door[1]))
    assert len(doors) == 10
    for map_, x, y, direction, door_x, door_y in doors:
        square = lib.dungeon_square_byte(map_, door_x, door_y)
        columns = emu.read_u32(emu.read_u32(G_MAP_COLUMNS) + 4 * map_)
        original_square = emu.read_u32(columns + 4 * door_x) + door_y
        saved_square = square[0]
        for state in (C.DOOR_STATE_ONE_FOURTH, C.DOOR_STATE_HALF, C.DOOR_STATE_THREE_FOURTHS, C.DOOR_STATE_BROKEN):  # three half open states, broken
            square[0] = saved_square & ~C.SQUARE_DOOR_STATE | state
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
GRID_SIZE, MASKS_SIZE = C.VIEW_GRID_SIZE, 4 * SQUARE_COUNT


class AnchoredZone(ctypes.Structure):
    _fields_ = [("zone", ctypes.c_uint16), ("anchor", ctypes.c_int16)]


class GridPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16)]


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
    lib.aspect_cell_to_xy.argtypes = [Location, ctypes.c_int]
    lib.aspect_cell_to_xy.restype = GridPoint
    rng = random.Random(21)
    for _ in range(1000):
        base_x, base_y = rng.randrange(-40, 40), rng.randrange(-40, 40)
        cell = rng.randrange(C.CELL_GRID_COUNT) if rng.random() < 0.8 else rng.randrange(0x10000)
        image_emu.call(VIEW_GRID5_CELL_TO_XY, base_x, base_y, cell, OUT_X, OUT_Y)
        point = lib.aspect_cell_to_xy(Location(base_x, base_y), cell)
        assert (point.x, point.y) == read_xy(image_emu), (base_x, base_y, cell)


def test_rotate_grid5_cell_matches_original(lib, image_emu):
    for cell in range(C.CELL_GRID_COUNT):
        for direction in range(C.DIR_COUNT):
            original = struct.unpack("<h", struct.pack("<H", image_emu.call(VIEW_ROTATE_GRID5_CELL, cell, direction) & 0xFFFF))[0]
            assert lib.aspect_rotate_cell(cell, direction) == original, (cell, direction)


def test_offset_creature_toward_party_matches_original(lib, image_emu):
    lib.view_offset_toward_party.argtypes = [GridPoint, GridPoint, ctypes.c_int16]
    lib.view_offset_toward_party.restype = GridPoint
    rng = random.Random(22)
    cases = [(px, py, 8, 8, size) for px in range(5, 12) for py in range(5, 12) for size in range(5)]
    cases += [(rng.randrange(-30, 90), rng.randrange(-30, 90), rng.randrange(-30, 90), rng.randrange(-30, 90),
               rng.randrange(6)) for _ in range(1000)]
    for party_x, party_y, cx, cy, size in cases:
        image_emu.write(OUT_X, struct.pack("<h", cx))
        image_emu.write(OUT_Y, struct.pack("<h", cy))
        image_emu.call(VIEW_OFFSET_CREATURE, party_x, party_y, size, OUT_X, OUT_Y)
        moved = lib.view_offset_toward_party(GridPoint(party_x, party_y), GridPoint(cx, cy), size)
        assert (moved.x, moved.y) == read_xy(image_emu), (party_x, party_y, cx, cy, size)


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
    grids = [(ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "g_view_creature_grid"), (ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "g_view_object_grid")]
    ctypes.memset(grids[0], 0, GRID_SIZE)
    ctypes.memset(grids[1], 0xFF, GRID_SIZE)
    squares = c_view_squares(lib, x, y, direction)
    masks = [bytes((ctypes.c_uint8 * MASKS_SIZE).in_dll(lib, name)) for name in ("g_view_missile_masks", "g_view_object_masks")]
    return (squares, bytes(grids[0]), bytes(grids[1]), *masks)


def views_with_things(lib, count, seed):
    """Random views of all maps whose squares hold objects or creatures."""
    rng = random.Random(seed)
    views = []
    while len(views) < count:
        map_ = rng.randrange(lib.dungeon_map_count())
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        direction = rng.randrange(C.DIR_COUNT)
        if not lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT:
            continue
        lib.view_enter_map(map_)
        c_view_squares(lib, x, y, direction)
        if any((ctypes.c_uint32 * SQUARE_COUNT).in_dll(lib, "g_view_object_masks")) or \
                any(v != 0xFF for v in (ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "g_view_object_grid")):
            views.append((map_, x, y, direction))
    return views


def test_thing_grids_match_original(lib, loaded, original_view):
    # The creature cells, the creatures in the object grid, and the cells with objects and missiles.
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    ctypes.memset((ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "g_view_object_grid"), 0xFF, GRID_SIZE)
    for map_, x, y, direction in views_with_things(lib, 300, 23):
        game.party_x, game.party_y, game.party_dir, game.party_map = x, y, direction, map_
        assert c_thing_grids(lib, map_, x, y, direction) == original_thing_grids(original_view, map_, x, y, direction), \
            (map_, x, y, direction)
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


DUN_GET_THING_DATA, G_TIMELINE_EVENTS = 0x1D2DA, 0x760E8
THING_END, CAT_WEAPON = C.THING_ENDOFLIST, C.CAT_WEAPON
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
    lib.zone_compute_anchored_rect.argtypes = [ctypes.POINTER(Bitmap), ctypes.POINTER(ctypes.c_int16), AnchoredZone,
                                               ctypes.POINTER(ScreenSize)]
    lib.zone_compute_anchored_rect.restype = ctypes.c_void_p
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    assert lib.zone_load(gdat) == 1
    assert lib.pal_load_color_ramps(gdat) == 1
    g = gdat
    index = next(i for i in range(128) if lib.gdat_find(g, CAT_WEAPON, i, C.GDAT_TYPE_IMAGE, 0) >= 0)
    map_ = game.party_map
    lib.view_enter_map(map_)
    view = None
    for x in range(lib.dungeon_map_width(map_)):
        for y in range(lib.dungeon_map_height(map_)):
            if view or not lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT:
                continue
            grid = (ctypes.c_uint8 * GRID_SIZE).in_dll(lib, "g_view_object_grid")
            ctypes.memset(grid, 0xFF, GRID_SIZE)
            squares = c_view_squares(lib, x, y, 0)
            front = struct.unpack_from("<bbHHH", squares, 3 * SQUARE_SIZE)
            if front[2] == C.ASPECT_FLOOR and front[4] == THING_END and all(v == 0xFF for v in grid):
                view = (x, y, front[0], front[1])
    assert view
    x, y, front_x, front_y = view
    thing = lib.dungeon_unused_thing(C.THING_WEAPON) & 0xFFFF | 3 << C.THING_CELL_SHIFT  # south west: the near left cell for a party looking north
    lib.thing_set_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    lib.thing_set_word(thing, 2, index)
    assert lib.dungeon_link_thing_to_square(thing, map_, front_x, front_y)
    with_object = c_view(lib, map_, x, y, 0, 0)
    lib.dungeon_unlink_thing_from_square(thing, map_, front_x, front_y)
    without = c_view(lib, map_, x, y, 0, 0)
    changed = [divmod(i, VIEW_W) for i in range(VIEW_W * VIEW_H) if with_object[i] != without[i]]
    assert changed
    # cell 16 of the square at depth 1: row 1, scale THING_SCALE[1 * 4 + 1 + 2]; offset of ordinal 0 is (2, -3)
    scale = THING_SCALE[7]
    info = image_info(lib, g, lib.gdat_find(g, CAT_WEAPON, index, C.GDAT_TYPE_IMAGE, 0))[1]
    picture = Bitmap(None, lib.gfx_scale_64(info.width, scale), lib.gfx_scale_64(info.height, scale), info.bpp)
    dx, dy = 2, -3
    for packed in (lib.gdat_entry_index(g, CAT_WEAPON, index, C.GDAT_TYPE_VALUE2, 0xFE), lib.gdat_image_offset(g, CAT_WEAPON, index, 0),
                   lib.gdat_entry_index(g, CAT_WEAPON, 0xFE, C.GDAT_TYPE_VALUE2, 0)):
        dx += struct.unpack("b", bytes([packed >> 8 & 0xFF]))[0]
        dy += struct.unpack("b", bytes([packed & 0xFF]))[0]
    offset = ScreenSize(lib.gfx_scale_64(dx, scale), lib.gfx_scale_64(dy, scale))
    rect = (ctypes.c_int16 * 4)()
    zone = 5000 + 3 * 25 + 16 | C.ZONE_RELATIVE
    assert lib.zone_compute_anchored_rect(ctypes.byref(picture), rect, AnchoredZone(zone, 0), ctypes.byref(offset))
    rx, ry, rw, rh = rect
    assert all(rx <= col < rx + rw and ry <= row < ry + rh for row, col in changed), (rect[:], changed[:5])
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


SPELL_MISSILES = (0, 2, 3, 4, 6, 7, 9, 10, 13, 49)
EXPLOSION_TYPES = (0, 2, 3, 4, 6, 7, 9, 10, 13, 14, 40, 48, 49)
THING_GROUP, THING_PROJECTILE, THING_EXPLOSION, EVENT_SIZE = (C.THING_GROUP, C.THING_PROJECTILE, C.THING_EXPLOSION,
                                                                C.TIMELINE_EVENT_SIZE)
HIDDEN_POSSESSION_TYPES = (19, 26, 37)  # creature infos 7, 10, 39: static, possessions not shown


class CreatureInfo(ctypes.Structure):
    _fields_ = [("flags", ctypes.c_uint16), ("possessions", ctypes.c_uint16), ("size", ctypes.c_uint8)]


class Things:
    """Writes thing records and event data the same way into the C port and into the original."""

    def __init__(self, lib, emu):
        self.lib, self.emu = lib, emu
        lib.dungeon_thing_record.restype = u8_p
        lib.dungeon_thing_record.argtypes = [ctypes.c_uint16]
        self.events = ctypes.cast(ctypes.c_void_p.in_dll(lib, "g_timeline"), u8_p)

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
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    things = Things(lib, original_view.emu)
    weapons = [t for t in range(lib.dungeon_thing_count(C.THING_WEAPON))
               if lib.dungeon_thing_valid(C.THING_WEAPON << C.THING_TYPE_SHIFT | t)]
    saved_event = things.read_event(0)
    free_groups = [t for t in range(lib.dungeon_thing_count(THING_GROUP)) if lib.thing_word(THING_GROUP << C.THING_TYPE_SHIFT | t, 0) == C.THING_NONE]
    free_weapons = [t for t in range(lib.dungeon_thing_count(C.THING_WEAPON))
                    if lib.thing_word(C.THING_WEAPON << C.THING_TYPE_SHIFT | t, 0) == C.THING_NONE]
    creature_types = [t for t in range(256) if lib.gdat_exists(original_view.gdat, C.CAT_CREATURE, t, C.GDAT_TYPE_CREATURE_DATA, 0xFD)]
    assert free_groups and free_weapons and creature_types
    lib.creature_info.restype = ctypes.POINTER(CreatureInfo)
    lib.creature_info.argtypes = [ctypes.c_void_p, ctypes.c_int]
    size = ctypes.c_uint32()

    def frame_data(creature):
        item = lib.gdat_find(original_view.gdat, C.CAT_CREATURE, creature, C.GDAT_TYPE_CREATURE_DATA, 0xFD)
        return ctypes.string_at(lib.gdat_item(original_view.gdat, item, ctypes.byref(size)), size.value)

    def frame_count(creature):
        return len(frame_data(creature)) // C.CREATURE_FRAME_SIZE

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
        cell = rng.randrange(C.CELL_COUNT) << C.THING_CELL_SHIFT
        kind = rng.random()
        if kind < 0.4:
            thing = THING_PROJECTILE << C.THING_TYPE_SHIFT | rng.randrange(lib.dungeon_thing_count(THING_PROJECTILE)) | cell
            payload = (C.SPELL_THING_FIRST + rng.choice(SPELL_MISSILES) if rng.random() < 0.7
                       else C.THING_WEAPON << C.THING_TYPE_SHIFT | rng.choice(weapons))
            power = rng.choice([0xFF, rng.randrange(256)])
            things.write(thing, 0, struct.pack("<HHBBH", THING_END, payload, power, 0, 0))
            things.write_event(0, 8, struct.pack("<H", rng.randrange(C.DIR_COUNT) << 10))
        elif kind < 0.75:
            thing = THING_GROUP << C.THING_TYPE_SHIFT | rng.choice(free_groups) | cell
            hidden = rng.random() < 0.3  # creatures that show no possessions
            shifted = rng.random() < 0.2
            creature = rng.choice(HIDDEN_POSSESSION_TYPES if hidden else shifted_types if shifted else creature_types)
            possessions = THING_END
            if hidden or rng.random() < 0.7:
                possessions = (C.THING_WEAPON << C.THING_TYPE_SHIFT | rng.choice(free_weapons)
                           | rng.randrange(C.CELL_COUNT) << C.THING_CELL_SHIFT)
                things.write(possessions, 0, struct.pack("<HH", THING_END, rng.randrange(40)))
            sequence = state = 0
            flags = lib.creature_info(original_view.gdat, creature).contents.flags
            frames = frame_count(creature)
            if flags & C.CREATURE_STATIC and frames:  # a static creature keeps its animation in the record: a still frame
                sequence = rng.randrange(frames)
                state = rng.randrange(min(64, frames - sequence))
            creature_dir = rng.randrange(C.DIR_COUNT)
            record = struct.pack("<HHBBHHHHH", THING_END, possessions, creature, 0xFF, 100, sequence, state, 0,
                                 creature_dir << 8)
            things.write(thing, 0, record)
            hidden_cases += hidden
            side = 2 if flags & C.CREATURE_FACES_PARTY else (direction - creature_dir) & C.DIR_MASK
            shifted_cases += frame_data(creature)[(sequence + state) * C.CREATURE_FRAME_SIZE + 6] != 0 and side in (0, 2)
        else:
            thing = THING_EXPLOSION << C.THING_TYPE_SHIFT | rng.randrange(lib.dungeon_thing_count(THING_EXPLOSION)) | cell
            data = rng.choice(EXPLOSION_TYPES) | rng.choice([0, 0x80]) | rng.randrange(256) << 8
            things.write(thing, 0, struct.pack("<HH", THING_END, data))
        things.write(last, 0, struct.pack("<H", thing))
        mine = c_view(lib, map_, x, y, direction, rng.choice([0, 3]))
        light = ctypes.c_int.in_dll(lib, "g_view_light_level").value
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
                ("item", ctypes.c_int16), ("zone", ctypes.c_uint16), ("anchor", ctypes.c_int16), ("dx", ctypes.c_int16),
                ("dy", ctypes.c_int16), ("src_x", ctypes.c_int16), ("src_y", ctypes.c_int16),
                ("rect", ctypes.c_int16 * 4), ("dst", ctypes.POINTER(Bitmap)), ("transparent", ctypes.c_int16),
                ("mode", ctypes.c_int), ("flip", ctypes.c_int16), ("scale_x", ctypes.c_int16), ("scale_y", ctypes.c_int16),
                ("palette_count", ctypes.c_int16), ("palette", ctypes.c_uint8 * 256)]


def test_shop_displays_match_original(lib, gdat, loaded, original_view):
    # Each shop of start.DAT seen from the front: the display steps to the next item (the shop flag 4 of the save),
    # then stays; a click below the center steps back, a click above steps forward, a click at the center steps back. The item, its name, and the prices
    # in coins, in 3 light levels.
    game = WeatherGame.in_dll(lib, "g_game")
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
                while t != C.THING_ENDOFLIST:
                    flags = lib.thing_word(t, 4)
                    if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == C.THING_SENSOR and lib.thing_word(t, 2) & 0x7F == 0x3F and flags >> 12:
                        ornament = lib.dungeon_map_wall_ornaments(map_)[(flags >> 12) - 1]
                        if lib.gdat_entry_index(gdat, C.CAT_WALL_ORNAMENT, ornament, C.GDAT_TYPE_VALUE, 0x0A) == 2:
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
                Things(lib, emu).write(shop, 4, struct.pack("<H", lib.thing_word(shop, 4) & ~C.SHOP_SHOWN))
                ctypes.c_int16.in_dll(lib, "g_view_hit_offset_y").value = offset
                emu.write(G_VIEW_HIT_OFFSET, struct.pack("<h", offset))
            original = original_view.draw(map_, party_x, party_y, (cell + 2) & C.DIR_MASK, light, b"\xff" + bytes(9))
            assert c_view(lib, map_, party_x, party_y, (cell + 2) & C.DIR_MASK, light) == original, (map_, x, y, step)
            items.append(lib.thing_word(display, 2) >> 7)
            assert struct.unpack("<H", emu.read(emu.call(DUN_GET_THING_DATA, display) + 2, 2))[0] >> 7 == items[-1]
        assert items[0] == items[1] == items[3] != items[2] == items[4], (map_, x, y)
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


def test_shop_with_an_empty_price_table_steps_through_the_item_indices(lib, gdat, loaded, original_view, tmp_path):
    # A damaged GRAPHICS.DAT whose price table of a shop ornament has 1 byte: the display steps through the item
    # indices as for an ornament without a table, and shows an item that the merchant sells.
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    lib.dungeon_map_wall_ornaments.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.gdat_select.argtypes = [ctypes.c_void_p]
    lib.money_parse_item_class_set.argtypes = [ctypes.c_int, ctypes.c_int, u8_p, ctypes.c_bool]
    shop = next((m, x, y, t) for m in range(lib.dungeon_map_count()) for x in range(lib.dungeon_map_width(m))
                for y in range(lib.dungeon_map_height(m)) for t in thing_list(lib, lib.dungeon_square_first_thing(m, x, y))
                if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == C.THING_SENSOR
                and lib.thing_word(t, 2) & 0x7F == C.WALL_CLICK_RESET and lib.thing_word(t, 4) >> 12
                and lib.gdat_entry_index(gdat, C.CAT_WALL_ORNAMENT, lib.dungeon_map_wall_ornaments(m)[
                    (lib.thing_word(t, 4) >> 12) - 1], C.GDAT_TYPE_VALUE, C.ORN_KEY_KIND) == C.KIND_SHOP)
    map_, x, y, thing = shop
    ornament = lib.dungeon_map_wall_ornaments(map_)[(lib.thing_word(thing, 4) >> 12) - 1]
    item = lib.gdat_find(gdat, C.CAT_WALL_ORNAMENT, ornament, C.GDAT_TYPE_TABLE, C.GFX_ORN_SHOP_TABLE)
    assert item > 0
    data = bytearray(GRAPHICS.read_bytes())
    count = int.from_bytes(data[2:4], "little")
    size_at = 8 + 2 * (item - 1)
    sizes = [int.from_bytes(data[8 + 2 * (i - 1):10 + 2 * (i - 1)], "little") for i in range(1, item)]
    at = 8 + 2 * (count - 1) + int.from_bytes(data[4:8], "little") + sum(sizes)
    del data[at + 1:at + int.from_bytes(data[size_at:size_at + 2], "little")]
    data[size_at:size_at + 2] = (1).to_bytes(2, "little")
    path = tmp_path / "GRAPHICS.DAT"
    path.write_bytes(data)
    damaged = ctypes.c_void_p()
    assert lib.gdat_load(str(path).encode(), ctypes.byref(damaged)) == 0
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map)
    lib.gdat_select(damaged)
    try:
        cell = thing >> 14
        lib.thing_set_word(thing, 4, lib.thing_word(thing, 4) & ~C.SHOP_SHOWN)
        c_view(lib, map_, x + (0, 1, 0, -1)[cell], y + (-1, 0, 1, 0)[cell], (cell + 2) & C.DIR_MASK, 0)
    finally:
        lib.gdat_select(gdat)
        lib.gdat_free(damaged)
    target = lib.thing_word(thing, 6)
    display = lib.dungeon_square_first_thing(map_, target >> 6 & 31, target >> 11)
    shown = lib.thing_word(display, 2) >> 7
    merchant, price_list = lib.thing_word(thing, 2) >> 7 & 0xFF, lib.thing_word(thing, 4) >> 7 & 0xF
    sold = (ctypes.c_uint8 * C.ITEM_CLASS_SET_SIZE)()
    assert lib.money_parse_item_class_set(merchant, 0x10 + 3 * price_list, sold, False)  # the class text of the list
    assert sold[shown // 8] >> shown % 8 & 1, shown
    game.party_x, game.party_y, game.party_dir, game.party_map = saved
    lib.view_enter_map(game.party_map)


def test_wet_image_matches_original(lib, loaded, original_view):
    # The rain background of wet ornaments of all widths, with small widths where the tile row of the rain bitmap
    # (rect width x rain height - 16 - 0 to 31 pixels) is shorter than its random start. There the original tiles
    # from past the end of the rain bitmap; the port fills the color key.
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.rain_intensity, game.wind_dir)
    gdat, emu = original_view.gdat, original_view.emu
    map_ = next(m for m in range(lib.dungeon_map_count())
                if lib.gdat_exists(gdat, C.CAT_SKY, lib.dungeon_map_graphics(m) >> 4 & 0xF, C.GDAT_TYPE_IMAGE, 0x71))
    x, y = next((x, y) for x in range(lib.dungeon_map_width(map_)) for y in range(lib.dungeon_map_height(map_))
                if lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT)
    lib.view_draw_wet_image.argtypes = [ctypes.POINTER(DrawCtx), ctypes.POINTER(Bitmap), ctypes.c_int]
    lib.rand_below.restype = ctypes.c_uint16
    lib.rand_below.argtypes = [ctypes.c_uint16]
    lib.rand_next.restype = ctypes.c_uint32
    seed = ctypes.c_uint32.in_dll(lib, "g_random_seed")
    rng = random.Random(26)
    ctx_addr = original_view.alloc(CTX_SIZE)
    drawn = filled = small_drawn = 0
    for case in range(400):
        direction, light = rng.randrange(C.DIR_COUNT), rng.choice([0, 3])
        game.rain_intensity, game.wind_dir = rng.choice([5, 0x20, 0x50, 0x90]), rng.randrange(C.DIR_COUNT)
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
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible)
    original_view.emu.write(G_WEATHER_VISIBLE, b"\0")
    game.weather_visible = 0
    emu = original_view.emu
    count = ctypes.c_int.in_dll(lib, "g_view_click_zone_count")
    zones = (ctypes.c_uint8 * (C.VIEW_CLICK_ZONE_MAX * CLICK_ZONE_SIZE)).in_dll(lib, "g_view_click_zones")
    rng = random.Random(21)
    views = with_zones = 0
    while views < 300:
        map_ = rng.randrange(lib.dungeon_map_count())
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        if not lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT:
            continue
        direction = rng.randrange(C.DIR_COUNT)
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
    game = WeatherGame.in_dll(lib, "g_game")
    saved = (game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible)
    original_view.emu.write(G_WEATHER_VISIBLE, b"\0")
    game.weather_visible = 0
    rng = random.Random(11)
    views = [(game.party_map, game.party_x, game.party_y, game.party_dir, 0)]
    while len(views) < 60:
        map_ = rng.randrange(lib.dungeon_map_count())
        x, y = rng.randrange(lib.dungeon_map_width(map_)), rng.randrange(lib.dungeon_map_height(map_))
        if lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT:
            views.append((map_, x, y, rng.randrange(C.DIR_COUNT), rng.choice([0, 2, 5])))
    pictures = 0
    for map_, x, y, direction, light in views:
        original = original_view.draw(map_, x, y, direction, light, b"\xff" + bytes(9), walking=1)
        pictures += len(set(original)) > 2
        assert c_view(lib, map_, x, y, direction, light, walking=1) == original, (map_, x, y, direction, light)
    assert pictures > 40
    game.party_x, game.party_y, game.party_dir, game.party_map, game.weather_visible = saved
    lib.view_enter_map(game.party_map)


def present_viewport(lib, gdat, walking):
    """The screen viewport rect after play_present_viewport of a view whose walk rectangle holds the values 2 up."""
    walk = (ctypes.c_int16 * 4).in_dll(lib, "g_zone_walk_view_rect")
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_load(gdat) == 1
    PartyHead.in_dll(lib, "g_party").move_steps = 0
    lib.zone_get_rect.restype = ctypes.c_void_p
    assert lib.zone_get_rect(C.ZONE_VIEWPORT, rect)
    wx, wy, ww, wh = walk
    view = bytearray([1]) * (VIEW_W * VIEW_H)  # 1: outside the walk rectangle
    for y in range(wh):
        for x in range(ww):
            view[(wy + y) * VIEW_W + wx + x] = 2 + (x * 7 + y * 13) % 250
    viewport = Bitmap.in_dll(lib, "g_view_viewport")
    ctypes.memmove(viewport.pixels, bytes(view), len(view))
    PartyHead.in_dll(lib, "g_party").move_steps = walking
    lib.play_present_viewport()
    PartyHead.in_dll(lib, "g_party").move_steps = 0
    screen = Bitmap.in_dll(lib, "g_draw_screen")
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


GDAT_KEYS, GDAT_KEY_ROWS, GDAT_ROW_SIZE, GDAT_ROW_VALUE = 0x2BF6, 20, 8, 6  # the key table of GRAPHICS.DAT
ORN_KEY_FRAMES, FRAMES_FROM_ONE = 0x0D, 0x8000


class ViewSquare(ctypes.Structure):
    """aspect.h ViewSquare: what the view shows of one square (18 bytes)."""
    _fields_ = [("x", ctypes.c_int8), ("y", ctypes.c_int8), ("aspect", ctypes.c_uint16), ("square", ctypes.c_uint16),
                ("first_thing", ctypes.c_uint16), ("side", ctypes.c_uint16 * 4), ("text", ctypes.c_uint16)]


def with_value(data, key, value):
    """GRAPHICS.DAT with the value of key (category, index, type, entry) set to value."""
    data = bytearray(data)
    for row in range(struct.unpack_from(">H", data, GDAT_KEYS + 2)[0]):
        at = GDAT_KEYS + GDAT_KEY_ROWS + row * GDAT_ROW_SIZE
        if tuple(data[at:at + 4]) == key:
            struct.pack_into(">H", data, at + GDAT_ROW_VALUE, value)
            return bytes(data)
    raise AssertionError(f"no key {key}")


def test_an_ornament_frames_value_of_0x8000_shows_frame_1(lib, gdat, loaded, tmp_path):
    # regression: a damaged GRAPHICS.DAT with the frames value 0x8000 (frames from 1, a count of 0) for an animated
    # wall ornament divided by 0 in ornament_frame; the face shows frame 1, as a cycle of 1
    lib.aspect_get.argtypes = [ctypes.c_void_p] + [ctypes.c_int16] * 4
    lib.gdat_entry_index.restype = ctypes.c_uint16
    lib.gdat_select.argtypes = [ctypes.c_void_p]
    s = ViewSquare()

    def faces(map_, x, y, d):
        lib.aspect_get(ctypes.byref(s), map_, d, x, y)
        return [(w & 0xFF, ctypes.c_int16(w).value >> C.ORN_FRAME_SHIFT) for w in s.side]

    found = next((m, x, y, d, k, ornament) for m in range(lib.dungeon_map_count()) for x in range(lib.dungeon_map_width(m))
                 for y in range(lib.dungeon_map_height(m)) if lib.dungeon_square(m, x, y) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_WALL
                 for d in range(C.DIR_COUNT) for k, (ornament, frame) in enumerate(faces(m, x, y, d))
                 if frame and lib.gdat_entry_index(gdat, C.CAT_WALL_ORNAMENT, ornament, C.GDAT_TYPE_VALUE, ORN_KEY_FRAMES) & FRAMES_FROM_ONE)
    map_, x, y, d, k, ornament = found
    assert loaded.game_time
    path = tmp_path / "GRAPHICS.DAT"
    path.write_bytes(with_value(GRAPHICS.read_bytes(), (C.CAT_WALL_ORNAMENT, ornament, C.GDAT_TYPE_VALUE, ORN_KEY_FRAMES),
                                FRAMES_FROM_ONE))
    damaged = ctypes.c_void_p()
    assert lib.gdat_load(str(path).encode(), ctypes.byref(damaged)) == 0
    lib.gdat_select(damaged)
    try:
        assert faces(map_, x, y, d)[k] == (ornament, 1)
    finally:
        lib.gdat_select(gdat)
        lib.gdat_free(damaged)


TEXT_NEWLINE, TEXT_END = 0x1C, 0x1F  # the codes of game_text.c: A to Z are 0 to 25


def encode_text(text):
    """The packed words of a dungeon text: 3 codes of 5 bits per word, then the end code."""
    codes = [TEXT_NEWLINE if c == "\n" else ord(c) - ord("A") for c in text] + [TEXT_END]
    codes += [TEXT_END] * (-len(codes) % 3)
    return [codes[i] << 10 | codes[i + 1] << 5 | codes[i + 2] for i in range(0, len(codes), 3)]


def test_a_wall_text_line_outside_its_plate_is_not_drawn(lib, gdat, loaded):
    # regression: a wall text with more lines than its plate holds (a damaged DUNGEON.DAT) wrote its first line above
    # the plate bitmap; the port skips that line
    lib.dungeon_square_first_thing.restype = lib.dungeon_next_thing.restype = lib.thing_word.restype = ctypes.c_uint16
    lib.dungeon_square_first_thing.argtypes = [ctypes.c_int16] * 3
    lib.dungeon_next_thing.argtypes = [ctypes.c_uint16]
    lib.thing_word.argtypes = [ctypes.c_uint16, ctypes.c_int]
    lib.thing_set_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    lib.dungeon_text_data.restype = ctypes.POINTER(ctypes.c_uint16)
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    assert lib.zone_load(gdat) == 1
    assert lib.pal_load_color_ramps(gdat) == 1
    game = WeatherGame.in_dll(lib, "g_game")
    saved_party = (game.party_x, game.party_y, game.party_dir, game.party_map)

    def things(m, x, y):
        t = lib.dungeon_square_first_thing(m, x, y)
        while t != C.THING_ENDOFLIST:
            yield t
            t = lib.dungeon_next_thing(t)

    def is_plate(t):
        data = lib.thing_word(t, C.TEXT_DATA)
        kind = data >> C.TEXT_KIND_SHIFT & C.TEXT_KIND_MASK
        return t >> C.THING_TYPE_SHIFT == C.THING_TEXT and (
            kind == C.TEXT_KIND_TEXT or kind == C.TEXT_KIND_ORNAMENT
            and data >> C.TEXT_SUBTYPE_SHIFT & C.TEXT_SUBTYPE_MASK == C.SUBTYPE_PLATE)

    plates = [(m, x, y, t) for m in range(lib.dungeon_map_count()) for x in range(lib.dungeon_map_width(m))
              for y in range(lib.dungeon_map_height(m)) if lib.dungeon_square(m, x, y) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_WALL
              for t in things(m, x, y) if is_plate(t)]
    lines, line_height = 6, 10  # the glyph size 8 and the line gap 2 of view.c
    texts = {"over": "X\nB\nC\nD\nE\nF", "blank": "\nB\nC\nD\nE\nF", "other": "\nB\nC\nD\nE\nG"}
    for map_, x, y, plate in plates:
        sets = lib.dungeon_map_wallset(map_)
        _, info = image_info(lib, gdat, lib.gdat_find(gdat, C.CAT_WALLSET, sets, C.GDAT_TYPE_IMAGE, C.GFX_MAP_PLATE_FRONT))
        top = (info.height >> 1) - (lines * line_height >> 1)
        if -8 < top < 0 and top + (lines - 1) * line_height + 8 <= info.height:
            break  # the first line starts above the plate, the last line ends inside it
    else:
        raise AssertionError("no plate of a height for the test")
    data = lib.thing_word(plate, C.TEXT_DATA)
    text_at = 0  # the words of the first dungeon text take the test texts
    words = lib.dungeon_text_data()
    saved = [words[text_at + i] for i in range(5)]
    lib.thing_set_word(plate, C.TEXT_DATA, C.TEXT_VISIBLE | text_at << C.TEXT_VALUE_SHIFT)
    try:
        views = None
        for d in range(C.DIR_COUNT):  # the party faces the plate from an open square beside it
            px, py = x - (d == 1) + (d == 3), y + (d == 0) - (d == 2)
            if not (0 <= px < lib.dungeon_map_width(map_) and 0 <= py < lib.dungeon_map_height(map_)) or \
                    lib.dungeon_square(map_, px, py) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_WALL:
                continue
            drawn = {}
            for name, text in texts.items():
                packed = encode_text(text)
                for i, word in enumerate(packed):
                    words[text_at + i] = word
                drawn[name] = c_view(lib, map_, px, py, d, 0)
            if drawn["blank"] != drawn["other"]:
                views = drawn
                break
        assert views, "no view shows the plate text"
        assert views["over"] == views["blank"]
    finally:
        for i, word in enumerate(saved):
            words[text_at + i] = word
        lib.thing_set_word(plate, C.TEXT_DATA, data)
        game.party_x, game.party_y, game.party_dir, game.party_map = saved_party
        lib.view_enter_map(game.party_map)


class ViewObjectClick(ctypes.Structure):
    """view.h ViewObjectClick: a click on the objects of a square cell."""
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16), ("pos", ctypes.c_int), ("at", Location),
                ("kind", ctypes.c_int)]


class ObjectClickResult(ctypes.Structure):
    """view.h ObjectClickResult: the outcome of a click on the objects of a cell."""
    _fields_ = [("kind", ctypes.c_int), ("thing", ctypes.c_uint16)]


OBJECT_CLICK_DONE = 1  # enum ObjectClick of view.h


def test_a_possessions_click_without_a_group_does_nothing(lib, loaded):
    # regression: a click on the possessions of a creature that left the square read the record of THING_NONE; the
    # click does nothing
    lib.view_hit_test_object.argtypes = [ctypes.POINTER(ViewObjectClick), ctypes.c_uint16, ctypes.c_bool]
    lib.view_hit_test_object.restype = ObjectClickResult
    lib.creature_group_at.argtypes = [Position]
    lib.creature_group_at.restype = ctypes.c_uint16
    party = PartyHead.in_dll(lib, "g_party")
    saved, party.cur_map = party.cur_map, loaded.party_map
    at = Location(loaded.party_x, loaded.party_y)
    assert lib.creature_group_at(Position(loaded.party_map, at.x, at.y)) == C.THING_NONE  # the party square
    click = ViewObjectClick(0, 0, C.POS_PARTY, at, C.CLICK_POSSESSIONS)
    try:
        for placing in (False, True):
            result = lib.view_hit_test_object(ctypes.byref(click), 0, placing)
            assert (result.kind, result.thing) == (OBJECT_CLICK_DONE, C.THING_NONE), placing
    finally:
        party.cur_map = saved


def test_an_explosion_tint_of_a_narrow_picture_draws_nothing(lib, gdat, loaded):
    # regression: the tint of an explosion on the party square tiles a row of its picture minus 40 pixels; with a tint
    # picture of at most 40 pixels (a damaged GRAPHICS.DAT) the row is not longer than its start and the tiled blit
    # never ended; the port draws no tint
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.dungeon_link_thing_to_square.argtypes = [ctypes.c_uint16] + [ctypes.c_int16] * 3
    lib.dungeon_unlink_thing_from_square.argtypes = [ctypes.c_uint16] + [ctypes.c_int16] * 3
    lib.dungeon_thing_record.restype = u8_p
    lib.dungeon_thing_record.argtypes = [ctypes.c_uint16]
    lib.draw_gdat_image.restype = ctypes.POINTER(Bitmap)
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    assert lib.zone_load(gdat) == 1
    assert lib.pal_load_color_ramps(gdat) == 1
    game = WeatherGame.in_dll(lib, "g_game")
    map_, x, y, d = game.party_map, game.party_x, game.party_y, game.party_dir
    tinted = [t for t in EXPLOSION_TYPES
              if lib.gdat_find(gdat, C.CAT_EXPLOSION, t, C.GDAT_TYPE_IMAGE, C.GFX_EXPLOSION_TINT) >= 0
              and lib.gdat_find(gdat, C.CAT_EXPLOSION, t, C.GDAT_TYPE_IMAGE, C.GFX_SPARKLE_PICTURE) < 0]
    assert tinted
    plain = c_view(lib, map_, x, y, d, 0)
    thing = lib.dungeon_unused_thing(THING_EXPLOSION)
    assert thing != C.THING_NONE
    record = lib.dungeon_thing_record(thing)
    for i, b in enumerate(struct.pack("<HH", THING_END, tinted[0])):  # power 0: the weak tint picture
        record[i] = b
    assert lib.dungeon_link_thing_to_square(thing, map_, x, y)
    tint = lib.draw_gdat_image(C.CAT_EXPLOSION, tinted[0], C.GFX_EXPLOSION_TINT).contents
    size = tint.width, tint.height
    try:
        assert c_view(lib, map_, x, y, d, 0) != plain
        tint.width, tint.height = 3, 13  # 39 pixels: a tile row of 0 to 30 pixels from a start of up to 65534
        assert c_view(lib, map_, x, y, d, 0) == plain
    finally:
        tint.width, tint.height = size
        lib.dungeon_unlink_thing_from_square(thing, map_, x, y)
        record[0], record[1] = 0xFF, 0xFF  # unused again
        game.party_x, game.party_y, game.party_dir, game.party_map = x, y, d, map_
        lib.view_enter_map(map_)
