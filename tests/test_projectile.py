"""Scenario tests of Step 16 with the emulator oracle: projectiles, explosions, missile launcher sensors, and the
creature attacks on champions run in the C port and in the original code from start.DAT; the game state must stay
equal."""

import ctypes
import random
import struct

import pytest

import test_creature as tc
from test_move import COMMAND_RECORD, ENDOFLIST, TIMELINE_ADD_EVENT, sensor_squares, square_event, squares
from test_move import gdat, start  # noqa: F401 (fixtures)

PROJECTILE_CREATE, EXPLOSION_CREATE, MOVE_CHECK_MISSILES = 0x16457, 0x16746, 0x4A0FE
SENSOR_LAUNCH_MISSILE, CHAMP_LAUNCH_PROJECTILE, CREATURE_ATTACK_CHAMPION = 0x57A63, 0x47773, 0x18758
CREATURE_GET_ORDERED_ATTACK_CELLS, DUN_UNLINK_THING = 0x1869A, 0x1D542
# Step 15 rules that the C port leaves as hooks: (address, stack arguments, result) as in hooks.c.
STEP15_STUBS = {
    0x46968: (0, 0),    # champ_get_dexterity
    0x46786: (0, 0),    # champ_test_luck
    0x46745: (0, 0),    # champ_get_stat_adjusted_attack
    0x474FC: (0, 0),    # champ_poison
}
G_THINGS, G_CHAMPIONS, G_CHAMPION_COUNT = 0x7F288, 0x7FBD0, 0x7F276
CHAMPION_SIZE, CHAMPION_CELL = 0x107, 29
SCRATCH_OUT = COMMAND_RECORD + 0x40
EVENT_EXPLOSION, EVENT_PROJECTILE, EVENT_PROJECTILE_2 = 0x19, 0x1D, 0x1E
SPELLS = (0xFF80, 0xFF81, 0xFF82, 0xFF83, 0xFF84, 0xFF86, 0xFF87, 0xFF89, 0xFF8A, 0xFF8D, 0xFF8E)
EXPLOSIONS = SPELLS + (0xFFA8, 0xFFB0, 0xFFB1, 0xFFE4)


@pytest.fixture
def pj(lib, gdat, start, tmp_path, monkeypatch):
    monkeypatch.setattr(tc, "PORTED", tc.PORTED + (MOVE_CHECK_MISSILES, SENSOR_LAUNCH_MISSILE))
    not_yet = dict(tc.NOT_YET)
    del not_yet[EXPLOSION_CREATE]
    monkeypatch.setattr(tc, "NOT_YET", not_yet)
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    s = tc.CreatureScenario(lib, gdat, start, tmp_path)
    emu = s.emu
    for addr, (stack, result) in STEP15_STUBS.items():
        emu.stub(addr, lambda *_, r=result: r, stack_args=stack)
    clear_unset_event_bytes(emu)
    return s


def clear_unset_event_bytes(emu):
    """The original leaves data bytes 4 and 5 of projectile and explosion events uninitialized; the C port clears them."""
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX

    def hook(uc, *_):
        event = uc.reg_read(UC_X86_REG_EAX)
        if emu.read(event + 4, 1)[0] in (EVENT_EXPLOSION, EVENT_PROJECTILE, EVENT_PROJECTILE_2):
            emu.write(event + 10, bytes(2))

    emu.uc.hook_add(UC_HOOK_CODE, hook, begin=TIMELINE_ADD_EVENT, end=TIMELINE_ADD_EVENT)


def both(s, c_fn, addr, *args):
    """Calls the C function and the original with the same arguments; returns both results as int16."""
    mine = c_fn(*args)
    theirs = s.emu.call(addr, *args)
    return ctypes.c_int16(mine).value, ctypes.c_int16(theirs).value


def flying(s):
    """The number of projectile and explosion events in the C timeline."""
    import test_save
    tl = test_save.Timeline.in_dll(s.lib, "timeline")
    raw = ctypes.string_at(ctypes.cast(tl.events, ctypes.c_void_p), tl.capacity * 12)
    return sum(raw[i * 12 + 4] in (EVENT_EXPLOSION, EVENT_PROJECTILE, EVENT_PROJECTILE_2) for i in range(tl.capacity))


def assert_saves(s, where):
    """The full check: the quick check and the champion records. (The save file comparison of test_move does not fit
    the creature scenario: the original save deactivates the creature groups of its active group table.)"""
    from test_interface import champion_records
    s.assert_creatures(where)
    assert bytes(champion_records(s.lib)) == s.emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE), where


def run_out(s, where, limit=120, every=40):
    """Steps until no projectile or explosion is left; compares the state after each step."""
    for n in range(limit):
        if not flying(s):
            break
        s.step()
        s.assert_creatures((where, n))
        if n % every == every - 1:
            assert_saves(s, (where, n))


def objects(lib):
    """(map, x, y, thing) of the objects on open squares: weapons, clothes, scrolls, potions, containers, misc."""
    out = []
    for map_, x, y, _ in squares(lib, (1,)):
        t = lib.dungeon_square_first_thing(map_, x, y)
        while t != ENDOFLIST:
            if 5 <= t >> 10 & 15 <= 10:
                out.append((map_, x, y, t))
            t = lib.dungeon_next_thing(t)
    return out


