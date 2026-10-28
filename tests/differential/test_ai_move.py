"""Differential tests of the creature perception, the AI command tests, the item filters, and the path search (Step 16,
ai_move.c) against the original code: start.DAT in both, the same AI context, the same calls, then the results, the
AI context, the dungeon, and the random seed must be equal."""

import ctypes
import random
import struct

import pytest

from dm2test import constants as C
from dm2test.oracle import G_RANDOM_SEED
from dm2test.creature import creatures  # noqa: F401 (fixture)
from dm2test.scenario import gdat, start  # noqa: F401 (fixtures)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

AI_BEGIN, AI_PREPARE, AI_END = 0x24A88, 0x24BFC, 0x24D88
DUN_SET_CURRENT_MAP = 0x1C724
G_AI = 0x7F548
CELL_MASK = 0x15286
G_AI_SQUARE, G_AI_PARTY = 0x7F8A4, 0x7F8D0
G_DIRECTION_BITS = 0x752E8
G_MEM_FREE_BYTES = 0x7FA7C
G_MAP_LIGHT, G_MAP_WALL_COST = 0x7F3F4, 0x7F3FC
G_PARTY_DISTANCE, G_ALT_DISTANCE = 0x7F8C8, 0x7F8C4
G_PARTY_DISTANCE_MAP, G_ALT_DISTANCE_MAP = 0x752A0, 0x752A2
G_PARTY_DISTANCE_WIDTH, G_ALT_DISTANCE_WIDTH = 0x7F8CE, 0x7F8CC
G_LIGHT_SUMS = (0x7F970, 0x7F974, 0x752A4, 0x752A6, 0x752A8)
G_VIEW_REDRAW = 0x7F38C
LIGHT_NAMES = ("g_light_source_sum", "g_explosion_light_sum", "g_cached_light_sum", "g_cached_explosion_light_sum", "g_last_party_map")
DISTANCE_MAPS = (("g_party_distance", 0x7F8C8, 0x752A0, 0x7F8CE), ("g_alt_distance", 0x7F8C4, 0x752A2, 0x7F8CC))
G_AI_SCRATCH, G_AI_CONTEXT_ACTIVE = 0x7F894, 0x7F7E6
# Functions of later parts that both sides stub: (address, stack arguments).

# Original functions of ai_move.c.
F = {
    "ai_square_is_obstacle": 0x2B96A, "ai_square_blocks_line": 0x2B9FC,
    "ai_blocks_line_to_target": 0x2BB78, "ai_trace_line": 0x2BBAD,
    "ai_cache_square": 0x2BEAF, "ai_cached_first_object": 0x2BF41,
    "ai_cached_first_group": 0x2BFAE, "ai_turn_toward": 0x2C005,
    "ai_find_target_in_lines": 0x2C0A2, "ai_try_attack": 0x2C404,
    "ai_try_door_action": 0x2CC42, "ai_projectile_incoming": 0x2D52D,
    "ai_try_move": 0x2D792, "ai_find_matching_item_inside": 0x2E8E1,
    "ai_try_take_item": 0x2EA68, "ai_find_wall_actuator": 0x2F0FA,
    "ai_wall_actuator_accepts": 0x2F1C4, "ai_try_use_wall_actuator": 0x2F2CF,
    "ai_thing_matches_filter": 0x2F636, "ai_find_matching_item": 0x2FF1E,
    "ai_remove_matching_items": 0x2FF89, "ai_count_matching_items": 0x2697B,
    "ai_find_marker": 0x3161E, "ai_find_marker_by_id": 0x31724,
    "ai_blocks_line_except_self": 0x317EE, "ai_search_area": 0x3188A,
    "ai_load_cached_path": 0x33DF9, "ai_next_path_step": 0x33E61,
    "ai_plan_path": 0x33EF3, "ai_refresh_party_maps": 0x2F8F5,
}
# The fields of the AI context: (C offset, original offset, size).
AI_FIELDS = {
    "command_result": (53, 0x23, 1), "map": (55, 0x25, 1), "flags": (60, 0x2A, 12), "marker_mask": (72, 0x36, 4),
    "path": (216, 0xC2, 0x64), "candidate_count": (316, 0x128, 2), "candidates": (318, 0x12C, 16 * 22),
    "cache": (670, 0x28C, 14), "filter_free": (1040, 0x34A, 2),
}
AI_SIZE = 1064
AI_ACTIVE_FLAG_C = 696  # offset of active_flag in the C AI context
AI_ARGS_C = 56  # offset of arg1 and arg2 in the C AI context
ENDOFLIST, NONE = C.THING_ENDOFLIST, C.THING_NONE
THING_SIZES = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)


def s16(v):
    return ctypes.c_int16(v & 0xFFFF).value


class Location(ctypes.Structure):
    """A square of the current map as the port passes it."""
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16)]


class AiCommand(ctypes.Structure):
    """The outcome of a command test of the port: the test passed, and the command result after it."""
    _fields_ = [("passed", ctypes.c_bool), ("result", ctypes.c_uint8)]


class AiLineTarget(ctypes.Structure):
    """The result of the line search of the port."""
    _fields_ = [("found", ctypes.c_bool), ("location", ctypes.c_uint16)]


class AiTest(ctypes.Structure):
    """A command test of the port: the mode and whether the test sets the action."""
    _fields_ = [("mode", ctypes.c_uint8), ("execute", ctypes.c_bool)]


class AiMoveTest(ctypes.Structure):
    """A move test of the port: the kind and the flags."""
    _fields_ = [("kind", ctypes.c_uint8), ("execute", ctypes.c_bool), ("no_movers", ctypes.c_bool),
                ("attack_groups", ctypes.c_bool)]


# The command tests of the port take each square as one Location: the index of its x in the arguments of the original.
SQUARE_ARGS = {name: (1, 3) for name in ("ai_try_attack", "ai_try_door_action", "ai_try_move", "ai_try_take_item",
                                         "ai_try_use_wall_actuator")}


def command_test(name, mode):
    """The test struct of the port from the packed mode byte of the original."""
    execute = bool(mode & C.AI_EXECUTE)
    if name == "ai_try_move":
        return AiMoveTest(mode & C.AI_MOVE_KIND_MASK, execute, bool(mode & C.AI_MOVE_NO_MOVERS),
                          bool(mode & C.AI_MOVE_ATTACK_GROUPS))
    return AiTest(mode & 0xFF, execute)


def c_args(name, args):
    """The arguments of the C function from the arguments of the original: each (x, y) pair becomes a Location, the
    packed mode byte of a command test its test struct."""
    squares, out, i = SQUARE_ARGS.get(name, ()), [], 0
    if squares:
        out.append(command_test(name, args[0]))
        i = 1
    while i < len(args):
        if i in squares:
            out.append(Location(args[i], args[i + 1]))
            i += 2
        else:
            out.append(args[i])
            i += 1
    return out


