import ctypes
import random
import struct

import pytest

from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

RAND_NEXT, RAND_BELOW, RAND_BIT, RAND_2BITS = 0x1C6A1, 0x1C6B7, 0x1C6DC, 0x1C6F6
G_RANDOM_SEED = 0x70518
TIMELINE_INIT, TIMELINE_DELETE, TIMELINE_ADD = 0x55D10, 0x562FF, 0x56390
TIMELINE_POP_FIRST, TIMELINE_IS_DUE = 0x5643D, 0x5646F
G_TIMELINE_EVENTS, G_TIMELINE_QUEUE, G_TIMELINE_STATE = 0x760E8, 0x80420, 0x80424  # state: fix_slot .. used_end
G_TIMELINE_CAPACITY, G_NEW_GAME, G_GAME_TIME = 0x80428, 0x7F280, 0x7F22C
EVENT_SIZE = 12
EVENTS, QUEUE, ARG = SCRATCH, SCRATCH + 0x10000, SCRATCH + 0x20000
SEEDS = [0, 1, 0x12345678, 0xFFFFFFFF]


class Timeline(ctypes.Structure):
    _fields_ = [("events", ctypes.c_void_p), ("queue", ctypes.c_void_p), ("fix_slot", ctypes.c_int16),
                ("first_free", ctypes.c_int16), ("capacity", ctypes.c_uint16), ("count", ctypes.c_uint16),
                ("used_end", ctypes.c_int16)]


@pytest.fixture
def emu(game_dir):
    return Emu(load_image(game_dir / "SKULL.EXE"))


@pytest.mark.parametrize("seed", SEEDS)
@pytest.mark.parametrize("name, addr, args, mask", [
    ("rand_next", RAND_NEXT, [], 0xFFFFFFFF),
    ("rand_bit", RAND_BIT, [], 0xFFFF),  # 16-bit results: the high word of EAX is not part of the result
    ("rand_2bits", RAND_2BITS, [], 0xFFFF),
    ("rand_below", RAND_BELOW, [0, 1, 2, 3, 7, 100, 1000, 0x7FFF, 0xFFFF], 0xFFFF),
])
def test_random_matches_original(lib, emu, seed, name, addr, args, mask):
    func = getattr(lib, name)
    func.restype = ctypes.c_uint32
    c_seed = ctypes.c_uint32.in_dll(lib, "random_seed")
    c_seed.value = seed
    emu.write_u32(G_RANDOM_SEED, seed)
    for i in range(1000):
        call_args = [args[i % len(args)]] if args else []
        assert func(*call_args) & mask == emu.call(addr, *call_args) & mask, i
        assert c_seed.value == emu.read_u32(G_RANDOM_SEED)


def c_state(lib, tl):
    events = ctypes.string_at(tl.events, tl.capacity * EVENT_SIZE)
    queue = ctypes.string_at(tl.queue, tl.capacity * 2)
    return events, queue, (tl.fix_slot, tl.first_free, tl.capacity, tl.count, tl.used_end)


def emu_state(emu, capacity):
    return (emu.read(EVENTS, capacity * EVENT_SIZE), emu.read(QUEUE, capacity * 2),
            struct.unpack("<hhHHh", emu.read(G_TIMELINE_STATE, 10)))


@pytest.mark.parametrize("seed", range(4))
def test_timeline_matches_original(lib, emu, seed):
    capacity = 48
    lib.timeline_init(capacity)
    tl = Timeline.in_dll(lib, "timeline")
    emu.write_u32(G_TIMELINE_EVENTS, EVENTS)
    emu.write_u32(G_TIMELINE_QUEUE, QUEUE)
    emu.write(G_TIMELINE_CAPACITY, struct.pack("<H", capacity))
    emu.write(G_NEW_GAME, struct.pack("<H", 1))
    emu.call(TIMELINE_INIT)
    assert c_state(lib, tl) == emu_state(emu, capacity)

    rng = random.Random(seed)
    queued = set()
    popped = ctypes.create_string_buffer(EVENT_SIZE)
    for step in range(3000):
        op = rng.random()
        if op < 0.5 and len(queued) < capacity:
            # Few distinct times, types and priorities so that ties decide the order.
            event = struct.pack("<IBB6s", rng.randrange(8) | rng.randrange(4) << 24, rng.randrange(4),
                                rng.randrange(3), rng.randbytes(6))
            emu.write(ARG, event)
            index = lib.timeline_add_event(event)
            assert index == ctypes.c_int16(emu.call(TIMELINE_ADD, ARG)).value, step
            if index >= 0:
                queued.add(index)
        elif op < 0.75 and queued:
            index = rng.choice(sorted(queued))
            lib.timeline_delete_event(index)
            emu.call(TIMELINE_DELETE, index)
            queued.discard(index)
        elif op < 0.9 and queued:
            game_time = rng.randrange(9)
            emu.write_u32(G_GAME_TIME, game_time)
            assert lib.timeline_is_event_due(game_time) == emu.call(TIMELINE_IS_DUE), step
            lib.timeline_pop_first_event(popped)
            emu.call(TIMELINE_POP_FIRST, ARG)
            assert popped.raw == emu.read(ARG, EVENT_SIZE), step
            queued = {i for i in queued if ctypes.string_at(tl.events + i * EVENT_SIZE + 4, 1) != b"\0"}
        assert c_state(lib, tl) == emu_state(emu, capacity), step
    assert len(queued) == tl.count
