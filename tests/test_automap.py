"""The automap of the advanced mode (port/src/automap.c, advanced.c): the seen squares, the AMAP save block, the map in
the viewport, and the cheats of the open automap."""

import ctypes

import pytest

from conftest import ROOT
from test_interface import VIEW_H, VIEW_W, c_view
from test_modes import (ADVANCED, INPUT_MODE_INVENTORY, INPUT_MODE_MOVE, KEY_F5, KEY_F9, KEY_PAUSE, SCAN_W, SCREEN_W,
                        VANILLA, advanced, key_command, press, set_mode)  # noqa: F401 (advanced is a fixture)
from test_input import run_ticks, session  # noqa: F401 (session is a fixture)
from test_move import gdat, start  # noqa: F401 (gdat and start are fixtures)

NORTH, EAST, SOUTH, WEST = range(4)
STEP = {NORTH: (0, -1), EAST: (1, 0), SOUTH: (0, 1), WEST: (-1, 0)}
WALL, DOOR, FAKE_WALL = 0, 4, 6
FAKE_WALL_OPEN, DOOR_STATE, DOOR_CLOSED = 0x04, 0x07, 4
ROW_HALF_WIDTH = (1, 1, 2, 2, 3)  # the side squares of each row of the dungeon view
KEY_TAB, KEY_M, KEY_ESC, KEY_I, KEY_F, KEY_L = 0x0F, 0x32, 0x01, 0x17, 0x21, 0x26
INPUT_BUTTON_LEFT, INPUT_LEFT_RELEASED = 2, 8
ZONE_VIEWPORT, UI_RED, CELL = 7, 8, 8
SCREEN_H, FILLER = 200, 0xEE
HEALTH, FOOD, WATER = 0x36, 0x44, 0x46  # champion record offsets
SAVE_OK = 0


@pytest.fixture
def automap(lib, advanced):  # noqa: F811
    """The advanced game with the bright light and the cheats off."""
    lib.dungeon_square.restype = ctypes.c_uint8
    for name in ("view_light_level", "cheat_light", "cheat_immortal", "cheat_food"):
        flag(lib, name).value = 0
    yield advanced
    for name in ("cheat_light", "cheat_immortal", "cheat_food", "advanced_automap_open"):
        flag(lib, name).value = 0


def flag(lib, name):
    return ctypes.c_int.in_dll(lib, name)


def ahead(x, y, d, depth, side=0):
    (fx, fy), (rx, ry) = STEP[d], STEP[(d + 1) % 4]
    return x + depth * fx + side * rx, y + depth * fy + side * ry


def on_map(lib, m, x, y):
    return 0 <= x < lib.dungeon_map_width(m) and 0 <= y < lib.dungeon_map_height(m)


def blocks(lib, m, x, y):
    if not on_map(lib, m, x, y):
        return True
    square = lib.dungeon_square(m, x, y)
    kind = square >> 5
    return kind == WALL or (kind == FAKE_WALL and not square & FAKE_WALL_OPEN) or \
        (kind == DOOR and square & DOOR_STATE == DOOR_CLOSED)


def expected(lib, m, x, y, d):
    """The squares that the view shows: the party square; a square beside an open seen square toward the middle, or in
    front of one; the rows up to the first middle square that blocks sight."""
    seen, open_ = set(), set()
    for depth in range(5):
        for side in sorted(range(-ROW_HALF_WIDTH[depth], ROW_HALF_WIDTH[depth] + 1), key=abs):
            inner = side - (side > 0) + (side < 0)
            if (depth or side) and not (side and (depth, inner) in open_) and (depth - 1, side) not in open_:
                continue
            square = ahead(x, y, d, depth, side)
            if on_map(lib, m, *square):
                seen.add(square)
            if not blocks(lib, m, *square):
                open_.add((depth, side))
        if (depth, 0) not in open_:
            break
    return seen


