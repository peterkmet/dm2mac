"""Differential tests of the champion rules of Step 15 on start.DAT: the same calls in the C port and in the original
code in dm2tools.emu, then the same results, random seed, champions, and dungeon."""

import ctypes
import random
import struct

import pytest

from test_interface import CHAMPION_SIZE, G_CHAMPIONS, champion_bytes, champion_records, sync_champions
from test_move import Scenario, gdat, start  # noqa: F401 (fixtures)

G_RANDOM_SEED, G_PARTY_SLEEPING, G_REGEN_COUNTER, G_CHAMPION_COUNT = 0x70518, 0x7F234, 0x7FFF4, 0x7F276
G_THING_DATA = 0x7F288
THING_SIZES = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
CHAMP_TAKE_DAMAGE, CHAMP_DAMAGE_PARTY, CHAMP_POISON, CHAMP_CURE_POISON = 0x4722A, 0x4766B, 0x474FC, 0x475D3
CHAMP_GET_STRENGTH, CHAMP_GET_DEXTERITY, CHAMP_TEST_LUCK = 0x46A19, 0x46968, 0x46786
CHAMP_APPLY_PENDING_DAMAGE, CHAMP_APPLY_TIME_EFFECTS, ITEMS_BURN_HELD_LIGHTS = 0x47113, 0x47CC3, 0x39044
CH = {"dir": 0x1C, "cell": 0x1D, "poisoned": 0x1F, "last_command": 0x20, "damage_event": 0x2E, "wounds": 0x34,
      "health": 0x36, "stamina": 0x3A, "mana": 0x3E, "defense": 0x42, "food": 0x44, "stats": 0x4A, "slots": 0xC3,
      "aura": 0x102, "aura_value": 0x103}
WEAPON, CLOTHING = 5, 6
DUN_UNLINK_THING_FROM_SQUARE = 0x1D542


class Rules:
    """start.DAT with four champions (copies of the first) in the port and in the original, and a pool of new
    weapons and clothes for their hands."""

    def __init__(self, scenario, lib, rng):
        self.s, self.lib, self.rng, self.emu = scenario, lib, rng, scenario.emu
        lib.dungeon_unused_thing.restype = ctypes.c_uint16
        lib.dungeon_thing_record.restype = ctypes.c_void_p
        self.first = bytes(champion_records(lib)[:CHAMPION_SIZE])
        self.pool = [lib.dungeon_unused_thing(t) for t in (WEAPON,) * 4 + (CLOTHING,) * 4]
        for t in self.pool:
            self.set_word(t, 0, 0xFFFE)
        scenario.game.champion_count = 4
        self.emu.write(G_CHAMPION_COUNT, struct.pack("<H", 4))
        lib.champ_test_luck.argtypes = [ctypes.c_void_p, ctypes.c_int]

    def record(self, thing):
        """(C address, original address) of a thing record."""
        kind = thing >> 10 & 15
        return (self.lib.dungeon_thing_record(thing),
                self.emu.read_u32(G_THING_DATA + 4 * kind) + THING_SIZES[kind] * (thing & 0x3FF))

    def set_word(self, thing, offset, value):
        mine, theirs = self.record(thing)
        ctypes.c_uint16.from_address(mine + offset).value = value
        self.emu.write(theirs + offset, struct.pack("<H", value))

    def randomize(self):
        """Random champions: cells, health, stamina, wounds, statistics, auras, parries, food, water, hand items."""
        rng, lib = self.rng, self.lib
        self.pick_up_pool()
        for t in self.pool:
            # a weapon kind (2: a torch), charges, and the do-not-discard bit
            kind = rng.choice((2, 2, rng.randrange(0x30)))
            self.set_word(t, 2, kind | 0x80 * rng.randrange(2) | rng.choice((0, 0x0400, 0x0400 * rng.randrange(16))))
        hands = rng.sample(self.pool, 8)
        for champ, cell in enumerate(rng.sample(range(4), 4)):
            champion_bytes(lib, champ, 0, self.first)
            maxima = [rng.randrange(20, 400) for _ in range(3)]
            values = {"cell": [cell], "dir": [rng.randrange(4)], "last_command": [rng.choice((0, 1, 2)) for _ in range(2)],
                      "health": struct.pack("<hh", rng.randrange(1, maxima[0] + 1), maxima[0]),
                      "stamina": struct.pack("<hh", rng.randrange(0, maxima[1] + 1), maxima[1]),
                      "mana": struct.pack("<hh", rng.randrange(0, maxima[2] + 40), maxima[2]),
                      "defense": [rng.randrange(256) for _ in range(2)],
                      "food": struct.pack("<hh", rng.randrange(-1100, 2100), rng.randrange(-1100, 2100)),
                      "wounds": struct.pack("<H", rng.randrange(0x40) if rng.random() < 0.3 else 0),
                      "stats": [v for _ in range(7) for v in (rng.randrange(10, 230), rng.randrange(20, 230))],
                      "aura": [rng.randrange(7)], "aura_value": struct.pack("<h", rng.choice((0, rng.randrange(120)))),
                      "slots": struct.pack("<HH", *(rng.choice((hands[2 * champ + k], 0xFFFF)) for k in range(2)))
                      + b"\xff\xff" * 28,  # the copies must not share the items of the first champion
                      "damage_event": b"\xff\xff", "poisoned": [0]}
            for name, data in values.items():
                champion_bytes(lib, champ, CH[name], data)
        sync_champions(lib, self.s)
        sleeping = int(rng.random() < 0.2)
        ctypes.c_int.in_dll(lib, "party_sleeping").value = sleeping
        self.emu.write(G_PARTY_SLEEPING, struct.pack("<I", sleeping))

    def pick_up_pool(self):
        """Takes the pool items that dead champions dropped off the party square in both."""
        lib, game = self.lib, self.s.game
        lib.dungeon_square_first_thing.restype = ctypes.c_uint16
        lib.dungeon_next_thing.restype = ctypes.c_uint16
        on_square = []
        t = lib.dungeon_square_first_thing(game.party_map, game.party_x, game.party_y)
        while t != 0xFFFE:
            if t & 0x3FFF in self.pool:
                on_square.append(t)
            t = lib.dungeon_next_thing(t)
        for t in on_square:
            lib.dungeon_unlink_thing_from_square(t, game.party_map, game.party_x, game.party_y)
            self.emu.call(DUN_UNLINK_THING_FROM_SQUARE, t, 0, game.party_x, game.party_y)
            self.set_word(t & 0x3FFF, 0, 0xFFFE)

    def alive(self):
        records = bytes(champion_records(self.lib))
        return sum(1 for c in range(4) if records[c * CHAMPION_SIZE + CH["health"]:c * CHAMPION_SIZE + CH["health"] + 2] != b"\0\0")

    def assert_same(self, where):
        assert ctypes.c_uint32.in_dll(self.lib, "random_seed").value == self.emu.read_u32(G_RANDOM_SEED), where
        regen = ctypes.c_uint16.in_dll(self.lib, "champ_regen_counter").value
        assert regen == struct.unpack("<H", self.emu.read(G_REGEN_COUNTER, 2))[0], where
        self.s.assert_same(where)


