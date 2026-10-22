"""The cheats of the advanced mode in the engine (src/engine/game.c, creature.c, move.c, sensor.c): the bit set,
one-hit kill, and walk through walls."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import session  # noqa: F401 (session is a fixture)
from dm2test.modes import VANILLA, set_mode
from dm2test.scenario import Party, gdat, start  # noqa: F401 (gdat and start are fixtures)
from dm2test.lib import load_lib

DIRS = ((0, -1), (1, 0), (0, 1), (-1, 0))
CHEATS = (C.CHEAT_LIGHT, C.CHEAT_IMMORTAL, C.CHEAT_FOOD, C.CHEAT_KILL, C.CHEAT_WALLS, C.CHEAT_MAP)
HEALTH = 1000  # far more health than the test hits take
FLAGS, DEFENSE, PUSH_RESISTANCE = 0, 2, 0x1D  # byte offsets in a creature info record
UNPUSHABLE = 0xFE  # push resistance 0xfe and 0xff: the party cannot push the creature


class Location(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16)]


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.game_get_cheats.restype = ctypes.c_uint8
    lib.game_set_cheats.argtypes = [ctypes.c_uint8]
    lib.game_toggle_cheat.argtypes = [ctypes.c_uint8]
    lib.game_has_cheat.argtypes = [ctypes.c_uint8]
    lib.game_has_cheat.restype = ctypes.c_bool
    lib.thing_word.restype = ctypes.c_uint16
    lib.set_thing_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    lib.dungeon_square.restype = ctypes.c_uint8
    lib.creature_get_group_at.restype = ctypes.c_uint16
    lib.creature_info.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.creature_info.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.creature_alert.argtypes = [ctypes.c_uint16, Location, ctypes.c_uint, ctypes.c_int, ctypes.c_int]
    lib.creature_process_event.argtypes = [ctypes.c_int16, ctypes.c_int16, ctypes.c_int]
    lib.move_party.restype = ctypes.c_bool
    return lib


@pytest.fixture
def game(lib, session):  # noqa: F811
    """start.DAT with all cheats off; all off again after the test."""
    lib.game_set_cheats(0)
    yield session[0]
    lib.game_set_cheats(0)
    set_mode(lib, VANILLA)


def test_cheat_bits(lib, game):
    for cheat in CHEATS:
        lib.game_toggle_cheat(cheat)
        assert lib.game_get_cheats() == cheat and lib.game_has_cheat(cheat)
        lib.game_toggle_cheat(cheat)
        assert lib.game_get_cheats() == 0 and not lib.game_has_cheat(cheat)
    lib.game_set_cheats(0xFF)
    assert lib.game_get_cheats() == C.CHEAT_ALL


# One-hit kill.


def first_group(lib, gdat, game, wanted=lambda info: True):  # noqa: F811
    """(group, x, y) of the first creature group of the dungeon whose creature info fits wanted(info); the party enters
    its map, the group is active."""
    for m in range(lib.dungeon_map_count()):
        lib.dun_set_current_map(m)
        for x in range(lib.dungeon_map_width(m)):
            for y in range(lib.dungeon_map_height(m)):
                group = lib.creature_get_group_at(x, y)
                if group != C.THING_NONE and wanted(lib.creature_info(gdat, lib.thing_word(group, C.GROUP_CREATURE) & 0xFF)):
                    enter(lib, game, m, x, y, 0)
                    assert lib.creature_get_group_at(x, y) == group
                    return group, x, y
    raise AssertionError("no creature group in the dungeon")


def enter(lib, game, m, x, y, d):
    """The party arrives on (x, y) of map m, looking to d; the groups of the map become active."""
    party = Party.in_dll(lib, "g_party")
    game.party_dir = d
    party.move_steps, party.cur_map = 0, -1  # the party square follows on the map change
    lib.game_enter_map(x, y, m, 0)


def unhurt_creature(lib, gdat):  # noqa: F811
    """A creature type whose defense is CREATURE_NO_HURT."""
    for creature in range(256):
        if lib.creature_info(gdat, creature)[DEFENSE] == C.CREATURE_NO_HURT:
            return creature
    raise AssertionError("no creature that cannot be hurt")


def hit(lib, group, x, y, damage):
    """A hit of damage on the group, with much health first; returns True when the group is gone."""
    lib.set_thing_word(group, C.GROUP_HEALTH, HEALTH)
    lib.creature_alert(group, Location(x, y), 0, 0, damage)
    lib.creature_process_event(x, y, C.EVENT_CREATURE_THINK)
    return lib.creature_get_group_at(x, y) != group or lib.thing_word(group, C.GROUP_HEALTH) <= 1


@pytest.mark.parametrize("cheat", [False, True])
def test_one_hit_kill(lib, gdat, game, cheat):  # noqa: F811
    group, x, y = first_group(lib, gdat, game)
    lib.game_set_cheats(C.CHEAT_KILL if cheat else 0)
    assert hit(lib, group, x, y, 3) == cheat
    if not cheat:
        assert lib.thing_word(group, C.GROUP_HEALTH) == HEALTH - 3


def test_one_hit_kill_keeps_a_creature_that_cannot_be_hurt(lib, gdat, game):  # noqa: F811
    group, x, y = first_group(lib, gdat, game)
    word = lib.thing_word(group, C.GROUP_CREATURE)
    lib.set_thing_word(group, C.GROUP_CREATURE, word & 0xFF00 | unhurt_creature(lib, gdat))
    lib.game_set_cheats(C.CHEAT_KILL)
    assert not hit(lib, group, x, y, 3)
    assert lib.thing_word(group, C.GROUP_HEALTH) == HEALTH


# Walk through walls.


def find_place(lib, game, wanted):
    """(map, x, y, dir): an empty floor square whose next square in dir fits wanted(map, x, y)."""
    for m in range(lib.dungeon_map_count()):
        lib.dun_set_current_map(m)
        for x in range(lib.dungeon_map_width(m)):
            for y in range(lib.dungeon_map_height(m)):
                if lib.dungeon_square(m, x, y) >> C.SQUARE_TYPE_SHIFT != C.SQUARE_FLOOR or \
                        lib.creature_get_group_at(x, y) != C.THING_NONE:
                    continue
                for d, (dx, dy) in enumerate(DIRS):
                    if wanted(m, x + dx, y + dy):
                        return m, x, y, d
    raise AssertionError("no such square")


def square_kind(lib, m, x, y):
    if not (0 <= x < lib.dungeon_map_width(m) and 0 <= y < lib.dungeon_map_height(m)):
        return None
    return lib.dungeon_square(m, x, y) >> C.SQUARE_TYPE_SHIFT


def walk(lib, game, place, cheat):
    """The party on place steps forward; returns its square after the step."""
    enter(lib, game, *place)
    party = Party.in_dll(lib, "g_party")
    lib.game_set_cheats(C.CHEAT_WALLS if cheat else 0)
    lib.move_party(C.CMD_MOVE_FORWARD)
    while party.move_steps:  # the walk of a slow party: the step comes with the last tick
        if party.move_steps == 1:
            lib.move_party(C.CMD_MOVE_FORWARD)
        party.move_steps -= 1
    return game.party_x, game.party_y


def is_wall(lib, m, x, y):
    return square_kind(lib, m, x, y) == C.SQUARE_WALL


def is_closed_door(lib, m, x, y):
    return square_kind(lib, m, x, y) == C.SQUARE_DOOR and \
        lib.dungeon_square(m, x, y) & C.SQUARE_DOOR_STATE == C.DOOR_STATE_CLOSED


def is_outside(lib, m, x, y):
    return square_kind(lib, m, x, y) is None


@pytest.mark.parametrize("wanted", [is_wall, is_closed_door])
def test_walk_through_walls(lib, game, wanted):
    place = find_place(lib, game, lambda m, x, y: wanted(lib, m, x, y))
    m, x, y, d = place
    assert walk(lib, game, place, False) == (x, y)
    ahead = (x + DIRS[d][0], y + DIRS[d][1])
    assert walk(lib, game, place, True) == ahead
    assert wanted(lib, m, *ahead), "a door stays closed"


def test_walk_through_walls_stops_at_the_map_edge(lib, game):
    place = find_place(lib, game, lambda m, x, y: is_outside(lib, m, x, y))
    assert walk(lib, game, place, True) == place[1:3]


def is_solid_and_unpushable(info):
    flags = info[FLAGS] | info[FLAGS + 1] << 8
    return not flags & (C.CREATURE_NOT_SOLID | C.CREATURE_STATIC) and info[PUSH_RESISTANCE] >= UNPUSHABLE


def test_walk_through_walls_stops_at_a_creature(lib, gdat, game):  # noqa: F811
    group, gx, gy = first_group(lib, gdat, game, is_solid_and_unpushable)
    m = game.party_map
    place = find_place(lib, game, lambda m2, x, y: (m2, x, y) == (m, gx, gy))
    assert walk(lib, game, place, True) == place[1:3]


class TimelineEvent(ctypes.Structure):
    _fields_ = [("time_map", ctypes.c_uint32), ("type", ctypes.c_uint8), ("priority", ctypes.c_uint8),
                ("data", ctypes.c_uint8 * C.TIMELINE_EVENT_DATA_SIZE)]


@pytest.mark.parametrize("cheat", [False, True])
def test_a_closing_door_hurts_the_party_only_without_the_cheat(lib, game, cheat):
    place = find_place(lib, game, lambda m, x, y: is_closed_door(lib, m, x, y))
    m, x, y = place[0], *walk(lib, game, place, True)
    lib.game_set_cheats(C.CHEAT_WALLS if cheat else 0)
    lib.dungeon_square_byte.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    square = lib.dungeon_square_byte(m, x, y)
    square[0] = square[0] & ~C.SQUARE_DOOR_STATE | 1  # nearly open
    door = lib.dungeon_square_first_thing(m, x, y)
    lib.set_thing_word(door, C.DOOR_DATA, lib.thing_word(door, C.DOOR_DATA) & ~C.DOOR_OPENING | C.DOOR_MOVING)
    damage = (ctypes.c_int16 * C.CHAMPION_MAX).in_dll(lib, "g_champ_pending_damage")
    ctypes.memset(damage, 0, ctypes.sizeof(damage))
    event = TimelineEvent(type=C.EVENT_DOOR_ANIMATION)
    event.data[C.EVENT_X], event.data[C.EVENT_Y] = x, y
    lib.timeline_event_door_animation(ctypes.byref(event))
    assert (sum(damage) > 0) != cheat
    assert square[0] & C.SQUARE_DOOR_STATE == C.DOOR_STATE_OPEN, "the door opens again over the party"
