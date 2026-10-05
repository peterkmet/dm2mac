"""Step 16 magic and minions against the original code in dm2tools.emu: runes, spells, magic light, auras, and the
minion map panel."""

import ctypes
import random
import struct

import pytest

from conftest import ROOT
from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image
from test_creature import CreatureScenario
from test_interface import (CHAMPION_SIZE, GAME_LEADER_HAND, ActionArea, Game, c_screen, champion_bytes, champion_records,
                            diff, draw_game_screen, open_hand, set_action, sync_champions, update_action_area)
from test_interface import gdat, original, port  # noqa: F401 (fixtures)
from test_move import start  # noqa: F401 (fixture)

# Original functions.
MAGIC_FIND_SPELL, MAGIC_CAST_SPELL, TIMELINE_ADD_EVENT = 0x421FD, 0x422F5, 0x56390
# Rules of Step 15 and of other Step 16 parts that the spells call: the tests record their arguments.
CHAMP_ADD_EXPERIENCE, CHAMP_ADD_ACTION_DELAY, CHAMP_ADD_OBJECT_TO_SLOT = 0x462FA, 0x40A0A, 0x45FCB
CHAMP_PUT_OBJECT_IN_LEADER_HAND, CHAMP_LAUNCH_PROJECTILE, LIGHT_UPDATE_PARTY_LIGHT = 0x45DEF, 0x47773, 0x389C2
# Original globals.
G_SPELL_TABLE, SPELL_COUNT, SPELL_SIZE = 0x757FE, 34, 8
G_CHAMPIONS, G_CHAMPION_COUNT, G_MAGIC_LIGHT, G_LEADER_HAND = 0x7FBD0, 0x7F276, 0x7FFEC, 0x7FBB4
GAME_MAGIC = GAME_LEADER_HAND + 2
HOOK_ADD_EXPERIENCE, HOOK_ADD_ACTION_DELAY, HOOK_ADD_OBJECT_TO_SLOT, HOOK_PUT_IN_LEADER_HAND, HOOK_LAUNCH_PROJECTILE = range(5)
# Champion record offsets.
CH_MANA, CH_MAX_MANA, CH_STATS, CH_EXPERIENCE, CH_SLOTS, CH_HEALTH = 0x3E, 0x40, 0x4A, 0x5F, 0xC3, 0x36
STAT_WISDOM = 3
RUNE_FIRST = 0x60


class HookCall(ctypes.Structure):
    _fields_ = [("kind", ctypes.c_int32), ("args", ctypes.c_int32 * 5)]


def c_hook_calls(lib):
    count = ctypes.c_int.in_dll(lib, "hook_call_count")
    calls = (HookCall * 64).in_dll(lib, "hook_calls")
    out = [(c.kind,) + tuple(c.args) for c in calls[:count.value]]
    count.value = 0
    return out


@pytest.fixture(scope="module")
def emu():
    path = ROOT / "game" / "SKULL.EXE"
    if not path.exists():
        pytest.skip("game/SKULL.EXE missing")
    return Emu(load_image(path))


def c_spell_bytes(lib, runes):
    lib.magic_find_spell.restype = ctypes.c_void_p
    lib.magic_find_spell.argtypes = [ctypes.c_char_p]
    p = lib.magic_find_spell(bytes(runes) + b"\0")
    return ctypes.string_at(p, SPELL_SIZE) if p else None


def original_spell_bytes(emu, runes):
    emu.write(SCRATCH, bytes(runes) + b"\0")
    p = emu.call(MAGIC_FIND_SPELL, SCRATCH)
    return emu.read(p, SPELL_SIZE) if p else None


def test_find_spell_matches_original_for_all_rune_sets(lib, emu):
    # Every rune sequence of the four rune sets (power rune, then up to 3 runes, each may end the runes), then
    # random sequences of any runes.
    sets = [[0] + list(range(RUNE_FIRST + 6 * k, RUNE_FIRST + 6 * k + 6)) for k in range(4)]
    cases = [[a, b, c, d] for a in sets[0] for b in sets[1] for c in sets[2] for d in sets[3]]
    rng = random.Random(16)
    cases += [[rng.choice([0] + list(range(RUNE_FIRST, RUNE_FIRST + 24))) for _ in range(4)] for _ in range(2000)]
    found = set()
    for runes in cases:
        mine, theirs = c_spell_bytes(lib, runes), original_spell_bytes(emu, runes)
        assert mine == theirs, runes
        if theirs:
            found.add(theirs)
    assert len(found) == SPELL_COUNT
    assert found == {emu.read(G_SPELL_TABLE + i * SPELL_SIZE, SPELL_SIZE) for i in range(SPELL_COUNT)}


def int16(v):
    return ctypes.c_int16(v).value


