"""Scenario tests of Step 16 with the emulator oracle: projectiles, explosions, missile launcher sensors, and the
creature attacks on champions run in the C port and in the original code from start.DAT; the game state must stay
equal."""

import ctypes
import random
import struct

import pytest

from dm2test import constants as C
from dm2test.creature import CreatureScenario, random_champions
from dm2test.scenario import COMMAND_RECORD, ENDOFLIST, sensor_squares, square_event, squares
from dm2test.input import CHAMPION_UNCOMPARED  # redraw flags
from dm2test.scenario import gdat, start  # noqa: F401 (fixtures)
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

PROJECTILE_CREATE, EXPLOSION_CREATE, MOVE_CHECK_MISSILES = 0x16457, 0x16746, 0x4A0FE
SENSOR_LAUNCH_MISSILE, CHAMP_LAUNCH_PROJECTILE, CREATURE_ATTACK_CHAMPION = 0x57A63, 0x47773, 0x18758
CREATURE_GET_ORDERED_ATTACK_CELLS, DUN_UNLINK_THING, DUN_LINK_THING = 0x1869A, 0x1D542, 0x1D3DB
G_SQUARE_FIRST_THINGS = 0x7F388
G_THINGS, G_CHAMPIONS, G_CHAMPION_COUNT, G_PARTY_SLEEPING = 0x7F288, 0x7FBD0, 0x7F276, 0x7F234
CHAMP_WAKE_PARTY, G_LIGHT_LEVEL = 0x46D9C, 0x7F282
CHAMPION_SIZE, CHAMPION_CELL, CHAMPION_EXPERIENCE = C.CHAMPION_SIZE, 29, 0x5F
SCRATCH_OUT = COMMAND_RECORD + 0x40
EVENT_EXPLOSION, EVENT_PROJECTILE, EVENT_PROJECTILE_2 = C.EVENT_EXPLOSION, C.EVENT_PROJECTILE_MOVE, C.EVENT_PROJECTILE_MOVE_2
SPELLS = (C.SPELL_FIREBALL, C.SPELL_SLIME, C.SPELL_LIGHTNING, C.SPELL_HARM_NON_MATERIAL, C.SPELL_DOOR_TOGGLE,
          C.SPELL_POISON_BOLT, C.SPELL_POISON_CLOUD, C.SPELL_PUSH, C.SPELL_PULL, C.SPELL_DOOR_SET,
          C.SPELL_MISSILE_WALL)
EXPLOSIONS = SPELLS + (C.EXPLOSION_DEATH_CLOUD, C.EXPLOSION_STORM_LIGHTNING, C.EXPLOSION_STORM_BLAST,
                       C.EXPLOSION_SMOKE)


