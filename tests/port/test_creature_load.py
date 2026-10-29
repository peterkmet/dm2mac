"""Creature groups of a loaded save before the session activates them (src/engine/creature.c)."""

import ctypes
import struct

import pytest

from dm2test import constants as C
from dm2test.lib import load_lib
from dm2test.scenario import Location, gdat, start  # noqa: F401 (gdat and start are fixtures)

STALE_INDEX = 200  # an active group index of the saved game, past the active groups of this session
MOVEMENT = 10  # byte offset of CreatureInfo.movement
MANY_GROUPS = 1000  # more groups than an active index byte can count
THING_RECORD_SIZE = 16  # bytes of a group record


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.creature_is_flying.restype = ctypes.c_bool
    lib.creature_is_flying.argtypes = [ctypes.c_uint16]
    lib.creature_get_group_at.restype = ctypes.c_uint16
    lib.creature_get_group_at.argtypes = [Location]
    lib.creature_info.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.creature_info.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.thing_word.restype = ctypes.c_uint16
    lib.thing_set_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    return lib


def walking_group(lib, gdat):  # noqa: F811
    """A creature group whose creature does not fly; its map is the current map."""
    for m in range(lib.dungeon_map_count()):
        lib.dun_set_current_map(m)
        for x in range(lib.dungeon_map_width(m)):
            for y in range(lib.dungeon_map_height(m)):
                group = lib.creature_get_group_at(Location(x, y))
                if group == C.THING_NONE:
                    continue
                info = lib.creature_info(gdat, lib.thing_word(group, C.GROUP_CREATURE) & 0xFF)
                if not (info[MOVEMENT] | info[MOVEMENT + 1] << 8) & C.CREATURE_FLIES:
                    return group
    raise AssertionError("no creature group that does not fly")


def test_a_stale_active_index_reads_no_active_group(lib, gdat, start, tmp_path):  # noqa: F811
    # regression: creature_is_flying read the active group of a stale index past the end of the active groups
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    group = walking_group(lib, gdat)
    word = lib.thing_word(group, C.GROUP_CREATURE)
    lib.thing_set_word(group, C.GROUP_CREATURE, word & 0xFF | STALE_INDEX << 8)
    assert not lib.creature_is_flying(group)


def test_the_active_groups_end_below_the_inactive_index(lib, gdat, tmp_path):  # noqa: F811
    # regression: with more than 255 walking groups, active index 255 read as GROUP_NOT_ACTIVE and later ones wrapped
    counts = [MANY_GROUPS if t == C.THING_GROUP else 0 for t in range(C.THING_TYPE_COUNT)]
    path = tmp_path / "groups.dat"
    path.write_bytes(struct.pack("<6H16H", 0, 0, 0, 0, 0, 0, *counts) + bytes(THING_RECORD_SIZE * MANY_GROUPS))
    assert lib.dungeon_load(str(path).encode()) == 1
    walker = next(t for t in range(256) if not lib.creature_info(gdat, t)[0] & C.CREATURE_STATIC)
    for i in range(MANY_GROUPS):
        lib.thing_set_word(C.THING_GROUP << C.THING_TYPE_SHIFT | i, C.GROUP_CREATURE, walker)
    lib.creature_alloc_active_groups()
    assert ctypes.c_int.in_dll(lib, "g_active_group_max").value == C.GROUP_NOT_ACTIVE
