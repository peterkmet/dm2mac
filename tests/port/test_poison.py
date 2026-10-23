"""The cure of poison (src/engine/champion_damage.c): which timeline slots the cure reads."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.input import session  # noqa: F401 (session is a fixture)
from dm2test.lib import load_lib
from dm2test.modes import ADVANCED, VANILLA, set_mode
from dm2test.save import Event, Timeline
from dm2test.scenario import gdat, start  # noqa: F401 (gdat and start are fixtures)

FILLER = 40  # events before the poison event; their deletion leaves the poison event past the event count
POISON = 6
CURE = 50  # more than the poison
DUE_LATER = 0x10000  # game steps from now: the filler events never come due in the test


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.timeline_add_event.argtypes = [ctypes.POINTER(Event)]
    lib.timeline_delete_event.argtypes = [ctypes.c_int]
    return lib


def poisoned_past_the_count(lib, game):
    """Champion 0 gets a poison event in a slot past the event count of the timeline; returns its slot."""
    filler = [lib.timeline_add_event(ctypes.byref(Event(time_map=game.game_time + DUE_LATER, type=C.EVENT_AURA)))
              for _ in range(FILLER)]
    lib.champ_poison(0, POISON + 1)  # the hit takes 1, the event keeps POISON
    for slot in filler:
        lib.timeline_delete_event(slot)
    timeline = Timeline.in_dll(lib, "g_timeline")
    slots = [i for i in range(timeline.capacity) if timeline.events[i].type == C.EVENT_POISON]
    assert len(slots) == 1 and slots[0] >= timeline.count
    return slots[0]


@pytest.mark.parametrize("mode, cured", [(VANILLA, False), (ADVANCED, True)])
def test_the_cure_finds_a_poison_event_past_the_event_count_only_in_the_advanced_mode(lib, session, mode, cured):
    # regression: an antidote did not cure a poison event that lay past the event count (as in the original)
    game, _ = session
    set_mode(lib, mode)
    try:
        slot = poisoned_past_the_count(lib, game)
        lib.champ_cure_poison(0, CURE)
        timeline = Timeline.in_dll(lib, "g_timeline")
        assert (timeline.events[slot].type != C.EVENT_POISON) == cured
    finally:
        set_mode(lib, VANILLA)
