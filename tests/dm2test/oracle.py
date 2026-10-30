"""The creature part of the emulator oracle: see CreatureOracle."""

import ctypes
import struct

from dm2test import constants as C
from dm2test.structs import Timeline

ACTIVE_GROUP_SIZE = C.ACTIVE_GROUP_SIZE
G_ACTIVE_GROUPS, G_ACTIVE_GROUP_COUNT, G_ACTIVE_GROUP_MAX = 0x7F898, 0x7301C, 0x7F89C
G_RANDOM_SEED = 0x70518
G_TIMELINE_QUEUE, G_TIMELINE_FIX_SLOT = 0x80420, 0x80424
CREATURE_ALLOC_ACTIVE_GROUPS, CREATURE_RESET_ACTIVE_GROUPS = 0x342F9, 0x342A3
CREATURE_ACTIVATE_GROUPS_ALL_MAPS = 0x34236
IMAGE_CACHE_LOOKUP, IMAGE_CACHE_ALLOC, IMAGE_CACHE_REALLOC = 0x3F1F8, 0x3F279, 0x3F2C2
IMAGE_CACHE_GET, IMAGE_CACHE_RELEASE, IMAGE_CACHE_FREE = 0x3F337, 0x3F3C2, 0x3F406
MEM_ALLOC, MEM_FREE_SIZE = 0x3BBBA, 0x3BCA5
G_AI_ACTIVE_GROUP = 0x7F552


class CreatureOracle:
    """The creature parts of the scenario harness (a mixin of dm2test.scenario.Scenario): the active groups in both, the
    image cache keys and the memory blocks of the creature AI."""

    @staticmethod
    def reset_creature_globals(lib):
        """The C library keeps its globals from test to test: back to the values of the program start, before the
        party is placed."""
        for name, value in (("g_light_source_sum", 0), ("g_explosion_light_sum", 0), ("g_cached_light_sum", -1),
                            ("g_cached_explosion_light_sum", -1), ("g_last_party_map", -1)):
            ctypes.c_int16.in_dll(lib, name).value = value
        ctypes.c_uint16.in_dll(lib, "g_search_direction_bits").value = 1
        for name in ("g_party_distance", "g_alt_distance"):
            ctypes.c_int16.in_dll(lib, name).value = -1

    def setup_creatures(self, reset):
        """reset: activate the groups in both, like a session start; else the caller starts the session."""
        emu, lib = self.emu, self.lib
        for addr in (IMAGE_CACHE_RELEASE, IMAGE_CACHE_FREE, MEM_FREE_SIZE):  # replaced below
            emu.uc.hook_del(self.view.no_ops.pop(addr))
        self.ai_cache = {}  # image cache keys of the AI data: key -> index
        self.sizes = {}
        emu.stub(IMAGE_CACHE_LOOKUP, self.cache_lookup)
        emu.stub(IMAGE_CACHE_REALLOC, self.cache_realloc)
        emu.uc.hook_del(self.view.cache_alloc_hook)
        emu.stub(IMAGE_CACHE_ALLOC, self.cache_alloc)
        emu.stub(IMAGE_CACHE_RELEASE, lambda *_: 0)
        emu.stub(IMAGE_CACHE_FREE, self.cache_free)
        self.blocks = []  # the 0x100 and 0x350 blocks of the creature updates: a stack, like the original allocator
        emu.uc.hook_del(self.view.alloc_hook)
        emu.stub(MEM_ALLOC, self.mem_alloc)
        emu.stub(MEM_FREE_SIZE, self.mem_free_size)
        emu.call(CREATURE_ALLOC_ACTIVE_GROUPS)
        if reset:
            emu.call(CREATURE_RESET_ACTIVE_GROUPS)
            lib.creature_reset_active_groups()

    def state(self):
        """The save files of both; the original session_save activates the groups again after the file (like the C port)."""
        result = self.save_states()
        self.emu.call(CREATURE_ACTIVATE_GROUPS_ALL_MAPS)
        return result

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

    def cache_alloc(self, index, size, *_):
        index &= 0xFFFF
        self.sizes[index] = size
        return self.view.cache_put(index, self.view.alloc(size))

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
        n = ctypes.c_int.in_dll(lib, "g_active_group_max").value
        assert n == struct.unpack("<H", emu.read(G_ACTIVE_GROUP_MAX, 2))[0]
        ptr = ctypes.c_void_p.in_dll(self.lib, "g_active_groups").value
        mine = ctypes.string_at(ptr, n * ACTIVE_GROUP_SIZE)
        theirs = emu.read(emu.read_u32(G_ACTIVE_GROUPS), n * ACTIVE_GROUP_SIZE)
        return mine, theirs

    def check_creatures(self, where):
        """The quick check with the creature state: active groups, their count, and the random seed."""
        mine, theirs = self.active_groups()
        for i in range(0, len(mine), ACTIVE_GROUP_SIZE):
            a, b = mine[i:i + ACTIVE_GROUP_SIZE].hex(), theirs[i:i + ACTIVE_GROUP_SIZE].hex()
            assert a == b, (where, i // ACTIVE_GROUP_SIZE, a, b)
        assert ctypes.c_int.in_dll(self.lib, "g_active_group_count").value == \
            struct.unpack("<H", self.emu.read(G_ACTIVE_GROUP_COUNT, 2))[0], where
        assert ctypes.c_uint32.in_dll(self.lib, "g_random_seed").value == self.emu.read_u32(G_RANDOM_SEED), where
        tl = Timeline.in_dll(self.lib, "g_timeline")
        theirs = struct.unpack("<hhHH", self.emu.read(G_TIMELINE_FIX_SLOT, 8))
        assert (tl.fix_slot, tl.first_free, tl.capacity, tl.count) == theirs, (where, "timeline")
        mine_q = ctypes.string_at(tl.queue, tl.count * 2)
        assert mine_q == self.emu.read(self.emu.read_u32(G_TIMELINE_QUEUE), tl.count * 2), (where, "timeline queue")


