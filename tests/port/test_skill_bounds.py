"""A GDAT skill outside the skill table (src/engine/champion.c, src/engine/champion_action.c)."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.interface import GAME_CHAMPIONS

KALAN_GAUNTLET_SKILL = 186  # the third action of the Kalan Gauntlet: "SK18SK6" reads as skill 186
TAIL = 1024  # bytes after the champion records: the rest of g_game and the globals behind it
EXPERIENCE = 100000


@pytest.fixture(scope="module", autouse=True)
def lib_prototypes(lib):
    lib.champ_add_experience.argtypes = [ctypes.c_int16, ctypes.c_int, ctypes.c_uint32]
    lib.champ_get_skill_level.argtypes = [ctypes.c_int16, ctypes.c_int16, ctypes.c_bool]


def test_a_skill_outside_the_table_is_untrained_and_writes_nothing(lib, session):
    # regression: the Kalan Gauntlet action wrote its experience past the experience table of the champion
    game = ctypes.addressof(ctypes.c_uint8.in_dll(lib, "g_game")) + GAME_CHAMPIONS
    size = C.CHAMPION_MAX * C.CHAMPION_SIZE + TAIL
    before = ctypes.string_at(game, size)
    for champion_index in range(C.CHAMPION_MAX):
        lib.champ_add_experience(champion_index, KALAN_GAUNTLET_SKILL, EXPERIENCE)
        assert lib.champ_get_skill_level(champion_index, KALAN_GAUNTLET_SKILL, True) == C.SKILL_LEVEL_UNTRAINED
    assert ctypes.string_at(game, size) == before