def clear_magic_event_bytes(emu):
    """The original leaves the data bytes after the word of the light, invisibility, and aura events uninitialized."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX
    keep = {0x46: 2, 0x47: 0, 0x48: 2}

    def hook(uc, *_):
        event = uc.reg_read(UC_X86_REG_EAX)
        n = keep.get(emu.read(event + 4, 1)[0])
        if n is not None:
            emu.write(event + 6 + n, bytes(6 - n))

    emu.uc.hook_add(UC_HOOK_CODE, hook, begin=TIMELINE_ADD_EVENT, end=TIMELINE_ADD_EVENT)


class MagicScenario(CreatureScenario):
    """start.DAT with four champions; the Step 15 rules and the projectiles of the spells record their arguments."""

    def __init__(self, lib, gdat, start, tmp_path):
        super().__init__(lib, gdat, start, tmp_path)
        emu = self.emu
        self.calls = []
        rec = self.calls.append
        emu.stub(CHAMP_ADD_EXPERIENCE, lambda c, s, a, *_: rec((HOOK_ADD_EXPERIENCE, c & 0xFFFF, s & 0xFFFF, a, 0, 0)))
        emu.stub(CHAMP_ADD_ACTION_DELAY, lambda c, t, h, *_: rec((HOOK_ADD_ACTION_DELAY, c & 0xFFFF, t & 0xFFFF, int16(h), 0, 0)))
        emu.stub(CHAMP_ADD_OBJECT_TO_SLOT, lambda c, t, s, *_: rec((HOOK_ADD_OBJECT_TO_SLOT, c & 0xFFFF, t & 0xFFFF, s & 0xFFFF, 0, 0)))
        emu.uc.hook_del(self.stubs.pop(CHAMP_PUT_OBJECT_IN_LEADER_HAND))
        emu.stub(CHAMP_PUT_OBJECT_IN_LEADER_HAND, lambda t, f, *_: rec((HOOK_PUT_IN_LEADER_HAND, t & 0xFFFF, int16(f), 0, 0, 0)))
        emu.stub(CHAMP_LAUNCH_PROJECTILE, lambda c, t, p, a, s, *_: rec(
            (HOOK_LAUNCH_PROJECTILE, (c - G_CHAMPIONS) // CHAMPION_SIZE, t & 0xFFFF, p & 0xFFFF, a & 0xFFFF, s & 0xFFFF)),
            stack_args=1)
        clear_magic_event_bytes(emu)
        c_hook_calls(lib)
        self.game_record = Game.in_dll(lib, "game")
        sync_champions(lib, self)  # the loads differ in the redraw flags, the skill bonuses, and the load (Step 14)

    def magic(self):
        mine = bytes((ctypes.c_uint8 * 6).from_address(ctypes.addressof(self.game_record) + GAME_MAGIC))
        return mine, self.emu.read(G_MAGIC_LIGHT, 6)

    def set_magic(self, data):
        (ctypes.c_uint8 * 6).from_address(ctypes.addressof(self.game_record) + GAME_MAGIC)[:] = list(data)
        self.emu.write(G_MAGIC_LIGHT, bytes(data))

    def assert_magic_state(self, where):
        """Champions, the party spells, the leader hand, the hook calls, then the creature and dungeon state."""
        mine = bytes(champion_records(self.lib))
        theirs = self.emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE)
        for i in range(4):
            a, b = mine[i * CHAMPION_SIZE:(i + 1) * CHAMPION_SIZE], theirs[i * CHAMPION_SIZE:(i + 1) * CHAMPION_SIZE]
            assert a.hex() == b.hex(), (where, "champion", i)
        m, t = self.magic()
        assert m == t, (where, "magic")
        leader_hand = ctypes.c_uint16.from_address(ctypes.addressof(self.game_record) + GAME_LEADER_HAND).value
        assert leader_hand == struct.unpack("<H", self.emu.read(G_LEADER_HAND, 2))[0], where
        assert c_hook_calls(self.lib) == self.calls, where
        self.calls.clear()
        self.assert_same(where)


@pytest.fixture
def magic(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return MagicScenario(lib, gdat, start, tmp_path)


def make_party(scenario, rng):
    """Four copies of the first champion with random wisdom, mana, and skill experience in random cells."""
    lib = scenario.lib
    first = bytes(champion_records(lib)[:CHAMPION_SIZE])
    for champ, cell in enumerate(rng.sample(range(4), 4)):
        champion_bytes(lib, champ, 0, first)
        champion_bytes(lib, champ, 0x1D, [cell])
        champion_bytes(lib, champ, CH_STATS + 2 * STAT_WISDOM, [rng.randrange(20, 120)])
        champion_bytes(lib, champ, CH_MANA, struct.pack("<hh", rng.randrange(0, 200), rng.randrange(20, 300)))
        for skill in range(20):
            champion_bytes(lib, champ, CH_EXPERIENCE + 4 * skill, struct.pack("<I", rng.choice((0, 500, 5000, 50000, 500000))))
    scenario.game_record.champion_count = 4
    scenario.emu.write(G_CHAMPION_COUNT, struct.pack("<H", 4))
    sync_champions(lib, scenario)


def flasks(lib, n):
    """n potion things of the dungeon, turned into empty flasks (GDAT index 0x14)."""
    lib.dungeon_thing_valid.argtypes = [ctypes.c_uint16]
    out = []
    for i in range(lib.dungeon_thing_count(8)):
        t = 8 << 10 | i
        if lib.dungeon_thing_valid(t) and len(out) < n:
            out.append(t)
    return out


def set_flask(scenario, thing):
    lib = scenario.lib
    lib.thing_word.restype = ctypes.c_uint16
    lib.set_thing_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    w = lib.thing_word(thing, 2) & 0x80FF | 0x14 << 8
    lib.set_thing_word(thing, 2, w)
    record = scenario.emu.read_u32(0x7F288 + 4 * 8) + (thing & 0x3FF) * 4
    scenario.emu.write(record + 2, struct.pack("<H", w))


def spell_runes(index, power):
    """The runes of spell index with a power rune."""
    key = struct.unpack_from("<I", SPELL_TABLE_BYTES, index * SPELL_SIZE)[0]
    return [RUNE_FIRST + power - 1] + [key >> s & 0xFF for s in (16, 8, 0) if key >> s & 0xFF]


SPELL_TABLE_BYTES = load_image(ROOT / "game" / "SKULL.EXE").data[G_SPELL_TABLE - 0x10000:G_SPELL_TABLE - 0x10000 + SPELL_COUNT * SPELL_SIZE] \
    if (ROOT / "game" / "SKULL.EXE").exists() else bytes(SPELL_COUNT * SPELL_SIZE)


def cast(scenario, champ, index, power):
    """Casts spell index with power in both; returns the two results."""
    lib = scenario.lib
    lib.magic_find_spell.restype = ctypes.c_void_p
    lib.magic_find_spell.argtypes = [ctypes.c_char_p]
    lib.magic_cast_spell.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    spell = lib.magic_find_spell(bytes(spell_runes(index, power)) + b"\0")
    assert spell and ctypes.string_at(spell, SPELL_SIZE) == SPELL_TABLE_BYTES[index * SPELL_SIZE:(index + 1) * SPELL_SIZE]
    mine = lib.magic_cast_spell(champ, spell, power)
    theirs = scenario.emu.call(MAGIC_CAST_SPELL, champ, G_SPELL_TABLE + index * SPELL_SIZE, power) & 0xFFFF
    return mine, theirs


@pytest.mark.parametrize("part", range(4))
def test_every_spell_cast_matches_original(magic, part):
    # Every spell of the spell table with each power by champions with random wisdom, mana, and skills; empty
    # flasks in the hands for the potion spells; the leader hand empty for the item spell. After each cast the
    # champions, the party spells, the timeline, the creatures, the dungeon, the random seed, and the hook
    # arguments must be equal; some timeline steps run the light, invisibility, and aura events.
    rng = random.Random(part)
    make_party(magic, rng)
    lib = magic.lib
    pots = flasks(lib, 8)
    results = set()
    for n, (index, power) in enumerate([(i, p) for i in range(SPELL_COUNT) for p in range(1, 7)][part::4]):
        champ = rng.randrange(4)
        if rng.random() < 0.7:
            pot = rng.choice(pots)
            set_flask(magic, pot)
            champion_bytes(lib, champ, CH_SLOTS + 2 * rng.randrange(2), struct.pack("<H", pot))
            sync_champions(lib, magic)
        mine, theirs = cast(magic, champ, index, power)
        assert mine == theirs, (n, index, power)
        results.add(theirs & 0xF0)
        magic.assert_magic_state((n, index, power))
        for _ in range(rng.randrange(1, 30)):
            magic.step()
        magic.assert_magic_state((n, index, power, "steps"))
    assert results >= {0, 0x10}
    for _ in range(300):
        magic.step()
    magic.assert_magic_state("end")
    magic.assert_equal("end")


MAGIC_ADD_RUNE, MAGIC_REMOVE_LAST_RUNE, MAGIC_CAST_ENTERED_RUNES = 0x42924, 0x429F5, 0x428A2
ITEM_CREATE_BY_INDEX, EXPLOSION_CREATE, UI_ACTION_AREA_CLOSE = 0x1DE8E, 0x16746, 0x3FD17
G_ACTION_CHAMPION, G_ACTION_DELAY, G_ACTION_RESULT, G_ACTION_MODE = 0x7FB70, 0x7FB68, 0x7FB78, 0x757B8
G_TIMELINE_EVENTS = 0x760E8
CH_RUNE_COUNT, CH_RUNES = 0x1E, 0x22


@pytest.fixture(scope="module")
def spell_ui(original):
    """The original interface with the Step 15 rules and the projectiles of the spells recorded."""
    emu = original.emu
    original.calls = calls = []
    rec = calls.append
    emu.stub(CHAMP_ADD_EXPERIENCE, lambda c, s, a, *_: rec((HOOK_ADD_EXPERIENCE, c & 0xFFFF, s & 0xFFFF, a, 0, 0)))
    emu.stub(CHAMP_ADD_ACTION_DELAY, lambda c, t, h, *_: rec((HOOK_ADD_ACTION_DELAY, c & 0xFFFF, t & 0xFFFF, int16(h), 0, 0)))
    emu.stub(CHAMP_ADD_OBJECT_TO_SLOT, lambda c, t, s, *_: rec((HOOK_ADD_OBJECT_TO_SLOT, c & 0xFFFF, t & 0xFFFF, s & 0xFFFF, 0, 0)))
    emu.stub(CHAMP_PUT_OBJECT_IN_LEADER_HAND, lambda t, f, *_: rec((HOOK_PUT_IN_LEADER_HAND, t & 0xFFFF, int16(f), 0, 0, 0)))
    emu.stub(CHAMP_LAUNCH_PROJECTILE, lambda c, t, p, a, s, *_: rec(
        (HOOK_LAUNCH_PROJECTILE, (c - G_CHAMPIONS) // CHAMPION_SIZE, t & 0xFFFF, p & 0xFFFF, a & 0xFFFF, s & 0xFFFF)),
        stack_args=1)
    emu.stub(LIGHT_UPDATE_PARTY_LIGHT, lambda *_: 0)
    emu.stub(ITEM_CREATE_BY_INDEX, lambda *_: 0xFFFF)
    emu.stub(EXPLOSION_CREATE, lambda *_: 0, stack_args=1)
    clear_magic_event_bytes(emu)
    return original


CMD_RUNE_FIRST, CMD_REMOVE_RUNE, CMD_CAST_SPELL = 0x65, 0x6B, 0x6C
COMMAND_EXECUTE, G_COMMAND_STOP = 0x21D6C, 0x7F484


def command(lib, emu, cmd, x=0, y=0):
    """Executes a command in both; returns the two results."""
    from test_move import COMMAND_RECORD, Command
    c = Command(x, y, cmd)
    emu.write(COMMAND_RECORD, bytes(c))
    return lib.command_execute(ctypes.byref(c)), emu.call(COMMAND_EXECUTE, COMMAND_RECORD)


def spell_kind(index):
    return SPELL_TABLE_BYTES[index * SPELL_SIZE + 6] & 0xF


def test_runes_and_casts_match_original(port, spell_ui):
    # The spell panel of the first champion: runes added (with enough mana and without), removed, and cast: unknown
    # rune sets, failed casts, casts without a flask, and successful spells (not the summons, which need active
    # creature groups: test_every_spell_cast_matches_original). The screen, the champion, the action area state,
    # the party spells, the timeline, and the hook arguments must be equal.
    lib, original = port, spell_ui
    emu = original.emu
    draw_game_screen(lib, original)
    action = ActionArea.in_dll(lib, "action")
    rng = random.Random(17)
    c_hook_calls(lib)
    original.calls.clear()
    spells = [i for i in range(SPELL_COUNT) if spell_kind(i) != 4]
    plan, results, added = [], set(), 0
    for n in range(400):
        if not action.champion:
            mana = rng.choice((0, 3, 40, 900))
            champion_bytes(lib, 0, CH_MANA, struct.pack("<h", mana))
            champion_bytes(lib, 0, CH_STATS + 2 * STAT_WISDOM, [rng.randrange(20, 120)])
            for skill in range(20):
                champion_bytes(lib, 0, CH_EXPERIENCE + 4 * skill, struct.pack("<I", rng.choice((0, 5000, 500000))))
            sync_champions(lib, original)
            open_hand(lib, original, 2)
            runes = spell_runes(rng.choice(spells), rng.randrange(1, 7))
            plan = runes if rng.random() < 0.7 else [RUNE_FIRST + 6 * k + rng.randrange(6) for k in range(rng.randrange(1, 5))]
        count = champion_records(lib)[CH_RUNE_COUNT]
        if count < len(plan) and rng.random() < 0.9:
            rune = (plan[count] - RUNE_FIRST) % 6
            if rng.random() < 0.5:
                assert command(lib, emu, CMD_RUNE_FIRST + rune) == (1, 1)
            else:
                lib.magic_add_rune(rune)
                emu.call(MAGIC_ADD_RUNE, rune)
            if champion_records(lib)[CH_RUNE_COUNT] > count:
                added += 1
            else:
                plan = plan[:count]  # not enough mana: cast what is entered
            what = ("add", rune)
        elif count and rng.random() < 0.2:
            if rng.random() < 0.5:
                assert command(lib, emu, CMD_REMOVE_RUNE) == (1, 1)
            else:
                lib.magic_remove_last_rune()
                emu.call(MAGIC_REMOVE_LAST_RUNE)
            what = ("remove",)
        elif count:
            if rng.random() < 0.5:
                mine, theirs = lib.magic_cast_entered_runes(), emu.call(MAGIC_CAST_ENTERED_RUNES)
            else:
                assert command(lib, emu, CMD_CAST_SPELL) == (1, 1)
                mine = ctypes.c_int.in_dll(lib, "input_command_stop").value
                theirs = struct.unpack("<H", emu.read(G_COMMAND_STOP, 2))[0]
            assert mine == theirs, n
            results.add((mine, champion_records(lib)[CH_RUNE_COUNT] > 0))
            what = ("cast", mine)
        else:
            lib.ui_action_area_close()
            emu.call(UI_ACTION_AREA_CLOSE)
            what = ("close",)
        update_action_area(lib, original)
        update_action_area(lib, original, tick=1)
        where = (n, what, plan)
        mine, theirs = c_screen(lib), original.screen()
        assert mine == theirs, (where, diff(mine, theirs, 320))
        assert bytes(champion_records(lib)) == emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE), where
        for field, addr in (("champion", G_ACTION_CHAMPION), ("redraw_delay", G_ACTION_DELAY), ("result", G_ACTION_RESULT),
                            ("mode", G_ACTION_MODE)):
            assert getattr(action, field) == struct.unpack("<h", emu.read(addr, 2))[0], (where, field)
        game = Game.in_dll(lib, "game")
        assert bytes((ctypes.c_uint8 * 6).from_address(ctypes.addressof(game) + GAME_MAGIC)) == emu.read(G_MAGIC_LIGHT, 6), where
        assert c_hook_calls(lib) == original.calls, where
        original.calls.clear()
        import test_save
        tl = test_save.Timeline.in_dll(lib, "timeline")
        events = ctypes.string_at(ctypes.cast(tl.events, ctypes.c_void_p), tl.capacity * 12)
        assert events == emu.read(emu.read_u32(G_TIMELINE_EVENTS), tl.capacity * 12), where
    assert added > 60
    assert results == {(0, False), (1, False), (1, True)}  # success, failure, and a missing flask that keeps the runes


TIMELINE_EVENT_MAGIC_LIGHT = 0x5917D


def test_magic_light_events_match_original(magic):
    # The light and darkness events of every level: the magic light changes and the next event of the fading light.
    import test_save
    from test_move import COMMAND_RECORD
    lib, emu = magic.lib, magic.emu
    lib.timeline_event_magic_light.argtypes = [ctypes.c_void_p]
    rng = random.Random(18)
    for level in range(-15, 16):
        magic.set_magic(struct.pack("<h", rng.randrange(-300, 300)) + bytes(4))
        e = test_save.Event(magic.time() | magic.game.party_map << 24, 0x46, 0, (ctypes.c_uint8 * 6)(*struct.pack("<h", level), 0, 0, 0, 0))
        emu.write(COMMAND_RECORD, bytes(e))
        lib.timeline_event_magic_light(ctypes.byref(e))
        emu.call(TIMELINE_EVENT_MAGIC_LIGHT, COMMAND_RECORD)
        magic.assert_magic_state(level)
    for _ in range(150):
        magic.step()
    magic.assert_magic_state("steps")
    magic.assert_equal("end")


def test_many_casts_match_original(magic):
    # 1500 casts of random spells (no summons) by champions with random wisdom and skills: the cast rolls, the
    # experience, the auras that add up (a big aura makes the next one a quarter), haste, light, and invisibility.
    rng = random.Random(19)
    make_party(magic, rng)
    lib = magic.lib
    spells = [i for i in range(SPELL_COUNT) if spell_kind(i) != 4]
    auras = [i for i in spells if spell_kind(i) == 3]
    seed = ctypes.c_uint32.in_dll(lib, "random_seed")
    big = 0
    for n in range(1500):
        if n % 100 == 0:
            for champ in range(4):
                champion_bytes(lib, champ, CH_STATS + 2 * STAT_WISDOM, [rng.randrange(10, 110)])
                champion_bytes(lib, champ, CH_MANA, struct.pack("<h", rng.randrange(0, 300)))
            sync_champions(lib, magic)
            aura = rng.choice(auras)
        index = aura if rng.random() < 0.5 else rng.choice(spells)
        mine, theirs = cast(magic, rng.randrange(4), index, rng.randrange(1, 7))
        assert mine == theirs, (n, index)
        assert seed.value == magic.emu.read_u32(0x70518), n
        assert c_hook_calls(lib) == magic.calls, n
        magic.calls.clear()
        assert bytes(champion_records(lib)) == magic.emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE), n
        big += any(struct.unpack_from("<h", bytes(champion_records(lib)), i * CHAMPION_SIZE + 0x103)[0] > 50 for i in range(4))
        if n % 10 == 9:
            magic.step()
    assert big > 50
    magic.assert_magic_state("end")


# ---- Minion maps ----

ACTIVE_SIZE, G_ACTIVE_GROUPS = 0x22, 0x7F898
G_THINGS, THING_SIZES = 0x7F288, (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
G_LEVEL_DIGIT_TABLE, DUN_SET_PARTY_LOCATION, G_CUR_MAP, G_PARTY_DIR = 0x7F3D0, 0x1C82D, 0x72244, 0x7F254
G_ACTION_FLAGS, G_ACTION_HAND = 0x7FB5E, 0x7FB72
# MinionMap fields: name -> (address, format).
MINION_GLOBALS = {"marker_state": (0x7FB52, "<h"), "y_offset": (0x7FB54, "<h"), "x_offset": (0x7FB56, "<h"),
                  "below_y": (0x7FB58, "<h"), "below_x": (0x7FB5A, "<h"), "above_map": (0x7FB5C, "<h"),
                  "marker_map": (0x7FB60, "<h"), "below_map": (0x7FB62, "<h"), "above_x": (0x7FB64, "<h"),
                  "above_y": (0x7FB66, "<h"), "marker_x": (0x7FB6A, "<h"), "marker_y": (0x7FB6C, "<h"),
                  "map": (0x7FB7D, "<b"), "x": (0x7FB7E, "<b"), "y": (0x7FB7F, "<b"), "dir": (0x7FB80, "<b"),
                  "mana_divisor": (0x7FB4A, "<h"), "mana_timer": (0x7FB95, "<h")}


class MinionMap(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int16) for n in ("marker_state", "y_offset", "x_offset", "below_y", "below_x", "above_map",
                                              "marker_map", "below_map", "above_x", "above_y", "marker_x", "marker_y")] + [
        (n, ctypes.c_int8) for n in ("map", "x", "y", "dir")] + [
        ("mana_divisor", ctypes.c_int16), ("mana_timer", ctypes.c_int16), ("clip", ctypes.c_int16 * 4)]


def record_address(emu, thing):
    t = thing >> 10 & 15
    return emu.read_u32(G_THINGS + 4 * t) + (thing & 0x3FF) * THING_SIZES[t]


def set_word(lib, emu, thing, offset, value):
    lib.set_thing_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    lib.set_thing_word(thing, offset, value)
    emu.write(record_address(emu, thing) + offset, struct.pack("<H", value))


def word(lib, thing, offset):
    lib.thing_word.restype = ctypes.c_uint16
    lib.thing_word.argtypes = [ctypes.c_uint16, ctypes.c_int]
    return lib.thing_word(thing, offset)


# ui_map_draw_square reads square bytes of the map that dun_get_location_on_map found without a bounds check; the
# C port ignores squares outside that map: (address of the read, address to skip to).
MAP_SQUARE_READS = ((0x44805, 0x44873), (0x448CB, 0x4493A))


@pytest.fixture(scope="module")
def map_ui(spell_ui, lib):
    """The spell interface with a level digit table of zeros (the C port before Step 20), and without the reads of
    square bytes outside a map."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_EIP
    emu = spell_ui.emu
    spell_ui.digits = spell_ui.alloc(64)

    def hook(uc, address, size, skip_to):
        reg = UC_X86_REG_EDI if address == 0x44805 else UC_X86_REG_EAX
        m = ctypes.c_int16(uc.reg_read(reg) & 0xFFFF).value
        ebp = uc.reg_read(UC_X86_REG_EBP)
        x, y = struct.unpack("<h", emu.read(ebp - 0x1E, 2))[0], struct.unpack("<h", emu.read(ebp - 0x22, 2))[0]
        if not (0 <= x < lib.dungeon_map_width(m) and 0 <= y < lib.dungeon_map_height(m)):
            uc.reg_write(UC_X86_REG_EIP, skip_to)

    for at, skip_to in MAP_SQUARE_READS:
        emu.uc.hook_add(UC_HOOK_CODE, hook, skip_to, begin=at, end=at)
    return spell_ui


