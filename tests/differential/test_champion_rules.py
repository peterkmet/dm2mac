"""Differential tests of the champion rules of Step 15 on start.DAT: the same calls in the C port and in the original
code in dm2tools.emu, then the same results, random seed, champions, and dungeon."""

import ctypes
import itertools
import random
import struct

import pytest

from dm2test import constants as C
from dm2test.interface import CHAMPION_SIZE, G_CHAMPIONS, champion_bytes, champion_records, sync_champions
from dm2test.scenario import Scenario, gdat, start  # noqa: F401 (fixtures)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

G_RANDOM_SEED, G_PARTY_SLEEPING, G_REGEN_COUNTER, G_CHAMPION_COUNT = 0x70518, 0x7F234, 0x7FFF4, 0x7F276
G_THING_DATA = 0x7F288
THING_SIZES = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
CHAMP_TAKE_DAMAGE, CHAMP_DAMAGE_PARTY, CHAMP_POISON, CHAMP_CURE_POISON = 0x4722A, 0x4766B, 0x474FC, 0x475D3
CHAMP_GET_STRENGTH, CHAMP_GET_DEXTERITY, CHAMP_TEST_LUCK = 0x46A19, 0x46968, 0x46786
CHAMP_APPLY_PENDING_DAMAGE, CHAMP_APPLY_TIME_EFFECTS, ITEMS_BURN_HELD_LIGHTS = 0x47113, 0x47CC3, 0x39044
CH = {"dir": 0x1C, "cell": 0x1D, "poisoned": 0x1F, "last_command": 0x20, "damage_event": 0x2E, "wounds": 0x34,
      "health": 0x36, "stamina": 0x3A, "mana": 0x3E, "defense": 0x42, "food": 0x44, "stats": 0x4A, "slots": 0xC3,
      "aura": 0x102, "aura_value": 0x103, "experience": 0x5F}
