"""The cure of poison (src/engine/champion_damage.c): which timeline slots the cure reads."""

import ctypes

import pytest

from dm2test import constants as C
from dm2test.interface import GAME_CHAMPIONS
from dm2test.modes import ADVANCED, VANILLA, set_mode
from dm2test.structs import Timeline, TimelineEvent

FILLER = 40  # events before the poison event; their deletion leaves the poison event past the event count
POISON = 6
CURE = 50  # more than the poison
DUE_LATER = 0x10000  # game steps from now: the filler events never come due in the test


@pytest.fixture(scope="module", autouse=True)
def lib_prototypes(lib):
    lib.timeline_add_event.argtypes = [ctypes.POINTER(TimelineEvent)]
    lib.timeline_delete_event.argtypes = [ctypes.c_int]


def poisoned_past_the_count(lib, game):
    """Champion 0 gets a poison event in a slot past the event count of the timeline; returns its slot."""
    filler = [lib.timeline_add_event(ctypes.byref(TimelineEvent(time_map=game.game_time + DUE_LATER, type=C.EVENT_AURA)))
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


TAIL = 512  # bytes after the champion records: the rest of g_game and the globals behind it


def bytes_after_the_champions(lib, gdat, start, folder, with_event):
    """Loads start.DAT, saves it with or without a poison event of champion CHAMPION_MAX, loads that save."""
    path = folder / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == C.SAVE_OK
    if with_event:
        event = TimelineEvent(time_map=0, type=C.EVENT_POISON, priority=C.CHAMPION_MAX)
        event.data[0] = POISON
        assert lib.timeline_add_event(ctypes.byref(event)) >= 0
    assert lib.save_write(str(path).encode(), str(path.with_suffix(".BAK")).encode(), b"POISON", gdat) == C.SAVE_OK
    assert lib.save_load(str(path).encode(), gdat) == C.SAVE_OK
    game = ctypes.addressof(ctypes.c_uint8.in_dll(lib, "g_game"))
    address = game + GAME_CHAMPIONS + C.CHAMPION_MAX * C.CHAMPION_SIZE
    return ctypes.string_at(address, TAIL)


def test_load_ignores_a_poison_event_of_a_missing_champion(lib, gdat, start, tmp_path):
    # regression: the load wrote the poison of a champion past the last record
    clean = bytes_after_the_champions(lib, gdat, start, tmp_path, with_event=False)
    poisoned = bytes_after_the_champions(lib, gdat, start, tmp_path, with_event=True)
    assert poisoned == clean
