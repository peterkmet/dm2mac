import ctypes
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
