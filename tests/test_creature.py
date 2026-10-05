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
CREATURE_ACTIVATE_GROUPS_ALL_MAPS = 0x34236
IMAGE_CACHE_LOOKUP, IMAGE_CACHE_ALLOC, IMAGE_CACHE_REALLOC = 0x3F1F8, 0x3F279, 0x3F2C2
IMAGE_CACHE_GET, IMAGE_CACHE_RELEASE, IMAGE_CACHE_FREE = 0x3F337, 0x3F3C2, 0x3F406
MEM_ALLOC, MEM_FREE_SIZE = 0x3BBBA, 0x3BCA5
# Original functions that Step 16 ports: the scenario runs them (test_move.STUBS replaces them by empty hooks).
PORTED = (0x34106, 0x306A8, 0x3085A, 0x3064D, 0x2538C, 0x24E62, 0x49EF8, 0x30D45, 0x30FE3, 0x4A096, 0x4A005, 0x30BA6,
          0x24328, 0x24171, 0x23E5B, 0x23F9B, 0x4DCEE, 0x56B39, 0x56BA5)
# Original functions of other Step 16 parts that this branch does not have yet, and rules of Step 15: the same
# empty results as the C placeholders (ai_pending.c, hooks.c). (address, stack arguments, result); FAIL also sets
# the command result of the AI context.
FAIL = "fail"
NOT_YET = {
    0x16746: (1, 0),        # explosion_create
    0x16457: (4, 0),        # projectile_create
    0x18758: (0, 0),        # creature_attack_champion
    0x46786: (0, 0), 0x46968: (0, 0), 0x1F8A7: (0, 0),                      # Step 15: luck, dexterity, value
    0x28574: (0, 0xFD), 0x28711: (0, 0xFD), 0x28E99: (0, 0xFD), 0x296D5: (0, 0xFD),  # Step 15: merchants
}
CHAMP_REMOVE_OBJECT_FROM_SLOT, G_CHAMPIONS, CHAMPION_SIZE_BYTES, CHAMPION_SLOTS = 0x45F27, 0x7FBD0, 0x107, 0xC3
G_AI_COMMAND_RESULT = 0x7F56B
G_AI_ACTIVE_GROUP = 0x7F552


class CreatureScenario(Scenario):
    """start.DAT after the start of a game session: the creature groups are active in both."""

    def __init__(self, lib, gdat, start, tmp_path):
        super().__init__(lib, gdat, start, tmp_path)
        emu = self.emu
        for addr in PORTED:
            emu.uc.hook_del(self.stubs.pop(addr))
        for addr, (stack, result) in NOT_YET.items():
            if result == FAIL:
                emu.stub(addr, lambda *_: emu.write(G_AI_COMMAND_RESULT, b"\xfd"), stack_args=stack)
            else:
                emu.stub(addr, lambda *_, r=result: r, stack_args=stack)
        emu.stub(CHAMP_REMOVE_OBJECT_FROM_SLOT, self.remove_object_from_slot)
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
        emu.call(CREATURE_RESET_ACTIVE_GROUPS)
        lib.creature_reset_active_groups()

    def remove_object_from_slot(self, champ, slot, *_):
        at = G_CHAMPIONS + (champ & 0xFFFF) * CHAMPION_SIZE_BYTES + CHAMPION_SLOTS + 2 * (slot & 0xFFFF)
        thing = struct.unpack("<H", self.emu.read(at, 2))[0]
        self.emu.write(at, b"\xff\xff")
        return thing

    def state(self):
        """The save files of both; the original game_save activates the groups again after the file (like the C port)."""
        result = super().state()
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
            a, b = mine[i:i + ACTIVE_GROUP_SIZE].hex(), theirs[i:i + ACTIVE_GROUP_SIZE].hex()
            assert a == b, (where, i // ACTIVE_GROUP_SIZE, a, b)
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


def test_creature_updates(creatures):
    # The update events of all creatures of the dungeon: moods, plans, actions, animation steps, and the timing of the
    # next update; every 50 steps the save files (the save deactivates and activates the groups again).
    for n in range(300):
        creatures.step()
        creatures.assert_creatures(n)
        if n % 50 == 49:
            creatures.assert_equal(n)


def creature_sensor_cells(lib, kinds):
    """(map, x, y, cell) of each sensor of the kinds."""
    from test_move import ENDOFLIST
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    out = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                t = lib.dungeon_square_first_thing(map_, x, y)
                while t != ENDOFLIST:
                    if t >> 10 & 15 == 3 and lib.thing_word(t, 2) & 0x7F in kinds:
                        out.append((map_, x, y, t >> 14))
                    t = lib.dungeon_next_thing(t)
    return sorted(set(out))


def test_creature_sensors(creatures, lib):
    # Square events on the sensors that turn creatures (0x42), alert them or make them vanish (0x28, 0x0b), and
    # animate static creatures (0x3a); then the creature updates that follow.
    import random
    from test_move import square_event
    rng = random.Random(16)
    cells = creature_sensor_cells(lib, (0x0B, 0x28, 0x42, 0x3A))
    assert len(cells) > 5
    for n, (map_, x, y, cell) in enumerate(cells):
        for effect in (0, 1, 2):
            square_event(creatures, map_, x, y, cell, effect, rng.randrange(2))
            for _ in range(rng.randrange(1, 4)):
                creatures.step()
            creatures.assert_creatures((n, map_, x, y, cell, effect))
    creatures.assert_equal("end")


MOVE_PUSH_CREATURE = 0x23F9B


def test_push_creatures(creatures, lib):
    # The party stands in front of creature groups of all maps and pushes them with each push arrow.
    import random
    rng = random.Random(17)
    lib.creature_group_at.restype = ctypes.c_uint16
    fronts = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                if lib.creature_group_at(map_, x, y) == 0xFFFF:
                    continue
                for d, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
                    px, py = x - dx, y - dy
                    if 0 <= px < lib.dungeon_map_width(map_) and 0 <= py < lib.dungeon_map_height(map_) \
                            and lib.dungeon_square(map_, px, py) >> 5 == 1 and lib.creature_group_at(map_, px, py) == 0xFFFF:
                        fronts.append((map_, px, py, d))
                        break
    assert len(fronts) > 20
    for n, (map_, x, y, d) in enumerate(rng.sample(fronts, min(80, len(fronts)))):
        creatures.place(map_, x, y, d)
        arrow = rng.randrange(6)
        lib.move_push_creature(arrow)
        creatures.emu.call(MOVE_PUSH_CREATURE, arrow)
        creatures.assert_creatures((n, map_, x, y, d, arrow, "push"))
        for _ in range(rng.randrange(1, 5)):
            creatures.step()
        creatures.assert_creatures((n, map_, x, y, d, arrow))
    creatures.assert_equal("end")