def seen_squares(lib, m):
    return {(x, y) for x in range(lib.dungeon_map_width(m)) for y in range(lib.dungeon_map_height(m))
            if lib.automap_is_seen(m, x, y)}


def floor_views(lib, m):
    """Every party position on an open square of map m with each direction."""
    return [(x, y, d) for x in range(lib.dungeon_map_width(m)) for y in range(lib.dungeon_map_height(m))
            if not blocks(lib, m, x, y) for d in range(4)]


def test_marks_start_and_squares_ahead(lib, automap):
    game, _ = automap
    m, x, y, d = game.party_map, game.party_x, game.party_y, game.party_dir
    lib.automap_reset()
    assert not seen_squares(lib, m)
    lib.automap_mark_seen(m, x, y, d)
    assert (x, y) in seen_squares(lib, m) and ahead(x, y, d, 1) in seen_squares(lib, m)
    assert seen_squares(lib, m) == expected(lib, m, x, y, d)


def test_the_game_marks_what_the_view_shows(lib, automap):
    # Each game step marks the view of the party; a turn shows more squares.
    game, _ = automap
    m = game.party_map
    run_ticks(lib, 20)
    first = seen_squares(lib, m)
    assert first == expected(lib, m, game.party_x, game.party_y, game.party_dir)
    press(lib, 0x4D)  # turn right
    assert seen_squares(lib, m) > first


def test_walls_stop_the_view_and_hide_the_side_squares(lib, automap):
    game, _ = automap
    m, stopped, hidden = game.party_map, 0, 0
    for x, y, d in floor_views(lib, m):
        lib.automap_reset()
        lib.automap_mark_seen(m, x, y, d)
        seen = seen_squares(lib, m)
        assert seen == expected(lib, m, x, y, d), (x, y, d)
        if blocks(lib, m, *ahead(x, y, d, 1)) and on_map(lib, m, *ahead(x, y, d, 2)):
            assert ahead(x, y, d, 1) in seen and ahead(x, y, d, 2) not in seen
            stopped += 1
        if blocks(lib, m, *ahead(x, y, d, 1)) and blocks(lib, m, *ahead(x, y, d, 0, 1)) \
                and on_map(lib, m, *ahead(x, y, d, 1, 1)):
            assert ahead(x, y, d, 1, 1) not in seen
            hidden += 1
    assert stopped and hidden


def test_side_squares_beside_open_squares_are_seen(lib, automap):
    game, _ = automap
    m, found = game.party_map, 0
    for x, y, d in floor_views(lib, m):
        if blocks(lib, m, *ahead(x, y, d, 1)):
            continue
        lib.automap_reset()
        lib.automap_mark_seen(m, x, y, d)
        for side in (-1, 1):
            if on_map(lib, m, *ahead(x, y, d, 1, side)):
                assert ahead(x, y, d, 1, side) in seen_squares(lib, m)
                found += 1
    assert found


def test_darkness_shortens_the_view_and_the_light_cheat_does_not(lib, automap):
    game, _ = automap
    m = game.party_map
    long_view = next(v for v in floor_views(lib, m) if all(not blocks(lib, m, *ahead(v[0], v[1], v[2], k)) for k in (1, 2, 3, 4)))
    x, y, d = long_view
    for level, light, depth in ((0, 0, 4), (4, 0, 3), (5, 0, 2), (5, 1, 4)):
        flag(lib, "view_light_level").value, flag(lib, "cheat_light").value = level, light
        lib.automap_reset()
        lib.automap_mark_seen(m, x, y, d)
        seen = seen_squares(lib, m)
        assert ahead(x, y, d, depth) in seen and ahead(x, y, d, depth + 1) not in seen, (level, light)


def write(lib, gdat, path, mode):  # noqa: F811
    set_mode(lib, mode)
    try:
        assert lib.save_write(str(path).encode(), str(path.with_suffix(".BAK")).encode(), b"AUTOMAP", gdat) == SAVE_OK
    finally:
        set_mode(lib, ADVANCED)
    return path.read_bytes()


