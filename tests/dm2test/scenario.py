"""The scenario harness: start.DAT in the C port and in the original code, run step by step."""

import ctypes
import struct

import pytest

from dm2test import constants as C
from dm2test.gdat import image_info
from dm2test.markers import ROOT
from dm2test.oracle import CreatureOracle
from dm2test.save import GRAPHICS, START, original_save
from dm2test.structs import CurrentMap, Game, ImageInfo, Party, Sound, Timeline, TimelineEvent
from dm2tools.emu import SCRATCH
from dm2test.view import OriginalView


def original_view_of(lib, gdat, start):
    """The original code after loading start, with the GDAT data of the C port (dm2test.view.OriginalView)."""
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.gdat_image_colormap.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.gdat_image_colormap.restype = ctypes.POINTER(ctypes.c_uint8)
    lib.gdat_decode_image.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
    lib.gdat_image_info.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ImageInfo)]
    lib.gdat_image_offset.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3
    lib.gdat_exists.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_exists.restype = ctypes.c_bool
    lib.gdat_text_int_field.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    return OriginalView(lib, gdat, start)

ENDOFLIST, NONE = 0xFFFE, 0xFFFF

# Original functions of the move and timeline code.
COMMAND_EXECUTE, MOVE_PARTY, MOVE_PARTY_TURN, MOVE_THING = 0x21D6C, 0x235BF, 0x2339D, 0x4B108
TIMELINE_PROCESS, TIMELINE_ADD_EVENT, DUN_SET_PARTY_LOCATION = 0x59A5C, 0x56390, 0x1C82D
SENSOR_PROCESS_FLOOR, GAME_ENTER_MAP = 0x4CDCC, 0x24629
# Functions of later steps and of the DOS layer: (address, stack arguments, result).
STUBS = {
    0x138D9: (0, 0),                                                         # screen
    0x209E0: (0, 0), 0x14C55: (0, 0), 0x14C68: (0, 0),                     # input flush, mouse (dm2test.view stubs the poll)
    0x5599F: (0, 0),                                                         # message area
    0x14B4F: (0, 0), 0x14C0E: (0, 0),                                       # mouse areas of an input mode (not ported)
    0x13A87: (0, 0), 0x13B7C: (0, 0),                                       # screen presents
    0x14889: (0, 0), 0x38392: (0, 0),                                       # the hand cursor and its icon
    0x14C7B: (0, 0), 0x14C87: (0, 0), 0x1483A: (0, 0), 0x14846: (0, 0),     # IBMIOP input lock, mouse capture, cursor
    0x1482E: (0, 0), 0x14743: (5, 0),                                       # mouse capture, cursor image
}
# Globals of the movement state: name -> (address, struct format); the CurrentMap fields, then the Party fields.
CURRENT_MAP_GLOBALS = {
    "map": (0x72244, "<h"), "square_x": (0x7F27C, "<h"), "square_y": (0x7F27A, "<h"), "square_map": (0x7F27E, "<h"),
    "ref_dir": (0x7F260, "<h"), "alt_view_active": (0x7F230, "<i"), "alt_view_x": (0x7F25C, "<h"),
    "alt_view_y": (0x7F25E, "<h"), "alt_view_dir": (0x7F272, "<h"), "alt_view_map": (0x7F278, "<h"),
}
PARTY_GLOBALS = {
    "move_steps": (0x7F258, "<h"), "move_command": (0x7F252, "<H"),
    "view_x": (0x7F266, "<h"), "view_y": (0x7F264, "<h"), "view_dir": (0x7F274, "<h"), "new_map": (0x717F4, "<h"),
    "dest_x": (0x80026, "<h"), "dest_y": (0x80024, "<h"), "dest_map": (0x80028, "<h"), "dest_cell": (0x80022, "<h"),
    "redraw": (0x7F38C, "<I"),
}
GLOBALS = CURRENT_MAP_GLOBALS | PARTY_GLOBALS
G_MAP_LIGHT, G_MAP_WALL_COST = 0x7F3F4, 0x7F3FC  # the light radius and wall cost of the party map graphics
G_DISTANCE_MAPS = (0x7F8C8, 0x7F8C4)  # the party distance maps of ai_refresh_party_maps
G_GAME_TIME, G_MOVE_COOLDOWN, G_THROW_COOLDOWN, G_PUSH_PENDING = 0x7F22C, 0x7F26A, 0x7F25A, 0x72FFC
G_ACTIVE_GROUPS, G_COMMAND_STOP = 0x7F898, 0x7F484
COMMAND_RECORD = SCRATCH + 0xF8000
SCHEDULED_EVENT = SCRATCH + 0xF8100
SENSOR_SCHEDULE_SQUARE_EVENT, GDAT_LOAD_MAP_GRAPHICS = 0x4BBE4, 0x3AB31
EXPLOSION_CREATE = 0x16746
# The wall sensor that creates an item (kind 0x3c) in wall_square_event.
WALL_CREATE_ITEM, WALL_NEXT_THING = 0x58B04, 0x58C56
# The wall sensor of the end of the game (kind 0x12).
WALL_END_GAME = 0x58A12
# The drawing part of ui_draw_movement_arrows (the screen is not mapped); it then sets the input mode.
ARROWS_DRAWING = (0x42B00, 0x42B6D)
# forward_effect: the reverse effect of a hold stays uninitialized (C port: a hold).
FORWARD_REVERSE_HOLD = 0x5723C
# The weather update, and transfer_object with its charge change that mode 3 leaves uninitialized.
WORLD_UPDATE_DAY_NIGHT, TRANSFER_OBJECT_FRAME, LIGHT_UPDATE_PARTY_LIGHT = 0x5A073, 0x57D54, 0x389C2
MAP_GRAPHICS_TAIL = SCRATCH + 0xF8200  # the end of gdat_load_map_graphics: world_update_day_night(0), then return
# Weather globals of the original: name -> (address, struct format); the C values come from World.
WEATHER = {
    "weather_visible": (0x8047B, "<B"), "heavy_rain": (0x8047C, "<B"), "wind_dir": (0x8047F, "<B"),
    "rain_intensity": (0x8047E, "<B"), "sky_darkness": (0x8047A, "<B"), "cloud_cover": (0x80479, "<B"),
    "storm_multiplier": (0x80480, "<B"), "storm_step": (0x80477, "<B"), "storm_curve": (0x80474, "<B"),
    "storm_intensity": (0x80470, "<H"), "next_hour_time": (0x80430, "<I"),
    "strike_threshold": (0x8046C, "<h"), "storm_previous": (0x8046A, "<H"), "hour_light": (0x80472, "<h"),
    "day_time_offset": (0x80434, "<I"), "first_init": (0x76184, "<i"), "thunder_toggle": (0x76188, "<i"),
    "lightning_flash": (0x7F248, "<i"), "light_level": (0x7F282, "<h"), "g_light_source_sum": (0x7F970, "<h"),
    "g_explosion_light_sum": (0x7F974, "<h"),
}
G_SKY_OBJECTS, SKY_OBJECTS_SIZE, G_VIEW_CLEAR_RECT, G_UI_COLORS = 0x80438, 50, 0x8002C, 0x7F210
# The sound requests: the original sound table (the sounds that the map graphics load) is a stub, every sound of
# GRAPHICS.DAT counts as loaded, as in the C port; the original sound_play_requests runs with stubs for its two HMI
# driver calls. The tests compare the requests that reach sound_play_requests and the order, loudness, and pan of the
# sounds that start.
SOUND_FIND_LOADED, SOUND_PLAY_REQUESTS, SOUND_FLUSH_REQUESTS = 0x15C10, 0x10573, 0x1616D
HMI_DIGI_SAMPLE_DONE, HMI_DIGI_START_SAMPLE = 0x608B9, 0x609B9
G_DIGI_ENABLED, G_DIGI_VOLUME, G_SAMPLE_HANDLES = 0x704BE, 0x7EF84, 0x7EDA0
G_SOS_SAMPLE_DATA, G_SOS_VOLUME, G_SOS_PAN = 0x703F8, 0x7040C, 0x70438