WEAPON, CLOTHING = C.THING_WEAPON, C.THING_ARMOUR
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
            self.set_word(t, 0, C.THING_ENDOFLIST)
            self.sync_record(t)
        scenario.game.champion_count = C.CHAMPION_MAX
        self.emu.write(G_CHAMPION_COUNT, struct.pack("<H", C.CHAMPION_MAX))
        lib.champ_test_luck.argtypes = [ctypes.c_void_p, ctypes.c_int]

    def record(self, thing):
        """(C address, original address) of a thing record."""
        kind = thing >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK
        return (self.lib.dungeon_thing_record(thing),
                self.emu.read_u32(G_THING_DATA + 4 * kind) + THING_SIZES[kind] * (thing & C.THING_INDEX_MASK))

    def sync_record(self, thing):
        """Copies the C record of thing into the original."""
        mine, theirs = self.record(thing)
        self.emu.write(theirs, ctypes.string_at(mine, THING_SIZES[thing >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK]))

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
            kind = rng.choice((2, 2, rng.randrange(0x40)))
            self.set_word(t, 2, kind | 0x80 * rng.randrange(2) | rng.choice((0, 0x0400, 0x0400 * rng.randrange(16))))
        hands = rng.sample(self.pool, 8)
        for champ, cell in enumerate(rng.sample(range(4), 4)):
            champion_bytes(lib, champ, 0, self.first)
            maxima = [rng.randrange(20, 400) for _ in range(3)]
            values = {"cell": [cell], "dir": [rng.randrange(C.DIR_COUNT)], "last_command": [rng.choice((0, 1, 2)) for _ in range(2)],
                      "health": struct.pack("<hh", rng.randrange(1, maxima[0] + 1), maxima[0]),
                      "stamina": struct.pack("<hh", rng.randrange(0, maxima[1] + 1), maxima[1]),
                      "mana": struct.pack("<hh", rng.randrange(0, maxima[2] + 40), maxima[2]),
                      "defense": [rng.randrange(256) for _ in range(2)],
                      "food": struct.pack("<hh", rng.randrange(-1100, 2100), rng.randrange(-1100, 2100)),
                      "wounds": struct.pack("<H", rng.randrange(0x40) if rng.random() < 0.3 else 0),
                      "stats": [v for _ in range(7) for v in (rng.randrange(10, 230), rng.randrange(20, 230))],
                      "aura": [rng.randrange(7)], "aura_value": struct.pack("<h", rng.choice((0, rng.randrange(120)))),
                      "slots": struct.pack("<HH", *(rng.choice((hands[2 * champ + k], C.THING_NONE)) for k in range(2)))
                      + b"\xff\xff" * 28,  # the copies must not share the items of the first champion
                      "damage_event": b"\xff\xff", "poisoned": [0],
                      "experience": struct.pack("<20I", *(rng.choice((0, rng.randrange(1 << rng.randrange(24)),
                                                                      rng.randrange(1 << 22, 1 << 24)))
                                                         for _ in range(20)))}
            for name, data in values.items():
                champion_bytes(lib, champ, CH[name], data)
        sync_champions(lib, self.s)
        sleeping = int(rng.random() < 0.2)
        ctypes.c_int.in_dll(lib, "g_party_sleeping").value = sleeping
        self.emu.write(G_PARTY_SLEEPING, struct.pack("<I", sleeping))

    def pick_up_pool(self):
        """Takes the pool items that dead champions dropped off the party square in both."""
        lib, game = self.lib, self.s.game
        lib.dungeon_square_first_thing.restype = ctypes.c_uint16
        lib.dungeon_next_thing.restype = ctypes.c_uint16
        on_square = []
        t = lib.dungeon_square_first_thing(game.party_map, game.party_x, game.party_y)
        while t != C.THING_ENDOFLIST:
            if t & C.THING_ID_MASK in self.pool:
                on_square.append(t)
            t = lib.dungeon_next_thing(t)
        for t in on_square:
            lib.dungeon_unlink_thing_from_square(t, game.party_map, game.party_x, game.party_y)
            self.emu.call(DUN_UNLINK_THING_FROM_SQUARE, t, 0, game.party_x, game.party_y)
            self.set_word(t & C.THING_ID_MASK, 0, C.THING_ENDOFLIST)

    def alive(self):
        records = bytes(champion_records(self.lib))
        return sum(1 for c in range(C.CHAMPION_MAX) if records[c * CHAMPION_SIZE + CH["health"]:c * CHAMPION_SIZE + CH["health"] + 2] != b"\0\0")

    def assert_same(self, where):
        assert ctypes.c_uint32.in_dll(self.lib, "g_random_seed").value == self.emu.read_u32(G_RANDOM_SEED), where
        regen = ctypes.c_uint16.in_dll(self.lib, "g_champ_regen_counter").value
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
        champ = rng.randrange(C.CHAMPION_MAX)
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
            lib.champ_burn_held_lights()
            emu.call(ITEMS_BURN_HELD_LIGHTS)
        assert ctypes.c_int16(mine).value == ctypes.c_int16(theirs).value, (n, kind)
        rules.assert_same((n, kind))
        if n % 100 == 99:
            scenario.assert_equal(n)
    assert hits > 50 and kills > 3, (hits, kills)


CHAMP_CONSUME_OBJECT, CHAMP_LIST_ACTIONS, CHAMP_PERFORM_ACTION = 0x39C3F, 0x3F9F5, 0x414A5
CHAMP_USE_ACTION_CHARGE, CHAMP_RELOAD_HAND, CHAMP_PROCESS_REPEATED_ACTIONS = 0x40D39, 0x40AA6, 0x40EE2
G_ACTION_LIST, G_ACTION_THING, G_ACTION_COUNT, G_ACTION_REPEAT = 0x7FB3C, 0x7FB4C, 0x7FB76, 0x7FAD8
G_ACTION_HAND, G_ACTION_RESULT, G_ACTION_RESULT_FLAG, G_ACTION_RESULT_AMMO, G_ACTION_DELAY = (
    0x7FB72, 0x7FB78, 0x7FB6E, 0x7FB74, 0x7FB68)
POTION, MISC = C.THING_POTION, C.THING_MISC
REPEATING_WEAPON = 14  # its third action repeats
# Item kinds (GDAT indices) with actions, by thing type.
ACTION_ITEMS = {WEAPON: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                        29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45],
                CLOTHING: [28, 29, 30, 31, 42, 47, 52], MISC: [1, 29, 30, 31, 32, 34, 43, 45, 63, 64]}
