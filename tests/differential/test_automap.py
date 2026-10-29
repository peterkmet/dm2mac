"""The automap of the advanced mode (src/ui/automap.c, ui/advanced.c): the seen squares, the AMAP save block, the map in
the viewport, and the cheats that change what the automap and the view show."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.interface import VIEW_H, VIEW_W, c_view
from dm2test.modes import (ADVANCED, INPUT_MODE_INVENTORY, INPUT_MODE_MOVE, KEY_F5, KEY_F9, KEY_PAUSE, SCAN_W, SCREEN_W,
                           VANILLA, advanced, key_command, press, set_mode)  # noqa: F401 (advanced is a fixture)
from dm2test.input import run_ticks, session  # noqa: F401 (session is a fixture)
from dm2test.scenario import GameRecord, Location, Position, gdat, start  # noqa: F401 (gdat and start are fixtures)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.seen_mark.argtypes = [Position, ctypes.c_int16]
    lib.seen_is_seen.argtypes = [Position]
    lib.view_draw_dungeon.argtypes = [Location, ctypes.c_int16]
    lib.seen_is_seen.restype = ctypes.c_bool
    return lib

NORTH, EAST, SOUTH, WEST = C.DIR_NORTH, C.DIR_EAST, C.DIR_SOUTH, C.DIR_WEST
STEP = {NORTH: (0, -1), EAST: (1, 0), SOUTH: (0, 1), WEST: (-1, 0)}
WALL, DOOR, FAKE_WALL = C.SQUARE_WALL, C.SQUARE_DOOR, C.SQUARE_FAKE_WALL
FAKE_WALL_OPEN, DOOR_STATE, DOOR_CLOSED = C.SQUARE_FAKE_WALL_OPEN, C.SQUARE_DOOR_STATE, C.DOOR_STATE_CLOSED
ROW_HALF_WIDTH = (1, 1, 2, 2, 3)  # the side squares of each row of the dungeon view
KEY_TAB, KEY_M, KEY_ESC, KEY_I, KEY_F, KEY_L = C.KEY_AUTOMAP, C.KEY_AUTOMAP_M, 0x01, 0x17, 0x21, 0x26
CHTS_SIZE = 6  # the CHTS block after the AMAP block of an advanced save
INPUT_BUTTON_LEFT, INPUT_LEFT_RELEASED = C.INPUT_BUTTON_LEFT, C.INPUT_LEFT_RELEASED
ZONE_VIEWPORT, UI_RED, CELL = C.ZONE_VIEWPORT, C.UI_RED, C.MAP_CELL_SIZE
SCREEN_H, FILLER = C.SCREEN_H, 0xEE
HEALTH, FOOD, WATER = 0x36, 0x44, 0x46  # champion record offsets
SAVE_OK = C.SAVE_OK


@pytest.fixture
def automap(lib, advanced):  # noqa: F811
    """The advanced game with the bright light and the cheats off."""
    lib.dungeon_square.restype = ctypes.c_uint8
    lib.game_set_cheats.argtypes = [ctypes.c_uint8]
    lib.game_get_cheats.restype = ctypes.c_uint8
    flag(lib, "g_view_light_level").value = 0
    lib.game_set_cheats(0)
    game, _ = advanced
    lib.seen_mark(Position(game.party_map, game.party_x, game.party_y), game.party_dir)  # the start step ran in vanilla
    yield advanced
    lib.game_set_cheats(0)
    flag(lib, "g_advanced_screen").value = 0


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
    kind = square >> C.SQUARE_TYPE_SHIFT
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
            if lib.seen_is_seen(Position(m, x, y))}


def floor_views(lib, m):
    """Every party position on an open square of map m with each direction."""
    return [(x, y, d) for x in range(lib.dungeon_map_width(m)) for y in range(lib.dungeon_map_height(m))
            if not blocks(lib, m, x, y) for d in range(C.DIR_COUNT)]


def test_marks_start_and_squares_ahead(lib, automap):
    game, _ = automap
    m, x, y, d = game.party_map, game.party_x, game.party_y, game.party_dir
    lib.seen_reset()
    assert not seen_squares(lib, m)
    lib.seen_mark(Position(m, x, y), d)
    assert (x, y) in seen_squares(lib, m) and ahead(x, y, d, 1) in seen_squares(lib, m)
    assert seen_squares(lib, m) == expected(lib, m, x, y, d)


def test_vanilla_marks_no_square(lib, session):  # noqa: F811
    # Only the advanced mode marks the view of the party (game.c): a vanilla walk and turn mark nothing.
    game, _ = session
    set_mode(lib, VANILLA)
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    lib.seen_reset()
    x, y = game.party_x, game.party_y
    run_ticks(lib, 20)
    press(lib, C.KEYPAD_FORWARD)  # forward
    run_ticks(lib, 40)
    press(lib, C.KEYPAD_TURN_RIGHT)  # turn right
    assert (game.party_x, game.party_y) != (x, y)
    assert not seen_squares(lib, game.party_map)


def test_the_game_marks_what_the_view_shows(lib, automap):
    # Each game step marks the view of the party; a turn shows more squares.
    game, _ = automap
    m = game.party_map
    run_ticks(lib, 20)
    first = seen_squares(lib, m)
    assert first == expected(lib, m, game.party_x, game.party_y, game.party_dir)
    press(lib, C.KEYPAD_TURN_RIGHT)  # turn right
    assert seen_squares(lib, m) > first


def test_walls_stop_the_view_and_hide_the_side_squares(lib, automap):
    game, _ = automap
    m, stopped, hidden = game.party_map, 0, 0
    for x, y, d in floor_views(lib, m):
        lib.seen_reset()
        lib.seen_mark(Position(m, x, y), d)
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
        lib.seen_reset()
        lib.seen_mark(Position(m, x, y), d)
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
        flag(lib, "g_view_light_level").value = level
        lib.game_set_cheats(C.CHEAT_LIGHT if light else 0)
        lib.seen_reset()
        lib.seen_mark(Position(m, x, y), d)
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
    press(lib, C.KEYPAD_TURN_RIGHT)
    seen = seen_squares(lib, game.party_map)
    seed = ctypes.c_uint32.in_dll(lib, "g_random_seed")
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
    block = mine[len(vanilla):-CHTS_SIZE]
    assert block[:6] == b"AMAP\x01" + bytes([lib.dungeon_map_count()]) and len(block) == block_size(lib)
    assert mine[-CHTS_SIZE:-1] == b"CHTS\x01"
    assert block[6:8] == bytes([lib.dungeon_map_width(0), lib.dungeon_map_height(0)])


def test_amap_block_round_trip(lib, gdat, saves, tmp_path):  # noqa: F811
    game, seen, vanilla, mine = saves
    lib.seen_reset()
    assert load(lib, gdat, tmp_path / "L.DAT", mine) == SAVE_OK
    assert seen_squares(lib, game.party_map) == seen
    assert load(lib, gdat, tmp_path / "L.DAT", vanilla) == SAVE_OK
    assert not seen_squares(lib, game.party_map), "a save without the block gives an empty map"


@pytest.mark.parametrize("damage", ["short", "long", "magic", "version", "map_count", "map_size", "value", "chts"])
def test_damaged_amap_block_gives_an_empty_map(lib, gdat, saves, tmp_path, damage):  # noqa: F811
    game, seen, vanilla, mine = saves
    n = len(vanilla)
    data = bytearray(mine)
    if damage == "short":
        del data[-CHTS_SIZE - 1]
    elif damage == "long":
        data.insert(len(data) - CHTS_SIZE, 0)
    elif damage == "chts":
        data[-2] += 1  # the version of the CHTS block
    else:
        offset = {"magic": 0, "version": 4, "map_count": 5, "map_size": 6, "value": block_size(lib) - 1}[damage]
        data[n + offset] = 2 if damage == "value" else data[n + offset] + 1
    position = (game.party_x, game.party_y, game.party_map)
    assert load(lib, gdat, tmp_path / "L.DAT", bytes(data)) == SAVE_OK
    assert (game.party_x, game.party_y, game.party_map) == position
    assert not seen_squares(lib, game.party_map)


def test_original_loader_ignores_the_amap_block(lib, gdat, saves, game_dir):  # noqa: F811
    # The original load of SKULL.EXE stops at the last bit of the game state: the block changes nothing it reads.
    from dm2test.save import RECORD_GLOBALS, original_load
    _, _, vanilla, mine = saves
    states = []
    for data in (vanilla, mine):
        emu = original_load(lib, gdat, data)
        states.append({name: emu.read(addr, size) for name, (addr, size) in RECORD_GLOBALS.items()})
    assert states[0] == states[1]


def test_quick_save_keeps_the_automap(lib, automap):
    game, data = automap
    run_ticks(lib, 20)
    press(lib, C.KEYPAD_TURN_RIGHT)
    seen = seen_squares(lib, game.party_map)
    press(lib, KEY_F5)
    assert (data / "QUICK.DAT").read_bytes()[-block_size(lib) - CHTS_SIZE:][:4] == b"AMAP"
    lib.seen_reset()
    press(lib, KEY_F9)
    assert seen_squares(lib, game.party_map) >= seen


def test_damaged_quick_save_keeps_the_automap(lib, automap):
    # The running game comes back from QUICK.TMP with its AMAP block.
    game, data = automap
    run_ticks(lib, 20)
    press(lib, C.KEYPAD_TURN_RIGHT)
    seen = seen_squares(lib, game.party_map)
    (data / "QUICK.DAT").write_bytes(bytes(300))
    press(lib, KEY_F9)
    assert seen and seen_squares(lib, game.party_map) >= seen


def test_new_game_starts_with_an_empty_automap_and_no_cheats(lib, automap, game_dir):
    game, data = automap
    from dm2test.dialog import Session
    run_ticks(lib, 20)
    assert seen_squares(lib, game.party_map)
    lib.game_set_cheats(C.CHEAT_ALL)
    (data / "DUNGEON.DAT").symlink_to(game_dir / "DATA" / "DUNGEON.DAT")
    state = Session.in_dll(lib, "g_session")
    state.new_game = 1
    try:
        assert lib.session_load() == 1
    finally:
        state.new_game = 0
    assert not any(seen_squares(lib, m) for m in range(lib.dungeon_map_count()))
    assert lib.game_get_cheats() == 0


def viewport(lib):
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(ZONE_VIEWPORT, rect)
    return tuple(rect)


def screen(lib):
    return (ctypes.c_uint8 * (SCREEN_W * SCREEN_H)).in_dll(lib, "g_video_fb")


def red(lib):
    return ctypes.POINTER(ctypes.c_uint8).in_dll(lib, "g_ui_colors")[UI_RED]


def party_frame(lib, dx=0):
    """A pixel of the red frame around the party square, dx squares right of the middle: the middle of the left side, or
    of the top side when the party faces west (the side toward the party direction is white)."""
    x, y, w, h = viewport(lib)
    west = GameRecord.in_dll(lib, "g_game").party_dir == C.DIR_WEST
    px, py = (CELL // 2, 0) if west else (0, CELL // 2)
    return screen(lib)[(y + (h - CELL) // 2 + py) * SCREEN_W + x + (w - CELL) // 2 + dx * CELL + px]


def test_automap_opens_only_in_the_dungeon_view_of_the_advanced_mode(lib, automap):
    assert key_command(lib, INPUT_MODE_MOVE, KEY_TAB)
    assert key_command(lib, INPUT_MODE_INVENTORY, KEY_TAB) == C.CMD_INVENTORY_LEADER, "Tab closes the inventory"
    set_mode(lib, VANILLA)
    assert key_command(lib, INPUT_MODE_MOVE, KEY_TAB) == key_command(lib, INPUT_MODE_MOVE, KEY_M) == 0
    press(lib, KEY_TAB)
    set_mode(lib, ADVANCED)
    assert not flag(lib, "g_advanced_screen").value
    lib.ui_toggle_inventory(0)
    press(lib, KEY_TAB)
    assert not flag(lib, "g_advanced_screen").value and lib.input_get_mode() == INPUT_MODE_MOVE
    press(lib, KEY_PAUSE)
    press(lib, KEY_M)
    press(lib, KEY_PAUSE)
    assert not flag(lib, "g_advanced_screen").value
    lib.input_key_down.argtypes = [ctypes.c_uint16]
    for key in (KEY_TAB, KEY_M):
        for close in (KEY_TAB, KEY_M, KEY_ESC):
            press(lib, key)
            assert flag(lib, "g_advanced_screen").value and party_frame(lib) == red(lib)
            press(lib, close)
            assert not flag(lib, "g_advanced_screen").value
            assert viewport_pixels(lib) == c_view(lib), "the dungeon view is back after a step"
    assert lib.input_get_mode() == INPUT_MODE_MOVE


def viewport_pixels(lib):
    x, y, w, h = viewport(lib)
    fb = bytes(screen(lib))
    return b"".join(fb[(y + row) * SCREEN_W + x:(y + row) * SCREEN_W + x + w] for row in range(h))


def test_close_key_drops_the_click_of_the_same_tick(lib, automap):
    # A click on the viewport and Esc in one tick: the automap closes and the click does not reach the game.
    game, _ = automap
    lib.input_mouse_click.argtypes = [ctypes.c_int16, ctypes.c_int16, ctypes.c_int]
    press(lib, KEY_TAB)
    x, y, w, h = viewport(lib)
    lib.input_mouse_click(x + w // 2, y + h // 2, INPUT_BUTTON_LEFT)
    lib.input_key_down(KEY_ESC)
    lib.input_key_down(SCAN_W)
    position = (game.party_x, game.party_y)
    run_ticks(lib, 40)
    assert not flag(lib, "g_advanced_screen").value and (game.party_x, game.party_y) == position
    assert lib.input_get_mode() == INPUT_MODE_MOVE


def test_time_stops_and_keys_are_swallowed_while_open(lib, automap):
    game, data = automap
    press(lib, KEY_TAB)
    state = (game.game_time, game.party_x, game.party_y, game.party_dir)
    for key in (SCAN_W, C.KEYPAD_TURN_RIGHT, KEY_F5, KEY_F9):
        press(lib, key)
    assert (game.game_time, game.party_x, game.party_y, game.party_dir) == state
    assert not (data / "QUICK.DAT").exists(), "F5 and F9 do nothing on the open automap"
    assert flag(lib, "g_advanced_screen").value
    press(lib, KEY_ESC)
    assert game.game_time > state[0] and not flag(lib, "g_advanced_screen").value


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


@pytest.mark.parametrize("draw", ["advanced_draw_automap", "cheats_open"])
def test_screen_fills_the_whole_viewport_while_the_party_walks(lib, automap, draw):
    # A screen opened during a step: the walk clips the view zones to g_zone_walk_view_rect, not the screens.
    x, y, w, h = viewport(lib)
    fb = screen(lib)
    for py in range(y, y + h):
        fb[py * SCREEN_W + x:py * SCREEN_W + x + w] = bytes([FILLER]) * w
    lib.move_set_walk_steps(2)
    try:
        getattr(lib, draw)()
    finally:
        lib.move_set_walk_steps(0)
    assert FILLER not in viewport_pixels(lib)


def test_drag_scrolls_the_map_in_whole_squares(lib, automap):
    lib.input_mouse_click.argtypes = [ctypes.c_int16, ctypes.c_int16, ctypes.c_int]
    press(lib, KEY_TAB)
    x, y, w, h = viewport(lib)
    mouse_x, mouse_y = ctypes.c_int.in_dll(lib, "g_input_mouse_x"), ctypes.c_int.in_dll(lib, "g_input_mouse_y")
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
    lib.input_mouse_click(start_x, start_y, INPUT_BUTTON_LEFT)
    run_ticks(lib, 2)
    mouse_x.value = start_x + 40 * CELL  # far past the left edge: the middle stops at the margin column -1
    lib.input_mouse_click(mouse_x.value, start_y, INPUT_LEFT_RELEASED)
    run_ticks(lib, 2)
    game, _ = automap
    assert party_frame(lib, game.party_x + 1) == red(lib)
    press(lib, KEY_TAB)
    press(lib, KEY_TAB)
    assert party_frame(lib) == red(lib), "a closed automap opens on the party"
    press(lib, KEY_TAB)


def test_readout_shows_position_and_direction(lib, automap):
    # Drawing each expected line again at its place changes no pixel; a wrong text changes some.
    game, _ = automap
    lib.text_draw_on_screen.argtypes = [ctypes.c_int] * 4 + [ctypes.c_char_p]
    colors = ctypes.POINTER(ctypes.c_uint8).in_dll(lib, "g_ui_colors")
    press(lib, KEY_TAB)
    x, y, _, _ = viewport(lib)
    lines = [b"LEVEL %d  X %d  Y %d" % (lib.dungeon_map_level(game.party_map), game.party_x, game.party_y),
             [b"NORTH", b"EAST", b"SOUTH", b"WEST"][game.party_dir]]
    shown = viewport_pixels(lib)
    for row, text in enumerate(lines):
        for line, same in ((text, True), (text[:-1] + b"X", False)):
            lib.text_draw_on_screen(x + 2, y + 6 + row * 7, colors[C.UI_WHITE], colors[C.UI_BLACK], line)
            assert (viewport_pixels(lib) == shown) == same, line
            lib.advanced_draw_automap()
    press(lib, KEY_TAB)


def test_the_automap_has_no_cheat_keys(lib, automap):
    press(lib, KEY_TAB)
    before = bytes(screen(lib))
    for key in (KEY_I, KEY_F, KEY_L):
        press(lib, key)
    assert lib.game_get_cheats() == 0 and bytes(screen(lib)) == before


def test_reveal_map_draws_every_square_and_keeps_the_seen_squares(lib, automap):
    game, _ = automap
    seen = seen_squares(lib, game.party_map)
    press(lib, KEY_TAB)
    shown = viewport_pixels(lib)
    lib.game_set_cheats(C.CHEAT_MAP)
    lib.advanced_draw_automap()
    assert viewport_pixels(lib) != shown and seen_squares(lib, game.party_map) == seen
    press(lib, KEY_TAB)


def champion_word(lib, offset, value=None):
    from dm2test.interface import champion_records
    word = ctypes.c_int16.from_buffer(champion_records(lib), offset)
    if value is not None:
        word.value = value
    return word.value


def test_immortal_keeps_one_health(lib, automap):
    damage = (ctypes.c_int16 * 4).in_dll(lib, "g_champ_pending_damage")
    lib.game_set_cheats(C.CHEAT_IMMORTAL)
    damage[0] = 30000
    lib.champ_apply_pending_damage()
    assert champion_word(lib, HEALTH) == 1
    damage[0] = 1
    lib.champ_apply_pending_damage()
    assert champion_word(lib, HEALTH) == 1, "the champion does not die"


def test_food_cheat_keeps_food_and_water(lib, automap):
    champion_word(lib, FOOD, 1000)
    champion_word(lib, WATER, 1000)
    lib.game_set_cheats(C.CHEAT_FOOD)
    for _ in range(20):
        lib.champ_apply_time_effects()
    assert (champion_word(lib, FOOD), champion_word(lib, WATER)) == (1000, 1000)
    lib.game_set_cheats(0)
    for _ in range(20):
        lib.champ_apply_time_effects()
    assert champion_word(lib, FOOD) < 1000 and champion_word(lib, WATER) < 1000


def test_light_cheat_draws_full_light(lib, automap):
    game, _ = automap
    level = flag(lib, "g_view_light_level")

    def view(light, cheat):
        level.value = light
        lib.game_set_cheats(C.CHEAT_LIGHT if cheat else 0)
        lib.view_draw_dungeon(Location(game.party_x, game.party_y), game.party_dir)
        return c_view(lib)
    dark, cheat, bright = view(5, 0), view(5, 1), view(0, 0)
    assert cheat == bright != dark and len(bright) == VIEW_W * VIEW_H
    assert level.value == 0 and view(5, 1) and level.value == 5, "the creature rules keep the real light"


def test_quick_load_takes_the_saved_cheats(lib, automap):
    lib.game_set_cheats(C.CHEAT_FOOD | C.CHEAT_KILL)
    press(lib, KEY_F5)
    lib.game_set_cheats(C.CHEAT_ALL)
    press(lib, KEY_F9)
    assert lib.game_get_cheats() == C.CHEAT_FOOD | C.CHEAT_KILL
    flag(lib, "g_advanced_screen").value = 1
    lib.play_start_session()
    assert not flag(lib, "g_advanced_screen").value
    assert lib.game_get_cheats() == C.CHEAT_FOOD | C.CHEAT_KILL, "a session start keeps the cheats"




TREE, MOVING, CREATURE_STATIC = 20, 27, 0x0001  # a static tree of the outdoor maps; a creature that map 0 takes
GROUP_CREATURE = 4  # the creature type byte of a group record


def test_automap_draws_the_static_creatures_as_a_minion_map_without_options(lib, automap):
    # A static creature (a tree) on a seen square shows; a moving creature does not. The map draws without a game
    # step: the AI never runs the tree that was a moving group.
    game, _ = automap
    lib.creature_create_group.restype = ctypes.c_uint16
    lib.creature_create_group.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int16, Location]
    lib.creature_group_info.restype = ctypes.POINTER(ctypes.c_uint16)
    lib.creature_group_info.argtypes = [ctypes.c_uint16]
    lib.dungeon_thing_record.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.dungeon_thing_record.argtypes = [ctypes.c_uint16]
    m, d = game.party_map, game.party_dir
    squares = [ahead(game.party_x, game.party_y, d, depth) for depth in (1, 2)]
    assert all(lib.dungeon_square(m, x, y) >> 5 == 1 and (x, y) in seen_squares(lib, m) for x, y in squares)
    press(lib, KEY_TAB)

    def cell(x, y):
        lib.advanced_draw_automap()
        vx, vy, w, h = viewport(lib)
        left = vx + (w - CELL) // 2 + (x - game.party_x) * CELL
        top = vy + (h - CELL) // 2 + (y - game.party_y) * CELL
        return [bytes(screen(lib)[(top + r) * SCREEN_W + left:(top + r) * SCREEN_W + left + CELL]) for r in range(CELL)]

    for (x, y), kind in zip(squares, (MOVING, TREE)):
        before = cell(x, y)
        lib.dun_set_current_map(m)
        g = lib.creature_create_group(MOVING, 1, C.DIR_NORTH, Location(x, y))
        assert g != 0xFFFF
        lib.dungeon_thing_record(g)[GROUP_CREATURE] = kind
        static = bool(lib.creature_group_info(g)[0] & CREATURE_STATIC)
        assert static == (kind == TREE)
        assert (cell(x, y) != before) == static, kind
    press(lib, KEY_TAB)


MINION, CAT_CONTAINER, MAP_ITEM_ICONS, ICON_KEY = 48, 0x14, 0x40, 10  # a moving creature without a map icon strip
ICON_SIZE, ICON_GAP = 7, 1


class Bitmap(ctypes.Structure):
    _fields_ = [("pixels", ctypes.POINTER(ctypes.c_uint8)), ("width", ctypes.c_int16), ("height", ctypes.c_int16),
                ("bpp", ctypes.c_uint8)]


def icon_pixels(lib, category, index, entry, icon):
    """The 7 x 7 pixels of icon of the GDAT image, as the map draws them: None where the icon is transparent."""
    lib.draw_gdat_image.restype = ctypes.POINTER(Bitmap)
    lib.draw_gdat_image_palette.restype = ctypes.POINTER(ctypes.c_uint8)
    image = lib.draw_gdat_image(category, index, entry).contents
    colors = lib.draw_gdat_image_palette(category, index, entry)
    row_bytes = (image.width + 1) // 2 if image.bpp == 4 else image.width
    rows = []
    for r in range(ICON_SIZE):
        row = []
        for c in range(icon * ICON_SIZE, (icon + 1) * ICON_SIZE):
            v = image.pixels[r * row_bytes + c // 2] >> (0 if c & 1 else 4) & 0xF if image.bpp == 4 else \
                image.pixels[r * row_bytes + c]
            row.append(None if v == ICON_KEY else (colors[v] if colors else v))
        rows.append(row)
    return rows


def test_automap_draws_a_creature_without_map_icons_with_the_map_item_icon(lib, automap):
    # A moving creature without its own map icons (a minion) takes the creature icon of the map item, as on the minion
    # map; the automap has no map item thing, so it takes the image of the party icons, not the missing image.
    game, _ = automap
    lib.creature_create_group.restype = ctypes.c_uint16
    lib.creature_create_group.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int16, Location]
    lib.dungeon_thing_record.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.dungeon_thing_record.argtypes = [ctypes.c_uint16]
    m, d = game.party_map, game.party_dir
    x, y = ahead(game.party_x, game.party_y, d, 1)
    assert lib.dungeon_square(m, x, y) >> 5 == 1 and (x, y) in seen_squares(lib, m)
    lib.dun_set_current_map(m)
    g = lib.creature_create_group(MOVING, 1, C.DIR_NORTH, Location(x, y))
    assert g != 0xFFFF
    lib.dungeon_thing_record(g)[GROUP_CREATURE] = MINION
    game.game_time &= ~1  # the creature icon 0 of the two that alternate
    press(lib, KEY_TAB)
    lib.advanced_draw_automap()
    vx, vy, w, h = viewport(lib)
    left = vx + (w - CELL) // 2 + (x - game.party_x) * CELL + ICON_GAP
    top = vy + (h - CELL) // 2 + (y - game.party_y) * CELL + ICON_GAP
    item = lib.mmap_party_icons_item()
    assert item >= 0
    icon = icon_pixels(lib, CAT_CONTAINER, item, MAP_ITEM_ICONS, 0)
    fb = screen(lib)
    drawn = [[fb[(top + r) * SCREEN_W + left + c] for c in range(ICON_SIZE)] for r in range(ICON_SIZE)]
    assert any(v is not None for row in icon for v in row)
    assert all(v is None or v == drawn[r][c] for r, row in enumerate(icon) for c, v in enumerate(row))
    press(lib, KEY_TAB)