class SoundStart(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("loudness", ctypes.c_int), ("pan", ctypes.c_int)]
G_DELAYED_SOUNDS, G_SOUND_TABLE, G_SOUND_DEFERRED, G_SAMPLE_DESCS, G_SOUND_REQUESTS = 0x7F188, 0x7F18C, 0x7F190, 0x7F194, 0x7F198
G_SOUND_REQUEST_COUNT, G_SOUND_DEFERRED_COUNT, SOUND_TABLE_MAX = 0x71690, 0x71692, 512


class SoundRequest(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint8) for n in ("category", "index", "entry", "priority", "volume")] + [
        ("lateral", ctypes.c_int8), ("forward", ctypes.c_int8)]


G_GFX_HAS_CLOUDS, G_GFX_HAS_RAIN, G_GFX_HAS_LIGHTNING = 0x80475, 0x80476, 0x8047D


class WeatherState(ctypes.Structure):
    """The C weather.h Weather."""
    _fields_ = [("strike_threshold", ctypes.c_int16), ("storm_previous", ctypes.c_uint16), ("hour_light", ctypes.c_int16),
                ("day_time_offset", ctypes.c_uint32), ("first_init", ctypes.c_bool), ("thunder_toggle", ctypes.c_bool),
                ("lightning_flash", ctypes.c_bool), ("dungeon_loaded", ctypes.c_bool)]


def c_weather(lib):
    """The weather globals of the C port, by the names of WEATHER."""
    game, weather = Game.in_dll(lib, "g_game"), WeatherState.in_dll(lib, "g_weather")
    out = {name: getattr(game, name) for name in list(WEATHER)[:11]}
    out |= {name: getattr(weather, name) for name in list(WEATHER)[11:18]}
    out["light_level"] = ctypes.c_int.in_dll(lib, "g_view_light_level").value
    out["g_light_source_sum"] = ctypes.c_int16.in_dll(lib, "g_light_source_sum").value
    out["g_explosion_light_sum"] = ctypes.c_int16.in_dll(lib, "g_explosion_light_sum").value
    return out


def c_sky(lib):
    """The C sky objects up to the end mark."""
    lib.weather_sky_objects.restype = ctypes.c_void_p
    return sky_list(ctypes.string_at(lib.weather_sky_objects(), SKY_OBJECTS_SIZE))


def sky_list(raw):
    out = []
    for i in range(0, SKY_OBJECTS_SIZE, 10):
        if raw[i] == 0xFF:
            break
        out.append(raw[i:i + 10])
    return out


def ignore_stale(state):
    """After a removal (no destination) the original leaves a stale register in the destination map."""
    return {k: v for k, v in state.items() if not (k == "dest_map" and state["dest_x"] < 0)}