SAVE_WRITE_DEFERRED_THINGS, G_SAVE_DEFERRED_COUNT = 0x34797, 0x75310
G_SAVE_DEFERRED_THINGS, G_SAVE_CONTAINER_REMAP = 0x7F8E4, 0x7F8EC
CONTAINER, PROJECTILE = C.THING_CONTAINER, C.THING_PROJECTILE


def zero_minion_links(rules):
    """The minion of an attack or guard minion item (63, 64) carries the item as its link: the original save writes
    the container order of that link from past the end of its table, the port writes 0 (docs/rules/champions.md).
    While the original writes the links, the table entries past its end are 0; the dict keeps their values for
    restore_minion_links."""
    from unicorn import UC_HOOK_CODE
    emu, lib = rules.emu, rules.lib
    saved = {}

    def hook(*_):
        try:
            things, remap = emu.read_u32(G_SAVE_DEFERRED_THINGS), emu.read_u32(G_SAVE_CONTAINER_REMAP)
            for i in range(struct.unpack("<h", emu.read(G_SAVE_DEFERRED_COUNT, 2))[0]):
                t = struct.unpack("<H", emu.read(things + 2 * i, 2))[0]
                if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK != PROJECTILE:
                    continue
                target = struct.unpack("<H", emu.read(rules.record(t)[1] + 2, 2))[0] & C.THING_INDEX_MASK
                if target >= lib.dungeon_thing_count(CONTAINER):
                    saved.setdefault(remap + 2 * target, emu.read(remap + 2 * target, 2))
                    emu.write(remap + 2 * target, b"\0\0")
        except Exception as e:  # exceptions do not cross the Unicorn callbacks
            emu._fail(f"minion links: {e!r}")

    emu.uc.hook_add(UC_HOOK_CODE, hook, begin=SAVE_WRITE_DEFERRED_THINGS, end=SAVE_WRITE_DEFERRED_THINGS)
    return saved


def restore_minion_links(rules, saved):
    for addr, data in reversed(saved.items()):
        rules.emu.write(addr, data)
    saved.clear()


class ActionState(ctypes.Structure):
    _fields_ = [("champion", ctypes.c_int16), ("hand", ctypes.c_int16), ("mode", ctypes.c_int16),
                ("dirty", ctypes.c_int16), ("redraw_delay", ctypes.c_int16), ("flags", ctypes.c_uint16),
                ("result", ctypes.c_int16), ("result_flag", ctypes.c_int16), ("result_ammo", ctypes.c_uint16)]


class PriceTerms(ctypes.Structure):
    _fields_ = [("creature", ctypes.c_int), ("list", ctypes.c_int), ("buying", ctypes.c_bool),
                ("apply_margin", ctypes.c_bool)]


def assert_same_actions(rules, where):
    """The action list, the action area results, and the repeats of both."""
    lib, emu = rules.lib, rules.emu
    mine = bytes((ctypes.c_uint8 * 64).in_dll(lib, "g_action_repeats"))
    assert mine == emu.read(G_ACTION_REPEAT, 64), where
    count = ctypes.c_int.in_dll(lib, "g_champ_action_count").value
    assert count == struct.unpack("<H", emu.read(G_ACTION_COUNT, 2))[0], where
    actions = bytes((ctypes.c_uint8 * 9).in_dll(lib, "g_champ_actions"))
    theirs = emu.read(G_ACTION_LIST, 12)
    assert all(actions[3 * i:3 * i + 3] == theirs[4 * i:4 * i + 3] for i in range(count)), where
    assert ctypes.c_uint16.in_dll(lib, "g_champ_action_thing").value == struct.unpack("<H", emu.read(G_ACTION_THING, 2))[0]
    a = ActionState.in_dll(lib, "g_action")
    theirs = [struct.unpack("<h", emu.read(addr, 2))[0] for addr in (G_ACTION_RESULT, G_ACTION_DELAY, G_ACTION_RESULT_FLAG)]
    assert [a.result, a.redraw_delay, a.result_flag] == theirs, where
    assert a.result_ammo == struct.unpack("<H", emu.read(G_ACTION_RESULT_AMMO, 2))[0], where