class Ai:
    """The C port and the original with the same creature AI context."""

    def __init__(self, sc):
        self.sc, self.lib, self.emu = sc, sc.lib, sc.emu
        lib, emu = self.lib, self.emu
        # CreatureScenario stubs these again over the empty stubs of OriginalView: two hooks return twice
        for addr in (0x3BCA5, 0x3F406, 0x3F3C2):
            if addr in sc.view.no_ops:
                emu.uc.hook_del(sc.view.no_ops.pop(addr))
        if F["ai_refresh_party_maps"] in sc.stubs:  # dm2test.scenario.STUBS: the hook of Step 14
            emu.uc.hook_del(sc.stubs.pop(F["ai_refresh_party_maps"]))
        emu.write_u32(G_MEM_FREE_BYTES, 0x7FFFFFFF)
        self.fix_cell_mask()
        self.sync_map_values()
        self.ai = ctypes.addressof(ctypes.c_uint8.in_dll(lib, "g_ai"))
        self.square = ctypes.addressof(ctypes.c_uint8.in_dll(lib, "g_ai_square"))
        self.scan = ctypes.addressof(ctypes.c_uint8.in_dll(lib, "g_ai_square_scan"))
        self.saved = ctypes.create_string_buffer(AI_SIZE)
        self.scratch = sc.view.alloc(0x2A * 0x40)
        lib.ai_begin.argtypes = [ctypes.c_uint16, ctypes.c_int16, ctypes.c_int16, ctypes.c_int, ctypes.c_void_p]
        lib.ai_end.argtypes = [ctypes.c_int, ctypes.c_void_p]
        lib.ai_scratch_push.restype = ctypes.c_void_p
        lib.dun_set_current_map.argtypes = [ctypes.c_int16]
        for name in ("ai_trace_line",):
            getattr(lib, name).argtypes = [ctypes.c_int16] * 4 + [ctypes.c_void_p]
        lib.ai_find_target_in_lines.argtypes = [ctypes.c_int, ctypes.c_void_p, Location, ctypes.c_int16, ctypes.c_bool]
        lib.ai_find_target_in_lines.restype = AiLineTarget
        lib.ai_search_area.argtypes = [Location] + [ctypes.c_int] * 2 + [ctypes.c_void_p] * 2
        for name in SQUARE_ARGS:
            test = AiMoveTest if name == "ai_try_move" else AiTest
            getattr(lib, name).argtypes = [test, Location, Location, ctypes.c_int16] + [ctypes.c_int] * (
                1 if name in ("ai_try_take_item", "ai_try_use_wall_actuator") else 0)
            getattr(lib, name).restype = AiCommand
        lib.ai_turn_toward.restype = AiCommand
        lib.ai_thing_matches_filter.restype = ctypes.c_bool
        lib.ai_remove_matching_items.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int16]
        self.begun = None
        emu.write(G_AI_CONTEXT_ACTIVE, b"\0")
        ctypes.c_uint8.from_address(self.ai + AI_ACTIVE_FLAG_C).value = 0
        # the C library lives through the tests: its AI state starts from the state of the original
        for c_off, o_off, size in AI_FIELDS.values():
            ctypes.memmove(self.ai + c_off, emu.read(G_AI + o_off, size), size)
        ctypes.c_uint16.in_dll(lib, "g_search_direction_bits").value = struct.unpack("<H", emu.read(G_DIRECTION_BITS, 2))[0]
        theirs = emu.read(G_AI_SQUARE, 0x1E)
        ctypes.memmove(self.square, theirs[:7] + b"\0" + theirs[8:14] + theirs[0x1C:0x1E], 16)
        ctypes.memmove(self.scan, theirs[14:21] + b"\0" + theirs[22:28], 14)
        for addr, name in zip(G_LIGHT_SUMS, LIGHT_NAMES):
            ctypes.c_int16.in_dll(lib, name).value = s16(struct.unpack("<H", emu.read(addr, 2))[0])
        for name, ptr, map_addr, width_addr in DISTANCE_MAPS:
            o_map, o_width = (s16(struct.unpack("<H", emu.read(a, 2))[0]) for a in (map_addr, width_addr))
            cells = emu.read(emu.read_u32(ptr), 32 * 32) if emu.read_u32(ptr) else bytes(32 * 32)
            ctypes.memmove(ctypes.addressof(ctypes.c_uint8.in_dll(lib, name)), struct.pack("<hh", o_map, o_width) + cells, 4 + 32 * 32)

    def fix_cell_mask(self):
        """The fix of the port: the cells of the possessions seen from side -1 (outside the table) are all cells."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP
        emu = self.emu

        def hook(uc, *_):
            possessions, side = uc.reg_read(UC_X86_REG_EAX) & 0xFFFF, uc.reg_read(UC_X86_REG_EDX) & 0xFFFF
            if side > 3 and possessions & 0x400 and not possessions & 0x200:
                sp = uc.reg_read(UC_X86_REG_ESP)
                uc.reg_write(UC_X86_REG_EAX, uc.reg_read(UC_X86_REG_EAX) & ~0xFF | C.CELLS_ALL)
                uc.reg_write(UC_X86_REG_EIP, emu.read_u32(sp))
                uc.reg_write(UC_X86_REG_ESP, sp + 4)

        emu.uc.hook_add(UC_HOOK_CODE, hook, begin=CELL_MASK, end=CELL_MASK)

    def sync_map_values(self):
        """The values of the party map graphics that the original reads at the map load: light radius, wall cost."""
        lib = self.lib
        lib.gdat_entry_index.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
        lib.gdat_entry_index.restype = ctypes.c_uint16
        gfx_set = lib.dungeon_map_graphics(self.sc.game.party_map) >> C.MAP_WALLSET_SHIFT & C.MAP_NIBBLE_MASK
        light = lib.gdat_entry_index(self.sc.gdat, C.CAT_WALLSET, gfx_set, C.GDAT_TYPE_VALUE, 0x6D)
        cost = lib.gdat_entry_index(self.sc.gdat, C.CAT_WALLSET, gfx_set, C.GDAT_TYPE_VALUE, 0x72) or 1
        self.emu.write(G_MAP_LIGHT, struct.pack("<H", light))
        self.emu.write(G_MAP_WALL_COST, struct.pack("<H", cost))

    def groups(self):
        """(group thing, x, y, map) of each active group."""
        lib = self.lib
        n = ctypes.c_int.in_dll(lib, "g_active_group_max").value
        ptr = ctypes.c_void_p.in_dll(lib, "g_active_groups").value
        raw = ctypes.string_at(ptr, n * C.ACTIVE_GROUP_SIZE)
        out = []
        for i in range(n):
            index, = struct.unpack_from("<H", raw, i * C.ACTIVE_GROUP_SIZE)
            if index == C.ACTIVE_FREE:
                continue
            pos, = struct.unpack_from("<H", raw, i * C.ACTIVE_GROUP_SIZE + 0xC)
            out.append((C.THING_GROUP << C.THING_TYPE_SHIFT | index, pos & C.LOCATION_COORD_MASK, pos >> C.LOCATION_Y_SHIFT & C.LOCATION_COORD_MASK, pos >> C.LOCATION_MAP_SHIFT))
        return out

    def set_map(self, map_):
        self.lib.dun_set_current_map(map_)
        self.emu.call(DUN_SET_CURRENT_MAP, map_)

    def begin(self, group, x, y, map_):
        """The AI context of group at (x, y) of map_ in both, prepared, with a scratch block for the item filters."""
        lib, emu = self.lib, self.emu
        self.set_map(map_)
        lib.ai_begin(group, x, y, 0x22, self.saved)
        lib.ai_prepare()
        lib.ai_scratch_push()
        self.begun = emu.call(AI_BEGIN, group, x, y, 0x22)
        emu.call(AI_PREPARE)
        emu.write_u32(G_AI_SCRATCH, self.scratch)
        self.check("begin")

    def end(self):
        self.check("end", dungeon=True)
        self.lib.ai_scratch_pop()
        self.lib.ai_end(0, None)
        self.emu.call(AI_END, self.begun)

    def call(self, name, *args, emu_args=None):
        """Calls the C function and the original with the same arguments; returns both results."""
        mine = getattr(self.lib, name)(*c_args(name, args))
        theirs = self.emu.call(F[name], *(args if emu_args is None else emu_args))
        if isinstance(mine, AiCommand):  # the command tests return the command result that the AI context holds
            assert mine.result == ctypes.string_at(self.ai + AI_FIELDS["command_result"][0], 1)[0], name
            mine = mine.passed
        return mine, theirs

    def ai_field(self, name):
        c_off, o_off, size = AI_FIELDS[name]
        return ctypes.string_at(self.ai + c_off, size).hex(), self.emu.read(G_AI + o_off, size).hex()

    def check(self, where, fields=AI_FIELDS, dungeon=False):
        """The AI context, the square cache, the party view, the random seeds, the active groups, the dungeon."""
        for name in fields:
            mine, theirs = self.ai_field(name)
            assert mine == theirs, (where, name)
        sq, theirs = ctypes.string_at(self.square, 16), self.emu.read(G_AI_SQUARE, 0x1E)
        assert sq[:6] + sq[6:7] + sq[8:14] == theirs[:7] + theirs[8:14], (where, "square")
        assert sq[14:16] == theirs[0x1C:0x1E], (where, "cost")
        scan = ctypes.string_at(self.scan, 14)
        assert scan[:7] + scan[8:] == theirs[14:21] + theirs[22:28], (where, "scan")
        party = ctypes.string_at(ctypes.addressof(ctypes.c_uint8.in_dll(self.lib, "g_ai_party")), 8)
        mine_party = struct.unpack("<4h", party)
        x, y, m, d = (s16(struct.unpack("<H", self.emu.read(a, 2))[0]) for a in (0x7F8D4, 0x7F8D0, 0x7F8D2, 0x7F8D6))
        assert mine_party == (x, y, m, d), (where, "party")
        assert ctypes.c_uint16.in_dll(self.lib, "g_search_direction_bits").value == \
            struct.unpack("<H", self.emu.read(G_DIRECTION_BITS, 2))[0], (where, "direction bits")
        assert ctypes.c_int16.in_dll(self.lib, "g_party").value == s16(struct.unpack("<H", self.emu.read(0x72244, 2))[0]), \
            (where, "current map")
        mine, theirs = self.sc.active_groups()
        for i in range(0, len(mine), C.ACTIVE_GROUP_SIZE):
            assert mine[i:i + C.ACTIVE_GROUP_SIZE].hex(" ") == theirs[i:i + C.ACTIVE_GROUP_SIZE].hex(" "), (where, "active group", i // C.ACTIVE_GROUP_SIZE)
        assert ctypes.c_uint32.in_dll(self.lib, "g_random_seed").value == self.emu.read_u32(G_RANDOM_SEED), (where, "seed")
        if dungeon:
            assert self.sc.differences() == [], where


@pytest.fixture
def ai(creatures):
    return Ai(creatures)


def test_context_after_begin(ai):
    # The AI context of every active group: begin and prepare give the same fields in both.
    groups = ai.groups()
    assert groups
    for g, x, y, m in groups:
        ai.begin(g, x, y, m)
        ai.end()


def squares_near(lib, map_, x, y, radius):
    w, h = lib.dungeon_map_width(map_), lib.dungeon_map_height(map_)
    return [(sx, sy) for sx in range(x - radius, x + radius + 1) for sy in range(y - radius, y + radius + 1)
            if -1 <= sx <= w and -1 <= sy <= h]


LINE_TESTS = ("ai_square_is_obstacle", "ai_square_blocks_line", "ai_blocks_line_to_target",
              "ai_blocks_line_except_self")


def test_square_and_line_tests(ai):
    # The square tests around every active group, and the lines from the group to the squares around it with each
    # square test (the random bit of the doors that the creatures sometimes see through included).
    lib = ai.lib
    rng = random.Random(16)
    tested = blocked = 0
    for g, x, y, m in ai.groups():
        ai.begin(g, x, y, m)
        for sx, sy in squares_near(lib, m, x, y, 3):
            for name in LINE_TESTS:
                mine, theirs = ai.call(name, sx, sy)
                assert mine == s16(theirs), (g, name, sx, sy)
                blocked += mine != 0
            ai.check((g, sx, sy))
        for _ in range(40):
            tx, ty = x + rng.randrange(-6, 7), y + rng.randrange(-6, 7)
            name = rng.choice(LINE_TESTS)
            fn = ctypes.cast(getattr(lib, name), ctypes.c_void_p)
            mine = lib.ai_trace_line(x, y, tx, ty, fn)
            theirs = ai.emu.call(F["ai_trace_line"], x, y, tx, ty, F[name])
            assert mine == s16(theirs), (g, name, tx, ty)
            tested += mine > 1
            ai.check((g, "line", tx, ty))
        ai.end()
    assert blocked > 50 and tested > 50


def test_square_cache(ai):
    # The square cache of the AI: the first object and the first group of squares around the groups.
    lib = ai.lib
    for f in ("ai_cached_first_object", "ai_cached_first_group"):
        getattr(lib, f).restype = ctypes.c_uint16
    for g, x, y, m in ai.groups():
        ai.begin(g, x, y, m)
        for sx, sy in squares_near(lib, m, x, y, 2):
            ai.call("ai_cache_square", sx, sy)
            for f in ("ai_cached_first_group", "ai_cached_first_object"):
                mine, theirs = ai.call(f)
                assert mine == theirs & 0xFFFF, (g, f, sx, sy)
            ai.check((g, sx, sy))
        ai.end()


def random_command(rng, test):
    kind = rng.choice((0, 0, 1, 3, 4, 5, 6, 7, 8, rng.randrange(32)))
    return kind | rng.choice((0, C.AI_EXECUTE)) | rng.choice((0, 0, C.AI_MOVE_NO_MOVERS)) | rng.choice((0, 0, C.AI_MOVE_ATTACK_GROUPS))


def places(ai, rng, per_group=3):
    """(group, x, y, map): every active group on its square, and on random open squares of its map."""
    lib = ai.lib
    out = []
    for g, x, y, m in ai.groups():
        out.append((g, x, y, m))
        w, h = lib.dungeon_map_width(m), lib.dungeon_map_height(m)
        open_squares = [(sx, sy) for sx in range(w) for sy in range(h) if lib.dungeon_square(m, sx, sy) >> C.SQUARE_TYPE_SHIFT in (C.SQUARE_FLOOR, C.SQUARE_PIT, C.SQUARE_STAIRS, C.SQUARE_DOOR, C.SQUARE_TELEPORTER)]
        out += [(g, sx, sy, m) for sx, sy in rng.sample(open_squares, min(per_group, len(open_squares)))]
    return out


def test_try_move(ai):
    # Moves of every group to the squares around it: all move kinds with the execute, mover, and attack bits, the four
    # directions, the level changes up and down, and the moves that keep the facing.
    rng = random.Random(17)
    moved = acted = 0
    for g, x, y, m in places(ai, rng):
        ai.begin(g, x, y, m)
        for n in range(60):
            command = random_command(rng, n)
            d = rng.choice((-1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 6))
            if d in (4, 5) or rng.random() < 0.4:
                tx = ty = -1
                if d == -1 or d == 6:
                    d = rng.randrange(4)
            else:
                tx, ty = x + rng.randrange(-2, 3), y + rng.randrange(-2, 3)
            mine, theirs = ai.call("ai_try_move", command, x, y, tx, ty, d)
            assert mine == theirs, (g, n, command, tx, ty, d)
            moved += mine
            acted += mine and command & C.AI_EXECUTE
            ai.check((g, n, command, tx, ty, d))
        ai.end()
    assert moved > 100 and acted > 30


AI_MOVE_FLAGS_C, AI_MOVE_FLAGS_ORIGINAL = AI_FIELDS["flags"][0], 0x2A  # offset of the move flags in the AI context
MOVE_OPEN_PIT, MOVE_FALL, MOVE_STAIRS, MOVE_LEVELS = C.MOVE_OPEN_PIT, C.MOVE_FALL, C.MOVE_STAIRS, C.MOVE_LEVELS
G_THINGS, GROUP_SIZE, GROUP_CREATURE = 0x7F288, 16, C.GROUP_CREATURE


def set_creature_type(ai, group, kind):
    """Sets the creature type of group in both."""
    lib = ai.lib
    lib.dungeon_thing_record.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.dungeon_thing_record(group)[GROUP_CREATURE] = kind
    ai.emu.write(ai.emu.read_u32(G_THINGS + 4 * 4) + (group & C.THING_INDEX_MASK) * GROUP_SIZE + GROUP_CREATURE, bytes([kind]))


def native_type(lib, m, x, y, delta):
    """A creature type of the map above (delta -1) or below (1) the square, or None."""
    nx, ny = ctypes.c_int16(x), ctypes.c_int16(y)
    lib.dungeon_location_on_map.argtypes = [ctypes.c_int16, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
    target = lib.dungeon_location_on_map(m, delta, ctypes.byref(nx), ctypes.byref(ny))
    if target < 0:
        return None
    count = lib.dungeon_map_object_counts(target) >> C.MAP_CREATURE_TYPES_SHIFT & C.MAP_NIBBLE_MASK
    lib.dungeon_map_creature_types.restype = ctypes.POINTER(ctypes.c_uint8)
    return lib.dungeon_map_creature_types(target)[0] if count else None


def test_try_move_changes_level(ai):
    # Moves up and down from the stairs and open pits of each map with a group: the group gets a creature type of
    # the other map and the move flags of level changes (no creature of start.DAT has them all). The level change,
    # the move on the other map, and the result there.
    rng = random.Random(19)
    lib = ai.lib
    lib.dungeon_thing_record.restype = ctypes.POINTER(ctypes.c_uint8)
    tried = changed = 0
    seen = set()
    for g, x, y, m in ai.groups():
        if m in seen:
            continue
        seen.add(m)
        original_type = lib.dungeon_thing_record(g)[GROUP_CREATURE]
        w, h = lib.dungeon_map_width(m), lib.dungeon_map_height(m)
        ways = [(sx, sy) for sx in range(w) for sy in range(h) if lib.dungeon_square(m, sx, sy) >> C.SQUARE_TYPE_SHIFT in (C.SQUARE_PIT, C.SQUARE_STAIRS)]
        for sx, sy in ways:
            for d in (C.DIR_LEVEL_UP, C.DIR_LEVEL_DOWN):
                kind = native_type(lib, m, sx, sy, -1 if d == C.DIR_LEVEL_UP else 1)
                if kind is None:
                    continue
                set_creature_type(ai, g, kind)
                ai.begin(g, sx, sy, m)
                flags = ctypes.c_uint16.from_address(ai.ai + AI_MOVE_FLAGS_C)
                flags.value |= MOVE_LEVELS | rng.choice((MOVE_OPEN_PIT, MOVE_FALL, MOVE_STAIRS, MOVE_OPEN_PIT | MOVE_FALL | MOVE_STAIRS))
                ai.emu.write(G_AI + AI_MOVE_FLAGS_ORIGINAL, struct.pack("<H", flags.value))
                command = rng.choice((0, 1, 3, 7, 8)) | rng.choice((0, C.AI_EXECUTE))
                mine, theirs = ai.call("ai_try_move", command, sx, sy, -1, -1, d)
                assert mine == theirs, (g, sx, sy, m, command, d)
                tried += 1
                changed += bool(mine)
                ai.check((g, sx, sy, command, d))
                ai.end()
        set_creature_type(ai, g, original_type)
    assert tried > 20 and changed > 5, (tried, changed)


DIRS = ((0, -1), (1, 0), (0, 1), (-1, 0))


def places_near(ai, rng, kinds, things=None, per_group=4):
    """(group, x, y, map, target x, target y): a group put on an open square next to a square of the kinds (with
    things when things is set) of its map."""
    lib = ai.lib
    out = []
    for g, _, _, m in ai.groups():
        w, h = lib.dungeon_map_width(m), lib.dungeon_map_height(m)
        found = []
        for tx in range(w):
            for ty in range(h):
                s = lib.dungeon_square(m, tx, ty)
                if s >> C.SQUARE_TYPE_SHIFT not in kinds or (things is not None and bool(s & C.SQUARE_HAS_THINGS) != things):
                    continue
                for dx, dy in DIRS:
                    x, y = tx - dx, ty - dy
                    if 0 <= x < w and 0 <= y < h and lib.dungeon_square(m, x, y) >> C.SQUARE_TYPE_SHIFT in (C.SQUARE_FLOOR, C.SQUARE_DOOR, C.SQUARE_TELEPORTER):
                        found.append((g, x, y, m, tx, ty))
        out += rng.sample(found, min(per_group, len(found)))
    return out


def filters(ai, g):
    """The item filters that stay inside the filter table of the original for group g."""
    record = ai.lib.dungeon_thing_record
    record.restype = ctypes.c_void_p
    variant = s16(struct.unpack("<H", ctypes.string_at(record(g) + 8, 2))[0])
    out = []
    for f in range(0x40):
        base = 0x10 if f == 0x28 else f
        if 0x10 <= base < 0x13:
            base += variant * 3
        if f == 0x29:
            base = 7
        if f in (0x3E, 0x3F) or 0 <= base < C.FILTER_SLOTS:
            out.append(f)
    return out


def test_turn_toward(ai):
    rng = random.Random(18)
    for g, x, y, m in places(ai, rng, 0):
        ai.begin(g, x, y, m)
        for d in range(C.DIR_COUNT):
            mine, theirs = ai.call("ai_turn_toward", d)
            assert mine == theirs, (g, d)
            ai.check((g, d))
        ai.end()


def test_try_attack(ai):
    # Attacks from the squares near walls, doors, and other squares on the squares in lines around them: the party
    # (modes 0, 1), groups (2), anything (3), with and without the action; the attack skills of each creature type.
    rng = random.Random(19)
    attacked = 0
    for g, x, y, m in places(ai, rng):
        ai.begin(g, x, y, m)
        for n in range(30):
            mode = rng.randrange(4) | rng.choice((0, C.AI_EXECUTE))
            if rng.random() < 0.3:
                tx = ty = -1
                d = rng.randrange(4)
            else:
                dx, dy = DIRS[rng.randrange(4)]
                k = rng.randrange(0, 5)
                tx, ty, d = x + dx * k, y + dy * k, rng.choice((-1, -1, rng.randrange(4)))
            if rng.random() < 0.3:
                # the party square as the target
                tx, ty = ai.sc.party.square_x, ai.sc.party.square_y
            mine, theirs = ai.call("ai_try_attack", mode, x, y, tx, ty, d)
            assert mine == theirs, (g, n, mode, tx, ty, d)
            attacked += mine
            ai.check((g, n, mode, tx, ty, d))
        ai.end()
    assert attacked > 20


def test_try_door_action(ai):
    # Open, close, and close-marked actions on the doors next to the groups and in lines up to 3 squares away.
    rng = random.Random(20)
    done = 0
    for g, x, y, m, tx, ty in places_near(ai, rng, (C.SQUARE_DOOR,), per_group=6):
        ai.begin(g, x, y, m)
        for mode in (0, 1, 2, 0x80, 0x81, 0x82):
            for target, d in (((tx, ty), -1), ((-1, -1), rng.randrange(4)), ((tx, ty), rng.randrange(4))):
                mine, theirs = ai.call("ai_try_door_action", mode, x, y, *target, d)
                assert mine == theirs, (g, x, y, mode, target, d)
                done += mine
                ai.check((g, mode, target, d))
        ai.end()
    assert done > 10


def test_try_take_item(ai):
    # Takes and gives of items of many filters on the squares with things next to the groups and on their own square:
    # floor items, alcoves, chests, possessions of groups.
    rng = random.Random(21)
    taken = 0
    for g, x, y, m, tx, ty in places_near(ai, rng, (C.SQUARE_WALL, C.SQUARE_FLOOR, C.SQUARE_PIT, C.SQUARE_DOOR, C.SQUARE_TELEPORTER), things=True, per_group=5):
        ai.begin(g, x, y, m)
        fs = filters(ai, g)
        for n in range(16):
            mode = rng.randrange(2) | rng.choice((0, C.AI_EXECUTE))
            target = rng.choice(((tx, ty, -1), (tx, ty, rng.randrange(4)), (x, y, -1), (x, y, rng.randrange(4)),
                                 (-1, -1, rng.randrange(4))))
            f = rng.choice(fs) | rng.choice((0, 0, 0x80))
            mine, theirs = ai.call("ai_try_take_item", mode, x, y, *target, f)
            assert mine == theirs, (g, x, y, n, mode, target, f)
            taken += mine
            ai.check((g, n, mode, target, f))
        ai.end()
    assert taken > 10


def test_try_use_wall_actuator(ai):
    # Wall actuators next to the groups: use, put, and take with many filters.
    rng = random.Random(22)
    used = 0
    for g, x, y, m, tx, ty in places_near(ai, rng, (C.SQUARE_WALL,), things=True, per_group=6):
        ai.begin(g, x, y, m)
        fs = filters(ai, g)
        for n in range(8):
            mode = rng.randrange(3) | rng.choice((0, C.AI_EXECUTE))
            target = rng.choice(((tx, ty, -1), (tx, ty, rng.randrange(4)), (-1, -1, rng.randrange(4))))
            mine, theirs = ai.call("ai_try_use_wall_actuator", mode, x, y, *target, rng.choice(fs))
            assert mine == theirs, (g, n, mode, target)
            used += mine
            ai.check((g, n, mode, target))
        ai.end()


def test_projectile_incoming(ai):
    rng = random.Random(23)
    for g, x, y, m in places(ai, rng):
        ai.begin(g, x, y, m)
        mine, theirs = ai.call("ai_projectile_incoming", x, y)
        assert mine == theirs, g
        ai.check(g)
        ai.end()


def all_things(lib):
    """Every thing of the dungeon on a square, with the contents of containers and the possessions of groups."""
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    out = []

    def walk(t):
        while t != ENDOFLIST:
            out.append(t)
            if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK in (C.THING_GROUP, C.THING_CONTAINER):
                walk(lib.thing_word(t, 2))
            t = lib.dungeon_next_thing(t)

    for m in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(m)):
            for y in range(lib.dungeon_map_height(m)):
                walk(lib.dungeon_square_first_thing(m, x, y))
    return out


def test_item_filters(ai):
    # Every thing of the dungeon against the item filters of several creature types (and inverted filters): the
    # class texts, the money boxes, the creature types, the wall actuators.
    rng = random.Random(24)
    lib = ai.lib
    things = all_things(lib)
    groups = ai.groups()
    matched = 0
    types = set()
    for g, x, y, m in rng.sample(groups, 12):
        record = lib.dungeon_thing_record
        record.restype = ctypes.c_void_p
        kind = ctypes.string_at(record(g) + 4, 1)[0]
        if kind in types:
            continue
        types.add(kind)
        ai.begin(g, x, y, m)
        fs = filters(ai, g)
        for f in fs:
            for t in rng.sample(things, 120):
                for flt in (f, f | 0x80):
                    mine, theirs = ai.call("ai_thing_matches_filter", t, flt)
                    assert mine == (theirs != 0), (g, hex(t), hex(flt))  # the original returns FILTER_INVERT for true
                    matched += theirs == 1
            ai.check((g, f))
        ai.end()
    assert matched > 100


def test_item_lists(ai):
    # The item searches, counts, and removals in the thing lists of the squares, chests, and groups.
    rng = random.Random(25)
    lib = ai.lib
    lib.ai_find_matching_item.restype = ctypes.c_uint16
    lib.ai_find_matching_item_inside.restype = ctypes.c_int16
    things = all_things(lib)
    containers = [t for t in things if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK in (C.THING_GROUP, C.THING_CONTAINER)]
    for g, x, y, m in rng.sample(ai.groups(), 10):
        ai.begin(g, x, y, m)
        fs = filters(ai, g)
        for n in range(40):
            f = rng.choice(fs) | rng.choice((0, 0x80))
            cell = rng.choice((C.CELL_ANY, rng.randrange(4)))
            first = rng.choice(things)
            mine, theirs = ai.call("ai_find_matching_item", f, first, cell)
            assert mine == theirs & 0xFFFF, (g, n, "find")
            mine, theirs = ai.call("ai_count_matching_items", f, first, cell, rng.randrange(2), rng.randrange(2))
            assert mine == s16(theirs), (g, n, "count")
            c = rng.choice(containers)
            mine, theirs = ai.call("ai_find_matching_item_inside", c, f, rng.randrange(4))
            assert mine == s16(theirs), (g, n, "inside", hex(c))
            ai.check((g, n))
        # removals from the contents of chests and the possessions of groups
        for c in rng.sample(containers, 6):
            f = rng.choice(fs) | rng.choice((0, 0x80))
            cell = rng.choice((C.CELL_ANY, rng.randrange(4)))
            mine = lib.dungeon_thing_record(c) + 2
            theirs = ai.emu.read_u32(0x7F288 + 4 * (c >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK)) + (c & C.THING_INDEX_MASK) * THING_SIZES[c >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK] + 2
            lib.ai_remove_matching_items(mine, f, cell)
            ai.emu.call(F["ai_remove_matching_items"], theirs, f, cell)
            ai.check((g, "remove", hex(c)), dungeon=True)
        ai.end()


def test_ai_markers(ai):
    # The AI marker texts of the squares: by kind mask and by id, for the level deltas.
    rng = random.Random(26)
    lib = ai.lib
    found = 0
    g, x, y, m = ai.groups()[0]
    marked = []
    for mp in range(lib.dungeon_map_count()):
        for sx in range(lib.dungeon_map_width(mp)):
            for sy in range(lib.dungeon_map_height(mp)):
                if lib.dungeon_square(mp, sx, sy) & C.SQUARE_HAS_THINGS:
                    marked.append((mp, sx, sy))
    ai.begin(g, x, y, m)
    for mp, sx, sy in rng.sample(marked, 400):
        ai.set_map(mp)
        ai.call("ai_cache_square", sx, sy)
        for delta in (-1, 0, 1, 2):
            mine, theirs = ai.call("ai_find_marker", delta, rng.randrange(0x10000))
            assert mine == s16(theirs), (mp, sx, sy, delta)
            found += mine != -1
            mine, theirs = ai.call("ai_find_marker_by_id", delta, rng.randrange(16))
            assert mine == s16(theirs), (mp, sx, sy, delta)
        ai.check((mp, sx, sy))
    ai.set_map(m)
    ai.end()
    assert found > 0


class Buffers:
    """The same bytes at a C address and at an emulator address."""

    def __init__(self, ai, size):
        self.ai, self.size = ai, size
        self.c = ctypes.create_string_buffer(size)
        self.emu = ai.sc.view.alloc(size)

    def put(self, data):
        ctypes.memmove(self.c, data, len(data))
        self.ai.emu.write(self.emu, data)

    def both(self):
        return self.c.raw, self.ai.emu.read(self.emu, self.size)


def test_find_target_in_lines(ai):
    # Targets in the four lines (or one) from the squares near the groups, with search maps of random distances.
    rng = random.Random(27)
    found = 0
    for g, x, y, m in places(ai, rng):
        ai.begin(g, x, y, m)
        fs = filters(ai, g)
        dist = Buffers(ai, 32 * 32 * 4)
        out = Buffers(ai, 2)
        for n in range(6):
            cells = bytearray(32 * 32 * 4)
            for i in range(0, len(cells), 4):
                cells[i] = rng.choice((0, 0, 1, 2, 3, 4, 5))
            dist.put(bytes(cells))
            args = (rng.choice(fs), dist, out, x, y, rng.choice((-1, rng.randrange(4))), rng.randrange(2))
            target = ai.lib.ai_find_target_in_lines(args[0], dist.c, Location(x, y), *args[5:])
            mine = int(target.found)
            if target.found:  # the original writes the target square to out
                ctypes.memmove(out.c, struct.pack("<H", target.location), 2)
            theirs = ai.emu.call(F["ai_find_target_in_lines"], args[0], dist.emu, out.emu, *args[3:])
            assert mine == theirs, (g, n, args)
            assert out.both()[0] == out.both()[1], (g, n)
            found += mine
            ai.check((g, n))
        ai.end()


def random_candidate(rng, fs, x, y, m):
    kind = rng.randrange(C.AI_TARGET_KIND_COUNT)
    marker = rng.choice((0xFFFF, (x + rng.randrange(-4, 5)) & C.LOCATION_COORD_MASK | (y + rng.randrange(-4, 5)) % 32 << C.LOCATION_Y_SHIFT | m << C.LOCATION_MAP_SHIFT))
    item = rng.choice((rng.choice(fs), rng.randrange(-4, 6) & 0xFFFF, rng.randrange(8)))
    if kind == C.TARGET_MOVE_TEST:
        item = rng.choice((0, 1, 3, 4, 5, 7, 8))
    if kind in (C.TARGET_MELEE_LINE, C.TARGET_SHOOT_LINE, C.TARGET_ITEM, C.TARGET_ITEM_2, C.TARGET_ITEM_WALL, C.TARGET_DAMAGE_GROUP, C.TARGET_ACTUATOR):
        item = rng.choice(fs) | rng.choice((0, 0x80))  # an item filter
    mask = rng.choice((0, 1, 2, 3, rng.randrange(0x10000)))
    cost = rng.randrange(1, 16)
    value = rng.choice((0, 0, 1, 2, 0xFF, 0x80))
    return struct.pack("<bBHHbbHHHbbBbI", cost, value, 0, 0, 0, kind, item, mask, marker, 0, 0, 0, 0, 0)


def test_search_area(ai):
    # The path search from the squares near the groups: the same candidate records in both (all target kinds, costs,
    # bonus values, filters, markers), the move commands of the plans; the found index, the candidates, the path.
    rng = random.Random(28)
    lib = ai.lib
    lib.ai_search_area.restype = ctypes.c_int
    found = paths = 0
    for g, x, y, m in places(ai, rng, 2):
        ai.begin(g, x, y, m)
        fs = filters(ai, g)
        for n in range(5):
            count = rng.choice((1, 1, 2, 3, 5))
            cands = Buffers(ai, 22 * count)
            path = Buffers(ai, 0x64)
            cands.put(b"".join(random_candidate(rng, fs, x, y, m) for _ in range(count)))
            command = rng.choice((0, 2, 3, 4, 5, 7, 8)) | rng.choice((0, 0, C.AI_MOVE_ATTACK_GROUPS, C.AI_MOVE_NO_MOVERS))
            mine = lib.ai_search_area(Location(x, y), command, count, cands.c, path.c)
            theirs = ai.emu.call(F["ai_search_area"], x, y, command, 0, count, cands.emu, path.emu)
            where = (g, x, y, n, hex(command), cands.c.raw.hex())
            assert mine == s16(theirs), where
            mc, tc = cands.both()
            for i in range(count):
                assert mc[i * 22:i * 22 + 22].hex(" ") == tc[i * 22:i * 22 + 22].hex(" "), (where, i)
            assert path.both()[0] == path.both()[1], where
            found += mine >= 0
            paths += mine >= 0 and mc[mine * 22 + 6] not in (0, 0xFF)
            ai.check(where, dungeon=True)
        ai.end()
    assert found > 50 and paths > 20


def store_path(ai, g, data):
    """The stored path of group g in both (C ai_store_path, original image cache key 0x30000000 | group)."""
    lib, sc = ai.lib, ai.sc
    lib.ai_store_path.restype = ctypes.c_void_p
    ctypes.memmove(lib.ai_store_path(len(data)), data, len(data))
    index = sc.view.new_index()
    sc.ai_cache[0x30000000 | g & C.THING_INDEX_MASK] = index
    addr = sc.view.alloc(len(data))
    sc.view.cache[index] = addr
    sc.sizes[index] = len(data)
    ai.emu.write(addr, data)


def put_ai(ai, name, data):
    c_off, o_off, size = AI_FIELDS[name]
    ctypes.memmove(ai.ai + c_off, data, len(data))
    ai.emu.write(G_AI + o_off, data)


def test_plan_path_and_next_step(ai):
    # A search from each group to a marked square nearby gives a path; with that path stored and the plan state set,
    # the path check of the plan and the next step of the path, also after random changes of the plan state.
    rng = random.Random(29)
    lib = ai.lib
    lib.ai_search_area.restype = ctypes.c_int
    held = steps = 0
    for g, x, y, m in places(ai, rng, 2):
        ai.begin(g, x, y, m)
        tx, ty = x + rng.randrange(-5, 6), y + rng.randrange(-5, 6)
        cand = struct.pack("<bBHHbbHHHbbBbI", 30, 0, 0, 0, 0, C.TARGET_MARKER, rng.randrange(4), 0, tx & C.LOCATION_COORD_MASK | (ty & C.LOCATION_COORD_MASK) << C.LOCATION_Y_SHIFT | m << C.LOCATION_MAP_SHIFT,
                           0, 0, 0, 0, 0)
        cands, path = Buffers(ai, 22), Buffers(ai, 0x64)
        cands.put(cand)
        command = rng.choice((0, 3, 5))
        mine = lib.ai_search_area(Location(x, y), command, 1, cands.c, path.c)
        theirs = ai.emu.call(F["ai_search_area"], x, y, command, 0, 1, cands.emu, path.emu)
        assert mine == s16(theirs) and cands.both()[0] == cands.both()[1] and path.both()[0] == path.both()[1], (g, x, y)
        record = cands.c.raw
        length = record[6] if mine == 0 and record[6] < 0x80 else rng.randrange(4)
        put_ai(ai, "candidate_count", bytes([1, 0]))
        put_ai(ai, "candidates", record + bytes(22 * 15))
        for n in range(4):
            left = rng.randrange(length + 1) if n else length
            kind = record[7] if n < 3 else rng.randrange(C.AI_TARGET_KIND_COUNT)
            cache = struct.pack("<BBbbHHHI", length, left, 0, kind, struct.unpack_from("<H", record, 8)[0], 0,
                                struct.unpack_from("<H", record, 4)[0], 0)
            put_ai(ai, "cache", cache)
            if length:
                store_path(ai, g, path.c.raw[:length * 2])
            mine, theirs = ai.call("ai_plan_path")
            assert mine == theirs, (g, x, y, n)
            held += mine
            ai.check((g, n, "plan"))
            put_ai(ai, "cache", cache)
            mine, theirs = ai.call("ai_next_path_step")
            assert mine == s16(theirs), (g, x, y, n)
            steps += mine > 0
            ai.check((g, n, "step"))
        ai.end()
    assert held > 10 and steps > 10


def distance_maps(ai):
    lib, emu = ai.lib, ai.emu
    out = []
    for name, ptr, map_addr, width_addr in DISTANCE_MAPS:
        raw = ctypes.string_at(ctypes.addressof(ctypes.c_uint8.in_dll(lib, name)), 4 + 32 * 32)
        c_map, c_width = struct.unpack_from("<hh", raw)
        o_map = s16(struct.unpack("<H", emu.read(map_addr, 2))[0])
        o_width = s16(struct.unpack("<H", emu.read(width_addr, 2))[0])
        cells = emu.read(emu.read_u32(ptr), 32 * max(o_width, 0)) if o_map >= 0 else b""
        out.append(((c_map, c_width if c_map >= 0 else None, raw[4:4 + len(cells)]), (o_map, o_width if o_map >= 0 else None, cells)))
    return out


def test_refresh_party_maps(ai):
    # The distance maps of the party map (and of the map of the alternate view) and the light pass after the party
    # moved: the party at random open squares of all maps.
    rng = random.Random(30)
    lib, emu, sc = ai.lib, ai.emu, ai.sc
    assert emu.read_u32(G_PARTY_DISTANCE) and emu.read_u32(G_ALT_DISTANCE)
    reached = 0
    places_ = [(m, x, y) for m in range(lib.dungeon_map_count()) for x in range(lib.dungeon_map_width(m))
               for y in range(lib.dungeon_map_height(m)) if lib.dungeon_square(m, x, y) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_FLOOR]
    for n, (m, x, y) in enumerate(rng.sample(places_, 25)):
        sc.place(m, x, y, rng.randrange(4))
        ai.sync_map_values()
        redraw = rng.choice((1, 2, 3, 3))
        sc.party.redraw = redraw
        emu.write_u32(G_VIEW_REDRAW, redraw)
        mine, theirs = ai.call("ai_refresh_party_maps")
        for c_map, o_map in distance_maps(ai):
            assert c_map[:2] == o_map[:2], (n, m, x, y)
            assert c_map[2].hex() == o_map[2].hex(), (n, m, x, y)
        reached += sum(1 for b in distance_maps(ai)[0][0][2] if b)
        for addr, name in zip(G_LIGHT_SUMS, LIGHT_NAMES):
            assert ctypes.c_int16.in_dll(lib, name).value == s16(struct.unpack("<H", emu.read(addr, 2))[0]), (n, name)
        assert sc.party.redraw == emu.read_u32(G_VIEW_REDRAW)
        ai.check((n, m, x, y), fields=("flags", "path", "candidates", "cache"), dungeon=True)
    assert reached > 200


DUN_GET_UNUSED_THING, DUN_LINK_THING_TO_SQUARE, TIMELINE_ADD_EVENT = 0x1DDD7, 0x1D3DB, 0x56390


def add_thing(ai, type_, words, map_, x, y, cell):
    """Adds the same new thing (record words from +2) on (x, y) of map_ in both; returns it."""
    lib, emu = ai.lib, ai.emu
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.thing_set_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    mine = lib.dungeon_unused_thing(type_)
    theirs = emu.call(DUN_GET_UNUSED_THING, type_) & 0xFFFF
    assert mine == theirs
    record = emu.read_u32(0x7F288 + 4 * type_) + (mine & C.THING_INDEX_MASK) * THING_SIZES[type_]
    for i, w in enumerate(words):
        lib.thing_set_word(mine, 2 + 2 * i, w)
        emu.write(record + 2 + 2 * i, struct.pack("<H", w))
    thing = mine | cell << C.THING_CELL_SHIFT
    saved = ai.sc.party.cur_map
    ai.set_map(map_)
    assert lib.dungeon_link_thing_to_square(thing, map_, x, y) == 1
    emu.call(DUN_LINK_THING_TO_SQUARE, thing, 0, x, y)
    ai.set_map(saved)
    return thing


def add_projectile(ai, payload, direction, map_, x, y, cell):
    """A projectile with its move event (the direction in data byte 3) in both."""
    from dm2test.save import Event
    lib, emu = ai.lib, ai.emu
    thing = add_thing(ai, C.THING_PROJECTILE, (payload, 0x40, 0), map_, x, y, cell)
    event = Event((ai.sc.time() + 50) | map_ << C.EVENT_MAP_SHIFT, C.EVENT_PROJECTILE_MOVE, 0,
                  (ctypes.c_uint8 * 6)(*struct.pack("<HBB2x", thing, 0, direction << 2)))
    buf = ai.sc.view.alloc(12)
    emu.write(buf, bytes(event))
    index = lib.timeline_add_event(ctypes.byref(event))
    assert index == emu.call(TIMELINE_ADD_EVENT, buf) & 0xFFFF
    lib.thing_set_word(thing, 6, index)
    record = emu.read_u32(0x7F288 + 4 * 14) + (thing & C.THING_INDEX_MASK) * 8
    emu.write(record + 6, struct.pack("<H", index))
    return thing


def test_with_clouds_and_projectiles(ai):
    # Clouds, door spells, and projectiles on the squares around the groups: the line tests, attacks, door actions,
    # dangerous moves (kinds 4 and 5), and the projectile test see them.
    rng = random.Random(31)
    lib = ai.lib
    groups = ai.groups()
    for g, x, y, m in rng.sample(groups, 30):
        for _ in range(3):
            sx, sy = x + rng.randrange(-3, 4), y + rng.randrange(-3, 4)
            if not (0 <= sx < lib.dungeon_map_width(m) and 0 <= sy < lib.dungeon_map_height(m)) \
                    or lib.dungeon_square(m, sx, sy) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_WALL:
                continue
            if rng.random() < 0.5:
                add_thing(ai, C.THING_EXPLOSION, (rng.choice((C.SPELL_MISSILE_WALL, C.SPELL_DOOR_TOGGLE, C.SPELL_DOOR_SET, C.SPELL_POISON_CLOUD, C.SPELL_FIREBALL)),), m, sx, sy, rng.randrange(4))
            else:
                add_projectile(ai, rng.choice((C.SPELL_DOOR_SET, C.SPELL_DOOR_TOGGLE, C.SPELL_FIREBALL)), rng.randrange(4), m, sx, sy, rng.randrange(4))
    ai.check("added", dungeon=True)
    acted = 0
    for g, x, y, m in groups:
        ai.begin(g, x, y, m)
        for sx, sy in squares_near(lib, m, x, y, 2):
            for name in LINE_TESTS:
                mine, theirs = ai.call(name, sx, sy)
                assert mine == s16(theirs), (g, name, sx, sy)
        for n in range(20):
            d = rng.randrange(4)
            k = rng.randrange(1, 4)
            tx, ty = x + DIRS[d][0] * k, y + DIRS[d][1] * k
            mine, theirs = ai.call("ai_try_attack", rng.randrange(4) | C.AI_EXECUTE, x, y, tx, ty, -1)
            assert mine == theirs, (g, n, "attack")
            mine, theirs = ai.call("ai_try_door_action", rng.randrange(3) | C.AI_EXECUTE, x, y, tx, ty, -1)
            assert mine == theirs, (g, n, "door")
            mine, theirs = ai.call("ai_try_move", rng.choice((4, 5, 0x84, 0x85)), x, y, -1, -1, d)
            assert mine == theirs, (g, n, "move")
            acted += mine
            mine, theirs = ai.call("ai_projectile_incoming", tx, ty)
            assert mine == theirs, (g, n, "incoming")
            ai.check((g, n))
        ai.end()
    assert acted > 20


def test_door_spells_on_the_way(ai):
    # A door spell that flies toward the door (or the other way) between a group and a door: the door action waits.
    rng = random.Random(32)
    waits = 0
    for g, x, y, m, tx, ty in places_near(ai, rng, (C.SQUARE_DOOR,), per_group=3)[:50]:  # start.DAT has 60 projectile records
        d = [i for i, (dx, dy) in enumerate(DIRS) if (x + dx, y + dy) == (tx, ty)][0]
        payload = rng.choice((C.SPELL_DOOR_SET, C.SPELL_DOOR_TOGGLE, C.SPELL_FIREBALL))
        add_projectile(ai, payload, rng.choice((d, (d + 2) & 3)), m, x, y, rng.randrange(4))
        ai.begin(g, x, y, m)
        flags = struct.unpack("<H", ctypes.string_at(ai.ai + AI_MOVE_FLAGS_C + 4, 2))[0]
        ctypes.memmove(ai.ai + AI_MOVE_FLAGS_C + 4, struct.pack("<H", flags | 1), 2)  # the door skill of shooting at the button
        ai.emu.write(G_AI + 0x2A + 4, struct.pack("<H", flags | 1))
        for mode in (0, 1, 0x80, 0x81):
            mine, theirs = ai.call("ai_try_door_action", mode, x, y, tx, ty, -1)
            assert mine == theirs, (g, x, y, mode)
            ai.check((g, mode))
            waits += mine and mode & C.AI_EXECUTE and ctypes.string_at(ai.ai + AI_FIELDS["command_result"][0], 1)[0] == 0xFC
        ai.end()
    assert waits > 3



MERCHANT_COMMANDS = {"shop_cmd_wait_for_offer": 0x28574, "shop_cmd_evaluate_offer": 0x28711,
                     "shop_cmd_count_payment": 0x28E99, "shop_cmd_pay_change": 0x296D5}
DUN_GET_THING_DATA, DUN_LINK_THING_TO_SQUARE, DUN_UNLINK_THING_FROM_SQUARE = 0x1D2DA, 0x1D3DB, 0x1D542
ITEM_CREATE_BY_INDEX = 0x1DE8E
MONEY_BOX = 0xA40F  # the money box on the shelf of the first room (map 22)
MERCHANT, CUSTOMER = (0x1089, 4, 6, 18), (0x1087, 3, 4)  # map 18: the merchant, and a static group as the counter
GROUP_DIR_WORD, WEST = C.GROUP_DIR, C.DIR_WEST


def set_word(ai, thing, offset, value):
    ai.lib.thing_set_word(thing, offset, value)
    ai.emu.write(ai.emu.call(DUN_GET_THING_DATA, thing & C.THING_ID_MASK) + offset, struct.pack("<H", value))


def test_merchant_commands(ai):
    # The merchant of map 18 faces a counter (a static group moved in front of it). The counter holds random coins
    # and goods in random cells; then the four merchant commands run in random order with random arguments: the
    # results, the AI context, the active groups (counters, action), the dungeon (coins of the change), and the seed.
    rng = random.Random(15)
    lib = ai.lib
    lib.thing_word.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.item_create_by_index.restype = ctypes.c_uint16
    lib.money_coin_item.restype = ctypes.c_uint16
    lib.money_init_coin_table()
    ai.emu.call(0x154E0)  # money_init_coin_table
    g, x, y, m = MERCHANT
    counter, cx, cy = CUSTOMER
    ai.set_map(m)
    lib.dungeon_unlink_thing_from_square(counter, m, cx, cy)
    ai.emu.call(DUN_UNLINK_THING_FROM_SQUARE, counter, 0, cx, cy)
    lib.dungeon_link_thing_to_square(counter, m, x - 1, y)
    ai.emu.call(DUN_LINK_THING_TO_SQUARE, counter, 0, x - 1, y)
    set_word(ai, g, GROUP_DIR_WORD, lib.thing_word(g, GROUP_DIR_WORD) & ~0x300 | WEST << 8)
    coins = [lib.money_coin_item(t) for t in range(lib.money_coin_type_count())]
    pool = []
    for index in coins * 4 + [0, 1, 2, 5, 8, 0x80, 0x81, 0x101, 0x180, 0x1E2]:
        mine, theirs = lib.item_create_by_index(index), ai.emu.call(ITEM_CREATE_BY_INDEX, index) & 0xFFFF
        assert mine == theirs != NONE, hex(index)
        pool.append(mine)
    # a money box of the merchant with coins of its own: the change goes into it
    box_index = lib.item_get_index(MONEY_BOX)
    box, theirs = lib.item_create_by_index(box_index), ai.emu.call(ITEM_CREATE_BY_INDEX, box_index) & 0xFFFF
    assert box == theirs != NONE and lib.item_is_moneybox(box)
    box_coins = [lib.item_create_by_index(index) for index in coins * 2]
    assert box_coins == [ai.emu.call(ITEM_CREATE_BY_INDEX, index) & 0xFFFF for index in coins * 2]
    ai.begin(g, x, y, m)
    goods = [t for t in pool if ai.call("ai_thing_matches_filter", t, 0x10)[0]]
    ai.end()
    assert goods
    results = {}
    boxes = change_in_box = 0
    for n in range(300):
        # the goods of the merchant lie in cell 1 (the cell of the counter on the merchant side, it faces west): the
        # original divides by 0 for a price of 0
        first = rng.choice(goods)
        things = [first] + rng.sample([t for t in pool if t != first], rng.randrange(len(pool) // 2))
        cells = [1] + [rng.randrange(4) for _ in things[1:]]
        for t in pool:  # no list of the last round goes on
            set_word(ai, t, 0, ENDOFLIST)
        for t, nxt, cell in zip(things, things[1:] + [None], cells[1:] + [0]):
            set_word(ai, t, 0, ENDOFLIST if nxt is None else nxt & C.THING_ID_MASK | cell << C.THING_CELL_SHIFT)
        set_word(ai, counter, 2, first & C.THING_ID_MASK | 1 << C.THING_CELL_SHIFT)
        # the change coins of the last round are freed: the records do not run out (no discarded item is reused,
        # dun_get_discarded_thing)
        t = lib.thing_word(g, 2)
        while t != ENDOFLIST:
            nxt = lib.dungeon_next_thing(t)
            if t & C.THING_ID_MASK not in pool and t != box:
                set_word(ai, t, 0, NONE)
            t = nxt
        set_word(ai, g, 2, ENDOFLIST)
        if rng.random() < 0.4:
            t = lib.thing_word(box, 2)
            while t != ENDOFLIST:
                nxt = lib.dungeon_next_thing(t)
                if t & C.THING_ID_MASK not in box_coins:
                    set_word(ai, t, 0, NONE)
                t = nxt
            inside = rng.sample(box_coins, rng.randrange(len(box_coins) + 1))
            for t, nxt in zip(inside, inside[1:] + [None]):
                set_word(ai, t, 2, lib.thing_word(t, 2) & 0xC07F | rng.randrange(8) << 8)
                set_word(ai, t, 0, ENDOFLIST if nxt is None else nxt)
            set_word(ai, box, 2, inside[0] if inside else ENDOFLIST)
            set_word(ai, box, 0, ENDOFLIST)
            set_word(ai, g, 2, box)
            boxes += 1
        ai.begin(g, x, y, m)
        for k in range(4):
            name = rng.choice(list(MERCHANT_COMMANDS))
            args = struct.pack("<hh", rng.choice((0, 0, 1, 2, 3)), rng.choice((0, 2)))
            ctypes.memmove(ai.ai + AI_ARGS_C, args, 4)
            ai.emu.write(G_AI + 0x26, args)
            mine, theirs = getattr(lib, name)() & 0xFF, ai.emu.call(MERCHANT_COMMANDS[name]) & 0xFF
            assert mine == theirs, (n, k, name)
            assert ctypes.string_at(ai.ai + AI_ARGS_C, 4) == ai.emu.read(G_AI + 0x26, 4), (n, k, name)
            ai.check((n, k, name), dungeon=True)
            results.setdefault(name, set()).add(mine)
        ai.end()
        t = lib.thing_word(box, 2)
        while t != ENDOFLIST and lib.thing_word(g, 2) == box:
            change_in_box += t & C.THING_ID_MASK not in box_coins
            t = lib.dungeon_next_thing(t)
    assert all(len(r) >= 2 for r in results.values()) and len(results) == 4 and boxes > 80 and change_in_box > 5, (
        results, boxes, change_in_box)


# The AI tables of ai_data.c in the data object of SKULL.EXE: (name, address, first byte, size).
AI_TABLES = (
    ("g_ai_scripts", 0x73020, 0, 8030), ("g_ai_plans", 0x74F7E, 0, 252), ("g_ai_class_flags", 0x7507A, 0, 188),
    ("g_ai_action_flags", 0x75136, 0, 86), ("g_ai_behaviors", 0x7518C, 0, 188), ("g_ai_command_enabled", 0x7528C, 0, 20),
    ("g_ai_after_command_table", 0x752AC, 0, 60), ("g_ai_kind_flags", 0x752EA, 0, 28),
    ("g_ai_possession_cells", 0x71658, 4, 64),  # kind 0 is another global of SKULL.EXE; the original never reads it
)


def test_tables_match_the_original(lib):
    # The copies of the AI tables against the bytes of SKULL.EXE as it loads.
    from dm2test.markers import ROOT
    from dm2tools.le import load_image
    image = load_image(ROOT / "game" / "SKULL.EXE")
    for name, address, first, size in AI_TABLES:
        mine = ctypes.string_at(ctypes.addressof(ctypes.c_uint8.in_dll(lib, name)), size)
        assert mine[first:] == image.data[address + first - image.base:address + size - image.base], name