def block_size(lib):
    count = lib.dungeon_map_count()
    return 6 + 2 * count + sum(lib.dungeon_map_width(m) * lib.dungeon_map_height(m) for m in range(count))


@pytest.fixture
def saves(lib, gdat, automap, tmp_path):  # noqa: F811
    """A marked game saved in vanilla and in the advanced mode; the seen squares."""
    game, _ = automap
    run_ticks(lib, 20)
    press(lib, 0x4D)
    seen = seen_squares(lib, game.party_map)
    seed = ctypes.c_uint32.in_dll(lib, "random_seed")
    kept = seed.value
    vanilla = write(lib, gdat, tmp_path / "V.DAT", VANILLA)
    seed.value = kept  # the reactivated creature groups draw random numbers after a save
    return game, seen, vanilla, write(lib, gdat, tmp_path / "A.DAT", ADVANCED)


def load(lib, gdat, path, data):  # noqa: F811
    path.write_bytes(data)
    return lib.save_load(str(path).encode(), gdat)


def test_advanced_save_appends_the_amap_block(lib, saves):
    game, seen, vanilla, mine = saves
    assert seen and mine[:len(vanilla)] == vanilla
    block = mine[len(vanilla):]
    assert block[:6] == b"AMAP\x01" + bytes([lib.dungeon_map_count()]) and len(block) == block_size(lib)
    assert block[6:8] == bytes([lib.dungeon_map_width(0), lib.dungeon_map_height(0)])


def test_amap_block_round_trip(lib, gdat, saves, tmp_path):  # noqa: F811
    game, seen, vanilla, mine = saves
    lib.automap_reset()
    assert load(lib, gdat, tmp_path / "L.DAT", mine) == SAVE_OK
    assert seen_squares(lib, game.party_map) == seen
    assert load(lib, gdat, tmp_path / "L.DAT", vanilla) == SAVE_OK
    assert not seen_squares(lib, game.party_map), "a save without the block gives an empty map"


@pytest.mark.parametrize("damage", ["short", "long", "magic", "version", "map_count", "map_size", "value"])
def test_damaged_amap_block_gives_an_empty_map(lib, gdat, saves, tmp_path, damage):  # noqa: F811
    game, seen, vanilla, mine = saves
    n = len(vanilla)
    data = bytearray(mine)
    if damage == "short":
        del data[-1]
    elif damage == "long":
        data.append(0)
    else:
        offset = {"magic": 0, "version": 4, "map_count": 5, "map_size": 6, "value": len(mine) - n - 1}[damage]
        data[n + offset] = 2 if damage == "value" else data[n + offset] + 1
    position = (game.party_x, game.party_y, game.party_map)
    assert load(lib, gdat, tmp_path / "L.DAT", bytes(data)) == SAVE_OK
    assert (game.party_x, game.party_y, game.party_map) == position
    assert not seen_squares(lib, game.party_map)


def test_original_loader_ignores_the_amap_block(lib, gdat, saves, game_dir):  # noqa: F811
    # The original load of SKULL.EXE stops at the last bit of the game state: the block changes nothing it reads.
    from test_save import RECORD_GLOBALS, original_load
    _, _, vanilla, mine = saves
    states = []
    for data in (vanilla, mine):
        emu = original_load(lib, gdat, data)
        states.append({name: emu.read(addr, size) for name, (addr, size) in RECORD_GLOBALS.items()})
    assert states[0] == states[1]


def test_quick_save_keeps_the_automap(lib, automap):
    game, data = automap
    run_ticks(lib, 20)
    press(lib, 0x4D)
    seen = seen_squares(lib, game.party_map)
    press(lib, KEY_F5)
    assert (data / "QUICK.DAT").read_bytes()[-block_size(lib):][:4] == b"AMAP"
    lib.automap_reset()
    press(lib, KEY_F9)
    assert seen_squares(lib, game.party_map) >= seen


