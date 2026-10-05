"""Differential tests of the creature perception, the AI command tests, the item filters, and the path search (Step 16,
ai_move.c) against the original code: start.DAT in both, the same AI context, the same calls, then the results, the
AI context, the dungeon, and the random seed must be equal."""

import ctypes
import random
import struct

import pytest

from test_creature import G_RANDOM_SEED, creatures  # noqa: F401 (fixture)
from test_move import gdat, start  # noqa: F401 (fixtures)

AI_BEGIN, AI_PREPARE, AI_END = 0x24A88, 0x24BFC, 0x24D88
DUN_SET_CURRENT_MAP = 0x1C724
G_AI = 0x7F548
G_AI_SQUARE, G_AI_PARTY = 0x7F8A4, 0x7F8D0
G_DIRECTION_BITS = 0x752E8
G_MEM_FREE_BYTES = 0x7FA7C
G_MAP_LIGHT, G_MAP_WALL_COST = 0x7F3F4, 0x7F3FC
G_PARTY_DISTANCE, G_ALT_DISTANCE = 0x7F8C8, 0x7F8C4
G_PARTY_DISTANCE_MAP, G_ALT_DISTANCE_MAP = 0x752A0, 0x752A2
G_PARTY_DISTANCE_WIDTH, G_ALT_DISTANCE_WIDTH = 0x7F8CE, 0x7F8CC
G_LIGHT_SUMS = (0x7F970, 0x7F974, 0x752A4, 0x752A6, 0x752A8)
G_VIEW_REDRAW = 0x7F38C
G_AI_SCRATCH = 0x7F894
# Functions of later parts that both sides stub: (address, stack arguments).
EMPTY = {0x16D72: 0, 0x181F0: 0, 0x38C46: 1, 0x389C2: 0}

# Original functions of ai_move.c.
F = {
    "creature_square_is_obstacle": 0x2B96A, "creature_square_blocks_line": 0x2B9FC,
    "creature_square_blocks_line_to_target": 0x2BB78, "creature_trace_line": 0x2BBAD,
    "creature_ai_cache_square": 0x2BEAF, "creature_ai_cached_first_object": 0x2BF41,
    "creature_ai_cached_first_group": 0x2BFAE, "creature_ai_turn_toward": 0x2C005,
    "creature_ai_find_target_in_lines": 0x2C0A2, "creature_ai_try_attack": 0x2C404,
    "creature_ai_try_door_action": 0x2CC42, "creature_ai_projectile_incoming": 0x2D52D,
    "creature_ai_try_move": 0x2D792, "creature_find_matching_item_inside": 0x2E8E1,
    "creature_ai_try_take_item": 0x2EA68, "creature_find_wall_actuator_in_cell": 0x2F0FA,
    "creature_wall_actuator_accepts": 0x2F1C4, "creature_ai_try_use_wall_actuator": 0x2F2CF,
    "creature_thing_matches_filter": 0x2F636, "creature_find_matching_item": 0x2FF1E,
    "creature_remove_matching_items": 0x2FF89, "creature_count_matching_items": 0x2697B,
    "creature_find_ai_marker": 0x3161E, "creature_find_ai_marker_by_id": 0x31724,
    "creature_square_blocks_line_except_self": 0x317EE, "creature_ai_search_area": 0x3188A,
    "creature_ai_load_cached_path": 0x33DF9, "creature_ai_next_path_step": 0x33E61,
    "creature_ai_plan_path": 0x33EF3, "creature_refresh_party_maps": 0x2F8F5,
}
# The fields of the AI context: (C offset, original offset, size).
AI_FIELDS = {
    "command_result": (61, 0x23, 1), "map": (63, 0x25, 1), "flags": (68, 0x2A, 12), "attr7": (80, 0x36, 4),
    "path": (224, 0xC2, 0x64), "candidate_count": (324, 0x128, 2), "candidates": (326, 0x12C, 16 * 22),
    "cache": (678, 0x28C, 14), "filter_free": (1048, 0x34A, 2),
}
AI_SIZE = 1072
ENDOFLIST, NONE = 0xFFFE, 0xFFFF


