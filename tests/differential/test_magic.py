"""Step 16 magic and minions against the original code in dm2tools.emu: runes, spells, magic light, auras, and the
minion map panel."""

import ctypes
import random
import struct

import pytest

from dm2test import constants as C
from dm2test.markers import ROOT
from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image
from dm2test.creature import CreatureScenario
from dm2test.interface import (CHAMPION_SIZE, GAME_LEADER_HAND, ActionArea, c_screen, champion_bytes, champion_records,
                               diff, draw_game_screen, open_hand, set_action, sync_champions, update_action_area)
from dm2test.interface import gdat, original, port  # noqa: F401 (fixtures)
from dm2test.save import Game
from dm2test.scenario import start  # noqa: F401 (fixture)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

# Original functions.
MAGIC_FIND_SPELL, MAGIC_CAST_SPELL, TIMELINE_ADD_EVENT = 0x421FD, 0x422F5, 0x56390
LIGHT_UPDATE_PARTY_LIGHT, INV_RENDER_HAND_ICON, TEXT_SCROLL_MESSAGE_AREA = 0x389C2, 0x38392, 0x139E4
# Original globals.
G_SPELL_TABLE, SPELL_COUNT, SPELL_SIZE = 0x757FE, 34, 8
G_CHAMPIONS, G_CHAMPION_COUNT, G_MAGIC_LIGHT, G_LEADER_HAND = 0x7FBD0, 0x7F276, 0x7FFEC, 0x7FBB4
GAME_MAGIC = GAME_LEADER_HAND + 2
# Champion record offsets.
CH_MANA, CH_MAX_MANA, CH_STATS, CH_EXPERIENCE, CH_SLOTS, CH_HEALTH = 0x3E, 0x40, 0x4A, 0x5F, 0xC3, 0x36
STAT_WISDOM = C.STAT_WISDOM
RUNE_FIRST = C.SPELL_RUNE_GLYPH_FIRST


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
    sets = [[0] + list(range(RUNE_FIRST + C.SPELL_RUNES_PER_SET * k, RUNE_FIRST + C.SPELL_RUNES_PER_SET * k + C.SPELL_RUNES_PER_SET)) for k in range(4)]
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
    """The original leaves the data bytes after the word of the light, invisibility, and aura events, and data bytes 4
    and 5 of the projectile and explosion events uninitialized."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX
    keep = {C.EVENT_MAGIC_LIGHT: 2, C.EVENT_INVISIBILITY: 0, C.EVENT_AURA: 2, C.EVENT_EXPLOSION: 4, C.EVENT_PROJECTILE_MOVE: 4,
            C.EVENT_PROJECTILE_MOVE_2: 4}

    def hook(uc, *_):
        event = uc.reg_read(UC_X86_REG_EAX)
        n = keep.get(emu.read(event + 4, 1)[0])
        if n is not None:
            emu.write(event + 6 + n, bytes(6 - n))

    emu.uc.hook_add(UC_HOOK_CODE, hook, begin=TIMELINE_ADD_EVENT, end=TIMELINE_ADD_EVENT)


class MagicScenario(CreatureScenario):
    """start.DAT with four champions."""

    def __init__(self, lib, gdat, start, tmp_path):
        super().__init__(lib, gdat, start, tmp_path)
        emu = self.emu
        clear_magic_event_bytes(emu)
        self.game_record = Game.in_dll(lib, "g_game")
        sync_champions(lib, self)  # the loads differ in the redraw flags, the skill bonuses, and the load (Step 14)

    def magic(self):
        mine = bytes((ctypes.c_uint8 * 6).from_address(ctypes.addressof(self.game_record) + GAME_MAGIC))
        return mine, self.emu.read(G_MAGIC_LIGHT, 6)

    def set_magic(self, data):
        (ctypes.c_uint8 * 6).from_address(ctypes.addressof(self.game_record) + GAME_MAGIC)[:] = list(data)
        self.emu.write(G_MAGIC_LIGHT, bytes(data))

    def assert_magic_state(self, where):
        """Champions, the party spells, the leader hand, then the creature and dungeon state."""
        mine = bytes(champion_records(self.lib))
        theirs = self.emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE)
        for i in range(4):
            a, b = mine[i * CHAMPION_SIZE:(i + 1) * CHAMPION_SIZE], theirs[i * CHAMPION_SIZE:(i + 1) * CHAMPION_SIZE]
            assert a.hex() == b.hex(), (where, "champion", i)
        m, t = self.magic()
        assert m == t, (where, "magic")
        leader_hand = ctypes.c_uint16.from_address(ctypes.addressof(self.game_record) + GAME_LEADER_HAND).value
        assert leader_hand == struct.unpack("<H", self.emu.read(G_LEADER_HAND, 2))[0], where
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
        for skill in range(C.SKILL_COUNT):
            champion_bytes(lib, champ, CH_EXPERIENCE + 4 * skill, struct.pack("<I", rng.choice((0, 500, 5000, 50000, 500000))))
    scenario.game_record.champion_count = C.CHAMPION_MAX
    scenario.emu.write(G_CHAMPION_COUNT, struct.pack("<H", C.CHAMPION_MAX))
    sync_champions(lib, scenario)


def flasks(lib, n):
    """n potion things of the dungeon, turned into empty flasks (GDAT index 0x14)."""
    lib.dungeon_thing_valid.argtypes = [ctypes.c_uint16]
    out = []
    for i in range(lib.dungeon_thing_count(C.THING_POTION)):
        t = C.THING_POTION << C.THING_TYPE_SHIFT | i
        if lib.dungeon_thing_valid(t) and len(out) < n:
            out.append(t)
    return out


def set_flask(scenario, thing):
    lib = scenario.lib
    lib.thing_word.restype = ctypes.c_uint16
    lib.thing_set_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    w = lib.thing_word(thing, 2) & 0x80FF | 0x14 << C.POTION_TYPE_SHIFT
    lib.thing_set_word(thing, 2, w)
    record = scenario.emu.read_u32(0x7F288 + 4 * C.THING_POTION) + (thing & C.THING_INDEX_MASK) * 4
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
        champ = rng.randrange(C.CHAMPION_MAX)
        if rng.random() < 0.7:
            pot = rng.choice(pots)
            set_flask(magic, pot)
            champion_bytes(lib, champ, CH_SLOTS + 2 * rng.randrange(C.SLOT_HAND_COUNT), struct.pack("<H", pot))
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
    """The original interface without the party light update."""
    emu = original.emu
    emu.stub(LIGHT_UPDATE_PARTY_LIGHT, lambda *_: 0)
    emu.stub(INV_RENDER_HAND_ICON, lambda *_: 0)  # the hand icon bitmap of the mouse cursor (not set up here)
    # the name of a new object in the leader hand scrolls the message area: the IBMIOP move as in test_interface;
    # the restore of the interface clears g_iop_buffer, so it is set at each scroll
    from dm2test.interface import G_IOP_BUFFER, IOP_SCROLL_CALL, IOP_SCROLL_NEXT, SCREEN_W, VGA, ZONE_GET_RECT
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EIP
    buffer = original.alloc(0x400)
    rect = original.alloc(8)
    emu.call(ZONE_GET_RECT, C.ZONE_MESSAGE_AREA, rect)
    x, y, w, h = struct.unpack("<4h", emu.read(rect, 8))

    def iop_scroll(uc, *_):
        for row in range(y, y + h - 7):  # the restore of the interface clears g_iop_buffer: see set_buffer
            emu.write(VGA + row * SCREEN_W + x, emu.read(VGA + (row + 7) * SCREEN_W + x, w))
        uc.reg_write(UC_X86_REG_EIP, IOP_SCROLL_NEXT)

    emu.uc.hook_add(UC_HOOK_CODE, iop_scroll, begin=IOP_SCROLL_CALL, end=IOP_SCROLL_CALL)
    emu.uc.hook_add(UC_HOOK_CODE, lambda *_: emu.write_u32(G_IOP_BUFFER, buffer), begin=TEXT_SCROLL_MESSAGE_AREA,
                    end=TEXT_SCROLL_MESSAGE_AREA)
    clear_magic_event_bytes(emu)
    return original


CMD_RUNE_FIRST, CMD_REMOVE_RUNE, CMD_CAST_SPELL = C.CMD_RUNE_FIRST, C.CMD_REMOVE_RUNE, C.CMD_CAST_SPELL
COMMAND_EXECUTE, G_COMMAND_STOP = 0x21D6C, 0x7F484


def command(lib, emu, cmd, x=0, y=0):
    """Executes a command in both; returns the two results."""
    from dm2test.scenario import COMMAND_RECORD, Command
    c = Command(x, y, cmd)
    emu.write(COMMAND_RECORD, bytes(c))
    return lib.command_execute(ctypes.byref(c)), emu.call(COMMAND_EXECUTE, COMMAND_RECORD)


INPUT_GET_CLICK_FEEDBACK_SCRIPT, INPUT_RUN_CLICK_FEEDBACK = 0x20EE0, 0x21394
G_CURRENT_COMMAND, G_CLICK_FEEDBACK_SCRIPT, G_CLICK_FEEDBACK_DELAY = 0x7F4CE, 0x7F47C, 0x7F52A
G_INPUT_DISABLED, G_COMMAND_QUEUE_HEAD, G_COMMAND_QUEUE_TAIL = 0x7F50A, 0x7F50E, 0x7F516
G_FEEDBACK_WAITING, G_FEEDBACK_BUSY = 0x7F474, 0x7F488
INPUT_MODE_MOVE, INPUT_BUTTON_LEFT = C.INPUT_MODE_MOVE, C.INPUT_BUTTON_LEFT
MAP_HAS_OPTIONS = C.MAP_HAS_OPTIONS
button_clicks = {}  # command: a screen point of its button and the rect of its zone


def click_feedback(lib, original, cmd, where):
    """A left click on the button of command cmd in the port; the original starts the click feedback script of the
    same command and rect. Both run the script tick by tick until it ends; the screens must be equal after each
    tick. The queued commands and the disabled input are cleared after the script."""
    from dm2test.input import MainLoop
    emu = original.emu
    if cmd not in button_clicks:
        rect = (ctypes.c_int16 * 4)()
        lib.input_click_command.restype = ctypes.c_uint16
        x, y = next((x, y) for y in range(C.SCREEN_H) for x in range(C.SCREEN_W)
                    if lib.input_click_command(INPUT_MODE_MOVE, x, y, INPUT_BUTTON_LEFT, rect) == cmd)
        button_clicks[cmd] = x, y, tuple(rect)
    x, y, rect = button_clicks[cmd]
    loop = MainLoop.in_dll(lib, "g_main_loop")
    lib.input_reset_states()
    lib.input_flush()
    emu.write(G_CLICK_FEEDBACK_DELAY, bytes(2))
    emu.write_u32(G_FEEDBACK_WAITING, 0)
    emu.write_u32(G_FEEDBACK_BUSY, 0)
    lib.input_set_mode(INPUT_MODE_MOVE)
    lib.input_mouse_click(x, y, INPUT_BUTTON_LEFT)
    emu.write(G_CURRENT_COMMAND, struct.pack("<3H4h", x, y, cmd, *rect))
    emu.write_u32(G_CLICK_FEEDBACK_SCRIPT, emu.call(INPUT_GET_CLICK_FEEDBACK_SCRIPT, G_CURRENT_COMMAND))
    for tick in range(20):
        loop.timer_ticks += 1
        lib.input_poll()
        emu.call(INPUT_RUN_CLICK_FEEDBACK, int(tick > 0))
        mine, theirs = c_screen(lib), original.screen()
        assert mine == theirs, (where, "feedback", hex(cmd), tick, diff(mine, theirs, C.SCREEN_W))
        if not emu.read_u32(G_CLICK_FEEDBACK_SCRIPT):
            break
    assert not emu.read_u32(G_CLICK_FEEDBACK_SCRIPT), where
    lib.input_flush()
    emu.write(G_INPUT_DISABLED, bytes(2))
    emu.write(G_COMMAND_QUEUE_HEAD, bytes(2))
    emu.write(G_COMMAND_QUEUE_TAIL, bytes(2))


def spell_kind(index):
    return SPELL_TABLE_BYTES[index * SPELL_SIZE + 6] & 0xF


@pytest.fixture
def cast_ui(spell_ui):
    """spell_ui; the heap of the original (the dungeon) goes back after the test, so the missiles of the casts do not
    stay. The port fixture puts the globals back."""
    from dm2test.view import HEAP
    heap = spell_ui.emu.read(HEAP, spell_ui.top - HEAP)
    yield spell_ui
    spell_ui.emu.write(HEAP, heap)


def test_runes_and_casts_match_original(port, cast_ui):
    # The spell panel of the first champion: runes added (with enough mana and without), removed, and cast: unknown
    # rune sets, failed casts, casts without a flask, and successful spells (not the summons, which need active
    # creature groups: test_every_spell_cast_matches_original). Some rune and cast commands come from a click with
    # its click feedback (the pressed rune button, the entered runes). The screen, the champion, the action area
    # state, the party spells, the timeline, and the hook arguments must be equal.
    lib, original = port, cast_ui
    emu = original.emu
    draw_game_screen(lib, original)
    party_map = Game.in_dll(lib, "g_game").party_map  # the missile spells fly on the current map
    lib.dun_set_current_map(party_map)
    emu.call(DUN_SET_CURRENT_MAP, party_map)
    action = ActionArea.in_dll(lib, "g_action")
    rng = random.Random(17)
    clicks = random.Random(19)  # the click feedback choices; rng keeps its sequence
    lib.input_init()
    spells = [i for i in range(SPELL_COUNT) if spell_kind(i) != 4]
    plan, results, added = [], set(), 0
    for n in range(400):
        if not action.champion:
            mana = rng.choice((0, 3, 40, 900))
            champion_bytes(lib, 0, CH_MANA, struct.pack("<h", mana))
            champion_bytes(lib, 0, CH_STATS + 2 * STAT_WISDOM, [rng.randrange(20, 120)])
            for skill in range(C.SKILL_COUNT):
                champion_bytes(lib, 0, CH_EXPERIENCE + 4 * skill, struct.pack("<I", rng.choice((0, 5000, 500000))))
            sync_champions(lib, original)
            open_hand(lib, original, 2)
            runes = spell_runes(rng.choice(spells), rng.randrange(1, 7))
            plan = runes if rng.random() < 0.7 else [RUNE_FIRST + C.SPELL_RUNES_PER_SET * k + rng.randrange(C.SPELL_RUNES_PER_SET) for k in range(rng.randrange(1, 5))]
        count = champion_records(lib)[CH_RUNE_COUNT]
        if count < len(plan) and rng.random() < 0.9:
            rune = (plan[count] - RUNE_FIRST) % C.SPELL_RUNES_PER_SET
            if rng.random() < 0.5:
                if clicks.random() < 0.5:
                    click_feedback(lib, original, CMD_RUNE_FIRST + rune, n)
                else:
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
                if clicks.random() < 0.5:
                    click_feedback(lib, original, CMD_CAST_SPELL, n)  # it queues the command
                assert command(lib, emu, CMD_CAST_SPELL) == (1, 1)
                mine = int(ctypes.c_bool.in_dll(lib, "g_input_command_stop").value)
                theirs = struct.unpack("<H", emu.read(G_COMMAND_STOP, 2))[0]
            assert mine == theirs, n
            results.add((mine, champion_records(lib)[CH_RUNE_COUNT] > 0))
            what = ("cast", mine)
        else:
            lib.action_area_close()
            emu.call(UI_ACTION_AREA_CLOSE)
            what = ("close",)
        update_action_area(lib, original)
        update_action_area(lib, original, tick=1)
        where = (n, what, plan)
        mine, theirs = c_screen(lib), original.screen()
        assert mine == theirs, (where, diff(mine, theirs, C.SCREEN_W))
        assert bytes(champion_records(lib)) == emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE), where
        for field, addr in (("champion", G_ACTION_CHAMPION), ("redraw_delay", G_ACTION_DELAY), ("result", G_ACTION_RESULT),
                            ("mode", G_ACTION_MODE)):
            assert getattr(action, field) == struct.unpack("<h", emu.read(addr, 2))[0], (where, field)
        game = Game.in_dll(lib, "g_game")
        assert bytes((ctypes.c_uint8 * 6).from_address(ctypes.addressof(game) + GAME_MAGIC)) == emu.read(G_MAGIC_LIGHT, 6), where
        from dm2test.save import Timeline
        tl = Timeline.in_dll(lib, "g_timeline")
        events = ctypes.string_at(ctypes.cast(tl.events, ctypes.c_void_p), tl.capacity * 12)
        assert events == emu.read(emu.read_u32(G_TIMELINE_EVENTS), tl.capacity * 12), where
    assert added > 60
    assert results == {(0, False), (1, False), (1, True)}  # success, failure, and a missing flask that keeps the runes


CMD_PARTY_ICON, CMD_HAND_FIRST, CMD_ACTION_FIRST = C.CMD_PARTY_ICON_FIRST, C.CMD_HAND_FIRST, 0x71
CH_CELL = 0x1D


def test_icon_click_feedback_matches_original(port, cast_ui):
    # A party of two (a copy of the first champion in the next cell): the click feedback of the spell icons of the
    # party (opcode 8), of the hands with the action area closed (opcode 9), and of the actions in the open action
    # menu (opcode 0x0c). While the area is open, no click finds a hand command: the other branch of opcode 9 (the
    # icon of the open hand) has no click.
    lib, original = port, cast_ui
    emu = original.emu
    first = bytes(champion_records(lib)[:CHAMPION_SIZE])
    champion_bytes(lib, 1, 0, first)
    champion_bytes(lib, 1, CH_CELL, [first[CH_CELL] + 1 & 3])
    champion_bytes(lib, 1, CH_SLOTS, b"\xff\xff" * C.SLOT_COUNT)  # the copy must not share the items of the first champion
    Game.in_dll(lib, "g_game").champion_count = 2
    emu.write(G_CHAMPION_COUNT, struct.pack("<H", 2))
    sync_champions(lib, original)
    draw_game_screen(lib, original)
    lib.input_init()

    def close():
        lib.action_area_close()
        emu.call(UI_ACTION_AREA_CLOSE)
        update_action_area(lib, original)

    for cmd in range(CMD_PARTY_ICON, CMD_PARTY_ICON + 2):
        close()
        # a corner of the spell half of the icon: the leader half would refine the click to a leader command
        icon = cmd - CMD_PARTY_ICON
        rect = (ctypes.c_int16 * 4)()
        lib.input_click_command.restype = ctypes.c_uint16
        next(1 for y in range(C.SCREEN_H) for x in range(C.SCREEN_W)
             if lib.input_click_command(INPUT_MODE_MOVE, x, y, INPUT_BUTTON_LEFT, rect) == cmd)
        x = rect[0] + rect[2] - 1 if icon in (1, 2) else rect[0]
        y = rect[1] + rect[3] - 1 if icon < 2 else rect[1]
        button_clicks[cmd] = x, y, tuple(rect)
        click_feedback(lib, original, cmd, "party icon")
    for cmd in range(CMD_HAND_FIRST, CMD_HAND_FIRST + 4):
        close()
        click_feedback(lib, original, cmd, "hand, closed")
    open_hand(lib, original, 0)
    actions = ctypes.c_int.in_dll(lib, "g_champ_action_count").value
    assert actions
    for cmd in range(CMD_ACTION_FIRST, CMD_ACTION_FIRST + actions):
        click_feedback(lib, original, cmd, "action")


TIMELINE_EVENT_MAGIC_LIGHT = 0x5917D


def test_magic_light_events_match_original(magic):
    # The light and darkness events of every level: the magic light changes and the next event of the fading light.
    from dm2test.save import Event
    from dm2test.scenario import COMMAND_RECORD
    lib, emu = magic.lib, magic.emu
    lib.magic_event_light.argtypes = [ctypes.c_void_p]
    rng = random.Random(18)
    for level in range(-15, 16):
        magic.set_magic(struct.pack("<h", rng.randrange(-300, 300)) + bytes(4))
        e = Event(magic.time() | magic.game.party_map << C.EVENT_MAP_SHIFT, C.EVENT_MAGIC_LIGHT, 0, (ctypes.c_uint8 * 6)(*struct.pack("<h", level), 0, 0, 0, 0))
        emu.write(COMMAND_RECORD, bytes(e))
        lib.magic_event_light(ctypes.byref(e))
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
    seed = ctypes.c_uint32.in_dll(lib, "g_random_seed")
    big = 0
    for n in range(1500):
        if n % 100 == 0:
            for champ in range(C.CHAMPION_MAX):
                champion_bytes(lib, champ, CH_STATS + 2 * STAT_WISDOM, [rng.randrange(10, 110)])
                champion_bytes(lib, champ, CH_MANA, struct.pack("<h", rng.randrange(0, 300)))
            sync_champions(lib, magic)
            aura = rng.choice(auras)
        index = aura if rng.random() < 0.5 else rng.choice(spells)
        mine, theirs = cast(magic, rng.randrange(C.CHAMPION_MAX), index, rng.randrange(1, 7))
        assert mine == theirs, (n, index)
        assert seed.value == magic.emu.read_u32(0x70518), n
        assert bytes(champion_records(lib)) == magic.emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE), n
        big += any(struct.unpack_from("<h", bytes(champion_records(lib)), i * CHAMPION_SIZE + 0x103)[0] > 50 for i in range(4))
        if n % 10 == 9:
            magic.step()
    assert big > 50
    magic.assert_magic_state("end")


# ---- Minion maps ----

ACTIVE_SIZE, G_ACTIVE_GROUPS = C.ACTIVE_GROUP_SIZE, 0x7F898
G_THINGS, THING_SIZES = 0x7F288, (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
G_LEVEL_DIGIT_TABLE, DUN_SET_PARTY_LOCATION, G_CUR_MAP, G_PARTY_DIR = 0x7F3D0, 0x1C82D, 0x72244, 0x7F254
DUN_SET_CURRENT_MAP = 0x1C724
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
    t = thing >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK
    return emu.read_u32(G_THINGS + 4 * t) + (thing & C.THING_INDEX_MASK) * THING_SIZES[t]


def set_word(lib, emu, thing, offset, value):
    lib.thing_set_word.argtypes = [ctypes.c_uint16, ctypes.c_int, ctypes.c_uint16]
    lib.thing_set_word(thing, offset, value)
    emu.write(record_address(emu, thing) + offset, struct.pack("<H", value))


def word(lib, thing, offset):
    lib.thing_word.restype = ctypes.c_uint16
    lib.thing_word.argtypes = [ctypes.c_uint16, ctypes.c_int]
    return lib.thing_word(thing, offset)


# show_square reads square bytes of the map that dun_get_location_on_map found without a bounds check; the
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
    from dm2test.scenario import GameRecord, Party
    Party.in_dll(lib, "g_party").cur_map = -1
    emu.write(G_CUR_MAP, struct.pack("<h", -1))
    GameRecord.in_dll(lib, "g_game").party_dir = d
    emu.write(G_PARTY_DIR, struct.pack("<H", d))
    lib.dun_set_party_location(x, y, map_)
    emu.call(DUN_SET_PARTY_LOCATION, x, y, map_)
    emu.write(0x75BFE, bytes([lib.dungeon_map_graphics(map_) >> C.MAP_WALLSET_SHIFT & C.MAP_NIBBLE_MASK]))  # g_map_gfx_set of game_enter_map


def assert_map_state(lib, original, where):
    emu = original.emu
    mine, theirs = c_screen(lib), original.screen()
    assert mine == theirs, (where, diff(mine, theirs, C.SCREEN_W))
    assert bytes(champion_records(lib)) == emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE), where
    action = ActionArea.in_dll(lib, "g_action")
    for field, addr in (("champion", G_ACTION_CHAMPION), ("redraw_delay", G_ACTION_DELAY), ("mode", G_ACTION_MODE),
                        ("flags", G_ACTION_FLAGS)):
        assert getattr(action, field) & 0xFFFF == struct.unpack("<H", emu.read(addr, 2))[0], (where, field)
    m = MinionMap.in_dll(lib, "g_minion_map")
    for name, (addr, fmt) in MINION_GLOBALS.items():
        assert getattr(m, name) == struct.unpack(fmt, emu.read(addr, struct.calcsize(fmt)))[0], (where, name)
    assert ctypes.c_uint32.in_dll(lib, "g_random_seed").value == emu.read_u32(0x70518), where
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    for t in (C.THING_CONTAINER, C.THING_PROJECTILE):
        n = lib.dungeon_thing_count(t)
        assert ctypes.string_at(lib.dungeon_thing_record(t << C.THING_TYPE_SHIFT), n * THING_SIZES[t]) == \
            emu.read(emu.read_u32(G_THINGS + 4 * t), n * THING_SIZES[t]), (where, "things", t)


def squares_of(lib, kinds):
    out = []
    for m in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(m)):
            for y in range(lib.dungeon_map_height(m)):
                if lib.dungeon_square(m, x, y) >> C.SQUARE_TYPE_SHIFT in kinds:
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
                while t != C.THING_ENDOFLIST:
                    if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == C.THING_GROUP:
                        record = ctypes.string_at(ctypes.c_void_p(lib.dungeon_thing_record(t)), 16)
                        out.append((t, m, x, y, record[4]))
                    t = lib.dungeon_next_thing(t)
    return out


def leads(lib, m, x, y, delta):
    """True when the dungeon has a map at delta levels from map m at the position of (x, y)."""
    lib.dungeon_location_on_map.argtypes = [ctypes.c_int16, ctypes.c_int] + [ctypes.POINTER(ctypes.c_int16)] * 2
    cx, cy = ctypes.c_int16(x), ctypes.c_int16(y)
    return lib.dungeon_location_on_map(m, delta, ctypes.byref(cx), ctypes.byref(cy)) >= 0


def set_time(lib, emu, t):
    Game.in_dll(lib, "g_game").game_time = t
    emu.write_u32(0x7F22C, t)


class Pool:
    """Free projectile and explosion records that the test puts at the end of the thing lists of squares."""

    def __init__(self, lib):
        self.lib = lib
        self.free = {t: [t << C.THING_TYPE_SHIFT | i for i in range(lib.dungeon_thing_count(t))
                         if word(lib, t << C.THING_TYPE_SHIFT | i, 0) == C.THING_NONE][1:9]
                     for t in (C.THING_PROJECTILE, C.THING_EXPLOSION)}  # the first free projectile links the minion

    def last_thing(self, m, x, y):
        lib = self.lib
        t = lib.dungeon_square_first_thing(m, x, y)
        if t == C.THING_ENDOFLIST:
            return None
        while lib.dungeon_next_thing(t) != C.THING_ENDOFLIST:
            t = lib.dungeon_next_thing(t)
        return t

    def inject(self, emu, rng, m, px, py):
        """Missiles and explosions on squares with things around (px, py); returns what remove takes back."""
        lib, undo = self.lib, []
        squares = [(x, y) for x in range(px - 3, px + 4) for y in range(py - 3, py + 4)
                   if 0 <= x < lib.dungeon_map_width(m) and 0 <= y < lib.dungeon_map_height(m) and self.last_thing(m, x, y)]
        rng.shuffle(squares)
        for (x, y), kind in zip(squares, (C.THING_PROJECTILE, C.THING_EXPLOSION, C.THING_PROJECTILE, C.THING_EXPLOSION)):
            last = self.last_thing(m, x, y)
            record = self.free[kind][len([u for u in undo if u[1] >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == kind])]
            thing = rng.randrange(C.CELL_COUNT) << C.THING_CELL_SHIFT | record
            if kind == C.THING_PROJECTILE:
                set_word(lib, emu, record, 2, rng.choice((C.SPELL_LIGHTNING, C.SPELL_FIREBALL, C.SPELL_DOOR_TOGGLE, C.SPELL_POISON_CLOUD)))
                set_word(lib, emu, record, 4, 0x6464)
                set_word(lib, emu, record, 6, rng.randrange(20))
            else:
                set_word(lib, emu, record, 2, rng.choice((0, 2, 3, 4, 14, 48)) | 0x4000)
            set_word(lib, emu, record, 0, C.THING_ENDOFLIST)
            set_word(lib, emu, last, 0, thing)
            undo.append((last, record))
        return undo

    def remove(self, emu, undo):
        for last, record in reversed(undo):
            set_word(self.lib, emu, last, 0, C.THING_ENDOFLIST)
            set_word(self.lib, emu, record, 0, C.THING_NONE)


def test_minion_maps_match_original(port, map_ui, gdat):
    # The minion maps of the dungeon in the hand of the first champion, of each kind (no location, minion,
    # location, without options), with a minion (a link projectile in the possessions of a creature group) and a
    # marker at random squares, at random party places and directions, with the option buttons, over game ticks that
    # take the mana (and darken the map without mana), and clicks on the grid that send the minion. Some option
    # commands come from a click with its click feedback (the pressed option button). The screen, the
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
    maps = [C.THING_CONTAINER << C.THING_TYPE_SHIFT | i for i in range(lib.dungeon_thing_count(C.THING_CONTAINER))
            if lib.dungeon_thing_valid(C.THING_CONTAINER << C.THING_TYPE_SHIFT | i)
            and lib.item_is_minion_map(C.THING_CONTAINER << C.THING_TYPE_SHIFT | i)]
    assert len(maps) >= 4
    all_groups = static_groups(lib)
    groups = [g for g in all_groups if lib.creature_group_info(g[0])[0] & 1]  # static: the animation state is in the record
    places = squares_of(lib, (C.SQUARE_FLOOR,))
    marks = places + squares_of(lib, (C.SQUARE_PIT, C.SQUARE_STAIRS))
    # stairs up and open pits that lead to another level
    ways = [(m, x, y) for m, x, y in squares_of(lib, (C.SQUARE_STAIRS,)) if lib.dungeon_square(m, x, y) & C.SQUARE_STAIRS_UP and leads(lib, m, x, y, -1)] * 3 \
        + [(m, x, y) for m, x, y in squares_of(lib, (C.SQUARE_PIT,)) if lib.dungeon_square(m, x, y) & C.SQUARE_PIT_OPEN and leads(lib, m, x, y, 1)]
    busy = [g for g in all_groups if g[4] in (17, 19, 22, 26, 36, 37, 47)]
    moving = [g for g in all_groups if not lib.creature_group_info(g[0])[0] & 1]
    lib.creature_alloc_active_groups()
    activated = []
    pool = Pool(lib)
    free_projectile = next(C.THING_PROJECTILE << C.THING_TYPE_SHIFT | i for i in range(lib.dungeon_thing_count(C.THING_PROJECTILE))
                           if word(lib, C.THING_PROJECTILE << C.THING_TYPE_SHIFT | i, 0) == C.THING_NONE)
    draw_game_screen(lib, original)
    rng = random.Random(20)
    feedback = random.Random(21)  # the click feedback choices; rng keeps its sequence
    lib.input_init()
    place_set = set(places)
    time = Game.in_dll(lib, "g_game").game_time
    kinds, clicks, moved, dark, offsets, markers = set(), 0, 0, 0, set(), set()
    for n in range(64):
        thing = rng.choice(maps)
        kind = n % 4
        set_word(lib, emu, thing, 4, word(lib, thing, 4) & 0x1FFF | kind << 13)
        location = 0xFFFF
        if kind in (1, 2) or rng.random() < 0.5:
            m, x, y = rng.choice(marks if kind != 1 and rng.random() < 0.5 else ways)
            location = x | y << C.LOCATION_Y_SHIFT | m << C.LOCATION_MAP_SHIFT
        set_word(lib, emu, thing, 6, location)
        set_word(lib, emu, thing, 2, C.THING_ENDOFLIST)
        if kind == 1 and rng.random() < 0.8:
            group, gm, gx, gy, _ = rng.choice(groups if rng.random() < 0.5 else moving)
            set_word(lib, emu, free_projectile, 0, word(lib, group, 2))
            set_word(lib, emu, free_projectile, 2, thing)
            dx, dy = rng.randrange(-2, 3), rng.randrange(-2, 3)
            mx = min(max(gx + dx, 0), lib.dungeon_map_width(gm) - 1)
            my = min(max(gy + dy, 0), lib.dungeon_map_height(gm) - 1)
            set_word(lib, emu, free_projectile, 4, mx | my << C.LOCATION_Y_SHIFT | gm << C.LOCATION_MAP_SHIFT)
            set_word(lib, emu, free_projectile, 6, 0)
            set_word(lib, emu, group, 2, free_projectile)
            set_word(lib, emu, thing, 2, group)
            size = ctypes.c_uint32()
            lib.gdat_item(gdat, lib.gdat_find(gdat, C.CAT_CREATURE, word(lib, group, 4) & 0xFF, C.GDAT_TYPE_CREATURE_DATA, 0xFD), ctypes.byref(size))
            sequence = rng.randrange(max(1, size.value // 8))  # another animation frame: another cell of the minion
            if lib.creature_group_info(group)[0] & 1:
                set_word(lib, emu, group, 8, sequence)
                set_word(lib, emu, group, 10, 0)
            else:
                # a moving minion: its animation is in active group 1
                active = ctypes.c_void_p.in_dll(lib, "g_active_groups").value
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
        place_party(lib, emu, m, x, y, rng.randrange(C.DIR_COUNT))
        undo = pool.inject(emu, rng, m, x, y)
        champion_bytes(lib, 0, CH_SLOTS, struct.pack("<H", thing))
        champion_bytes(lib, 0, CH_MANA, struct.pack("<h", rng.choice((0, 2, 5, 60, 300))))
        sync_champions(lib, original)
        set_action(lib, original, champion=0)
        update_action_area(lib, original)
        open_hand(lib, original, 0)
        kinds.add((kind, ActionArea.in_dll(lib, "g_action").flags & 0x60))
        assert_map_state(lib, original, (n, "open", hex(thing), kind))
        for option in range(4):
            if rng.random() < 0.6:
                if ActionArea.in_dll(lib, "g_action").flags & MAP_HAS_OPTIONS and feedback.random() < 0.5:
                    click_feedback(lib, original, C.CMD_MAP_OPTION_FIRST + option, (n, "option"))
                else:
                    assert command(lib, emu, C.CMD_MAP_OPTION_FIRST + option) == (1, 1)
        mm = MinionMap.in_dll(lib, "g_minion_map")
        offsets.add((mm.x_offset, mm.y_offset))
        if word(lib, thing, 2) != C.THING_ENDOFLIST and rng.random() < 0.7:
            # the minion goes where the marker square leads: the grid shows the marker on its other level
            for m, x, y in rng.sample([(mm.above_map, mm.above_x, mm.above_y), (mm.below_map, mm.below_x, mm.below_y)], 2):
                if m >= 0:
                    set_word(lib, emu, free_projectile, 4, x | y << C.LOCATION_Y_SHIFT | m << C.LOCATION_MAP_SHIFT)
                    break
        for k in range(rng.randrange(10, 60)):
            r = rng.random()
            if r < 0.1:
                assert command(lib, emu, C.CMD_MAP_OPTION_FIRST + rng.randrange(4)) == (1, 1)
            elif r < 0.25 and kind == 1:
                rect = original.alloc(8)
                from dm2test.interface import ZONE_GET_RECT
                emu.call(ZONE_GET_RECT, C.ZONE_MAP_GRID, rect)
                zx, zy, zw, zh = struct.unpack("<4h", emu.read(rect, 8))
                before = word(lib, thing, 6)
                assert command(lib, emu, C.CMD_MINION_MAP, zx + rng.randrange(zw), zy + rng.randrange(zh)) == (1, 1)
                clicks += 1
                moved += word(lib, thing, 6) != before
            time += 1
            set_time(lib, emu, time)
            update_action_area(lib, original, tick=1)
            dark += not ActionArea.in_dll(lib, "g_action").flags & 0x200
            markers.add(MinionMap.in_dll(lib, "g_minion_map").marker_state)
            assert_map_state(lib, original, (n, k, hex(thing), kind))
        lib.action_area_close()
        emu.call(UI_ACTION_AREA_CLOSE)
        update_action_area(lib, original)
        assert_map_state(lib, original, (n, "close"))
        pool.remove(emu, undo)
        for group, w in activated:
            set_word(lib, emu, group, 4, w)
        activated.clear()
        if word(lib, thing, 2) != C.THING_ENDOFLIST:  # unlink the minion again
            group = word(lib, thing, 2)
            set_word(lib, emu, group, 2, word(lib, free_projectile, 0))
            set_word(lib, emu, free_projectile, 0, C.THING_NONE)
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
                while t != C.THING_ENDOFLIST and t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK <= C.THING_SENSOR:
                    w = word(lib, t, 2)
                    if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == C.THING_TEXT and w & 6 == 2 and w >> C.TEXT_SUBTYPE_SHIFT in (
                            C.SUBTYPE_WARP_ONCE, C.SUBTYPE_WARP):
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