def place_party(lib, emu, map_, x, y, d):
    from test_move import GameRecord, Party
    Party.in_dll(lib, "party").cur_map = -1
    emu.write(G_CUR_MAP, struct.pack("<h", -1))
    GameRecord.in_dll(lib, "game").party_dir = d
    emu.write(G_PARTY_DIR, struct.pack("<H", d))
    lib.dun_set_party_location(x, y, map_)
    emu.call(DUN_SET_PARTY_LOCATION, x, y, map_)
    emu.write(0x75BFE, bytes([lib.dungeon_map_graphics(map_) >> 4 & 0xF]))  # g_map_gfx_set of game_enter_map


def assert_map_state(lib, original, where):
    emu = original.emu
    mine, theirs = c_screen(lib), original.screen()
    assert mine == theirs, (where, diff(mine, theirs, 320))
    assert bytes(champion_records(lib)) == emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE), where
    action = ActionArea.in_dll(lib, "action")
    for field, addr in (("champion", G_ACTION_CHAMPION), ("redraw_delay", G_ACTION_DELAY), ("mode", G_ACTION_MODE),
                        ("flags", G_ACTION_FLAGS)):
        assert getattr(action, field) & 0xFFFF == struct.unpack("<H", emu.read(addr, 2))[0], (where, field)
    m = MinionMap.in_dll(lib, "minion_map")
    for name, (addr, fmt) in MINION_GLOBALS.items():
        assert getattr(m, name) == struct.unpack(fmt, emu.read(addr, struct.calcsize(fmt)))[0], (where, name)
    assert ctypes.c_uint32.in_dll(lib, "random_seed").value == emu.read_u32(0x70518), where
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    for t in (9, 14):
        n = lib.dungeon_thing_count(t)
        assert ctypes.string_at(lib.dungeon_thing_record(t << 10), n * THING_SIZES[t]) == \
            emu.read(emu.read_u32(G_THINGS + 4 * t), n * THING_SIZES[t]), (where, "things", t)