def test_eating_drinking_and_actions_match_original(rules, lib):
    # Potions of all kinds and powers, food, water from flasks, and every action of random items in the hands of
    # random champions at random places: the actions, their charges, the reload of a free hand, and the repeats. Then
    # every action of every weapon kind.
    from dm2test.scenario import squares
    rng, emu, scenario = rules.rng, rules.emu, rules.s
    lib.champ_list_actions.argtypes = [ctypes.c_int, ctypes.c_uint16, ctypes.c_int]
    potions = [lib.dungeon_unused_thing(POTION) for _ in range(4)]
    misc = [lib.dungeon_unused_thing(MISC) for _ in range(4)]
    for t in potions + misc:
        rules.set_word(t, 0, C.THING_ENDOFLIST)
        rules.sync_record(t)
    places = squares(lib, (C.SQUARE_FLOOR,))
    # places that face a creature group, for the melee actions and their repeats
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    facing = []
    for map_, x, y, _ in places:
        for d, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
            fx, fy = x + dx, y + dy
            if 0 <= fx < lib.dungeon_map_width(map_) and 0 <= fy < lib.dungeon_map_height(map_):
                t = lib.dungeon_square_first_thing(map_, fx, fy)
                while t != C.THING_ENDOFLIST:
                    if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == 4:
                        facing.append((map_, x, y, d))
                        break
                    t = lib.dungeon_next_thing(t)
    action_state = ActionState.in_dll(lib, "g_action")
    emu.write(G_ACTION_THING, struct.pack("<H", C.THING_NONE))  # the port starts without an action item
    saved_links, minion_links = zero_minion_links(rules), 0
    performed = consumed = 0
    commands = set()
    repeats = 0

    def hold(champ, hand, t, word):
        rules.set_word(t, 2, word)
        for c in range(C.CHAMPION_MAX):  # the item must not be in two hands
            for k in range(2):
                off = c * CHAMPION_SIZE + CH["slots"] + 2 * k
                if struct.unpack_from("<H", bytes(champion_records(lib)), off)[0] == t:
                    champion_bytes(lib, c, CH["slots"] + 2 * k, b"\xff\xff")
        champion_bytes(lib, champ, CH["slots"] + 2 * hand, struct.pack("<H", t))
        sync_champions(lib, scenario)

    for n in range(1500):
        if n % 10 == 0:
            rules.randomize()
            if rng.random() < 0.5:
                place = rng.choice(facing)
                scenario.place(*place)
                for c in range(C.CHAMPION_MAX):
                    champion_bytes(lib, c, CH["dir"], [place[3]])
                sync_champions(lib, scenario)
            else:
                map_, x, y, _ = rng.choice(places)
                scenario.place(map_, x, y, rng.randrange(C.DIR_COUNT))
        champ = rng.randrange(C.CHAMPION_MAX)
        if not champion_records(lib)[champ * CHAMPION_SIZE + CH["health"]:champ * CHAMPION_SIZE + CH["health"] + 2] != b"\0\0":
            continue
        kind = rng.randrange(4)
        if kind == 0:
            t = rng.choice(potions + misc)
            if t >> C.THING_TYPE_SHIFT == POTION:
                rules.set_word(t, 2, rng.choice((rng.randrange(6, 16), rng.randrange(0x80))) << C.POTION_TYPE_SHIFT | rng.randrange(0x100))
            else:
                rules.set_word(t, 2, rng.choice((1, 8, 0x0b, 0x1d, 0x3a, rng.randrange(0x60))) | rng.randrange(4) << C.MISC_CHARGES_SHIFT)
            slot = rng.choice((C.SLOT_READY_HAND, C.SLOT_ACTION_HAND, C.SLOT_POUCH_FIRST))
            champion_bytes(lib, champ, CH["slots"] + 2 * slot, struct.pack("<H", t))
            sync_champions(lib, scenario)
            lib.champ_consume_object(champ, t, slot)
            emu.call(CHAMP_CONSUME_OBJECT, champ, t, slot)
            consumed += 1
        elif kind == 1:
            hand = rng.randrange(C.SLOT_HAND_COUNT)
            if rng.random() < 0.7:  # an item with actions: a weapon, clothing, or a miscellaneous item
                kind_, t = rng.choice(((WEAPON, rng.choice(rules.pool[:4])), (CLOTHING, rng.choice(rules.pool[4:])),
                                       (MISC, rng.choice(misc))))
                index = rng.choice(ACTION_ITEMS[kind_])
                if kind_ == WEAPON and rng.random() < 0.6:
                    index = REPEATING_WEAPON
                charges = rng.randrange(4) << C.MISC_CHARGES_SHIFT if kind_ == MISC else rng.randrange(16) << (C.WEAPON_CHARGES_SHIFT if kind_ == WEAPON else C.ARMOUR_CHARGES_SHIFT)
                hold(champ, hand, t, index | charges)
            action_state.hand = hand
            emu.write(G_ACTION_HAND, struct.pack("<h", hand))
            thing = struct.unpack_from("<H", bytes(champion_records(lib)), champ * CHAMPION_SIZE + CH["slots"] + 2 * hand)[0]
            mine = lib.champ_list_actions(champ, thing, hand)
            theirs = emu.call(CHAMP_LIST_ACTIONS, champ, thing, hand)
            assert mine == theirs, n
            count = ctypes.c_int.in_dll(lib, "g_champ_action_count").value
            if count:
                index = rng.randrange(count) | rng.choice((0, 0, 0x8000))
                mine = lib.champ_perform_action(champ, index)
                theirs = emu.call(CHAMP_PERFORM_ACTION, champ, index)
                assert mine == theirs, n
                performed += 1
                commands.add(bytes(champion_records(lib))[champ * CHAMPION_SIZE + CH["last_command"] + hand])
                if rng.random() < 0.5:
                    assert lib.champ_use_action_charge(champ) == emu.call(CHAMP_USE_ACTION_CHARGE, champ), n
                for _ in range(rng.choice((0, 0, 2, 3))):  # the repeats of the next game steps
                    lib.champ_process_repeated_actions()
                    emu.call(CHAMP_PROCESS_REPEATED_ACTIONS)
                    rules.assert_same((n, "repeat"))
                    repeats += any(r for r in (ctypes.c_uint16 * 32).in_dll(lib, "g_action_repeats")[1::4])
        elif kind == 2:
            hand = rng.randrange(3)
            lib.champ_reload_hand(champ, hand)
            emu.call(CHAMP_RELOAD_HAND, champ, hand)
        else:
            lib.champ_process_repeated_actions()
            emu.call(CHAMP_PROCESS_REPEATED_ACTIONS)
        rules.assert_same((n, kind))
        assert_same_actions(rules, (n, kind))
        if n % 100 == 99:
            scenario.assert_equal(n)
            minion_links += len(saved_links)
            restore_minion_links(rules, saved_links)
    assert performed > 150 and consumed > 150 and len(commands) > 20 and repeats > 2 and minion_links, (
        performed, consumed, sorted(commands), repeats, minion_links)
    exercised, listed = set(), set()
    for index in ACTION_ITEMS[WEAPON]:  # every action of every weapon kind, in the first hand of the first champion
        for action, _ in itertools.product(range(3), range(10)):  # random champions until one lists the action
            if (index, action) in exercised:
                continue
            rules.randomize()
            t = rules.pool[0]
            hold(0, 0, t, index | C.CHARGES_MASK << C.WEAPON_CHARGES_SHIFT)
            action_state.hand = 0
            emu.write(G_ACTION_HAND, struct.pack("<h", 0))
            assert lib.champ_list_actions(0, t, 0) == emu.call(CHAMP_LIST_ACTIONS, 0, t, 0), (index, action)
            count = ctypes.c_int.in_dll(lib, "g_champ_action_count").value
            listed |= {(index, a) for a in range(count)}
            if action < count:
                assert lib.champ_perform_action(0, action) == emu.call(CHAMP_PERFORM_ACTION, 0, action), (index, action)
                exercised.add((index, action))
            rules.assert_same((index, action))
            assert_same_actions(rules, (index, action))
    restore_minion_links(rules, saved_links)
    assert {i for i, _ in exercised} == set(ACTION_ITEMS[WEAPON]) and exercised == listed, sorted(listed - exercised)