class Location(ctypes.Structure):
    """A square of the current map (dungeon.h, Location)."""
    _fields_ = [(name, ctypes.c_int16) for name in ("x", "y")]


class Position(ctypes.Structure):
    """A square with its map (dungeon.h, Position)."""
    _fields_ = [(name, ctypes.c_int16) for name in ("map", "x", "y")]


def port_globals(party, current_map):
    """The movement globals of the C port by the names of GLOBALS."""
    return ({name: getattr(current_map, name) for name in CURRENT_MAP_GLOBALS}
            | {name: getattr(party, name) for name in PARTY_GLOBALS})


G_PENDING_WOUNDS, G_PENDING_DAMAGE, G_CHAMPIONS, CHAMPION_SIZE = 0x7FBA4, 0x7FBAC, 0x7FBD0, C.CHAMPION_SIZE
CHAMPION_REDRAW = 0x32  # the redraw flags: the original status box drawing does not run in these tests


def int16(value):
    return ctypes.c_int16(value).value


class Command(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int16), ("y", ctypes.c_int16), ("command", ctypes.c_uint16), ("rect", ctypes.c_int16 * 4)]


@pytest.fixture(scope="module")
def gdat(lib):
    if not GRAPHICS.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == 0
    yield g
    lib.gdat_free(g)


@pytest.fixture(scope="module")
def start():
    if not START.exists():
        pytest.skip("tests/saves/start.DAT missing")
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return START.read_bytes()