def squares_of(lib, kinds):
    out = []
    for m in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(m)):
            for y in range(lib.dungeon_map_height(m)):
                if lib.dungeon_square(m, x, y) >> 5 in kinds:
                    out.append((m, x, y))
    return out


def static_groups(lib):
    """(group, map, x, y) of the static creature groups on the squares of the dungeon."""
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.argtypes = [ctypes.c_uint16]
    out = []
    for m in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(m)):
            for y in range(lib.dungeon_map_height(m)):
                t = lib.dungeon_square_first_thing(m, x, y)
                while t != 0xFFFE:
                    if t >> 10 & 15 == 4:
                        record = ctypes.string_at(ctypes.c_void_p(lib.dungeon_thing_record(t)), 16)
                        out.append((t, m, x, y, record[4]))
                    t = lib.dungeon_next_thing(t)
    return out


def leads(lib, m, x, y, delta):
    """True when the dungeon has a map at delta levels from map m at the position of (x, y)."""
    lib.dungeon_location_on_map.argtypes = [ctypes.c_int] * 2 + [ctypes.POINTER(ctypes.c_int)] * 2
    cx, cy = ctypes.c_int(x), ctypes.c_int(y)
    return lib.dungeon_location_on_map(m, delta, ctypes.byref(cx), ctypes.byref(cy)) >= 0


