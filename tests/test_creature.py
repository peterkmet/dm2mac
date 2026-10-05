"""Scenario tests of Step 16 with the emulator oracle: creature groups, the creature AI, projectiles, explosions,
magic, and minions run in the C port and in the original code from start.DAT; then the game state must be equal."""

import ctypes
import struct

import pytest

from test_move import Scenario, gdat, start  # noqa: F401 (gdat and start are fixtures)

ACTIVE_GROUP_SIZE = 0x22
G_ACTIVE_GROUPS, G_ACTIVE_GROUP_COUNT, G_ACTIVE_GROUP_MAX = 0x7F898, 0x7301C, 0x7F89C
G_RANDOM_SEED = 0x70518
CREATURE_ALLOC_ACTIVE_GROUPS, CREATURE_RESET_ACTIVE_GROUPS = 0x342F9, 0x342A3
IMAGE_CACHE_LOOKUP, IMAGE_CACHE_ALLOC, IMAGE_CACHE_REALLOC = 0x3F1F8, 0x3F279, 0x3F2C2
IMAGE_CACHE_GET, IMAGE_CACHE_RELEASE, IMAGE_CACHE_FREE = 0x3F337, 0x3F3C2, 0x3F406
MEM_ALLOC, MEM_FREE_SIZE = 0x3BBBA, 0x3BCA5
# Original functions that Step 16 ports: the scenario runs them (test_move.STUBS replaces them by empty hooks).
PORTED = (0x34106, 0x306A8, 0x3085A, 0x3064D, 0x2538C, 0x24E62, 0x49EF8, 0x30D45, 0x30FE3, 0x4A096, 0x4A005, 0x30BA6)
# Original functions of Step 16 that the C port does not have yet: (address, stack arguments, result).
NOT_YET = {
    0x262F7: None,          # creature_ai_decide: action 0
    0x25FB2: (0, 0),        # creature_ai_plan_flags
    0x2B75E: (0, 0),        # creature_ai_execute_action
    0x30102: (0, 0),        # creature_group_in_state5
    0x16746: (1, 0),        # explosion_create
}
G_AI_ACTIVE_GROUP = 0x7F552


class CreatureScenario(Scenario):
    """start.DAT after the start of a game session: the creature groups are active in both."""

    def __init__(self, lib, gdat, start, tmp_path):
        super().__init__(lib, gdat, start, tmp_path)
        emu = self.emu
        for addr in PORTED:
            emu.uc.hook_del(self.stubs.pop(addr))
        for addr, stub in NOT_YET.items():
            if stub is None:
                emu.stub(addr, lambda *_: emu.write(emu.read_u32(G_AI_ACTIVE_GROUP) + 0x1A, b"\0"))
            else:
                emu.stub(addr, lambda *_, r=stub[1]: r, stack_args=stub[0])
        self.ai_cache = {}  # image cache keys of the AI data: key -> index
        self.sizes = {}
        emu.stub(IMAGE_CACHE_LOOKUP, self.cache_lookup)
        emu.stub(IMAGE_CACHE_REALLOC, self.cache_realloc)
        emu.stub(IMAGE_CACHE_RELEASE, lambda *_: 0)
        emu.stub(IMAGE_CACHE_FREE, self.cache_free)
        self.blocks = []  # the 0x100 and 0x350 blocks of the creature updates: a stack, like the original allocator
        emu.uc.hook_del(self.view.alloc_hook)
        emu.stub(MEM_ALLOC, self.mem_alloc)
        emu.stub(MEM_FREE_SIZE, self.mem_free_size)
        emu.call(CREATURE_ALLOC_ACTIVE_GROUPS)
        emu.call(CREATURE_RESET_ACTIVE_GROUPS)
        lib.creature_reset_active_groups()

    def mem_alloc(self, size, *_):
        if size in (0x100, 0x350):
            addr = self.block_base + sum(b for b in self.blocks)
            self.blocks.append(size + 15 & ~15)
            return addr
        return self.view.alloc(size)

    @property
    def block_base(self):
        if not hasattr(self, "_block_base"):
            self._block_base = self.view.alloc(0x4000)
        return self._block_base

    def mem_free_size(self, size, *_):
        if self.blocks and self.blocks[-1] == size + 15 & ~15:
            self.blocks.pop()

    def cache_lookup(self, key, index, create, *_):
        key &= 0xFFFFFFFF
        if key in self.ai_cache:
            i = self.ai_cache[key]
            self.emu.write(index, struct.pack("<H", i))
            return self.sizes.get(i, 0)
        if not create & 0xFFFF:
            return 0
        i = self.view.new_index()
        self.ai_cache[key] = i
        self.emu.write(index, struct.pack("<H", i))
        return 0

    def cache_realloc(self, index, size, *_):
        index &= 0xFFFF
        old, old_size = self.view.cache[index], self.sizes.get(index, 0)
        addr = self.view.alloc(size)
        self.emu.write(addr, self.emu.read(old, old_size))
        self.view.cache[index] = addr
        self.sizes[index] = size
        return addr

    def cache_free(self, index, *_):
        index &= 0xFFFF
        for key, i in list(self.ai_cache.items()):
            if i == index:
                del self.ai_cache[key]

    def active_groups(self):
        """(C, original) bytes of the active group tables."""
        lib, emu = self.lib, self.emu
        n = ctypes.c_int.in_dll(lib, "active_group_max").value
        assert n == struct.unpack("<H", emu.read(G_ACTIVE_GROUP_MAX, 2))[0]
        ptr = ctypes.c_void_p.in_dll(self.lib, "active_groups").value
        mine = ctypes.string_at(ptr, n * ACTIVE_GROUP_SIZE)
        theirs = emu.read(emu.read_u32(G_ACTIVE_GROUPS), n * ACTIVE_GROUP_SIZE)
        return mine, theirs

    def assert_creatures(self, where):
        """The quick check with the creature state: active groups, their count, and the random seed."""
        mine, theirs = self.active_groups()
        for i in range(0, len(mine), ACTIVE_GROUP_SIZE):
            assert mine[i:i + ACTIVE_GROUP_SIZE].hex() == theirs[i:i + ACTIVE_GROUP_SIZE].hex(), (where, i // ACTIVE_GROUP_SIZE)
        assert ctypes.c_int.in_dll(self.lib, "active_group_count").value == \
            struct.unpack("<H", self.emu.read(G_ACTIVE_GROUP_COUNT, 2))[0], where
        assert ctypes.c_uint32.in_dll(self.lib, "random_seed").value == self.emu.read_u32(G_RANDOM_SEED), where
        self.assert_same(where)


@pytest.fixture
def creatures(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return CreatureScenario(lib, gdat, start, tmp_path)


def test_activation_after_load(creatures):
    creatures.assert_creatures("activation")


def test_creature_updates_without_ai(creatures):
    # The update events of all creatures of the dungeon: animation steps and the timing of the next update.
    for n in range(200):
        creatures.step()
        creatures.assert_creatures(n)