MONEY_INIT_COIN_TABLE, ITEM_CREATE_BY_INDEX, MONEY_ADD_COIN_TO_BOX = 0x154E0, 0x1DE8E, 0x1F413
MONEY_TAKE_COIN_FROM_BOX, MONEY_NORMALIZE_COINS, MONEY_SPLIT_AMOUNT = 0x1F50D, 0x29598, 0x15958
SHOP_PARSE_ITEM_CLASS_SET, SHOP_GET_ITEM_VALUE, MONEY_BOX_CLICK_COIN = 0x1538D, 0x15737, 0x42A35
G_ACTION_CHAMPION, G_ACTION_MODE, UI_UPDATE_ACTION_AREA = 0x7FB70, 0x757B8, 0x3FE68
MONEY_BOX = 0xA40F  # the money box on the shelf of the first room (map 22)
ACTION_MODE_MONEY_BOX = C.ACTION_MODE_MONEY_BOX


def test_money_and_prices_match_original(rules, lib):
    # Coins into and out of a money box (also through clicks on its panel with the leader hand), the exchange of
    # coins, the split of an amount into coins, the item class sets of the merchants, and their prices.
    from dm2tools.emu import SCRATCH
    from dm2test.interface import G_LEADER_HAND
    rng, emu, scenario = rules.rng, rules.emu, rules.s
    lib.money_init_coin_table()
    emu.call(MONEY_INIT_COIN_TABLE)
    lib.money_coin_item.restype = ctypes.c_uint16
    coins = [lib.money_coin_item(i) for i in range(lib.money_coin_type_count())]
    lib.item_create_by_index.restype = ctypes.c_uint16
    lib.money_take_coin_from_box.restype = ctypes.c_uint16
    lib.money_add_coin_to_box.argtypes = [ctypes.c_uint16, ctypes.c_uint16]
    lib.money_get_item_value.argtypes = [ctypes.c_uint16, ctypes.POINTER(PriceTerms), ctypes.c_int16]
    buffer, out = SCRATCH + 0xF9000, SCRATCH + 0xF9100
    creatures = [t for t in range(0x100) if lib.gdat_exists(rules.s.gdat, C.CAT_CREATURE, t, 5, 0x10)]
    action_state = ActionState.in_dll(lib, "g_action")
    emu.stub(UI_UPDATE_ACTION_AREA, lambda *_: 0)  # the drawing of the panel (the screen is not mapped here)
    lib.action_reset()
    clicks = taken = 0
    for n in range(600):
        kind = rng.randrange(7)
        if kind == 0:
            coin = rng.choice(coins)
            mine, theirs = lib.item_create_by_index(coin), emu.call(ITEM_CREATE_BY_INDEX, coin) & 0xFFFF
            assert mine == theirs, n
            if mine != C.THING_NONE:
                assert lib.money_add_coin_to_box(MONEY_BOX, mine) == emu.call(MONEY_ADD_COIN_TO_BOX, MONEY_BOX, mine), n
        elif kind == 1:
            t = rng.randrange(-1, len(coins))
            mine = lib.money_take_coin_from_box(MONEY_BOX, t) if t >= 0 else C.THING_NONE
            theirs = emu.call(MONEY_TAKE_COIN_FROM_BOX, MONEY_BOX, t) & 0xFFFF if t >= 0 else C.THING_NONE
            assert mine == theirs, n
            taken += mine != C.THING_NONE
        elif kind == 2:
            check = rng.randrange(2)
            assert lib.money_normalize_coins(check, MONEY_BOX) == emu.call(MONEY_NORMALIZE_COINS, check, MONEY_BOX), n
        elif kind == 3:
            amount, most = rng.randrange(-5, 2000), rng.randrange(1, 18)
            types, count = (ctypes.c_uint8 * 32)(), ctypes.c_int16()
            mine = lib.money_split_amount(amount, most, types, ctypes.byref(count))
            theirs = emu.call(MONEY_SPLIT_AMOUNT, amount, most, buffer, out, 0)
            their_count = struct.unpack("<h", emu.read(out, 2))[0]
            assert (mine, count.value) == (theirs, their_count), n
            assert bytes(types[:count.value]) == emu.read(buffer, their_count), n
        elif kind == 4:
            creature, list_, skip = rng.choice(creatures), rng.randrange(-1, 16), rng.randrange(2)
            bits = (ctypes.c_uint8 * 0x40)()
            mine = lib.money_parse_item_class_set(creature, list_, bits, skip)
            theirs = emu.call(SHOP_PARSE_ITEM_CLASS_SET, creature, list_, buffer, skip)
            assert mine == theirs and (not mine or bytes(bits) == emu.read(buffer, 0x40)), (n, creature, list_)
        elif kind == 5:
            creature, list_ = rng.choice(creatures), rng.randrange(5)
            item = rng.randrange(0x1fd)
            bits = (ctypes.c_uint8 * 0x40)()
            if rng.random() < 0.7 and lib.money_parse_item_class_set(creature, list_ * 3 + rng.choice((0x11, 0x12)), bits, 0):
                listed = [i for i in range(0x1fd) if bits[i >> 3] >> (i & 7) & 1]
                item = rng.choice(listed) if listed else item  # an item of a price class of the merchant
            args = (item, creature, list_, rng.randrange(2), rng.randrange(2), rng.choice((-1, 0, 3, 15)))
            mine = lib.money_get_item_value(args[0], ctypes.byref(PriceTerms(*args[1:5])), args[5])
            theirs = emu.call(SHOP_GET_ITEM_VALUE, *args)
            assert ctypes.c_int16(mine).value == ctypes.c_int16(theirs).value, (n, args)
        else:
            champion_bytes(lib, 0, CH["slots"] + 2, struct.pack("<H", MONEY_BOX))
            sync_champions(lib, scenario)
            action_state.champion, action_state.hand, action_state.mode = 1, 1, ACTION_MODE_MONEY_BOX
            emu.write(G_ACTION_CHAMPION, struct.pack("<hh", 1, 1))
            emu.write(G_ACTION_MODE, struct.pack("<h", ACTION_MODE_MONEY_BOX))
            slot = rng.randrange(6)
            lib.money_box_click_coin(slot)
            emu.call(MONEY_BOX_CLICK_COIN, slot)
            clicks += 1
            action_state.mode = ACTION_MODE_MONEY_BOX
            emu.write(G_ACTION_MODE, struct.pack("<h", ACTION_MODE_MONEY_BOX))
        rules.assert_same((n, kind))
        mine_hand = ctypes.c_uint16.from_address(ctypes.addressof(scenario.game) + 252 + 4 * 263).value
        assert mine_hand == struct.unpack("<H", emu.read(G_LEADER_HAND, 2))[0], n
    assert clicks > 30 and taken > 30
    scenario.assert_equal("end")