def s16(v):
    return ctypes.c_int16(v & 0xFFFF).value


class Ai:
    """The C port and the original with the same creature AI context."""

    def __init__(self, sc):
        self.sc, self.lib, self.emu = sc, sc.lib, sc.emu
        lib, emu = self.lib, self.emu
        for addr, stack in EMPTY.items():
            emu.stub(addr, lambda *_: 0, stack_args=stack)
        emu.write_u32(G_MEM_FREE_BYTES, 0x7FFFFFFF)
        self.sync_map_values()
        self.ai = ctypes.addressof(ctypes.c_uint8.in_dll(lib, "ai"))
        self.square = ctypes.addressof(ctypes.c_uint8.in_dll(lib, "ai_square"))
        self.scan = ctypes.addressof(ctypes.c_uint8.in_dll(lib, "ai_square_scan"))
        self.saved = ctypes.create_string_buffer(AI_SIZE)
        self.scratch = sc.view.alloc(0x2A * 0x40)
        lib.creature_ai_begin.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
        lib.creature_ai_end.argtypes = [ctypes.c_int, ctypes.c_void_p]
        lib.ai_scratch_push.restype = ctypes.c_void_p
        lib.dun_set_current_map.argtypes = [ctypes.c_int]
        for name in ("creature_trace_line",):
            getattr(lib, name).argtypes = [ctypes.c_int] * 4 + [ctypes.c_void_p]
        lib.creature_ai_find_target_in_lines.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p] + [ctypes.c_int] * 4
        lib.creature_ai_search_area.argtypes = [ctypes.c_int] * 4 + [ctypes.c_void_p] * 2
        lib.creature_remove_matching_items.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        self.begun = None

    def sync_map_values(self):
        """The values of the party map graphics that the original reads at the map load: light radius, wall cost."""
        lib = self.lib
        lib.gdat_entry_index.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
        lib.gdat_entry_index.restype = ctypes.c_uint16
        gfx_set = lib.dungeon_map_graphics(self.sc.game.party_map) >> 4 & 0xF
        light = lib.gdat_entry_index(self.sc.gdat, 8, gfx_set, 0x0B, 0x6D)
        cost = lib.gdat_entry_index(self.sc.gdat, 8, gfx_set, 0x0B, 0x72) or 1
        self.emu.write(G_MAP_LIGHT, struct.pack("<H", light))
        self.emu.write(G_MAP_WALL_COST, struct.pack("<H", cost))

    def groups(self):
        """(group thing, x, y, map) of each active group."""
        lib = self.lib
        n = ctypes.c_int.in_dll(lib, "active_group_max").value
        ptr = ctypes.c_void_p.in_dll(lib, "active_groups").value
        raw = ctypes.string_at(ptr, n * 0x22)
        out = []
        for i in range(n):
            index, = struct.unpack_from("<H", raw, i * 0x22)
            if index == 0xFFFF:
                continue
            pos, = struct.unpack_from("<H", raw, i * 0x22 + 0xC)
            out.append((0x1000 | index, pos & 0x1F, pos >> 5 & 0x1F, pos >> 10))
        return out

    def set_map(self, map_):
        self.lib.dun_set_current_map(map_)
        self.emu.call(DUN_SET_CURRENT_MAP, map_)

    def begin(self, group, x, y, map_):
        """The AI context of group at (x, y) of map_ in both, prepared, with a scratch block for the item filters."""
        lib, emu = self.lib, self.emu
        self.set_map(map_)
        lib.creature_ai_begin(group, x, y, 0x22, self.saved)
        lib.creature_ai_prepare()
        lib.ai_scratch_push()
        self.begun = emu.call(AI_BEGIN, group, x, y, 0x22)
        emu.call(AI_PREPARE)
        emu.write_u32(G_AI_SCRATCH, self.scratch)
        # the square cache and its extra bytes start equal
        sq = ctypes.string_at(self.square, 16)
        emu.write(G_AI_SQUARE, sq[:7] + b"\0" + sq[8:14] + bytes(14) + sq[14:16])
        ctypes.memset(self.scan, 0, 14)
        self.check("begin")

    def end(self):
        self.check("end", dungeon=True)
        self.lib.ai_scratch_pop()
        self.lib.creature_ai_end(0, None)
        self.emu.call(AI_END, self.begun)

    def call(self, name, *args, emu_args=None):
        """Calls the C function and the original with the same arguments; returns both results."""
        mine = getattr(self.lib, name)(*args)
        theirs = self.emu.call(F[name], *(args if emu_args is None else emu_args))
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
        party = ctypes.string_at(ctypes.addressof(ctypes.c_uint8.in_dll(self.lib, "ai_party")), 8)
        mine_party = struct.unpack("<4h", party)
        x, y, m, d = (s16(struct.unpack("<H", self.emu.read(a, 2))[0]) for a in (0x7F8D4, 0x7F8D0, 0x7F8D2, 0x7F8D6))
        assert mine_party == (x, y, m, d), (where, "party")
        assert ctypes.c_uint16.in_dll(self.lib, "search_direction_bits").value == \
            struct.unpack("<H", self.emu.read(G_DIRECTION_BITS, 2))[0], (where, "direction bits")
        assert ctypes.c_int16.in_dll(self.lib, "party").value == s16(struct.unpack("<H", self.emu.read(0x72244, 2))[0]), \
            (where, "current map")
        mine, theirs = self.sc.active_groups()
        for i in range(0, len(mine), 0x22):
            assert mine[i:i + 0x22].hex(" ") == theirs[i:i + 0x22].hex(" "), (where, "active group", i // 0x22)
        assert ctypes.c_uint32.in_dll(self.lib, "random_seed").value == self.emu.read_u32(G_RANDOM_SEED), (where, "seed")
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


LINE_TESTS = ("creature_square_is_obstacle", "creature_square_blocks_line", "creature_square_blocks_line_to_target",
              "creature_square_blocks_line_except_self")


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
            mine = lib.creature_trace_line(x, y, tx, ty, fn)
            theirs = ai.emu.call(F["creature_trace_line"], x, y, tx, ty, F[name])
            assert mine == s16(theirs), (g, name, tx, ty)
            tested += mine > 1
            ai.check((g, "line", tx, ty))
        ai.end()
    assert blocked > 50 and tested > 50


def test_square_cache(ai):
    # The square cache of the AI: the first object and the first group of squares around the groups.
    lib = ai.lib
    for f in ("creature_ai_cached_first_object", "creature_ai_cached_first_group"):
        getattr(lib, f).restype = ctypes.c_uint16
    for g, x, y, m in ai.groups():
        ai.begin(g, x, y, m)
        for sx, sy in squares_near(lib, m, x, y, 2):
            ai.call("creature_ai_cache_square", sx, sy)
            for f in ("creature_ai_cached_first_group", "creature_ai_cached_first_object"):
                mine, theirs = ai.call(f)
                assert mine == theirs & 0xFFFF, (g, f, sx, sy)
            ai.check((g, sx, sy))
        ai.end()


def random_command(rng, test):
    kind = rng.choice((0, 0, 1, 3, 4, 5, 6, 7, 8, rng.randrange(32)))
    return kind | rng.choice((0, 0x80)) | rng.choice((0, 0, 0x40)) | rng.choice((0, 0, 0x20))


def places(ai, rng, per_group=3):
    """(group, x, y, map): every active group on its square, and on random open squares of its map."""
    lib = ai.lib
    out = []
    for g, x, y, m in ai.groups():
        out.append((g, x, y, m))
        w, h = lib.dungeon_map_width(m), lib.dungeon_map_height(m)
        open_squares = [(sx, sy) for sx in range(w) for sy in range(h) if lib.dungeon_square(m, sx, sy) >> 5 in (1, 2, 3, 4, 5)]
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
            mine, theirs = ai.call("creature_ai_try_move", command, x, y, tx, ty, d)
            assert mine == theirs, (g, n, command, tx, ty, d)
            moved += mine
            acted += mine and command & 0x80
            ai.check((g, n, command, tx, ty, d))
        ai.end()
    assert moved > 100 and acted > 30