@pytest.fixture
def pj(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    s = CreatureScenario(lib, gdat, start, tmp_path)
    emu = s.emu
    s.woken = 0
    emu.stub(CHAMP_WAKE_PARTY, lambda *_: wake(s))
    lib.thing_word.restype = ctypes.c_uint16
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.creature_get_group_at.restype = ctypes.c_uint16
    return s


def wake(s):
    """champ_wake_party of the original without its screen and input part: the party wakes."""
    s.emu.write(G_PARTY_SLEEPING, bytes(4))
    s.woken += 1


def both(s, c_fn, addr, *args):
    """Calls the C function and the original with the same arguments; returns both results as int16."""
    mine = c_fn(*args)
    theirs = s.emu.call(addr, *args)
    return ctypes.c_int16(mine).value, ctypes.c_int16(theirs).value


def flying(s):
    """The number of projectile and explosion events in the C timeline."""
    from dm2test.save import Timeline
    tl = Timeline.in_dll(s.lib, "g_timeline")
    raw = ctypes.string_at(ctypes.cast(tl.events, ctypes.c_void_p), tl.capacity * C.TIMELINE_EVENT_SIZE)
    return sum(raw[i * C.TIMELINE_EVENT_SIZE + 4] in (EVENT_EXPLOSION, EVENT_PROJECTILE, EVENT_PROJECTILE_2) for i in range(tl.capacity))


def assert_saves(s, where):
    """The full check: the quick check and the champion records. (The save file comparison of test_move does not fit
    the creature scenario: the original save deactivates the creature groups of its active group table.)"""
    from dm2test.interface import champion_records
    check(s, where)
    mine, theirs = bytearray(champion_records(s.lib)), bytearray(s.emu.read(G_CHAMPIONS, C.CHAMPION_MAX * CHAMPION_SIZE))
    for champ in range(C.CHAMPION_MAX):
        for at, size in CHAMPION_UNCOMPARED:
            for i in range(champ * CHAMPION_SIZE + at, champ * CHAMPION_SIZE + at + size):
                mine[i] = theirs[i] = 0
    assert mine == theirs, where


def check(s, where):
    """The quick check of the creature scenario and the first-thing table of the squares."""
    lib = s.lib
    lib.dungeon_square_things.restype = ctypes.POINTER(ctypes.c_uint16)
    n = lib.dungeon_square_thing_count()
    mine = ctypes.string_at(lib.dungeon_square_things(), 2 * n)
    assert mine == s.emu.read(s.emu.read_u32(G_SQUARE_FIRST_THINGS), 2 * n), where
    s.assert_same(where)


def run_out(s, where, limit=120, every=40):
    """Steps until no projectile or explosion is left; compares the state after each step."""
    for n in range(limit):
        if not flying(s):
            break
        s.step()
        check(s, (where, n))
        if n % every == every - 1:
            assert_saves(s, (where, n))


def objects(lib):
    """(map, x, y, thing) of the objects on open squares: weapons, clothes, scrolls, potions, containers, misc."""
    out = []
    for map_, x, y, _ in squares(lib, (C.SQUARE_FLOOR,)):
        t = lib.dungeon_square_first_thing(map_, x, y)
        while t != ENDOFLIST:
            if C.THING_WEAPON <= t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK <= C.THING_MISC:
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
                    if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == C.THING_GROUP:
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
                and lib.dungeon_square(map_, px, py) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_FLOOR:
            s.place(map_, px, py, rng.randrange(C.DIR_COUNT))
            return
    s.place(map_, x, y, rng.randrange(C.DIR_COUNT))


def four_champions(s, rng):
    """Four champions (copies of the first) in random cells, in both."""
    from dm2test.interface import champion_bytes, champion_records, sync_champions
    lib = s.lib
    first = bytes(champion_records(lib)[:CHAMPION_SIZE])
    s.game.champion_count = C.CHAMPION_MAX
    s.emu.write(G_CHAMPION_COUNT, struct.pack("<H", C.CHAMPION_MAX))
    for champ, cell in enumerate(rng.sample(range(C.CELL_COUNT), C.CHAMPION_MAX)):
        champion_bytes(lib, champ, 0, first)
        champion_bytes(lib, champ, CHAMPION_CELL, [cell])
        champion_bytes(lib, champ, CHAMPION_CELL - 1, [rng.randrange(C.DIR_COUNT)])  # direction
        experience = b"".join(struct.pack("<I", rng.choice((0, 600, 5000, 40000, 300000))) for _ in range(C.SKILL_COUNT))
        champion_bytes(lib, champ, CHAMPION_EXPERIENCE, experience)
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
        check(pj, ("launch", n))
        run_out(pj, ("object", n, map_, x, y, t))
    assert_saves(pj, "end")


@pytest.mark.parametrize("part", range(2))
def test_spell_missiles(pj, lib, part):
    # Spell missiles of every kind fly from random squares near the party and near creatures: explosions, clouds,
    # door spells, push and pull, hits on creatures and champions.
    rng = random.Random(160 + part)
    four_champions(pj, rng)
    places = [p[:3] for p in groups(lib)] * 2 + [p[:3] for p in squares(lib, (C.SQUARE_FLOOR,))]
    for n in range(150):
        map_, x, y = rng.choice(places)
        place_near(pj, rng, map_, x, y)
        sx, sy = x + rng.randrange(-3, 4), y + rng.randrange(-3, 4)
        if not (0 <= sx < lib.dungeon_map_width(map_) and 0 <= sy < lib.dungeon_map_height(map_)) \
                or lib.dungeon_square(map_, sx, sy) >> C.SQUARE_TYPE_SHIFT != C.SQUARE_FLOOR:
            sx, sy = pj.game.party_x, pj.game.party_y
        spell = SPELLS[(n + part) % len(SPELLS)] if n % 3 else rng.choice(SPELLS)
        direction = toward(sx, sy, x, y) if (sx, sy) != (x, y) else rng.randrange(4)
        launch(pj, spell, sx, sy, rng.randrange(4), direction, rng.randrange(1, 256), rng.randrange(256),
               rng.randrange(1, 16))
        check(pj, ("launch", n))
        run_out(pj, ("spell", n, map_, sx, sy, hex(spell)))
    assert_saves(pj, "end")


def test_explosions(pj, lib):
    # Explosions of every type, centered or in a cell, on creatures, on the party, on doors, and on open squares.
    rng = random.Random(161)
    four_champions(pj, rng)
    places = [p[:3] for p in groups(lib)] + [p[:3] for p in squares(lib, (C.SQUARE_FLOOR, C.SQUARE_DOOR))]
    for n in range(200):
        map_, x, y = rng.choice(places)
        place_near(pj, rng, map_, x, y)
        if n % 5 == 0:
            x, y = pj.game.party_x, pj.game.party_y
        kind = EXPLOSIONS[n % len(EXPLOSIONS)]
        cell = rng.choice((0, 1, 2, 3, C.EXPLOSION_AT_CENTER))
        both(pj, lib.explosion_create, EXPLOSION_CREATE, kind, rng.randrange(256), x, y, cell)
        check(pj, ("create", n))
        run_out(pj, ("explosion", n, map_, x, y, hex(kind)), limit=300)
    assert_saves(pj, "end")




def safe_command(s, rng):
    """A random move or turn; no move onto pits, stairs, and teleporters (the creature scenario draws no fall)."""
    command = rng.choice((C.CMD_TURN_LEFT, C.CMD_TURN_RIGHT, C.CMD_MOVE_FORWARD, C.CMD_MOVE_FORWARD, C.CMD_MOVE_RIGHT,
                          C.CMD_MOVE_BACK, C.CMD_MOVE_LEFT))
    if command in (C.CMD_TURN_LEFT, C.CMD_TURN_RIGHT):
        return command
    lib, g = s.lib, s.game
    d = (g.party_dir + (0, 1, 2, 3)[command - C.CMD_MOVE_FORWARD]) & C.DIR_MASK
    x, y = g.party_x + (0, 1, 0, -1)[d], g.party_y + (-1, 0, 1, 0)[d]
    m = g.party_map
    if 0 <= x < lib.dungeon_map_width(m) and 0 <= y < lib.dungeon_map_height(m) and lib.dungeon_square(m, x, y) >> C.SQUARE_TYPE_SHIFT in (
            C.SQUARE_PIT, C.SQUARE_STAIRS, C.SQUARE_TELEPORTER):
        return C.CMD_TURN_LEFT
    return command


def test_party_walks_into_missiles(pj, lib):
    # Missiles fly toward the party from up to 3 squares; the party walks and turns meanwhile, into the missiles
    # and out of their way (move_check_missiles_hit_party in move_thing), and the missiles hit the party square.
    rng = random.Random(162)
    places = squares(lib, (C.SQUARE_FLOOR,))
    things = objects(lib)
    rng.shuffle(things)
    hits = 0
    for n in range(150):
        four_champions(pj, rng)
        map_, x, y, _ = rng.choice(places)
        d = rng.randrange(4)
        pj.place(map_, x, y, d)
        dist = rng.randrange(1, 4)
        sx, sy = x + (0, 1, 0, -1)[d] * dist, y + (-1, 0, 1, 0)[d] * dist
        if not (0 <= sx < lib.dungeon_map_width(map_) and 0 <= sy < lib.dungeon_map_height(map_)) \
                or lib.dungeon_square(map_, sx, sy) >> C.SQUARE_TYPE_SHIFT != C.SQUARE_FLOOR:
            sx, sy = x, y
        if n % 2 and things:
            tm, tx, ty, payload = things.pop()
            if tm != map_:
                continue
            take(pj, tx, ty, payload)
        else:
            payload = rng.choice(SPELLS)
        launch(pj, payload, sx, sy, rng.randrange(4), (d + 2) & 3, rng.randrange(20, 256), rng.randrange(256),
               rng.randrange(1, 4))
        for i in range(12):
            pj.step()
            if i % 3 == 0:
                mine, theirs = pj.command(safe_command(pj, rng))
                assert mine == theirs, (n, i)
            hits += bool(pj.damage)
            check(pj, (n, i, map_, x, y, hex(payload)))
        run_out(pj, ("rest", n))
    assert hits > 10
    assert_saves(pj, "end")


def test_missiles_hit_party_on_moves(pj, lib):
    # move_check_missiles_hit_party directly: projectiles in all cells of the square that the party leaves and of the
    # square that it enters, with 1 to 4 champions in random cells.
    rng = random.Random(163)
    places = squares(lib, (C.SQUARE_FLOOR,))
    hit = 0
    for n in range(150):
        random_champions(pj, rng)
        map_, x, y, _ = rng.choice(places)
        pj.place(map_, x, y, rng.randrange(4))
        d = rng.randrange(4)
        tx, ty = x + (0, 1, 0, -1)[d], y + (-1, 0, 1, 0)[d]
        if not (0 <= tx < lib.dungeon_map_width(map_) and 0 <= ty < lib.dungeon_map_height(map_)) \
                or lib.dungeon_square(map_, tx, ty) >> C.SQUARE_TYPE_SHIFT != C.SQUARE_FLOOR:
            tx, ty = x, y  # projectiles start only on open squares
        for _ in range(rng.randrange(1, 5)):
            sx, sy = rng.choice(((x, y), (tx, ty)))
            launch(pj, rng.choice(SPELLS), sx, sy, rng.randrange(4), rng.randrange(4), rng.randrange(1, 256),
                   rng.randrange(256), rng.randrange(16))
        for _ in range(rng.randrange(4)):
            pj.step()  # the first move at the second step: the events become 0x1e
        pj.lib.move_check_missiles_hit_party(x, y, tx, ty)
        pj.emu.call(MOVE_CHECK_MISSILES, x, y, tx, ty)
        hit += bool(pj.damage)
        check(pj, ("check", n, map_, x, y, tx, ty))
        run_out(pj, ("rest", n))
    assert hit > 20
    assert_saves(pj, "end")


LAUNCHER_TYPES = (C.LAUNCH_ITEM, C.LAUNCH_SPELL, C.LAUNCH_ITEMS, C.LAUNCH_SPELLS, C.LAUNCH_OBJECT, C.LAUNCH_OBJECTS)


def launchers(lib):
    """(map, x, y, cell) of the missile launcher sensors of the dungeon."""
    out = []
    for map_, x, y, cell in sensor_squares(lib):
        t = lib.dungeon_square_first_thing(map_, x, y)
        while t != ENDOFLIST:
            if t >> C.THING_TYPE_SHIFT & C.THING_TYPE_MASK == C.THING_SENSOR and t >> C.THING_CELL_SHIFT == cell and lib.thing_word(t, 2) & 0x7F in LAUNCHER_TYPES:
                out.append((map_, x, y, cell))
                break
            t = lib.dungeon_next_thing(t)
    return out


def test_missile_launchers(pj, lib):
    # Square events on every missile launcher sensor of the dungeon, each effect, then the flight of the missiles.
    rng = random.Random(164)
    cells = launchers(lib)
    assert cells
    pool = objects_by_map(lib)
    for rounds in range(3):
        for n, (map_, x, y, cell) in enumerate(cells):
            place_near(pj, rng, map_, x, y)
            for _ in range(rng.randrange(3)):  # objects for the launchers of the objects of their square
                if pool.get(map_):
                    ox, oy, t = pool[map_].pop()
                    take(pj, ox, oy, t)
                    t = t & C.THING_ID_MASK | (cell + rng.randrange(2)) % C.CELL_COUNT << C.THING_CELL_SHIFT
                    lib.dungeon_link_thing_to_square(t, map_, x, y)
                    pj.emu.call(DUN_LINK_THING, t, 0, x, y)
            square_event(pj, map_, x, y, cell, rng.randrange(4), rng.randrange(2))
            for _ in range(2):
                pj.step()
                check(pj, (rounds, n, map_, x, y, cell))
            run_out(pj, ("launcher", rounds, n))
    assert_saves(pj, "end")


def test_champions_throw(pj, lib):
    # champ_launch_projectile: champions throw objects and cast spells from the party square.
    rng = random.Random(165)
    things = objects(lib)
    rng.shuffle(things)
    for n, (map_, x, y, t) in enumerate(things[:80]):
        four_champions(pj, rng)
        place_near(pj, rng, map_, x, y)
        take(pj, x, y, t)
        champ = rng.randrange(4)
        payload = t if n % 2 else rng.choice(SPELLS)
        args = (payload, rng.randrange(-10, 300), rng.randrange(-10, 300), rng.randrange(-5, 20))
        lib.champ_launch_projectile(champ, *args)
        pj.emu.call(CHAMP_LAUNCH_PROJECTILE, G_CHAMPIONS + champ * CHAMPION_SIZE, *args)
        assert (pj.game.throw_cooldown, pj.game.throw_dir) == struct.unpack("<HH", pj.emu.read(0x7F25A, 2)
                                                                              + pj.emu.read(0x7F270, 2)), n
        check(pj, ("throw", n))
        run_out(pj, ("throw", n, hex(payload)))
    assert_saves(pj, "end")


def test_creature_attacks_champions(pj, lib):
    # creature_attack_champion for every creature group of the dungeon on random champions, awake and asleep,
    # visible and invisible, in light and darkness: the damage hook arguments and the random numbers.
    from dm2test.interface import set_magic
    rng = random.Random(166)
    light = ctypes.c_int.in_dll(lib, "g_view_light_level")
    sleeping = ctypes.c_int.in_dll(lib, "party_sleeping")
    attacks = woke = 0
    for n, (map_, x, y, g) in enumerate(groups(lib) * 3):
        four_champions(pj, rng)
        place_near(pj, rng, map_, x, y)
        set_magic(lib, pj, rng.randrange(2) * rng.randrange(1, 50), 0)
        light.value = rng.randrange(6)
        pj.emu.write(G_LIGHT_LEVEL, struct.pack("<h", light.value))
        sleeping.value = asleep = int(rng.random() < 0.1)
        pj.emu.write(G_PARTY_SLEEPING, struct.pack("<i", asleep))
        woken = pj.woken
        champ = rng.randrange(5)
        record = pj.emu.read_u32(G_THINGS + 4 * 4) + (g & C.THING_INDEX_MASK) * 16
        mine = ctypes.c_int16(lib.creature_attack_champion(g, champ)).value
        theirs = ctypes.c_int16(pj.emu.call(CREATURE_ATTACK_CHAMPION, record, champ)).value
        assert mine == theirs, (n, g, champ)
        assert sleeping.value == struct.unpack("<i", pj.emu.read(G_PARTY_SLEEPING, 4))[0], (n, g, champ)
        woke += pj.woken - woken
        attacks += bool(pj.damage)
        check(pj, ("attack", n, map_, x, y, hex(g), champ))
    assert attacks > 50 and woke > 5
    sleeping.value = 0


class Location(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16)]