class Scenario(CreatureOracle):
    """start.DAT in the C port and in the original code, run step by step."""

    def __init__(self, lib, gdat, start, tmp_path, place_party=True):
        """place_party: both put the party on its square of the save, like a load; else the caller starts the
        game session in both."""
        self.lib, self.gdat, self.start, self.tmp = lib, gdat, start, tmp_path
        self.reset_creature_globals(lib)
        path = tmp_path / "SKSAVE0.DAT"
        path.write_bytes(start)
        assert lib.save_load(str(path).encode(), gdat) == 0
        lib.draw_init(gdat)
        self.game = Game.in_dll(lib, "g_game")
        self.party = Party.in_dll(lib, "g_party")
        self.current_map = CurrentMap.in_dll(lib, "g_current_map")
        assert lib.zone_load(gdat) == 1 and lib.pal_load_color_ramps(gdat) == 1
        assert lib.text_init(gdat)  # the messages of the leader hand
        view = original_view_of(lib, gdat, start)
        self.emu = emu = view.emu
        from dm2test.interface import G_FONT_BITMAP, G_UI_COLORS, VGA
        from unicorn import UC_PROT_ALL
        emu.uc.mem_map(VGA, 0x10000, UC_PROT_ALL)  # the screen of the pressed buttons, messages, and panels
        emu.write_u32(G_UI_COLORS, view.entry_data(1, 0, 0x0D, 0xFE))
        font = view.item_bytes(lib.gdat_find(gdat, 1, 0, 7, 0))[:0x300]
        emu.write_u32(G_FONT_BITMAP, view.put(font + bytes(0x80)))
        self.stubs = {addr: emu.stub(addr, lambda *_, r=result: r, stack_args=stack)
                      for addr, (stack, result) in STUBS.items()}
        self.fix_square_events()
        self.view = view
        self.map_graphics_tail()
        self.set_local(TRANSFER_OBJECT_FRAME, 0, 0)
        self.sound_setup()
        self.record_explosions()
        emu.write_u32(G_UI_COLORS, view.entry_data(1, 0, 0x0D, 0xFE))  # view_clear_viewport fills the band with color 0
        self.sync_weather()
        self.skip(WALL_END_GAME, WALL_NEXT_THING)
        self.skip(*ARROWS_DRAWING)  # ui_draw_movement_arrows: only its input mode
        self.clear_uninitialized_event_bytes()
        self.set_local(FORWARD_REVERSE_HOLD, 0, 3)
        emu.write(GLOBALS["map"][0], struct.pack("<h", -1))
        emu.write(G_PUSH_PENDING, struct.pack("<H", NONE))
        # the C port starts from the movement state of the program start (move.c), like the original before its load
        ctypes.memset(ctypes.addressof(self.party), 0, ctypes.sizeof(self.party))
        ctypes.memset(ctypes.addressof(self.current_map), 0, ctypes.sizeof(self.current_map))
        self.current_map.map, self.party.new_map, self.party.push_pending_group = -1, -1, NONE
        # ... and from the interface state of the program start: an earlier test can leave a recruit open
        from dm2test.interface import UiState
        ctypes.memset(ctypes.addressof(UiState.in_dll(lib, "g_ui")), 0, ctypes.sizeof(UiState))
        # ... and from a running game: the end-game sensor of an earlier test (session_complete) stops all damage
        from dm2test.dialog import Session
        from dm2test.input import MainLoop
        session = Session.in_dll(lib, "g_session")
        session.completed = session.exit_code = 0
        MainLoop.in_dll(lib, "g_main_loop").quit = 0
        if place_party:
            self.place(self.game.party_map, self.game.party_x, self.game.party_y, self.game.party_dir)
        self.setup_creatures(place_party)

    def map_graphics_tail(self):
        """gdat_load_map_graphics runs load_map_graphics, then the original world_update_day_night(0) like the C
        callers of view_enter_map."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EIP
        call = WORLD_UPDATE_DAY_NIGHT - (MAP_GRAPHICS_TAIL + 7)
        self.emu.write(MAP_GRAPHICS_TAIL, b"\x31\xc0\xe8" + struct.pack("<i", call) + b"\xc3")

        def hook(uc, *_):
            try:
                self.load_map_graphics()
            except Exception as e:  # exceptions do not cross the Unicorn callbacks
                self.emu._fail(f"map graphics stub: {e!r}")
            uc.reg_write(UC_X86_REG_EIP, MAP_GRAPHICS_TAIL)

        self.emu.uc.hook_add(UC_HOOK_CODE, hook, begin=GDAT_LOAD_MAP_GRAPHICS, end=GDAT_LOAD_MAP_GRAPHICS)

    def record_explosions(self):
        """Records the arguments of the explosion_create calls of the original (type, attack, x, y, cell); the
        explosions themselves run in both."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
        self.explosions = []

        def hook(uc, *_):
            cell = struct.unpack("<h", self.emu.read(uc.reg_read(UC_X86_REG_ESP) + 4, 2))[0]
            self.explosions.append(tuple(uc.reg_read(r) & 0xFFFF for r in (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                                                                           UC_X86_REG_ECX)) + (cell,))

        self.emu.uc.hook_add(UC_HOOK_CODE, hook, begin=EXPLOSION_CREATE, end=EXPLOSION_CREATE)

    def sound_setup(self):
        """The sound buffers of the original, its sound table filled on demand, and the recorder of its driver calls."""
        emu, lib, view = self.emu, self.lib, self.view
        lib.sndreq_load.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3 + [ctypes.POINTER(Sound)]
        for addr, size in ((G_DELAYED_SOUNDS, 0x70), (G_SOUND_DEFERRED, 0x48), (G_SOUND_REQUESTS, 0xF0),
                           (G_SOUND_TABLE, 7 * SOUND_TABLE_MAX), (G_SAMPLE_DESCS, 16 * SOUND_TABLE_MAX)):
            emu.write_u32(addr, view.put(bytes(size)))
        emu.write(G_SOUND_REQUEST_COUNT, bytes(4))
        self.sound_keys = []
        self.sounds = []  # the requests that reached the driver of the original

        def find_loaded(category, index, entry, *_):
            key = (category & 0xFF, index & 0xFF, entry & 0xFF)
            s = Sound()
            if lib.sndreq_load(self.gdat, *key, ctypes.byref(s)) != 0:
                return 0
            if key not in self.sound_keys:
                n = len(self.sound_keys)
                self.sound_keys.append(key)
                emu.write(emu.read_u32(G_SOUND_TABLE) + 7 * n, struct.pack("<HBBBH", n, *key, 0xFFFF))
                emu.write(emu.read_u32(G_SAMPLE_DESCS) + 16 * n, struct.pack("<IH", ctypes.cast(s.data, ctypes.c_void_p).value & 0xFFFFFFFF, s.length & 0xFFFF))
            return self.sound_keys.index(key) + 1

        def play(uc, *_):
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX
            count, requests = uc.reg_read(UC_X86_REG_EAX) & 0xFFFF, uc.reg_read(UC_X86_REG_EDX)
            descs = emu.read_u32(G_SAMPLE_DESCS)
            for i in range(count):
                desc, priority, volume, lateral, forward = struct.unpack_from("<IBBbb", emu.read(requests + 12 * i, 8))
                self.sounds.append(self.sound_keys[(desc - descs) // 16] + (priority, volume, lateral, forward))

        def start(handle, channel, *_):
            volume = emu.read_u32(G_SOS_VOLUME)
            self.started.append((emu.read_u32(G_SOS_SAMPLE_DATA), volume >> 7, emu.read_u32(G_SOS_PAN)))
            return channel + 1  # a handle; the voice is free again at once (sample done)

        from unicorn import UC_HOOK_CODE
        self.started = []  # the sounds that the original starts: (sample data, loudness, pan)
        emu.stub(SOUND_FIND_LOADED, find_loaded)
        emu.uc.hook_add(UC_HOOK_CODE, play, begin=SOUND_PLAY_REQUESTS, end=SOUND_PLAY_REQUESTS)
        emu.stub(HMI_DIGI_SAMPLE_DONE, lambda *_: 1)
        emu.stub(HMI_DIGI_START_SAMPLE, start)
        emu.write_u32(G_DIGI_ENABLED, 1)
        emu.write(G_DIGI_VOLUME, struct.pack("<h", 7))
        emu.write(G_SAMPLE_HANDLES, struct.pack("<64i", *[-1] * 64))
        ctypes.c_int.in_dll(lib, "g_sound_played_count").value = 0
        ctypes.c_int.in_dll(lib, "g_sound_started_count").value = 0
        lib.sound_stop_all()
        lib.sndreq_reset_requests()  # both start from the sound state of the program start

    def flush_sounds(self):
        """The end of a game step: the sound requests of the step play in both."""
        self.lib.sndreq_flush_requests(False)
        self.emu.call(SOUND_FLUSH_REQUESTS, 0)

    def assert_same_sounds(self, where):
        """The sound requests that reached the voices in both since the last check."""
        count = ctypes.c_int.in_dll(self.lib, "g_sound_played_count")
        played = (SoundRequest * 64).in_dll(self.lib, "g_sound_played")
        mine = [(r.category, r.index, r.entry, r.priority, r.volume, r.lateral, r.forward) for r in played[:count.value]]
        assert mine == self.sounds[:64], (where, mine, self.sounds)
        count.value = 0
        self.sounds.clear()
        count = ctypes.c_int.in_dll(self.lib, "g_sound_started_count")
        started = (SoundStart * 64).in_dll(self.lib, "g_sound_started")
        mine = [((s.data or 0) & 0xFFFFFFFF, s.loudness, s.pan) for s in started[:count.value]]
        assert mine == self.started[:64], (where, mine, self.started)
        count.value = 0
        self.started.clear()
        self.lib.sound_stop_all()  # the original voices are free again at once (its sample done stub)

    def sync_weather(self):
        """The weather state that the save does not keep: the original load stubs weather_init, so it gets the values
        of the C weather_init (the strike threshold and the hour light depend on the time and seed before the load)."""
        theirs = self.weather()
        mine = c_weather(self.lib)
        assert mine["day_time_offset"] == theirs["day_time_offset"]
        for name in ("strike_threshold", "storm_previous", "hour_light", "first_init", "thunder_toggle", "lightning_flash",
                     "light_level", "g_light_source_sum", "g_explosion_light_sum"):
            addr, fmt = WEATHER[name]
            self.emu.write(addr, struct.pack(fmt, mine[name]))

    def weather(self):
        """The weather globals of the original."""
        return {name: struct.unpack(fmt, self.emu.read(addr, struct.calcsize(fmt)))[0] for name, (addr, fmt) in WEATHER.items()}

    def assert_same_weather(self, where):
        """The weather, the light, and the sky objects (while the weather shows) of both; check_creatures compares the
        lightning explosions in the dungeon and the timeline."""
        self.explosions.clear()
        assert c_weather(self.lib) == self.weather(), where
        if self.weather()["weather_visible"]:
            assert c_sky(self.lib) == sky_list(self.emu.read(G_SKY_OBJECTS, SKY_OBJECTS_SIZE)), where

    def clear_band(self, gfx_set):
        """The band between the ceiling and floor pictures that view_clear_viewport fills (G_VIEW_CLEAR_RECT), and
        its size; 0 when the pictures fill the viewport."""
        sizes = []
        for entry in (0, 1):  # floor, ceiling
            item = self.lib.gdat_find(self.gdat, 8, gfx_set, 1, entry)
            if item < 0:
                return 0
            info = image_info(self.lib, self.gdat, item)[1]
            sizes.append((info.width, info.height))
        (fw, fh), (cw, ch) = sizes
        floor, rest = fw * fh & 0xFFFF, (C.VIEWPORT_WIDTH * C.VIEWPORT_HEIGHT - cw * ch) & 0xFFFF
        if floor >= rest:
            return 0
        self.emu.write(G_VIEW_CLEAR_RECT, struct.pack("<4h", 0, ch, C.VIEWPORT_WIDTH, C.VIEWPORT_HEIGHT - (ch + fh)))
        return rest - floor

    def load_map_graphics(self):
        """The view globals of the party map, as the C view_enter_map sets them."""
        from dm2test import view as tv
        lib, emu, view = self.lib, self.emu, self.view
        map_ = struct.unpack("<h", emu.read(0x7F262, 2))[0]
        graphics, flags = lib.dungeon_map_graphics(map_), lib.dungeon_map_flags(map_)
        gfx_set = graphics >> 4 & 0xF
        emu.write(tv.G_MAP_GFX_SET, bytes([gfx_set]))
        emu.write(tv.G_CUR_MAP_DOOR_TYPES, bytes([graphics >> 8 & 0xF if flags & 0x80 else 0xFF,
                                                  graphics >> 12 & 0xF if flags & 0x100 else 0xFF]))
        emu.write(tv.G_MAP_WALL_COLOR_KEY, struct.pack("<H", view.entry_index(8, gfx_set, 0x0B, 0x64)))
        emu.write(tv.G_MAP_FLIP_FLAGS, struct.pack("<H", view.entry_index(8, gfx_set, 0x0B, 0x65)))
        emu.write(G_MAP_LIGHT, struct.pack("<H", view.entry_index(8, gfx_set, 0x0B, 0x6D)))
        emu.write(G_MAP_WALL_COST, struct.pack("<H", view.entry_index(8, gfx_set, 0x0B, 0x72) or 1))
        emu.write(tv.G_VIEW_CLEAR, struct.pack("<H", self.clear_band(gfx_set)))
        lib.weather_sky_objects.restype = ctypes.c_void_p
        emu.write(tv.G_SKY_OBJECTS, ctypes.string_at(lib.weather_sky_objects(), SKY_OBJECTS_SIZE))
        emu.write(tv.G_SKY_STATE, struct.pack("<H", view.entry_index(8, gfx_set, 0x0B, 0x66)))
        for addr, entry in ((G_GFX_HAS_CLOUDS, 0x6A), (G_GFX_HAS_RAIN, 0x67), (G_GFX_HAS_LIGHTNING, 0x64)):
            emu.write(addr, bytes([lib.gdat_exists(self.gdat, 0x17, gfx_set, 1, entry)]))
        emu.write(G_SOUND_REQUEST_COUNT, bytes(2))  # sound_wait_and_clear_requests
        lib.view_enter_map(map_)

    def clear_uninitialized_event_bytes(self):
        """The original leaves some event bytes uninitialized (stack garbage); the C port clears them. This clears
        them in each event that the original queues: (data byte, mask of the bytes to keep) by event type."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EAX
        keep = {0x02: (0xFF, 0xFF, 0, 0, 0, 0), 0x0D: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0), 0x56: (0xFF, 0xFF, 0xFF, 0x01, 0, 0),
                0x59: (0, 0, 0xFF, 0xFF, 0, 0), 0x5A: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0), 0x5B: (0xFF, 0xFF, 0, 0, 0, 0),
                0x5D: (0xFF, 0x0F, 0xFF, 0, 0, 0), 0x5E: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0), 0x04: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0),
                0x57: (0xFF, 0xFF, 0, 0, 0, 0), 0x58: (0xFF, 0xFF, 0, 0, 0, 0), 0x5C: (0xFF, 0xFF, 0, 0, 0, 0),
                0x21: (0xFF, 0xFF, 0, 0, 0, 0), 0x22: (0xFF, 0xFF, 0, 0, 0, 0), 0x3C: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0),
                0x3D: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0), 0x19: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0), 0x1D: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0),
                0x1E: (0xFF, 0xFF, 0xFF, 0xFF, 0, 0), 0x54: (0, 0, 0, 0, 0, 0), 0x15: (0xFF, 0xFF, 0, 0, 0, 0),
                0x0C: (0, 0, 0, 0, 0, 0), 0x0E: (0xFF, 0xFF, 0xFF, 0, 0, 0), 0x4B: (0xFF, 0xFF, 0, 0, 0, 0),
                0x47: (0, 0, 0, 0, 0, 0), 0x46: (0xFF, 0xFF, 0, 0, 0, 0), 0x48: (0xFF, 0xFF, 0, 0, 0, 0)}
        no_priority = {0x47}  # the invisibility event of an action leaves its priority uninitialized too
        emu = self.emu

        def hook(uc, *_):
            event = uc.reg_read(UC_X86_REG_EAX)
            mask = keep.get(emu.read(event + 4, 1)[0])
            if mask:
                data = emu.read(event + 6, 6)
                emu.write(event + 6, bytes(b & m for b, m in zip(data, mask)))
            if emu.read(event + 4, 1)[0] in no_priority:
                emu.write(event + 5, b"\0")

        emu.uc.hook_add(UC_HOOK_CODE, hook, begin=TIMELINE_ADD_EVENT, end=TIMELINE_ADD_EVENT)

    def set_local(self, at, offset, value):
        """Sets the dword at ESP + offset when the original reaches at: a local that it leaves uninitialized."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP
        self.emu.uc.hook_add(UC_HOOK_CODE, lambda uc, *_: self.emu.write_u32(uc.reg_read(UC_X86_REG_ESP) + offset, value),
                             begin=at, end=at)

    def skip(self, begin, end):
        """Runs the original from end when it reaches begin: a code part of a later step."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EIP
        self.skips = getattr(self, "skips", {})
        self.skips[begin] = self.emu.uc.hook_add(UC_HOOK_CODE, lambda uc, *_: uc.reg_write(UC_X86_REG_EIP, end),
                                                 begin=begin, end=begin)

    def fix_square_events(self):
        """The original sensor_schedule_square_event leaves 2 data bytes (and the priority of a hold) uninitialized;
        this runs it with them cleared, like the C port, and then the original timeline_add_event."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, \
            UC_X86_REG_ESP
        emu = self.emu

        def hook(uc, *_):
            try:
                schedule(uc)
            except Exception as e:  # exceptions do not cross the Unicorn callbacks
                emu._fail(f"square event stub: {e!r}")

        def schedule(uc):
            x, y, cell, effect = (uc.reg_read(r) & 0xFF for r in (UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                                                                  UC_X86_REG_ECX))
            sp = uc.reg_read(UC_X86_REG_ESP)
            ret, time = emu.read_u32(sp), emu.read_u32(sp + 4)
            cur_map = struct.unpack("<h", emu.read(GLOBALS["map"][0], 2))[0]
            event = struct.pack("<IBB6B", (time | (cur_map & 0xFF) << 24) & 0xFFFFFFFF, 4, (1, 3, 2, 3)[effect & 3],
                                x, y, cell, effect, 0, 0)
            emu.write(SCHEDULED_EVENT, event)
            emu.write_u32(sp + 4, ret)
            uc.reg_write(UC_X86_REG_ESP, sp + 4)
            uc.reg_write(UC_X86_REG_EAX, SCHEDULED_EVENT)
            uc.reg_write(UC_X86_REG_EIP, TIMELINE_ADD_EVENT)

        emu.uc.hook_add(UC_HOOK_CODE, hook, begin=SENSOR_SCHEDULE_SQUARE_EVENT, end=SENSOR_SCHEDULE_SQUARE_EVENT)

    def place(self, map_, x, y, direction):
        """Puts the party on (x, y) of map_ in both, like a load."""
        self.game.party_dir = direction
        self.emu.write(0x7F254, struct.pack("<h", direction))
        self.current_map.map = -1
        self.emu.write(GLOBALS["map"][0], struct.pack("<h", -1))
        self.lib.dungeon_set_party_position(Position(map_, x, y))
        self.emu.call(DUN_SET_PARTY_LOCATION, x, y, map_)
        self.load_map_graphics()

    def time(self):
        return self.game.game_time

    def set_time(self, t):
        self.game.game_time = t
        self.emu.write_u32(G_GAME_TIME, t)

    def command(self, command):
        """Executes a command in both; returns the two results."""
        c = Command(0, 0, command)
        self.emu.write(COMMAND_RECORD, bytes(c))
        mine = self.lib.command_execute(ctypes.byref(c))
        theirs = self.emu.call(COMMAND_EXECUTE, COMMAND_RECORD)
        return mine, theirs

    def step(self):
        """The game loop part that this test runs per step: the timeline, the weather, the walk, and the cooldowns."""
        self.lib.game_process_timeline()
        self.emu.call(TIMELINE_PROCESS)
        self.lib.world_update_day_night(0)
        self.emu.call(WORLD_UPDATE_DAY_NIGHT, 0)
        self.flush_sounds()
        if self.party.move_steps:
            if self.party.move_steps == 1:
                self.lib.move_party(self.party.move_command)
                self.emu.call(MOVE_PARTY, self.party.move_command)
            self.party.move_steps -= 1
            self.emu.write(GLOBALS["move_steps"][0], struct.pack("<h", self.party.move_steps))
        for addr, field in ((G_MOVE_COOLDOWN, "party_move_cooldown"), (G_THROW_COOLDOWN, "throw_cooldown")):
            value = getattr(self.game, field)
            if value:
                setattr(self.game, field, value - 1)
                self.emu.write(addr, struct.pack("<H", value - 1))
        self.set_time(self.time() + 1)

    def save_states(self):
        """(C, original) save files and movement globals."""
        self.emu.written = bytearray()
        theirs = original_save(self.emu, self.start)
        path, bak = self.tmp / "SKSAVE0.DAT", self.tmp / "SKSAVE0.BAK"
        path.write_bytes(self.start)
        name = self.start[2:0x12].split(b"\0")[0]
        assert self.lib.save_write(str(path).encode(), str(bak).encode(), name, self.gdat) == 0
        mine = path.read_bytes()
        mine_globals = port_globals(self.party, self.current_map)
        their_globals = {name: struct.unpack(fmt, self.emu.read(addr, struct.calcsize(fmt)))[0]
                         for name, (addr, fmt) in GLOBALS.items()}
        return (mine, mine_globals), (theirs, their_globals)

    def differences(self):
        """Where the dungeons differ: squares, thing records, timeline events."""
        lib, emu = self.lib, self.emu
        out = []
        columns = emu.read_u32(0x7F3C8)
        for map_ in range(lib.dungeon_map_count()):
            w, h = lib.dungeon_map_width(map_), lib.dungeon_map_height(map_)
            table = emu.read_u32(columns + 4 * map_)
            for x in range(w):
                col = emu.read(emu.read_u32(table + 4 * x), h)
                for y in range(h):
                    if col[y] != lib.dungeon_square(map_, x, y):
                        out.append(("square", map_, x, y, hex(lib.dungeon_square(map_, x, y)), hex(col[y])))
        lib.dungeon_thing_record.restype = ctypes.c_void_p
        sizes = (4, 6, 4, 8, 16, 4, 4, 4, 4, 8, 4, 0, 0, 0, 8, 4)
        for t in range(16):
            n = lib.dungeon_thing_count(t)
            if not n:
                continue
            mine = ctypes.string_at(lib.dungeon_thing_record(t << 10), n * sizes[t])
            theirs = emu.read(emu.read_u32(0x7F288 + 4 * t), n * sizes[t])
            for i in range(n):
                a, b = mine[i * sizes[t]:(i + 1) * sizes[t]], theirs[i * sizes[t]:(i + 1) * sizes[t]]
                if a != b:
                    out.append(("thing", t, i, a.hex(), b.hex()))
        tl = Timeline.in_dll(lib, "g_timeline")
        mine = ctypes.string_at(ctypes.cast(tl.events, ctypes.c_void_p), tl.capacity * 12)
        theirs = emu.read(emu.read_u32(0x760E8), tl.capacity * 12)
        for i in range(tl.capacity):
            if mine[i * 12:i * 12 + 12] != theirs[i * 12:i * 12 + 12]:
                out.append(("event", i, mine[i * 12:i * 12 + 12].hex(), theirs[i * 12:i * 12 + 12].hex()))
        return out

    def pending(self):
        """(C, original) damage and wounds of the champions that the end of the step applies."""
        mine = tuple((ctypes.c_int16 * 4).in_dll(self.lib, "g_champ_pending_damage")) + \
            tuple((ctypes.c_uint16 * 4).in_dll(self.lib, "g_champ_pending_wounds"))
        theirs = struct.unpack("<4h", self.emu.read(G_PENDING_DAMAGE, 8)) + struct.unpack("<4H", self.emu.read(G_PENDING_WOUNDS, 8))
        return mine, theirs

    @property
    def damage(self):
        """True when a champion has pending damage or lost health since the last read (the coverage of a test)."""
        from dm2test.interface import champion_records
        records = bytes(champion_records(self.lib))
        health = tuple(struct.unpack_from("<h", records, i * CHAMPION_SIZE + 0x36)[0] for i in range(4))
        last, self._health = getattr(self, "_health", health), health
        return any(self.pending()[0][:4]) or any(h < l for h, l in zip(health, last))

    def champion_records(self):
        """(C, original) champion records without the redraw flags."""
        from dm2test.interface import champion_records
        mine, theirs = bytearray(champion_records(self.lib)), bytearray(self.emu.read(G_CHAMPIONS, 4 * CHAMPION_SIZE))
        for record in (mine, theirs):
            for i in range(4):
                record[i * CHAMPION_SIZE + CHAMPION_REDRAW:i * CHAMPION_SIZE + CHAMPION_REDRAW + 2] = bytes(2)
        return bytes(mine), bytes(theirs)

    def assert_same_damage(self, where):
        """The pending damage and wounds and the champion records of both."""
        mine, theirs = self.pending()
        assert mine == theirs, where
        mine, theirs = self.champion_records()
        if mine != theirs:
            first = next(i for i in range(len(mine)) if mine[i] != theirs[i])
            pytest.fail(f"{where}: champion {first // CHAMPION_SIZE} differs at {first % CHAMPION_SIZE:#x}: "
                        f"{mine[first:first + 4].hex()} {theirs[first:first + 4].hex()}")

    def assert_same(self, where):
        """The quick check: creatures, movement globals, damage calls, and the dungeon records and events."""
        self.check_creatures(where)
        self.assert_same_damage(where)
        mine = port_globals(self.party, self.current_map)
        theirs = {name: struct.unpack(fmt, self.emu.read(addr, struct.calcsize(fmt)))[0]
                  for name, (addr, fmt) in GLOBALS.items()}
        assert ignore_stale(mine) == ignore_stale(theirs), where
        assert self.differences() == [], where
        self.assert_same_weather(where)
        self.assert_same_sounds(where)
        assert ctypes.c_uint32.in_dll(self.lib, "g_random_seed").value == self.emu.read_u32(0x70518), where

    def assert_equal(self, where):
        self.assert_same_damage(where)
        (mine, mg), (theirs, tg) = self.state()
        assert ignore_stale(mg) == ignore_stale(tg), where
        if mine != theirs:
            for d in self.differences()[:20]:
                print(d)
            first = next(i for i in range(min(len(mine), len(theirs))) if mine[i] != theirs[i])
            pytest.fail(f"{where}: save files differ from byte {first:#x} ({len(mine)} and {len(theirs)} bytes)")


@pytest.fixture
def scenario(lib, gdat, start, tmp_path):
    lib.timeline_add_event.argtypes = [ctypes.c_void_p]
    return Scenario(lib, gdat, start, tmp_path)


def square_event(scenario, map_, x, y, cell, effect, delay):
    """Queues the same square event (type 4) in both."""
    e = TimelineEvent((scenario.time() + delay) | map_ << 24, 4, {0: 1, 1: 3, 2: 2, 3: 3}[effect],
              (ctypes.c_uint8 * 6)(x, y, cell, effect, 0, 0))
    scenario.emu.write(COMMAND_RECORD, bytes(e))
    scenario.lib.timeline_add_event(ctypes.byref(e))
    scenario.emu.call(TIMELINE_ADD_EVENT, COMMAND_RECORD)


def squares(lib, kinds):
    """All (map, x, y, square) whose square type is in kinds."""
    out = []
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                s = lib.dungeon_square(map_, x, y)
                if s >> 5 in kinds:
                    out.append((map_, x, y, s))
    return out


G_CHAMPION_COUNT, CHAMPION_CELL = 0x7F276, 29


def sensor_squares(lib):
    """(map, x, y, cell) of every square cell with a sensor or a text thing."""
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    lib.thing_word.restype = ctypes.c_uint16
    out, bad = set(), set()
    for map_ in range(lib.dungeon_map_count()):
        for x in range(lib.dungeon_map_width(map_)):
            for y in range(lib.dungeon_map_height(map_)):
                t = lib.dungeon_square_first_thing(map_, x, y)
                while t != ENDOFLIST:
                    w = lib.thing_word(t, 2)
                    if t >> 10 & 15 == 3 and w & 0x7F == 0x16 and w >> 7 & 0x3F >= lib.dungeon_map_count():
                        bad.add((map_, x, y, t >> 14))  # it sends events to a map outside the dungeon (C: fixed)
                    elif t >> 10 & 15 in (2, 3):
                        out.add((map_, x, y, t >> 14))
                    t = lib.dungeon_next_thing(t)
    return sorted(out - bad)


DIRS = ((0, -1), (1, 0), (0, 1), (-1, 0))


def approaches(lib, kind, condition=lambda s: True, limit=60):
    """(map, x, y, dir): a free square beside a square of the kind, facing it."""
    out = []
    for map_, x, y, s in squares(lib, (kind,)):
        if not condition(s):
            continue
        for d, (dx, dy) in enumerate(DIRS):
            px, py = x - dx, y - dy
            if 0 <= px < lib.dungeon_map_width(map_) and 0 <= py < lib.dungeon_map_height(map_) \
                    and lib.dungeon_square(map_, px, py) >> 5 == 1:
                out.append((map_, px, py, d))
                break
    return out[:limit]


def square_things(lib, map_, x, y):
    lib.dungeon_square_first_thing.restype = ctypes.c_uint16
    lib.dungeon_next_thing.restype = ctypes.c_uint16
    out, t = [], lib.dungeon_square_first_thing(map_, x, y)
    while t not in (0xFFFE, 0xFFFF):
        out.append(t)
        t = lib.dungeon_next_thing(t)
    return out