def set_time(lib, emu, t):
    Game.in_dll(lib, "game").game_time = t
    emu.write_u32(0x7F22C, t)


class Pool:
    """Free projectile and explosion records that the test puts at the end of the thing lists of squares."""

    def __init__(self, lib):
        self.lib = lib
        self.free = {t: [t << 10 | i for i in range(lib.dungeon_thing_count(t)) if word(lib, t << 10 | i, 0) == 0xFFFF][1:9]
                     for t in (14, 15)}  # the first free projectile links the minion

    def last_thing(self, m, x, y):
        lib = self.lib
        t = lib.dungeon_square_first_thing(m, x, y)
        if t == 0xFFFE:
            return None
        while lib.dungeon_next_thing(t) != 0xFFFE:
            t = lib.dungeon_next_thing(t)
        return t

    def inject(self, emu, rng, m, px, py):
        """Missiles and explosions on squares with things around (px, py); returns what remove takes back."""
        lib, undo = self.lib, []
        squares = [(x, y) for x in range(px - 3, px + 4) for y in range(py - 3, py + 4)
                   if 0 <= x < lib.dungeon_map_width(m) and 0 <= y < lib.dungeon_map_height(m) and self.last_thing(m, x, y)]
        rng.shuffle(squares)
        for (x, y), kind in zip(squares, (14, 15, 14, 15)):
            last = self.last_thing(m, x, y)
            record = self.free[kind][len([u for u in undo if u[1] >> 10 & 15 == kind])]
            thing = rng.randrange(4) << 14 | record
            if kind == 14:
                set_word(lib, emu, record, 2, rng.choice((0xFF82, 0xFF80, 0xFF84, 0xFF87)))
                set_word(lib, emu, record, 4, 0x6464)
                set_word(lib, emu, record, 6, rng.randrange(20))
            else:
                set_word(lib, emu, record, 2, rng.choice((0, 2, 3, 4, 14, 48)) | 0x4000)
            set_word(lib, emu, record, 0, 0xFFFE)
            set_word(lib, emu, last, 0, thing)
            undo.append((last, record))
        return undo

    def remove(self, emu, undo):
        for last, record in reversed(undo):
            set_word(self.lib, emu, last, 0, 0xFFFE)
            set_word(self.lib, emu, record, 0, 0xFFFF)