def test_ordered_attack_cells(pj, lib):
    # creature_order_attack_cells from all directions and cells, and in random order.
    rng = random.Random(167)
    order = (ctypes.c_uint8 * 4)()
    for n in range(400):
        x, y = rng.randrange(32), rng.randrange(32)
        px, py = x + rng.randrange(-2, 3), y + rng.randrange(-2, 3)
        cell = rng.choice((0, 1, 2, 3, C.CELL_ANY))
        lib.creature_order_attack_cells(order, Location(x, y), Location(px, py), cell)
        pj.emu.call(CREATURE_GET_ORDERED_ATTACK_CELLS, SCRATCH_OUT, x, y, px, py, cell)
        assert bytes(order) == pj.emu.read(SCRATCH_OUT, 4), (n, x, y, px, py, cell)
        assert ctypes.c_uint32.in_dll(lib, "g_random_seed").value == pj.emu.read_u32(0x70518), n



DIRS = ((0, -1), (1, 0), (0, 1), (-1, 0))


def objects_by_map(lib):
    out = {}
    for map_, x, y, t in objects(lib):
        out.setdefault(map_, []).append((x, y, t))
    return out


def open_square(lib, map_, x, y):
    return 0 <= x < lib.dungeon_map_width(map_) and 0 <= y < lib.dungeon_map_height(map_) \
        and lib.dungeon_square(map_, x, y) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_FLOOR