@pytest.fixture
def rules(lib, gdat, start, tmp_path):  # noqa: F811
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return Rules(Scenario(lib, gdat, start, tmp_path), lib, random.Random(151))


def test_damage_rules_match_original(rules, lib):
    # Damage of each attack type and wound mask, party damage, poison and its cure, strength, dexterity, luck, the
    # pending damage at the end of a step (with deaths), the time effects, and burning torches, on random champions.
    rng, emu, scenario = rules.rng, rules.emu, rules.s
    records = ctypes.addressof(champion_records(lib))
    kills = hits = 0
    rules.randomize()
    for n in range(800):
        if n % 8 == 0:
            rules.randomize()
        champ = rng.randrange(4)
        kind = rng.randrange(10)
        mine = theirs = 0
        if kind < 3:
            args = (champ, rng.randrange(1, 150), rng.randrange(0x40), rng.randrange(10) | rng.choice((0, 0x8000)))
            mine, theirs = lib.champ_take_damage(*args), emu.call(CHAMP_TAKE_DAMAGE, *args)
            hits += ctypes.c_int16(mine).value != 0
        elif kind == 3:
            args = (rng.randrange(0, 200), rng.randrange(0x40), rng.randrange(10))
            mine, theirs = lib.champ_damage_party(*args), emu.call(CHAMP_DAMAGE_PARTY, *args)
        elif kind == 4:
            args = (champ, rng.choice((rng.randrange(1, 400), rng.randrange(1, 3500))))
            lib.champ_poison(*args)
            emu.call(CHAMP_POISON, *args)
            if rng.random() < 0.5:
                args = (champ, rng.randrange(1, 600))
                lib.champ_cure_poison(*args)
                emu.call(CHAMP_CURE_POISON, *args)
        elif kind == 5:
            args = (champ, rng.randrange(2), rng.randrange(-1, 20))
            mine, theirs = lib.champ_get_strength(*args), emu.call(CHAMP_GET_STRENGTH, *args)
        elif kind == 6:
            mine, theirs = lib.champ_get_dexterity(champ), emu.call(CHAMP_GET_DEXTERITY, champ)
        elif kind == 7:
            chance = rng.randrange(150)
            mine = lib.champ_test_luck(records + champ * CHAMPION_SIZE, chance)
            theirs = emu.call(CHAMP_TEST_LUCK, G_CHAMPIONS + champ * CHAMPION_SIZE, chance) & 0xFF
        elif kind == 8:
            alive = rules.alive()
            lib.champ_apply_pending_damage()
            emu.call(CHAMP_APPLY_PENDING_DAMAGE)
            kills += alive - rules.alive()
            if not rules.alive():
                rules.assert_same((n, kind))
                rules.randomize()
        else:
            step = rng.randrange(1, 300)
            scenario.set_time(scenario.time() + step if rng.random() < 0.5 else (scenario.time() | 0xFF) + 1)
            lib.champ_apply_time_effects()
            emu.call(CHAMP_APPLY_TIME_EFFECTS)
            lib.items_burn_held_lights()
            emu.call(ITEMS_BURN_HELD_LIGHTS)
        assert ctypes.c_int16(mine).value == ctypes.c_int16(theirs).value, (n, kind)
        rules.assert_same((n, kind))
        if n % 100 == 99:
            scenario.assert_equal(n)
    assert hits > 50 and kills > 3, (hits, kills)