def groups(lib):
    """(map, x, y, group) of the creature groups of the dungeon."""
    out = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                t = lib.dungeon_square_first_thing(map_, x, y)
                while t != ENDOFLIST:
                    if t >> 10 & 15 == 4:
                        out.append((map_, x, y, t))
                    t = lib.dungeon_next_thing(t)
    return out


def take(s, x, y, thing):
    """Removes an object from its square of the current map in both."""
    s.lib.dungeon_unlink_thing_from_square(thing, s.party.cur_map, x, y)
    s.emu.call(DUN_UNLINK_THING, thing, 0, x, y)


def launch(s, payload, x, y, cell, direction, energy, attack, step):
    s.lib.projectile_create(payload, x, y, cell, direction, energy, attack, step)
    s.emu.call(PROJECTILE_CREATE, payload, x, y, cell, direction, energy, attack, step)


def place_near(s, rng, map_, x, y):
    """Puts the party on map_ near (x, y) (or on it) so that the current map is map_."""
    lib = s.lib
    for _ in range(20):
        px, py = x + rng.randrange(-3, 4), y + rng.randrange(-3, 4)
        if 0 <= px < lib.dungeon_map_width(map_) and 0 <= py < lib.dungeon_map_height(map_) \
                and lib.dungeon_square(map_, px, py) >> 5 == 1:
            s.place(map_, px, py, rng.randrange(4))
            return
    s.place(map_, x, y, rng.randrange(4))


def four_champions(s, rng):
    """Four champions (copies of the first) in random cells, in both."""
    from test_interface import champion_bytes, champion_records, sync_champions
    lib = s.lib
    first = bytes(champion_records(lib)[:CHAMPION_SIZE])
    s.game.champion_count = 4
    s.emu.write(G_CHAMPION_COUNT, struct.pack("<H", 4))
    for champ, cell in enumerate(rng.sample(range(4), 4)):
        champion_bytes(lib, champ, 0, first)
        champion_bytes(lib, champ, CHAMPION_CELL, [cell])
        champion_bytes(lib, champ, CHAMPION_CELL - 1, [rng.randrange(4)])  # direction
    sync_champions(lib, s)


def toward(x1, y1, x2, y2):
    """The direction from (x1, y1) to (x2, y2) along the longer axis."""
    if abs(x2 - x1) > abs(y2 - y1):
        return 1 if x2 > x1 else 3
    return 2 if y2 > y1 else 0


def test_thrown_objects(pj, lib):
    # Objects of the dungeon fly from random squares in all directions with random power: flight, walls, doors,
    # wall sensors, creatures, the party square, and the fall of the object.
    rng = random.Random(16)
    four_champions(pj, rng)
    things = objects(lib)
    rng.shuffle(things)
    for n, (map_, x, y, t) in enumerate(things[:120]):
        place_near(pj, rng, map_, x, y)
        take(pj, x, y, t)
        launch(pj, t, x, y, rng.randrange(4), rng.randrange(4), rng.randrange(1, 256), rng.randrange(256),
               rng.randrange(16))
        pj.assert_creatures(("launch", n))
        run_out(pj, ("object", n, map_, x, y, t))
    assert_saves(pj, "end")


@pytest.mark.parametrize("part", range(2))
def test_spell_missiles(pj, lib, part):
    # Spell missiles of every kind fly from random squares near the party and near creatures: explosions, clouds,
    # door spells, push and pull, hits on creatures and champions.
    rng = random.Random(160 + part)
    four_champions(pj, rng)
    places = [p[:3] for p in groups(lib)] * 2 + [p[:3] for p in squares(lib, (1,))]
    for n in range(150):
        map_, x, y = rng.choice(places)
        place_near(pj, rng, map_, x, y)
        sx, sy = x + rng.randrange(-3, 4), y + rng.randrange(-3, 4)
        if not (0 <= sx < lib.dungeon_map_width(map_) and 0 <= sy < lib.dungeon_map_height(map_)) \
                or lib.dungeon_square(map_, sx, sy) >> 5 != 1:
            sx, sy = pj.game.party_x, pj.game.party_y
        spell = SPELLS[(n + part) % len(SPELLS)] if n % 3 else rng.choice(SPELLS)
        direction = toward(sx, sy, x, y) if (sx, sy) != (x, y) else rng.randrange(4)
        launch(pj, spell, sx, sy, rng.randrange(4), direction, rng.randrange(1, 256), rng.randrange(256),
               rng.randrange(1, 16))
        pj.assert_creatures(("launch", n))
        run_out(pj, ("spell", n, map_, sx, sy, hex(spell)))
    assert_saves(pj, "end")


def test_explosions(pj, lib):
    # Explosions of every type, centered or in a cell, on creatures, on the party, on doors, and on open squares.
    rng = random.Random(161)
    four_champions(pj, rng)
    places = [p[:3] for p in groups(lib)] + [p[:3] for p in squares(lib, (1, 4))]
    for n in range(200):
        map_, x, y = rng.choice(places)
        place_near(pj, rng, map_, x, y)
        if n % 5 == 0:
            x, y = pj.game.party_x, pj.game.party_y
        kind = EXPLOSIONS[n % len(EXPLOSIONS)]
        cell = rng.choice((0, 1, 2, 3, 0xFF))
        both(pj, lib.explosion_create, EXPLOSION_CREATE, kind, rng.randrange(256), x, y, cell)
        pj.assert_creatures(("create", n))
        run_out(pj, ("explosion", n, map_, x, y, hex(kind)), limit=300)
    assert_saves(pj, "end")