def payload_for(s, rng, pool, map_, n):
    """An object of map_ (taken from its square) for odd n while any is left, else a random spell."""
    if n % 2 and pool.get(map_):
        x, y, t = pool[map_].pop()
        take(s, x, y, t)
        return t
    return rng.choice(SPELLS)


def test_projectiles_at_creatures(pj, lib):
    # Objects and spells fly at every creature group from 1 to 3 squares in all directions and cells: hits, misses,
    # bounces, objects that creatures catch, spells that creatures reflect, damage and alerts.
    rng = random.Random(168)
    four_champions(pj, rng)
    pool = objects_by_map(lib)
    for p in pool.values():
        rng.shuffle(p)
    shots = 0
    for n, (map_, x, y, g) in enumerate(groups(lib) * 2):
        d = rng.randrange(4)
        dist = rng.randrange(1, 4)
        sx, sy = x - DIRS[d][0] * dist, y - DIRS[d][1] * dist
        if not open_square(lib, map_, sx, sy):
            continue
        place_near(pj, rng, map_, sx, sy)
        payload = payload_for(pj, rng, pool, map_, n)
        launch(pj, payload, sx, sy, rng.randrange(4), d, rng.randrange(1, 256), rng.randrange(256), rng.randrange(1, 8))
        shots += 1
        check(pj, ("launch", n))
        run_out(pj, ("creature", n, map_, x, y, hex(g), hex(payload)))
    assert shots > 150
    assert_saves(pj, "end")


