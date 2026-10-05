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
                            diff, draw_game_screen, open_hand, sync_champions, update_action_area)
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
        emu.stub(LIGHT_UPDATE_PARTY_LIGHT, lambda *_: 0)
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
        self.assert_creatures(where)


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