ITEM_GET_VALUE = 0x1F8A7


def test_item_value_matches_original(rules, lib):
    # The value of potions of all kinds and powers, of weapons, clothes, and miscellaneous items with charges, and
    # of chests and money boxes with items and stacks of coins inside.
    rng, emu = rules.rng, rules.emu
    lib.money_init_coin_table()
    emu.call(MONEY_INIT_COIN_TABLE)
    lib.money_coin_item.restype = ctypes.c_uint16
    lib.item_get_value.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    coins = [lib.money_coin_item(i) & 0x7F for i in range(lib.money_coin_type_count())]
    box_word = lib.thing_word(MONEY_BOX, 4)
    potions, misc = ([lib.dungeon_unused_thing(kind) for _ in range(4)] for kind in (POTION, MISC))
    box = lib.dungeon_unused_thing(CONTAINER)
    for t in potions + misc + [box]:
        rules.set_word(t, 0, C.THING_ENDOFLIST)
    rules.set_word(box, 2, C.THING_ENDOFLIST)
    items = rules.pool + potions + misc
    valued = boxes = 0
    for n in range(1500):
        for t in rules.pool:
            charges = rng.randrange(16) << (C.WEAPON_CHARGES_SHIFT if t >> C.THING_TYPE_SHIFT == WEAPON else C.ARMOUR_CHARGES_SHIFT)
            rules.set_word(t, 2, rng.randrange(0x80) | charges)
        for t in potions:
            rules.set_word(t, 2, rng.randrange(0x80) << 8 | rng.choice((0, 1, 0xFF, rng.randrange(0x100))))
        for t in misc:
            index = rng.choice(coins) if rng.random() < 0.5 else rng.randrange(0x80)
            rules.set_word(t, 2, index | rng.randrange(0x40) << 8 | rng.randrange(4) << C.MISC_CHARGES_SHIFT)
        contents = rng.sample(items, rng.randrange(len(items)))
        for t, after in zip(contents, contents[1:] + [C.THING_ENDOFLIST]):
            rules.set_word(t, 0, after)
        rules.set_word(box, 2, contents[0] if contents else C.THING_ENDOFLIST)
        rules.set_word(box, 4, rng.choice((box_word, box_word, rng.randrange(8) << 13 | rng.choice((0, 2, 4)))))
        boxes += lib.item_is_moneybox(box) != 0
        for t in items + [box]:
            mine, theirs = lib.item_get_value(t), emu.call(ITEM_GET_VALUE, t) & 0xFFFF
            assert mine == theirs, (n, hex(t), [hex(lib.thing_word(t, w)) for w in (2, 4)])
            valued += mine > 0
        for t in contents:
            rules.set_word(t, 0, C.THING_ENDOFLIST)
    assert valued > 1000 and boxes > 300, (valued, boxes)


