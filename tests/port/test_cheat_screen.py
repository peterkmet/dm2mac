"""The cheat screen of the advanced mode (src/ui/cheats.c, advanced.c) and the cheat cast (src/engine/magic.c): the
spell lines, the pages and rows, the C key, and the cast without the failure roll and experience."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import run_ticks
from dm2test.scenario import champion_records
from dm2test.modes import (ADVANCED, INPUT_MODE_INVENTORY, INPUT_MODE_MOVE, VANILLA, key_command, press, set_mode)
from dm2test.lib import load_lib

SPELL_COUNT, SPELLS_PER_PAGE = 34, 8
PAGE_COUNT = (SPELL_COUNT + SPELLS_PER_PAGE - 1) // SPELLS_PER_PAGE
ROW_LIGHT, ROW_FOOD, ROW_QUICK_SAVE, ROW_QUICK_LOAD, ROW_SPELLS, ROW_CLOSE = 0, 2, 6, 7, 8, 9
STAY, CLOSE, QUICK_SAVE, QUICK_LOAD = C.CHEATS_STAY, C.CHEATS_CLOSE, C.CHEATS_QUICK_SAVE, C.CHEATS_QUICK_LOAD
SCAN_ESCAPE, SCAN_RETURN, SCAN_S = 0x01, 0x1C, 0x1F
FIREBALL_EFFECT = 0x5402  # a missile spell of type 0 (fireball)
# Champion record offsets (the Champion struct of engine/champion.h).
CH_HEALTH, CH_MANA, CH_EXPERIENCE, EXPERIENCE_SIZE = 0x36, 0x3E, 0x5F, 20 * 4
LIGHT, HEALTH_POTION = 0, 25  # spell table indices: OH IR RA, VI


class Spell(ctypes.Structure):
    _fields_ = [("runes", ctypes.c_uint32), ("difficulty", ctypes.c_uint8), ("skill", ctypes.c_uint8),
                ("effect", ctypes.c_uint16)]


class Cursor(ctypes.Structure):
    _fields_ = [("page", ctypes.c_int16), ("row", ctypes.c_int16)]


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.magic_get_spell.restype = ctypes.POINTER(Spell)
    lib.cheats_get_spell_line.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
    lib.cheats_get_cursor.restype = Cursor
    lib.cheats_move_selection.argtypes = [ctypes.c_int16]
    lib.cheats_process_click.argtypes = [ctypes.c_int16, ctypes.c_int16]
    lib.magic_cast_cheat_spell.argtypes = [ctypes.c_int16, ctypes.c_void_p]
    lib.game_has_cheat.argtypes = [ctypes.c_uint8]
    lib.game_has_cheat.restype = ctypes.c_bool
    lib.game_set_cheats.argtypes = [ctypes.c_uint8]
    lib.champ_remove_object_from_slot.restype = ctypes.c_uint16
    lib.champ_add_object_to_slot.argtypes = [ctypes.c_int16, ctypes.c_uint16, ctypes.c_int16]
    lib.item_set_gdat_index.argtypes = [ctypes.c_uint16, ctypes.c_int]
    lib.item_gdat_index.argtypes = [ctypes.c_uint16]
    lib.dungeon_thing_valid.argtypes = [ctypes.c_uint16]
    return lib


@pytest.fixture
def screen(lib, advanced):
    """The open cheat screen in the advanced mode; closed and all cheats off after the test."""
    lib.game_set_cheats(0)
    yield advanced
    ctypes.c_int.in_dll(lib, "g_advanced_screen").value = C.ADVANCED_SCREEN_NONE
    lib.game_set_cheats(0)


def line(lib, spell):
    text = ctypes.create_string_buffer(64)
    lib.cheats_get_spell_line(ctypes.byref(spell) if isinstance(spell, Spell) else spell, text, len(text))
    return text.value.decode()


def cursor(lib):
    c = lib.cheats_get_cursor()
    return c.page, c.row


def select(lib, row):
    """Selects row of the shown page from its first row."""
    for _ in range(C.ROWS_MAX_COUNT):
        lib.cheats_move_selection(-1)
    for _ in range(row):
        lib.cheats_move_selection(1)


@pytest.mark.parametrize("index, text", [(16, "FUL IR (X,4,4) FIREBALL"), (5, "FUL (X,4) MAGIC TORCH"),
                                         (0, "OH IR RA (X,3,4,5) LIGHT"),
                                         (7, "(X,3,2,2) AURA OF DEXTERITY")])  # too wide: no rune names
def test_spell_line(lib, index, text):
    assert line(lib, lib.magic_get_spell(index)) == text


def test_spell_line_with_a_power_rune(lib):
    mon_ful_ir = (C.SPELL_RUNE_GLYPH_FIRST + C.POWER_MON - 1) << 24 | 0x696F << 8
    assert line(lib, Spell(mon_ful_ir, 3, 0x10, FIREBALL_EFFECT)) == "MON FUL IR (6,4,4) FIREBALL"


def test_every_spell_has_a_name(lib):
    assert all(not line(lib, lib.magic_get_spell(i)).endswith("?") for i in range(SPELL_COUNT))
    assert not lib.magic_get_spell(SPELL_COUNT)


def test_rows_and_pages(lib, screen):
    lib.cheats_open()
    assert cursor(lib) == (C.CHEATS_PAGE, ROW_LIGHT)
    lib.cheats_move_selection(-1)
    assert cursor(lib) == (C.CHEATS_PAGE, ROW_LIGHT), "the selection stops at the first row"
    select(lib, ROW_CLOSE + 1)
    assert cursor(lib) == (C.CHEATS_PAGE, ROW_CLOSE), "the selection stops at the last row"
    for row, result in ((ROW_QUICK_SAVE, QUICK_SAVE), (ROW_QUICK_LOAD, QUICK_LOAD), (ROW_CLOSE, CLOSE)):
        select(lib, row)
        assert lib.cheats_run_selected_row() == result
    select(lib, ROW_FOOD)
    assert lib.cheats_run_selected_row() == STAY and lib.game_has_cheat(C.CHEAT_FOOD)
    assert lib.cheats_run_selected_row() == STAY and not lib.game_has_cheat(C.CHEAT_FOOD)
    # SPELLS opens page 1 on NEXT; NEXT goes through the pages and wraps; BACK from page 1 is the SPELLS row.
    select(lib, ROW_SPELLS)
    assert lib.cheats_run_selected_row() == STAY and cursor(lib) == (1, SPELLS_PER_PAGE)
    for page in list(range(2, PAGE_COUNT + 1)) + [1]:
        assert lib.cheats_run_selected_row() == STAY
        assert cursor(lib) == (page, min(SPELLS_PER_PAGE, SPELL_COUNT - (page - 1) * SPELLS_PER_PAGE))
    lib.cheats_move_selection(1)
    assert lib.cheats_run_selected_row() == STAY and cursor(lib) == (C.CHEATS_PAGE, ROW_SPELLS)
    lib.cheats_run_selected_row()
    lib.cheats_move_selection(2)
    assert lib.cheats_run_selected_row() == CLOSE


def test_back_goes_to_the_page_before(lib, screen):
    lib.cheats_open()
    select(lib, ROW_SPELLS)
    lib.cheats_run_selected_row()
    lib.cheats_run_selected_row()
    lib.cheats_run_selected_row()
    assert cursor(lib) == (3, SPELLS_PER_PAGE)
    lib.cheats_move_selection(1)
    lib.cheats_run_selected_row()
    assert cursor(lib) == (2, SPELLS_PER_PAGE)


def test_c_key_opens_the_screen_in_the_dungeon_view_and_stops_the_time(lib, screen):
    game, _ = screen
    mode = ctypes.c_int.in_dll(lib, "g_advanced_screen")
    assert key_command(lib, INPUT_MODE_MOVE, C.KEY_CHEATS) == C.CMD_CHEATS
    assert key_command(lib, INPUT_MODE_INVENTORY, C.KEY_CHEATS) == 0
    set_mode(lib, VANILLA)
    assert key_command(lib, INPUT_MODE_MOVE, C.KEY_CHEATS) == 0
    press(lib, C.KEY_CHEATS)
    assert mode.value == C.ADVANCED_SCREEN_NONE
    set_mode(lib, ADVANCED)
    for close in (C.KEY_CHEATS, SCAN_ESCAPE):
        press(lib, C.KEY_CHEATS)
        assert mode.value == C.ADVANCED_SCREEN_CHEATS and cursor(lib) == (C.CHEATS_PAGE, ROW_LIGHT)
        time = game.game_time
        press(lib, SCAN_S)
        press(lib, SCAN_S)
        press(lib, SCAN_RETURN)
        assert lib.game_has_cheat(C.CHEAT_FOOD) and game.game_time == time
        lib.game_set_cheats(0)
        press(lib, close)
        assert mode.value == C.ADVANCED_SCREEN_NONE
    run_ticks(lib, 40)
    assert game.game_time > time


def test_click_on_a_row_runs_it(lib, screen):
    lib.cheats_open()
    rect = (ctypes.c_int16 * 4)()
    assert lib.zone_get_rect(C.ZONE_VIEWPORT, rect)
    x, y = rect[0] + rect[2] // 2, rect[1] + 22 + ROW_FOOD * 10 + 5  # the middle of the FOOD AND WATER row
    assert lib.cheats_process_click(x, y) == STAY and lib.game_has_cheat(C.CHEAT_FOOD)
    assert cursor(lib) == (C.CHEATS_PAGE, ROW_FOOD)
    assert lib.cheats_process_click(x, rect[1] + 2) == STAY and lib.game_has_cheat(C.CHEAT_FOOD), "the title"


def experience_and_mana(lib, champ):
    record = bytes(champion_records(lib))[champ * C.CHAMPION_SIZE:(champ + 1) * C.CHAMPION_SIZE]
    return record[CH_EXPERIENCE:CH_EXPERIENCE + EXPERIENCE_SIZE], record[CH_MANA:CH_MANA + 2]


def test_cheat_cast_has_no_failure_and_no_experience(lib, screen):
    before = experience_and_mana(lib, 0)
    for _ in range(10):
        assert lib.magic_cast_cheat_spell(0, lib.magic_get_spell(LIGHT)) == C.CAST_OK
    assert experience_and_mana(lib, 0) == before


def test_cheat_cast_without_a_living_champion_says_so(lib, screen):
    # regression: a dead leader made the spell line fail without a message
    records = champion_records(lib)
    health = bytes(records[CH_HEALTH:CH_HEALTH + 2])
    records[CH_HEALTH:CH_HEALTH + 2] = b"\0\0"
    try:
        lib.cheats_open()
        select(lib, ROW_SPELLS)
        lib.cheats_run_selected_row()
        select(lib, 0)
        assert lib.cheats_run_selected_row() == CLOSE
        lib.text_message_row.restype = ctypes.c_char_p
        rows = [lib.text_message_row(r) for r in range(C.MESSAGE_ROWS)]
        assert b"NO CHAMPION CAN CAST" in rows
    finally:
        records[CH_HEALTH:CH_HEALTH + 2] = health


def empty_flask(lib):
    for i in range(lib.dungeon_thing_count(C.THING_POTION)):
        thing = C.THING_POTION << C.THING_TYPE_SHIFT | i
        if lib.dungeon_thing_valid(thing):
            lib.item_set_gdat_index(thing, C.EMPTY_FLASK)
            return thing
    raise AssertionError("no potion in the dungeon")


def test_cheat_potion_needs_an_empty_flask(lib, screen):
    hand = C.SLOT_HAND_COUNT - 1
    for slot in range(C.SLOT_HAND_COUNT):
        lib.champ_remove_object_from_slot(0, slot)
    potion = lib.magic_get_spell(HEALTH_POTION)
    assert lib.magic_cast_cheat_spell(0, potion) == C.CAST_NEEDS_FLASK
    flask = empty_flask(lib)
    lib.champ_add_object_to_slot(0, flask, hand)
    before = experience_and_mana(lib, 0)
    assert lib.magic_cast_cheat_spell(0, potion) == C.CAST_OK
    assert lib.item_gdat_index(flask) == C.POTION_HEALTH and experience_and_mana(lib, 0) == before


def test_quick_load_from_the_screen_restores_the_saved_game_and_closes_the_screen(lib, screen):
    mode = ctypes.c_int.in_dll(lib, "g_advanced_screen")
    press(lib, C.KEY_QUICK_SAVE)
    lib.game_set_cheats(C.CHEAT_FOOD)
    press(lib, C.KEY_CHEATS)
    assert mode.value == C.ADVANCED_SCREEN_CHEATS
    press(lib, C.KEY_QUICK_LOAD)
    assert mode.value == C.ADVANCED_SCREEN_NONE
    assert not lib.game_has_cheat(C.CHEAT_FOOD), "the saved game without the cheat is back"
