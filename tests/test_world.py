"""Step 17 tests with the emulator oracle: weather, day and night, the party light, and the object transfer sensors
(Skullkeep machines). start.DAT goes into the C port and into the original code (test_move.Scenario); both run the
same calls; then the weather, the light, the sky objects, the dungeon, and the timeline must be equal."""

import ctypes
import random
import struct

import pytest

from test_interface import CHAMPION_SIZE, GAME_CHAMPIONS, GAME_LEADER_HAND, GAME_MAGIC, G_LEADER_HAND, champion_bytes, sync_champions
from test_move import (  # noqa: F401 (gdat and start are fixtures)
    COMMAND_RECORD, G_SKY_OBJECTS, LIGHT_UPDATE_PARTY_LIGHT, NONE, SKY_OBJECTS_SIZE, WEATHER, WORLD_UPDATE_DAY_NIGHT,
    Scenario, WeatherState, WorldGame, gdat, sky_list, square_event, squares, start,
)
from test_save import Game, Timeline

WEATHER_INIT, TIMELINE_DELETE_EVENT, LIGHT_ADD_SQUARE_LIGHT, ZONE_INTERPOLATE_OFFSET = 0x59F38, 0x562FF, 0x38C46, 0x19234
MOVE_THING, VIEW_SEED_ADDR = 0x4B108, 0x70518
G_GLOBAL_BYTES, G_INVENTORY, G_PARTY_SLEEPING, G_MAGIC_LIGHT = 0x7F0C0, 0x7F972, 0x7F234, 0x7FFEC
G_THINGS, THING_SIZES = 0x7F288, (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
EVENT_WEATHER, CHAMPION_HANDS = 0x54, 0xC3
OUTDOOR_MAPS = (1, 14, 15, 16, 17, 25, 27, 29, 30, 33, 39, 41, 42)  # sky state 1 or 4: the weather shows
MAP_LIGHTNING_TARGET = 1  # its graphics set gives a fixed square that lightning hits
INDOOR_MAPS = (2, 5, 11, 20, 38)  # no weather: the hour does not update the light


@pytest.fixture
def world(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    lib.view_sky_objects.restype = ctypes.c_void_p
    lib.item_attr.restype = ctypes.c_uint16
    lib.dungeon_thing_record.restype = ctypes.c_void_p
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    sc = Scenario(lib, gdat, start, tmp_path)
    sc.emu.uc.hook_del(sc.view.weather_init_stub)
    return sc


def set_weather(sc, **values):
    """Writes weather globals (names of test_move.WEATHER) into both."""
    lib, emu = sc.lib, sc.emu
    game, weather = WorldGame.in_dll(lib, "game"), WeatherState.in_dll(lib, "weather")
    for name, value in values.items():
        if name == "light_level":
            ctypes.c_int.in_dll(lib, "view_light_level").value = value
        elif name in ("light_source_sum", "explosion_light_sum"):
            ctypes.c_int16.in_dll(lib, name).value = value
        elif hasattr(weather, name):
            setattr(weather, name, value)
        else:
            setattr(game, name, value)
        addr, fmt = WEATHER[name]
        emu.write(addr, struct.pack(fmt, value))


def set_global(sc, var, value):
    sc.lib.dun_modify_global_var(var, value, 6)
    sc.emu.write(G_GLOBAL_BYTES + var - 0x40, bytes([value]))


def assert_world(sc, where):
    """Weather, light, sky, explosions, script bytes 0x40 and 0x41, random seed, dungeon, and timeline."""
    sc.assert_same_weather(where)
    for var in (0x40, 0x41):
        assert sc.lib.dun_get_global_var(var) == sc.emu.read(G_GLOBAL_BYTES + var - 0x40, 1)[0], (where, var)
    assert ctypes.c_uint32.in_dll(sc.lib, "random_seed").value == sc.emu.read_u32(VIEW_SEED_ADDR), where
    assert sc.differences() == [], where


def delete_weather_events(sc):
    """Removes the queued storm steps in both: a direct call adds one each time."""
    tl = Timeline.in_dll(sc.lib, "timeline")
    for i in range(tl.capacity):
        if tl.events[i].type == EVENT_WEATHER:
            sc.lib.timeline_delete_event(i)
            sc.emu.call(TIMELINE_DELETE_EVENT, i)


def random_weather(rng):
    return dict(heavy_rain=rng.randrange(2), wind_dir=rng.randrange(4), rain_intensity=rng.choice((0, 0, 1, 0x20, 0x90)),
                sky_darkness=rng.randrange(256), cloud_cover=rng.choice((0, 0x30, 0x50, 0x90, 0xD0, 0xFF)),
                storm_multiplier=rng.randrange(1, 4), storm_step=rng.randrange(32), storm_curve=rng.randrange(4),
                storm_intensity=rng.choice((0, 0, rng.randrange(256), 0xB5, 0xB6, 0xCC, 0xCD, 0xD0, 0xFF)),
                hour_light=rng.randrange(1, 6), thunder_toggle=rng.randrange(2), lightning_flash=rng.randrange(2))


def test_weather_init_matches_original(world):
    # A new weather cycle (the first of a run and a later one) and a load that keeps the weather.
    rng = random.Random(170)
    for n in range(60):
        world.set_time(rng.randrange(1 << 20))
        set_weather(world, first_init=rng.randrange(2), storm_previous=rng.randrange(256), **random_weather(rng))
        keep = rng.randrange(2)
        world.lib.weather_init(keep)
        world.emu.call(WEATHER_INIT, keep)
        assert_world(world, n)
        delete_weather_events(world)
    world.assert_equal("end")


def test_storm_steps_match_original(world):
    # Storm steps (timeline event 0x54) of all curves; the last step starts a new weather cycle.
    rng = random.Random(171)
    for n in range(300):
        set_weather(world, **random_weather(rng))
        if rng.random() < 0.2:
            set_weather(world, storm_step=31)
        world.lib.world_update_day_night(1)
        world.emu.call(WORLD_UPDATE_DAY_NIGHT, 1)
        assert_world(world, n)
        delete_weather_events(world)
    world.assert_equal("end")


def draw_both(sc):
    """The view of the party in both, each with its own light level and sky objects."""
    import test_view as tv
    lib, emu, game = sc.lib, sc.emu, sc.game
    light = struct.unpack("<h", emu.read(WEATHER["light_level"][0], 2))[0]
    sky = emu.read(G_SKY_OBJECTS, SKY_OBJECTS_SIZE)
    seed = ctypes.c_uint32.in_dll(lib, "random_seed")
    saved = seed.value
    kept = [(addr, emu.read(addr, 4)) for addr in (tv.G_PARTY_MOVE_STEPS, tv.G_PUSH_MODE, tv.G_PUSH_PENDING_GROUP)]
    steps, sc.party.move_steps = sc.party.move_steps, 0  # the view of the party square, not of a walk
    theirs = sc.view.draw(game.party_map, game.party_x, game.party_y, game.party_dir, light, sky)
    for addr, data in kept:
        emu.write(addr, data)
    seed.value = tv.VIEW_SEED
    viewport = tv.Bitmap.in_dll(lib, "view_viewport")
    ctypes.memset(viewport.pixels, 0, tv.VIEW_W * tv.VIEW_H)
    lib.view_draw_dungeon(game.party_dir, game.party_x, game.party_y)
    mine = ctypes.string_at(viewport.pixels, tv.VIEW_W * tv.VIEW_H)
    sc.party.move_steps = steps
    seed.value = saved
    emu.write_u32(VIEW_SEED_ADDR, saved)
    return mine, theirs


@pytest.mark.parametrize("part", range(3))
def test_weather_steps_on_outdoor_maps(world, lib, part):
    # Game steps on the outdoor maps: hours, rain, clouds, storm steps, lightning that strikes squares, creatures, and
    # the party square (late, in a strong storm), a fixed lightning square, bolts and thunder, the flash; the party
    # walks, opens the inventory, or sleeps. Every 10 steps both draw the view with their light level and sky.
    rng = random.Random(172 + part)
    sc = world
    strikes = bolts = pictures = 0
    for n in range(14):
        map_ = MAP_LIGHTNING_TARGET if n % 5 == 0 else rng.choice(OUTDOOR_MAPS) if n % 4 else rng.choice(INDOOR_MAPS)
        _, x, y, _ = rng.choice([s for s in squares(lib, (1,)) if s[0] == map_])
        sc.place(map_, x, y, rng.randrange(4))
        sc.set_time(rng.choice((rng.randrange(12000), 12000 + rng.randrange(1 << 20))))
        weather = random_weather(rng)
        weather["next_hour_time"] = sc.time() + rng.randrange(-5, 40)
        set_weather(sc, strike_threshold=rng.randrange(4, 8), light_level=rng.randrange(6), **weather)
        set_global(sc, 0x40, rng.choice((0, 3, 9, 30)))
        inventory, sleeping = rng.random() < 0.3, rng.random() < 0.3
        ctypes.c_int16.in_dll(lib, "ui").value = inventory  # UiState.inventory
        sc.emu.write(G_INVENTORY, struct.pack("<h", inventory))
        ctypes.c_int.in_dll(lib, "party_sleeping").value = sleeping
        sc.emu.write_u32(G_PARTY_SLEEPING, sleeping)
        for i in range(40):
            sc.step()
            if rng.random() < 0.1:
                command = rng.choice((1, 2, 3, 4, 5, 6))
                mine, theirs = sc.command(command)
                assert mine == theirs, (n, i, command)
            strikes += len(sc.explosions)
            bolts += any(o[0] in (0x64, 0x65, 0x66) for o in sky_list(sc.emu.read(G_SKY_OBJECTS, SKY_OBJECTS_SIZE)))
            assert_world(sc, (n, map_, i))
            if i % 10 == 9:
                mine, theirs = draw_both(sc)
                assert mine == theirs, (n, map_, i)
                pictures += 1
        ctypes.c_int16.in_dll(lib, "ui").value = 0
        sc.emu.write(G_INVENTORY, bytes(2))
        ctypes.c_int.in_dll(lib, "party_sleeping").value = 0
        sc.emu.write_u32(G_PARTY_SLEEPING, 0)
    assert strikes > 1 and bolts > 3 and pictures == 56
    sc.assert_equal("end")


def thing_records(lib, types):
    """All things of the types that are in use."""
    out = []
    for t in types:
        for i in range(lib.dungeon_thing_count(t)):
            thing = t << 10 | i
            if lib.thing_word(thing, 0) != NONE:
                out.append(thing)
    return out


def sync_thing(sc, thing):
    """Copies the C thing record into the original."""
    t, size = thing >> 10 & 15, THING_SIZES[thing >> 10 & 15]
    record = ctypes.string_at(sc.lib.dungeon_thing_record(thing), size)
    sc.emu.write(sc.emu.read_u32(G_THINGS + 4 * t) + (thing & 0x3FF) * size, record)


def test_party_light_matches_original(world, lib):
    # Torches with random charges in the leader hand and in the champion hands (one to four champions), square and
    # explosion light, magic light, dark and lit maps, the hour, heavy rain, the weather, and the lightning flash.
    rng = random.Random(173)
    sc = world
    torches = [t for t in thing_records(lib, range(5, 11)) if lib.item_attr(t, 0) & 0x10]
    others = thing_records(lib, (5, 6, 10))[:200]
    assert len(torches) > 5
    game = Game.in_dll(lib, "game")
    magic = (ctypes.c_uint8 * 2).from_address(ctypes.addressof(game) + GAME_MAGIC)
    levels = set()
    first = bytes((ctypes.c_uint8 * CHAMPION_SIZE).from_address(ctypes.addressof(game) + GAME_CHAMPIONS))
    for n in range(400):
        if n % 20 == 0:
            map_ = rng.randrange(lib.dungeon_map_count())
            _, x, y, _ = rng.choice([s for s in squares(lib, (1,)) if s[0] == map_] or [(0, 0, 0, 0)])
            sc.place(map_, x, y, rng.randrange(4))
        count = rng.randrange(1, 5)
        sc.game.champion_count = count
        sc.emu.write(0x7F276, struct.pack("<H", count))
        for champ in range(count):
            champion_bytes(lib, champ, 0, first)
            for hand in range(2):
                thing = rng.choice(torches) if n % 4 == 0 else rng.choice(torches + others + [NONE] * 10)
                champion_bytes(lib, champ, CHAMPION_HANDS + 2 * hand, struct.pack("<H", thing))
        sync_champions(lib, sc)
        hand = rng.choice(torches) if n % 4 == 0 else rng.choice(torches + [NONE] * 3)
        ctypes.c_uint16.from_address(ctypes.addressof(game) + GAME_LEADER_HAND).value = hand
        sc.emu.write(G_LEADER_HAND, struct.pack("<H", hand))
        for t in torches:
            lib.item_add_charges(t, rng.randrange(-15, 16))
            sync_thing(sc, t)
        light = rng.choice((0, 0, 5, 30, 120, -20))
        magic[0], magic[1] = light & 0xFF, light >> 8 & 0xFF
        sc.emu.write(G_MAGIC_LIGHT, struct.pack("<h", light))
        set_weather(sc, weather_visible=rng.randrange(2), heavy_rain=rng.randrange(2), hour_light=rng.randrange(1, 6),
                    lightning_flash=rng.randrange(2), light_source_sum=rng.choice((0, 0, 10, 60, 200)),
                    explosion_light_sum=rng.choice((0, 0, 12, 13, 40)))
        lib.light_update_party_light()
        sc.emu.call(LIGHT_UPDATE_PARTY_LIGHT)
        sc.assert_same_weather(n)
        levels.add(ctypes.c_int.in_dll(lib, "view_light_level").value)
    assert levels == set(range(6))


def test_square_light_matches_original(world, lib):
    # The light of the squares around the party on all maps: wall and floor ornaments (lamps, windows), objects,
    # explosions, spell missiles, creatures, by distance, view direction, and the flags of what gives light.
    rng = random.Random(174)
    sc = world
    nonzero = 0
    for map_ in range(lib.dungeon_map_count()):
        places = [s for s in squares(lib, (1,)) if s[0] == map_]
        if not places:
            continue
        _, px, py, _ = rng.choice(places)
        sc.place(map_, px, py, rng.randrange(4))
        cells = [(x, y) for x in range(lib.dungeon_map_width(map_)) for y in range(lib.dungeon_map_height(map_))]
        for x, y in cells if map_ % 3 == 0 or map_ in OUTDOOR_MAPS else rng.sample(cells, 40):
            for d in range(4):
                args = (rng.randrange(10), d, x, y, rng.choice((7, 7, rng.randrange(8))))
                set_weather(sc, light_source_sum=0, explosion_light_sum=0, heavy_rain=rng.randrange(2),
                            hour_light=rng.randrange(1, 6))
                lib.light_add_square_light(*args)
                sc.emu.call(LIGHT_ADD_SQUARE_LIGHT, *args[:4], args[4])
                sc.assert_same_weather((map_, args))
                nonzero += ctypes.c_int16.in_dll(lib, "light_source_sum").value != 0
    assert nonzero > 80


def test_zone_interpolate_offset_matches_original(world, lib):
    # The offset between two layout zones at a percent: the place of a lightning bolt in the sky.
    from dm2tools.emu import SCRATCH
    rng = random.Random(175)
    dx, dy = ctypes.c_int16(), ctypes.c_int16()
    for n in range(400):
        a, b, p = rng.randrange(1, 7000), rng.randrange(1, 7000), rng.randrange(-20, 130)
        if rng.random() < 0.5:
            a = rng.choice((6001, 6002, 6003, 3100, 5000))
            b = a + 1
        dx.value, dy.value = 7, 7
        sc = world
        sc.emu.write(SCRATCH + 0xF8300, struct.pack("<hh", 7, 7))
        mine = lib.zone_interpolate_offset(a, b, p, ctypes.byref(dx), ctypes.byref(dy))
        theirs = sc.emu.call(ZONE_INTERPOLATE_OFFSET, a, b, p, SCRATCH + 0xF8300, SCRATCH + 0xF8302)
        assert (mine == -1) == (theirs & 0xFFFFFFFF == 0xFFFFFFFF), (a, b, p)
        assert (dx.value, dy.value) == struct.unpack("<hh", sc.emu.read(SCRATCH + 0xF8300, 4)), (a, b, p)


def transfer_sensors(lib):
    """(map, x, y, cell, sensor) of every object transfer sensor."""
    out = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                t = lib.dungeon_square_first_thing(map_, x, y)
                while t != 0xFFFE:
                    kind = lib.thing_word(t, 2) & 0x7F
                    if t >> 10 & 15 == 3 and kind in (0x3B, 0x40, 0x47, 0x48, 0x49):
                        out.append((map_, x, y, t >> 14, t))
                    t = lib.dungeon_next_thing(t)
    return out


def objects_on_map(lib, map_):
    """(thing, x, y) of the objects that lie on the squares of map_."""
    out = []
    for x in range(lib.dungeon_map_width(map_)):
        for y in range(lib.dungeon_map_height(map_)):
            t = lib.dungeon_square_first_thing(map_, x, y)
            while t != 0xFFFE:
                if 5 <= t >> 10 & 15 <= 10:
                    out.append((t, x, y))
                t = lib.dungeon_next_thing(t)
    return out


def set_word(sc, thing, offset, value):
    """Writes a word of a thing record in both."""
    ctypes.c_uint16.from_address(sc.lib.dungeon_thing_record(thing) + offset).value = value
    sync_thing(sc, thing)


def test_object_transfer_sensors_match_original(world, lib):
    # Every object transfer sensor of the dungeon on walls and floors, in its own kind and as each of the kinds 0x3b,
    # 0x40, 0x47, 0x48, 0x49, wanting its item or any object, with each charge change, inverted or not, sometimes
    # aimed at its own square: objects of its map go on its source cell, then square events with each effect; the
    # objects move to the target (or back), with the charge changes; bones bring back a champion (Step 15 hook).
    rng = random.Random(176)
    sc = world
    moved = recharged = 0
    for n, (map_, x, y, cell, sensor) in enumerate(transfer_sensors(lib) * 3):
        places = [s for s in squares(lib, (1,)) if s[0] == map_]
        _, px, py, _ = rng.choice(places)
        sc.place(map_, px, py, rng.randrange(4))
        data, flags = lib.thing_word(sensor, 2), lib.thing_word(sensor, 4)
        if n >= len(transfer_sensors(lib)):
            kind = rng.choice((0x3B, 0x40, 0x47, 0x48, 0x49))
            item = 0x1FF if rng.random() < 0.5 else data >> 7
            set_word(sc, sensor, 2, item << 7 | kind)
            set_word(sc, sensor, 4, flags & ~0x3C | rng.randrange(4) << 3 | rng.randrange(2) << 2 | rng.randrange(2) << 5)
            if rng.random() < 0.3:
                set_word(sc, sensor, 6, lib.thing_word(sensor, 6) & 0xF | cell << 4 | x << 6 | y << 11)
        kind, target = lib.thing_word(sensor, 2) & 0x7F, lib.thing_word(sensor, 6)
        sx, sy, scell = (target >> 6 & 31, target >> 11, target >> 4 & 3) if kind in (0x47, 0x49) else (x, y, cell)
        wall = lib.dungeon_square(map_, sx, sy) >> 5 == 0
        for _ in range(4):
            for _ in range(rng.randrange(1, 4)):
                things = [o for o in objects_on_map(lib, map_) if (o[1], o[2]) != (sx, sy)]
                if not things:
                    break
                thing, ox, oy = rng.choice(things)
                c = scell if wall or rng.random() < 0.5 else rng.randrange(4)
                arg = (thing & 0x3FFF) | c << 14
                lib.move_thing(arg, ox, oy, sx, sy)
                sc.emu.call(MOVE_THING, arg, ox, oy, sx, sy)
            before = sc.differences()
            assert before == [], (n, "objects")
            square_event(sc, map_, x, y, cell, rng.randrange(3), 0)
            charges = [lib.item_add_charges(t, 0) for t, _, _ in objects_on_map(lib, map_)]
            first = objects_on_map(lib, map_)
            for _ in range(3):
                sc.step()
            moved += objects_on_map(lib, map_) != first
            recharged += charges != [lib.item_add_charges(t, 0) for t, _, _ in objects_on_map(lib, map_)]
            sc.assert_same((n, map_, x, y, cell, hex(kind)))
        if n % 10 == 9:
            sc.assert_equal(n)
    assert moved > 30 and recharged > 10, (moved, recharged)
    sc.assert_equal("end")


def lightning_seed(lib, rng, storm, chance, target):
    """A random seed with which the next weather step of a storm (rain already falling) rolls a lightning that hits
    squares and tries target first; the C random functions follow the calls of world_update_day_night."""
    seed = ctypes.c_uint32.in_dll(lib, "random_seed")
    lib.rand_next.restype = ctypes.c_uint32
    lib.rand_below.restype = ctypes.c_uint16
    while True:
        value = rng.getrandbits(32)
        seed.value = value
        span = (0x100 - storm + (lib.rand_next() & 0xF)) & 0xFFFF
        if lib.rand_below(span) > chance or lib.rand_below((storm + 1) & 0xFFFF) < 60:
            continue
        lib.rand_next()
        if (lib.rand_below(32), lib.rand_below(32)) == target:
            return value


def set_party_state(sc, inventory, sleeping):
    """The open inventory (champion ordinal) and the sleep of the party in both."""
    ctypes.c_int16.in_dll(sc.lib, "ui").value = inventory  # UiState.inventory
    sc.emu.write(G_INVENTORY, struct.pack("<h", inventory))
    ctypes.c_int.in_dll(sc.lib, "party_sleeping").value = sleeping
    sc.emu.write_u32(G_PARTY_SLEEPING, sleeping)


def test_lightning_strikes_match_original(world, lib):
    # Lightning that tries the party square (early or late game time, weak or strong storm, script byte 0x40 below or
    # above the threshold), a creature square (party 1 to 4 squares away, facing it or not), or a square near the
    # party while the inventory is open or the party sleeps (script byte 0x41).
    rng = random.Random(177)
    sc = world
    groups = {}
    for map_ in OUTDOOR_MAPS[1:]:
        for _, x, y, _ in [s for s in squares(lib, (1,)) if s[0] == map_]:
            t = lib.dungeon_square_first_thing(map_, x, y)
            while t != 0xFFFE:
                if t >> 10 & 15 == 4:
                    groups.setdefault(map_, []).append((x, y))
                t = lib.dungeon_next_thing(t)
    party_hits = creature_hits = seen = 0
    for n in range(240):
        case = n % 3
        map_ = rng.choice(sorted(groups))
        floors = [(x, y) for m, x, y, _ in squares(lib, (1,)) if m == map_]
        target = rng.choice(groups[map_]) if case == 1 else rng.choice(floors)
        near = [(x, y) for x, y in floors if 1 <= abs(x - target[0]) + abs(y - target[1]) <= 4] or floors
        party = target if case == 0 else rng.choice(near)
        sc.place(map_, *party, rng.randrange(4))
        storm = rng.choice((0xB0, 0xB6, 0xF0))
        sc.set_time(rng.choice((11999, 12000, 40000)))
        set_weather(sc, rain_intensity=1, storm_intensity=storm, storm_multiplier=1, next_hour_time=sc.time() + 100,
                    strike_threshold=5, light_level=0)
        set_global(sc, 0x40, rng.choice((5, 6, 20)))
        set_party_state(sc, int(case == 2 and rng.random() < 0.6), int(case == 2 and rng.random() < 0.4))
        value = lightning_seed(lib, rng, storm, 0x28 if storm >= 0xCD else 7, target)
        ctypes.c_uint32.in_dll(lib, "random_seed").value = value
        sc.emu.write_u32(VIEW_SEED_ADDR, value)
        before = lib.dun_get_global_var(0x41)
        lib.world_update_day_night(0)
        sc.emu.call(WORLD_UPDATE_DAY_NIGHT, 0)
        hit = [e for e in sc.explosions if (e[2], e[3]) == target]
        party_hits += case == 0 and bool(hit)
        creature_hits += case == 1 and bool(hit)
        seen += lib.dun_get_global_var(0x41) != before
        assert_world(sc, (n, map_, target, party))
    set_party_state(sc, 0, 0)
    assert party_hits > 5 and creature_hits > 5 and seen > 5, (party_hits, creature_hits, seen)


def test_square_light_of_added_things_matches_original(world, lib, gdat):
    # start.DAT has no explosions or missiles: an explosion (light types and others, with a power), a spell missile
    # (with a power), or a creature that glows goes at the end of the things of a floor, pit, or teleporter square
    # (with or without a floor ornament), then both add the light of the square.
    rng = random.Random(178)
    sc = world
    free = {t: [i for i in range(lib.dungeon_thing_count(t)) if lib.thing_word(t << 10 | i, 0) == NONE] for t in (4, 14, 15)}
    glowing = [c for c in range(256) if lib.gdat_entry_index(gdat, 0x0F, c, 0x0B, 0xF8)]
    assert free[4] and free[14] and free[15] and glowing
    lib.gdat_entry_index.restype = ctypes.c_uint16
    lib.gdat_entry_index.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lit = 0
    for map_ in range(lib.dungeon_map_count()):
        cells = [(x, y) for m, x, y, _ in squares(lib, (1, 2, 5)) if m == map_]
        if not cells:
            continue
        sc.place(map_, *rng.choice(cells), rng.randrange(4))
        for x, y in rng.sample(cells, min(len(cells), 25)):
            last = lib.dungeon_square_first_thing(map_, x, y)
            if last == 0xFFFE:
                continue
            while lib.dungeon_next_thing(last) != 0xFFFE:
                last = lib.dungeon_next_thing(last)
            kind = rng.randrange(3)
            if kind == 0:
                thing = 15 << 10 | rng.choice(free[15]) | rng.randrange(4) << 14
                data = rng.choice((0, 2, 0x30, 0x30, 1, 7)) | rng.randrange(2) << 7 | rng.randrange(256) << 8
                record = struct.pack("<HH", 0xFFFE, data)
            elif kind == 1:
                thing = 14 << 10 | rng.choice(free[14]) | rng.randrange(4) << 14
                payload = 0xFF80 if rng.random() < 0.8 else 0xFF81
                record = struct.pack("<HHBBH", 0xFFFE, payload, rng.randrange(256), 0, 0)
            else:
                thing = 4 << 10 | rng.choice(free[4]) | rng.randrange(4) << 14
                record = struct.pack("<HHBBHHHHH", 0xFFFE, 0xFFFE, rng.choice(glowing), 0, 100, 0, 0, 0, 0)
            write_record(sc, thing, record)
            set_word(sc, last, 0, thing)
            for d in range(4):
                args = (rng.randrange(10), d, x, y, rng.choice((1, 3, 7)))
                set_weather(sc, light_source_sum=0, explosion_light_sum=0)
                lib.light_add_square_light(*args)
                sc.emu.call(LIGHT_ADD_SQUARE_LIGHT, *args[:4], args[4])
                sc.assert_same_weather((map_, hex(thing), args))
                lit += ctypes.c_int16.in_dll(lib, "explosion_light_sum").value != 0
            set_word(sc, last, 0, 0xFFFE)
            write_record(sc, thing, struct.pack("<H", NONE) + bytes(len(record) - 2))
    assert lit > 100


def write_record(sc, thing, record):
    """Writes the first bytes of a thing record in both."""
    ctypes.memmove(sc.lib.dungeon_thing_record(thing), record, len(record))
    sync_thing(sc, thing)