G_MAGIC_LIGHT = 0x7FFEC  # 6 bytes: magic light, invisibility, action effect, haste, ...
CHAMPIONS_OFFSET, MAGIC_LIGHT_OFFSET = 252, 252 + 4 * CHAMPION_SIZE + 2  # offsetof(Game, ...) in save.h
EVENT_ITEM_EFFECT, EVENT_MAGIC_LIGHT, EVENT_INVISIBILITY, EVENT_AURA, EVENT_POISON = (
    C.EVENT_ITEM_EFFECT, C.EVENT_MAGIC_LIGHT, C.EVENT_INVISIBILITY, C.EVENT_AURA, C.EVENT_POISON)
POTION, MISC = C.THING_POTION, C.THING_MISC


@pytest.mark.parametrize("dead", [False, True])
def test_load_restores_timed_effects_like_original(lib, gdat, start, tmp_path, dead):  # noqa: F811
    # A save with magic light events (powers in and out of the table), invisibility, an aura on the first champion,
    # poison, and the timed effects of eaten items (34 and 50 have skill modifiers that count when consumed): the
    # load restores the light, the invisibility, the aura value (not on a dead champion), the poison, and the item
    # effects, in the C port and in the original.
    from dm2test.save import Event, Game, original_load
    path, bak = tmp_path / "SKSAVE0.DAT", tmp_path / "SKSAVE0.BAK"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    lib.draw_init(gdat)
    game = Game.in_dll(lib, "g_game")
    champions = ctypes.addressof(game) + CHAMPIONS_OFFSET
    if dead:
        ctypes.c_int16.from_address(champions + CH["health"]).value = 0
    before = ctypes.string_at(champions, 4 * CHAMPION_SIZE)
    events = [(EVENT_MAGIC_LIGHT, 0, struct.pack("<h", v)) for v in (3, -5, 15, -15, 16, 0)]
    events += [(EVENT_INVISIBILITY, 0, b""), (EVENT_INVISIBILITY, 0, b""), (EVENT_AURA, 1, struct.pack("<h", 7)),
               (EVENT_AURA, 2, struct.pack("<h", 9)), (EVENT_POISON, 0, struct.pack("<h", 30))]
    events += [(EVENT_ITEM_EFFECT, 0, bytes([kind, 0, index]))
               for kind, index in ((MISC, 34), (MISC, 50), (MISC, 3), (POTION, 0), (POTION, 5))]
    for n, (type_, priority, data) in enumerate(events):
        event = Event(game.game_time + 50 + n | game.party_map << 24, type_, priority,
                      (ctypes.c_uint8 * 6)(*data.ljust(6, b"\0")))
        lib.timeline_add_event(ctypes.byref(event))
    assert lib.save_write(str(path).encode(), str(bak).encode(), b"GAME 0", gdat) == 0
    saved = path.read_bytes()
    assert lib.save_load(str(path).encode(), gdat) == 0
    mine = ctypes.string_at(champions, 4 * CHAMPION_SIZE)
    light = ctypes.string_at(ctypes.addressof(game) + MAGIC_LIGHT_OFFSET, 6)
    emu = original_load(lib, gdat, saved)
    assert light == emu.read(G_MAGIC_LIGHT, 6)
    assert mine == emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE)
    assert mine != before and light[2] == 2 and struct.unpack("<h", light[:2])[0] != 0
    aura = struct.unpack_from("<h", mine, CH["aura_value"])[0] - struct.unpack_from("<h", before, CH["aura_value"])[0]
    assert aura == (0 if dead else 7)