def test_minion_maps_match_original(port, map_ui, gdat):
    # The minion maps of the dungeon in the hand of the first champion, of each kind (no location, minion,
    # location, without options), with a minion (a link projectile in the possessions of a creature group) and a
    # marker at random squares, at random party places and directions, with the option buttons, over game ticks that
    # take the mana (and darken the map without mana), and clicks on the grid that send the minion. The screen, the
    # champion, the action area flags, the map state, and the map records must be equal to the original.
    lib, original = port, map_ui
    emu = original.emu
    emu.write_u32(G_LEVEL_DIGIT_TABLE, original.digits)
    lib.item_is_minion_map.argtypes = [ctypes.c_uint16]
    lib.dungeon_thing_valid.argtypes = [ctypes.c_uint16]
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    lib.dungeon_thing_record.argtypes = [ctypes.c_uint16]
    lib.creature_group_info.restype = ctypes.POINTER(ctypes.c_uint16)
    lib.creature_group_info.argtypes = [ctypes.c_uint16]
    maps = [9 << 10 | i for i in range(lib.dungeon_thing_count(9)) if lib.dungeon_thing_valid(9 << 10 | i)
            and lib.item_is_minion_map(9 << 10 | i)]
    assert len(maps) >= 4
    all_groups = static_groups(lib)
    groups = [g for g in all_groups if lib.creature_group_info(g[0])[0] & 1]  # static: the animation state is in the record
    places = squares_of(lib, (1,))
    marks = places + squares_of(lib, (2, 3))
    # stairs up and open pits that lead to another level
    ways = [(m, x, y) for m, x, y in squares_of(lib, (3,)) if lib.dungeon_square(m, x, y) & 4 and leads(lib, m, x, y, -1)] * 3 \
        + [(m, x, y) for m, x, y in squares_of(lib, (2,)) if lib.dungeon_square(m, x, y) & 8 and leads(lib, m, x, y, 1)]
    busy = [g for g in all_groups if g[4] in (17, 19, 22, 26, 36, 37, 47)]
    moving = [g for g in all_groups if not lib.creature_group_info(g[0])[0] & 1]
    lib.creature_alloc_active_groups()
    activated = []
    pool = Pool(lib)
    free_projectile = next(14 << 10 | i for i in range(lib.dungeon_thing_count(14)) if word(lib, 14 << 10 | i, 0) == 0xFFFF)
    draw_game_screen(lib, original)
    rng = random.Random(20)
    place_set = set(places)
    time = Game.in_dll(lib, "game").game_time
    kinds, clicks, moved, dark, offsets, markers = set(), 0, 0, 0, set(), set()
    for n in range(64):
        thing = rng.choice(maps)
        kind = n % 4
        set_word(lib, emu, thing, 4, word(lib, thing, 4) & 0x1FFF | kind << 13)
        location = 0xFFFF
        if kind in (1, 2) or rng.random() < 0.5:
            m, x, y = rng.choice(marks if kind != 1 and rng.random() < 0.5 else ways)
            location = x | y << 5 | m << 10
        set_word(lib, emu, thing, 6, location)
        set_word(lib, emu, thing, 2, 0xFFFE)
        if kind == 1 and rng.random() < 0.8:
            group, gm, gx, gy, _ = rng.choice(groups if rng.random() < 0.5 else moving)
            set_word(lib, emu, free_projectile, 0, word(lib, group, 2))
            set_word(lib, emu, free_projectile, 2, thing)
            dx, dy = rng.randrange(-2, 3), rng.randrange(-2, 3)
            mx = min(max(gx + dx, 0), lib.dungeon_map_width(gm) - 1)
            my = min(max(gy + dy, 0), lib.dungeon_map_height(gm) - 1)
            set_word(lib, emu, free_projectile, 4, mx | my << 5 | gm << 10)
            set_word(lib, emu, free_projectile, 6, 0)
            set_word(lib, emu, group, 2, free_projectile)
            set_word(lib, emu, thing, 2, group)
            size = ctypes.c_uint32()
            lib.gdat_item(gdat, lib.gdat_find(gdat, 0x0F, word(lib, group, 4) & 0xFF, 7, 0xFD), ctypes.byref(size))
            sequence = rng.randrange(max(1, size.value // 8))  # another animation frame: another cell of the minion
            if lib.creature_group_info(group)[0] & 1:
                set_word(lib, emu, group, 8, sequence)
                set_word(lib, emu, group, 10, 0)
            else:
                # a moving minion: its animation is in active group 1
                active = ctypes.c_void_p.in_dll(lib, "active_groups").value
                ctypes.memmove(active + ACTIVE_SIZE + 8, struct.pack("<HH", sequence, 0), 4)
                emu.write(emu.read_u32(G_ACTIVE_GROUPS) + ACTIVE_SIZE + 8, struct.pack("<HH", sequence, 0))
                activated.append((group, word(lib, group, 4)))
                set_word(lib, emu, group, 4, word(lib, group, 4) & 0xFF | 1 << 8)
            set_word(lib, emu, group, 10, 0)
        if rng.random() < 0.4:
            _, m, gx, gy, _ = rng.choice(busy)
            near = [(m, x, y) for x in range(gx - 2, gx + 3) for y in range(gy - 2, gy + 3) if (m, x, y) in place_set]
            m, x, y = rng.choice(near or places)
        else:
            m, x, y = rng.choice(places)
        place_party(lib, emu, m, x, y, rng.randrange(4))
        undo = pool.inject(emu, rng, m, x, y)
        champion_bytes(lib, 0, CH_SLOTS, struct.pack("<H", thing))
        champion_bytes(lib, 0, CH_MANA, struct.pack("<h", rng.choice((0, 2, 5, 60, 300))))
        sync_champions(lib, original)
        set_action(lib, original, champion=0)
        update_action_area(lib, original)
        open_hand(lib, original, 0)
        kinds.add((kind, ActionArea.in_dll(lib, "action").flags & 0x60))
        assert_map_state(lib, original, (n, "open", hex(thing), kind))
        for option in range(4):
            if rng.random() < 0.6:
                assert command(lib, emu, 0x56 + option) == (1, 1)
        mm = MinionMap.in_dll(lib, "minion_map")
        offsets.add((mm.x_offset, mm.y_offset))
        if word(lib, thing, 2) != 0xFFFE and rng.random() < 0.7:
            # the minion goes where the marker square leads: the grid shows the marker on its other level
            for m, x, y in rng.sample([(mm.above_map, mm.above_x, mm.above_y), (mm.below_map, mm.below_x, mm.below_y)], 2):
                if m >= 0:
                    set_word(lib, emu, free_projectile, 4, x | y << 5 | m << 10)
                    break
        for k in range(rng.randrange(10, 60)):
            r = rng.random()
            if r < 0.1:
                assert command(lib, emu, 0x56 + rng.randrange(4)) == (1, 1)
            elif r < 0.25 and kind == 1:
                rect = original.alloc(8)
                from test_interface import ZONE_GET_RECT
                emu.call(ZONE_GET_RECT, 99, rect)
                zx, zy, zw, zh = struct.unpack("<4h", emu.read(rect, 8))
                before = word(lib, thing, 6)
                assert command(lib, emu, 0x55, zx + rng.randrange(zw), zy + rng.randrange(zh)) == (1, 1)
                clicks += 1
                moved += word(lib, thing, 6) != before
            time += 1
            set_time(lib, emu, time)
            update_action_area(lib, original, tick=1)
            dark += not ActionArea.in_dll(lib, "action").flags & 0x200
            markers.add(MinionMap.in_dll(lib, "minion_map").marker_state)
            assert_map_state(lib, original, (n, k, hex(thing), kind))
        lib.ui_action_area_close()
        emu.call(UI_ACTION_AREA_CLOSE)
        update_action_area(lib, original)
        assert_map_state(lib, original, (n, "close"))
        pool.remove(emu, undo)
        for group, w in activated:
            set_word(lib, emu, group, 4, w)
        activated.clear()
        if word(lib, thing, 2) != 0xFFFE:  # unlink the minion again
            group = word(lib, thing, 2)
            set_word(lib, emu, group, 2, word(lib, free_projectile, 0))
            set_word(lib, emu, free_projectile, 0, 0xFFFF)
    lib.creature_free_active_groups()
    assert {k for k, _ in kinds} == {0, 1, 2, 3} and any(k == 1 and f & 0x20 for k, f in kinds)
    assert clicks > 20 and moved > 5 and dark > 20
    assert len(offsets) > 2 and markers == {0, 1, 2, 3}, (offsets, markers)


MOVE_PARTY_WARP_TO_MARKER = 0x4BFD4


def test_warp_to_marker_matches_original(magic):
    # The party on every square with a warp marker text (subtypes 15 and 16) and on squares around them: the move to the
    # marker of its id, the marker that a one-way warp shows, the result.
    lib, emu = magic.lib, magic.emu
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.argtypes = [ctypes.c_uint16]
    places = []
    for m in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(m)):
            for y in range(lib.dungeon_map_height(m)):
                t = lib.dungeon_square_first_thing(m, x, y)
                while t != 0xFFFE and t >> 10 & 15 <= 3:
                    w = word(lib, t, 2)
                    if t >> 10 & 15 == 2 and w & 6 == 2 and w >> 11 in (15, 16):
                        places.append((m, x, y))
                    t = lib.dungeon_next_thing(t)
    assert places
    moved = 0
    for n, (m, x, y) in enumerate(places + [(m, x + 1, y) for m, x, y in places if x + 1 < lib.dungeon_map_width(m)]):
        magic.place(m, x, y, n & 3)
        mine, theirs = lib.move_party_warp_to_marker(), emu.call(MOVE_PARTY_WARP_TO_MARKER)
        assert mine == theirs, (n, m, x, y)
        moved += mine
        magic.assert_same((n, m, x, y))
    assert moved
    magic.assert_equal("end")