CREATURE_ALERT, ALERT_HIT = 0x24E62, 0x200D  # the alert of a projectile hit
INFO_DOOR_FLAGS, RESISTS_NON_FIRE = 0x19, C.CREATURE_RESISTS_NON_FIRE  # creature info byte and its flag: only fire hurts the creature fully


def group_square(lib, map_, group):
    """(x, y) of group on map_, None when it is gone."""
    for x in range(lib.dungeon_map_width(map_)):
        for y in range(lib.dungeon_map_height(map_)):
            t = lib.dungeon_square_first_thing(map_, x, y)
            while t != ENDOFLIST:
                if t & C.THING_ID_MASK == group & C.THING_ID_MASK:
                    return x, y
                t = lib.dungeon_next_thing(t)
    return None


def test_attacks_on_creatures_that_resist_non_fire(pj, lib):
    # Lightning blasts, storm blasts, and poison and death clouds on the groups that resist non-fire, and lightning
    # bolts from the square of the group at shot time and from 1 and 2 squares away: the damage is a quarter.
    # start.DAT has one such group (creature type 84).
    rng = random.Random(170)
    four_champions(pj, rng)
    lib.creature_group_info.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.creature_group_info.argtypes = [ctypes.c_uint16]
    targets = [t for t in groups(lib) if lib.creature_group_info(t[3])[INFO_DOOR_FLAGS] & RESISTS_NON_FIRE]
    assert targets
    hits = [0]
    resisting = {t[3] & C.THING_ID_MASK for t in targets}

    def alert(uc, *_):  # creature_alert(group, x, y, flags, chance, damage) of the original
        damage = struct.unpack("<h", pj.emu.read(uc.reg_read(UC_X86_REG_ESP) + 8, 2))[0]
        if uc.reg_read(UC_X86_REG_EAX) & C.THING_ID_MASK in resisting and uc.reg_read(UC_X86_REG_ECX) & 0xFFFF == ALERT_HIT and damage > 0:
            hits[0] += 1

    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP
    pj.emu.uc.hook_add(UC_HOOK_CODE, alert, begin=CREATURE_ALERT, end=CREATURE_ALERT)
    for n, (map_, x, y, g) in enumerate(targets * (40 // len(targets))):
        place_near(pj, rng, map_, x, y)
        kind = rng.choice((C.SPELL_LIGHTNING, C.EXPLOSION_STORM_BLAST, C.SPELL_POISON_CLOUD, C.EXPLOSION_DEATH_CLOUD))  # lightning, storm blast, poison cloud, death cloud
        both(pj, lib.explosion_create, EXPLOSION_CREATE, kind, rng.randrange(64, 256), x, y, rng.choice((0, 1, 2, 3, C.EXPLOSION_AT_CENTER)))
        check(pj, ("create", n))
        run_out(pj, ("resists", n, map_, x, y, hex(g), hex(kind)))
        at = group_square(lib, map_, g)  # the group moves: aim at its square now
        if at is None:
            continue
        gx, gy = at
        d, dist = rng.randrange(4), rng.randrange(3)
        sx, sy = gx - DIRS[d][0] * dist, gy - DIRS[d][1] * dist
        if dist and not open_square(lib, map_, sx, sy):
            sx, sy = gx, gy
        place_near(pj, rng, map_, gx, gy)
        launch(pj, C.SPELL_LIGHTNING, sx, sy, rng.randrange(4), d, 200, 200, 1)
        check(pj, ("bolt", n))
        run_out(pj, ("bolt", n, map_, sx, sy, d))
    assert_saves(pj, "end")
    assert hits[0] > 3, hits  # bolts that hurt a group that resists non-fire


def set_square(s, map_, x, y, value):
    """Sets a square byte in both."""
    s.lib.dungeon_square_byte.restype = ctypes.POINTER(ctypes.c_uint8)
    s.lib.dungeon_square_byte(map_, x, y)[0] = value
    emu = s.emu
    table = emu.read_u32(emu.read_u32(0x7F3C8) + 4 * map_)
    emu.write(emu.read_u32(table + 4 * x) + y, bytes([value]))


def test_projectiles_at_doors(pj, lib):
    # Objects, spells, and explosions at every door in each door state: grates, breaking doors, door spells, door
    # buttons; then the timeline runs the door events that they queue.
    rng = random.Random(169)
    four_champions(pj, rng)
    pool = objects_by_map(lib)
    for p in pool.values():
        rng.shuffle(p)
    shots = 0
    for n, (map_, x, y, square) in enumerate(squares(lib, (C.SQUARE_DOOR,)) * 2):
        set_square(pj, map_, x, y, square & ~7 | rng.randrange(6))
        d = rng.randrange(4)
        dist = rng.randrange(1, 3)
        sx, sy = x - DIRS[d][0] * dist, y - DIRS[d][1] * dist
        if open_square(lib, map_, sx, sy):
            place_near(pj, rng, map_, sx, sy)
            payload = payload_for(pj, rng, pool, map_, n)
            launch(pj, payload, sx, sy, rng.randrange(4), d, rng.randrange(1, 256), rng.randrange(256),
                   rng.randrange(1, 8))
            shots += 1
        else:
            place_near(pj, rng, map_, x, y)
            both(pj, lib.explosion_create, EXPLOSION_CREATE, rng.choice((C.SPELL_FIREBALL, C.SPELL_LIGHTNING, C.SPELL_DOOR_TOGGLE, C.SPELL_DOOR_SET,
                         C.EXPLOSION_STORM_LIGHTNING)),
                 rng.randrange(256), x, y, C.EXPLOSION_AT_CENTER)
        check(pj, ("launch", n))
        run_out(pj, ("door", n, map_, x, y, hex(square)))
    assert shots > 25
    assert_saves(pj, "end")


def test_projectiles_at_wall_sensors(pj, lib):
    # Objects fly at every wall cell with a sensor or a text: hit sensors, portal sensors, alcoves that catch them.
    rng = random.Random(170)
    four_champions(pj, rng)
    pool = objects_by_map(lib)
    for p in pool.values():
        rng.shuffle(p)
    wanted = [(m, x, y, t) for m, x, y, t in objects(lib) if lib.item_get_index(t) == 0x13A]  # the item of the sensors
    shots = 0
    for n, (map_, x, y, cell) in enumerate(c for c in sensor_squares(lib) if lib.dungeon_square(c[0], c[1], c[2]) >> C.SQUARE_TYPE_SHIFT == C.SQUARE_WALL):
        dist = rng.randrange(1, 3)
        sx, sy = x + DIRS[cell][0] * dist, y + DIRS[cell][1] * dist
        if not open_square(lib, map_, sx, sy):
            continue
        if wanted and n % 3 == 0:
            om, ox, oy, payload = wanted.pop()
            place_near(pj, rng, om, ox, oy)
            if pj.party.cur_map != om:
                continue
            take(pj, ox, oy, payload)
            place_near(pj, rng, map_, sx, sy)
        else:
            place_near(pj, rng, map_, sx, sy)
            if not pool.get(map_):
                continue
            px, py, payload = pool[map_].pop()
            take(pj, px, py, payload)
        launch(pj, payload, sx, sy, rng.randrange(4), (cell + 2) & 3, rng.randrange(10, 256), rng.randrange(256), 1)
        shots += 1
        check(pj, ("launch", n))
        run_out(pj, ("wall", n, map_, x, y, cell, hex(payload)))
    assert shots > 40
    assert_saves(pj, "end")


def test_missile_walls_and_packed_explosions(pj, lib):
    # Spells fly into a lasting missile wall (explosion 0xff8e) and turn back; explosions get the packed squares of a
    # hit (x above 0xff: the hit square in the low bytes, the explosion square in the high bytes).
    rng = random.Random(171)
    four_champions(pj, rng)
    places = squares(lib, (C.SQUARE_FLOOR,))
    for n in range(120):
        map_, x, y, _ = rng.choice(places)
        d = rng.randrange(4)
        sx, sy = x - DIRS[d][0] * 2, y - DIRS[d][1] * 2
        if not open_square(lib, map_, sx, sy):
            continue
        place_near(pj, rng, map_, sx, sy)
        if n % 2:
            both(pj, lib.explosion_create, EXPLOSION_CREATE, C.SPELL_MISSILE_WALL, rng.randrange(20, 256), x, y, C.EXPLOSION_AT_CENTER)
            launch(pj, rng.choice(SPELLS), sx, sy, rng.randrange(4), d, rng.randrange(1, 256), rng.randrange(256), 1)
        else:
            hx, hy = x - DIRS[d][0], y - DIRS[d][1]
            both(pj, lib.explosion_create, EXPLOSION_CREATE, rng.choice((C.SPELL_FIREBALL, C.SPELL_LIGHTNING, C.EXPLOSION_STORM_LIGHTNING, C.EXPLOSION_STORM_BLAST,
                         C.SPELL_POISON_CLOUD)),
                 rng.randrange(256), hx | (x + 1) << 8, hy | y << 8, rng.randrange(4))
        check(pj, ("start", n))
        run_out(pj, ("wall", n, map_, x, y), limit=300)
    assert_saves(pj, "end")