def viewport(lib):
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(ZONE_VIEWPORT, rect)
    return tuple(rect)


def screen(lib):
    return (ctypes.c_uint8 * (SCREEN_W * SCREEN_H)).in_dll(lib, "fb")


def red(lib):
    return ctypes.POINTER(ctypes.c_uint8).in_dll(lib, "ui_colors")[UI_RED]


def party_frame(lib, dx=0):
    """The top left pixel of the red frame around the party square, dx squares right of the middle."""
    x, y, w, h = viewport(lib)
    return screen(lib)[(y + (h - CELL) // 2) * SCREEN_W + x + (w - CELL) // 2 + dx * CELL]


def test_automap_opens_only_in_the_dungeon_view_of_the_advanced_mode(lib, automap):
    assert key_command(lib, INPUT_MODE_MOVE, KEY_TAB) and key_command(lib, INPUT_MODE_INVENTORY, KEY_TAB) == 0
    set_mode(lib, VANILLA)
    assert key_command(lib, INPUT_MODE_MOVE, KEY_TAB) == key_command(lib, INPUT_MODE_MOVE, KEY_M) == 0
    press(lib, KEY_TAB)
    set_mode(lib, ADVANCED)
    assert not flag(lib, "advanced_automap_open").value
    lib.inv_toggle(0)
    press(lib, KEY_TAB)
    lib.inv_toggle(0)
    press(lib, KEY_PAUSE)
    press(lib, KEY_M)
    press(lib, KEY_PAUSE)
    assert not flag(lib, "advanced_automap_open").value
    for key in (KEY_TAB, KEY_M):
        for close in (KEY_TAB, KEY_M, KEY_ESC):
            press(lib, key)
            assert flag(lib, "advanced_automap_open").value and party_frame(lib) == red(lib)
            press(lib, close)
            assert not flag(lib, "advanced_automap_open").value
    assert lib.input_get_mode() == INPUT_MODE_MOVE


def test_time_stops_and_keys_are_swallowed_while_open(lib, automap):
    game, _ = automap
    press(lib, KEY_TAB)
    state = (game.game_time, game.party_x, game.party_y, game.party_dir)
    for key in (SCAN_W, 0x4D, KEY_F5, KEY_F9):
        press(lib, key)
    assert (game.game_time, game.party_x, game.party_y, game.party_dir) == state
    assert flag(lib, "advanced_automap_open").value
    press(lib, KEY_ESC)
    assert game.game_time > state[0] and not flag(lib, "advanced_automap_open").value


def test_automap_draws_inside_the_viewport_with_the_party_icon(lib, automap):
    press(lib, KEY_TAB)
    x, y, w, h = viewport(lib)
    fb = screen(lib)
    inside = {(px, py) for px in range(x, x + w) for py in range(y, y + h)}
    for px, py in inside:
        fb[py * SCREEN_W + px] = FILLER
    before = bytes(fb)
    lib.advanced_draw_automap()
    after = bytes(fb)
    changed = {(i % SCREEN_W, i // SCREEN_W) for i in range(len(after)) if after[i] != before[i]}
    assert changed <= inside and FILLER not in {after[py * SCREEN_W + px] for px, py in inside}
    assert party_frame(lib) == red(lib)


def test_drag_scrolls_the_map_in_whole_squares(lib, automap):
    lib.input_mouse_click.argtypes = [ctypes.c_int] * 3
    press(lib, KEY_TAB)
    x, y, w, h = viewport(lib)
    mouse_x, mouse_y = ctypes.c_int.in_dll(lib, "input_mouse_x"), ctypes.c_int.in_dll(lib, "input_mouse_y")
    start_x, start_y = x + w // 2, y + h // 2
    mouse_x.value, mouse_y.value = start_x, start_y
    lib.input_mouse_click(start_x, start_y, INPUT_BUTTON_LEFT)
    run_ticks(lib, 2)
    mouse_x.value = start_x + CELL - 1  # less than a square: no scroll
    run_ticks(lib, 2)
    assert party_frame(lib) == red(lib)
    mouse_x.value = start_x + CELL
    run_ticks(lib, 2)
    assert party_frame(lib, 1) == red(lib) and party_frame(lib) != red(lib)
    lib.input_mouse_click(start_x + CELL, start_y, INPUT_LEFT_RELEASED)
    run_ticks(lib, 2)
    mouse_x.value = start_x + 3 * CELL  # the drag has ended
    run_ticks(lib, 2)
    assert party_frame(lib, 1) == red(lib)
    press(lib, KEY_TAB)
    press(lib, KEY_TAB)
    assert party_frame(lib) == red(lib), "a closed automap opens on the party"
    press(lib, KEY_TAB)


def test_cheat_keys_work_only_on_the_advanced_automap(lib, automap):
    names = {KEY_I: "cheat_immortal", KEY_F: "cheat_food", KEY_L: "cheat_light"}
    for key in names:
        press(lib, key)
    assert not any(flag(lib, n).value for n in names.values())
    press(lib, KEY_TAB)
    x, y, w, h = viewport(lib)
    for key, name in names.items():
        before = bytes(screen(lib))
        press(lib, key)
        assert flag(lib, name).value == 1 and bytes(screen(lib)) != before, name
    for key, name in names.items():
        press(lib, key)
        assert flag(lib, name).value == 0, name


def champion_word(lib, offset, value=None):
    from test_interface import champion_records
    word = ctypes.c_int16.from_buffer(champion_records(lib), offset)
    if value is not None:
        word.value = value
    return word.value


def test_immortal_keeps_one_health(lib, automap):
    damage = (ctypes.c_int16 * 4).in_dll(lib, "champ_pending_damage")
    flag(lib, "cheat_immortal").value = 1
    damage[0] = 30000
    lib.champ_apply_pending_damage()
    assert champion_word(lib, HEALTH) == 1
    damage[0] = 1
    lib.champ_apply_pending_damage()
    assert champion_word(lib, HEALTH) == 1, "the champion does not die"


def test_food_cheat_keeps_food_and_water(lib, automap):
    champion_word(lib, FOOD, 1000)
    champion_word(lib, WATER, 1000)
    flag(lib, "cheat_food").value = 1
    for _ in range(20):
        lib.champ_apply_time_effects()
    assert (champion_word(lib, FOOD), champion_word(lib, WATER)) == (1000, 1000)
    flag(lib, "cheat_food").value = 0
    for _ in range(20):
        lib.champ_apply_time_effects()
    assert champion_word(lib, FOOD) < 1000 and champion_word(lib, WATER) < 1000


def test_light_cheat_draws_full_light(lib, automap):
    game, _ = automap
    level = flag(lib, "view_light_level")

    def view(light, cheat):
        level.value, flag(lib, "cheat_light").value = light, cheat
        lib.view_draw_dungeon(game.party_dir, game.party_x, game.party_y)
        return c_view(lib)
    dark, cheat, bright = view(5, 0), view(5, 1), view(0, 0)
    assert cheat == bright != dark and len(bright) == VIEW_W * VIEW_H
    assert level.value == 0 and view(5, 1) and level.value == 5, "the creature rules keep the real light"


def test_new_or_loaded_game_turns_the_cheats_off(lib, automap):
    press(lib, KEY_F5)
    for name in ("cheat_immortal", "cheat_food", "cheat_light"):
        flag(lib, name).value = 1
    press(lib, KEY_F9)
    assert not any(flag(lib, n).value for n in ("cheat_immortal", "cheat_food", "cheat_light"))
    flag(lib, "advanced_automap_open").value = 1
    lib.game_start_session()
    assert not flag(lib, "advanced_automap_open").value

