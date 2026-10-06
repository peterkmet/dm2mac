import ctypes
import math
import struct

import pytest
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INSN, UC_MODE_32, Uc
from unicorn.x86_const import (
    UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ES,
    UC_X86_REG_ESP, UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_GS, UC_X86_REG_SS,
)

from conftest import ROOT

GAME = ROOT / "game"
SONG_COUNT = 29
SONG_TEST_HMP = 10  # game/TEST.HMP is song 10
SONG_GAME = 2       # the song of map 0
SAMPLE_RATE = 44100
TICK_RATE = 120
OK = 0
# SONGLIST.DAT bytes 0 to 45 (docs/skull-formats.md, section 2)
SONGLIST = [2, 17, 14, 27, 4, 12, 12, 18, 15, 13, 12, 12, 16, 6, 21, 14, 17, 17, 17, 17, 3, 8, 17, 14, 2, 23, 22,
            20, 17, 0, 2, 2, 2, 9, 2, 3, 14, 16, 28, 22, 19, 9, 22, 3, 17, 2]

# HMP layout (port/src/hmp.c)
HMP_BRANCHES, HMP_TRACK_COUNT, HMP_FIRST_TRACK, TRACK_DATA, BRANCH_SIZE = 0x20, 0x30, 0x388, 12, 0x18
EVENT_LENGTH = {0x8: 3, 0x9: 3, 0xA: 3, 0xB: 3, 0xC: 2, 0xD: 2, 0xE: 3}
HMI_FIRST, HMI_LAST, HMI_LOOP = 0x67, 0x79, 0x6F
LOOP_ID = 0x80  # the loop point id of TEST.HMP

# fmmidi.com, the OPL2 driver in HMIMDRV.386 (docs/skull-formats.md, section 5)
DRIVER_FILE_OFFSET, DRIVER_SIZE = 0x5C, 0x318C
DRV_INIT, DRV_RESET, DRV_SET_BANK, DRV_SEND = 0xA0A, 0xAE1, 0xBC9, 0x80D
DRV_SEMITONE_LOOP = 0x1A93  # note - 12 mod 12 by repeated subtraction; billions of steps for notes below 12
OPL_PORT = 0x388
GDT, STACK, DATA, RET = 0x10000, 0x20000, 0x30000, 0x40000
MELODIC_AT, DRUM_AT, EVENT_AT = DATA + 0x1000, DATA + 0x3000, DATA + 0x10

u8_p = ctypes.POINTER(ctypes.c_uint8)
SEND = ctypes.CFUNCTYPE(None, u8_p)
WRITE_HOOK = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int)


class Music(ctypes.Structure):
    _fields_ = [("enabled", ctypes.c_int), ("songlist_loaded", ctypes.c_int), ("songlist", ctypes.c_uint8 * 63),
                ("songlist_size", ctypes.c_int), ("fade", ctypes.c_int16), ("paused", ctypes.c_int),
                ("playing", ctypes.c_int), ("started", ctypes.c_int), ("requested_song", ctypes.c_int16),
                ("current_song", ctypes.c_uint8), ("next_song", ctypes.c_uint8), ("volume", ctypes.c_int16)]


@pytest.fixture(scope="module")
def gd(lib):
    if not (GAME / "DATA" / "GRAPHICS.DAT").exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    lib.gdat_load.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.gdat_free.argtypes = [ctypes.c_void_p]
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = u8_p
    lib.fm_set_bank.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t]
    lib.fm_send.argtypes = [ctypes.c_char_p]
    lib.fm_set_write_hook.argtypes = [ctypes.c_void_p]
    lib.hmp_start.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p]
    lib.music_init.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.music_render.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
    handle = ctypes.c_void_p()
    assert lib.gdat_load(str(GAME / "DATA" / "GRAPHICS.DAT").encode(), ctypes.byref(handle)) == OK
    yield handle
    lib.gdat_free(handle)


@pytest.fixture
def music(lib, gd):
    """The music module after music_init, with the start values of the original globals."""
    state = Music.in_dll(lib, "music")
    lib.music_quit()
    ctypes.memset(ctypes.byref(state), 0, ctypes.sizeof(state))
    state.requested_song, state.current_song = -1, 0xFF
    assert lib.music_init(gd, str(GAME).encode()) == OK
    yield state
    lib.music_quit()


def song_bytes(lib, gd, song):
    size = ctypes.c_uint32()
    item = lib.gdat_find(gd, 4, song, 3, 0)
    return ctypes.string_at(lib.gdat_item(gd, item, ctypes.byref(size)), size.value)


def varlen(data, i):
    value = shift = 0
    while True:
        b = data[i]
        value |= (b & 0x7F) << shift
        shift += 7
        i += 1
        if b & 0x80:
            return value, i


def tracks(data):
    """(data start, chunk end, loop records) of each track."""
    count, branches = struct.unpack_from("<I", data, HMP_TRACK_COUNT)[0], struct.unpack_from("<I", data, HMP_BRANCHES)[0]
    record, offset, result = branches + count, HMP_FIRST_TRACK, []
    for t in range(count):
        size = struct.unpack_from("<I", data, offset + 4)[0]
        loops = {}
        for _ in range(data[branches + t]):
            point, loop_id, program, _, ctrl_bytes = struct.unpack_from("<I4B", data, record)
            ctrls = struct.unpack_from("<I", data, record + 0xC)[0]
            loops[loop_id] = (point, program, data[ctrls:ctrls + ctrl_bytes])
            record += BRANCH_SIZE
        result.append((offset + TRACK_DATA, offset + size, loops))
        offset += size
    return result


def track_events(data, pos, end, first_tick):
    """(tick, event) of a track from pos: the first event at first_tick(first delta), then the deltas."""
    delta, pos = varlen(data, pos)
    tick, result = first_tick(delta), []
    while pos < end and data[pos] != 0xFF:
        length = EVENT_LENGTH[data[pos] >> 4]
        result.append((tick, data[pos:pos + length]))
        pos += length
        delta, pos = varlen(data, pos)
        tick += delta
    return result


def is_hmi(event):
    return event[0] & 0xF0 == 0xB0 and HMI_FIRST <= event[1] <= HMI_LAST


def sent(event):
    """The event as the player sends it with song and master volume 127: channel volumes scaled."""
    if event[0] & 0xF0 == 0xB0 and event[1] == 7:
        return bytes([event[0], 7, 127 * (event[2] * 127 >> 7) >> 7])
    return bytes(event[:3]).ljust(3, b"\0")


def expected_events(data, until):
    """(tick, track, event) that the player sends before tick until and before the first HMI loop."""
    found, loop_tick = [], until
    for t, (start, end, _) in enumerate(tracks(data)):
        for i, (tick, event) in enumerate(track_events(data, start, end, lambda d: max(d, 1))):
            if is_hmi(event) and event[1] == HMI_LOOP:
                loop_tick = min(loop_tick, tick)
            if t and not is_hmi(event):
                found.append((tick, t, i, sent(event)))
    return [(tick, t, e) for tick, t, i, e in sorted(found) if tick < loop_tick], loop_tick


def normalized(event):
    return event[:EVENT_LENGTH[event[0] >> 4]].ljust(3, b"\0")


_callbacks = []  # the player may call a callback after its test; keep them all alive


class Recorder:
    """Runs the song timer and records (tick, event) of each event sent."""

    def __init__(self, lib, data):
        self.lib, self.events, self.tick = lib, [], 0
        self.send = SEND(lambda p: self.events.append((self.tick, normalized(ctypes.string_at(p, 3)))))
        _callbacks.append(self.send)
        assert lib.hmp_start(data, len(data), ctypes.cast(self.send, ctypes.c_void_p)) == OK
        lib.hmp_set_master_volume(127)
        lib.hmp_set_song_volume(127)
        lib.hmp_unmute()
        self.events.clear()

    def run(self, ticks):
        for _ in range(ticks):
            self.tick += 1
            self.lib.hmp_tick()


def hmp_sources(lib, gd):
    return {"TEST.HMP": (GAME / "TEST.HMP").read_bytes(), "song 2": song_bytes(lib, gd, SONG_GAME)}


@pytest.mark.parametrize("name", ["TEST.HMP", "song 2"])
def test_event_times_equal_hmp_times(lib, gd, name):
    data = hmp_sources(lib, gd)[name]
    seconds = 10
    expected, loop_tick = expected_events(data, seconds * TICK_RATE + 1)
    rec = Recorder(lib, data)
    rec.run(seconds * TICK_RATE)
    actual = [(tick, e) for tick, e in rec.events if tick < loop_tick]
    lib.hmp_stop()
    assert len(expected) > 100
    assert actual == [(tick, e) for tick, _, e in expected]


def test_loop_restarts_tracks_at_their_loop_points(lib, gd):
    data = (GAME / "TEST.HMP").read_bytes()
    _, loop_tick = expected_events(data, 1 << 30)
    rec = Recorder(lib, data)
    rec.run(loop_tick - 1)
    rec.events.clear()
    rec.run(TICK_RATE * 5)
    events, rec.events = rec.events, []
    lib.hmp_stop()
    all_tracks = tracks(data)
    loop_track = next(t for t, (start, end, _) in enumerate(all_tracks)
                      if any(is_hmi(e) and e[1] == HMI_LOOP for _, e in track_events(data, start, end, lambda d: d)))
    expected = []
    for t, (start, end, loops) in enumerate(all_tracks[1:], 1):
        if LOOP_ID not in loops:
            continue
        point, program, ctrls = loops[LOOP_ID]
        channel = track_events(data, start + point, end, lambda d: d)[0][1][0] & 0x0F
        setup = [bytes([0xC0 | channel, program, 0])] + [sent([0xB0 | channel, ctrls[i], ctrls[i + 1]])
                                                          for i in range(0, len(ctrls), 2)]
        expected += [(loop_tick, t, i - len(setup), e) for i, e in enumerate(setup)]
        # The loop track runs on in the same tick; later tracks count the loop tick as their first tick.
        first = (lambda d: loop_tick + d) if t == loop_track else (lambda d: loop_tick + max(d, 1) - 1)
        for i, (tick, event) in enumerate(track_events(data, start + point, end, first)):
            if not is_hmi(event) and tick < loop_tick + TICK_RATE * 5:
                expected.append((tick, t, i, sent(event)))
    setup_count = sum(1 for e in expected if e[2] < 0)
    actual_setup = [e for tick, e in events if tick == loop_tick][:setup_count]
    assert actual_setup == [e for _, _, i, e in sorted(expected, key=lambda x: (x[1], x[2])) if i < 0]
    later = [(tick, e) for tick, e in events if tick > loop_tick]
    assert later == [(tick, e) for tick, t, i, e in sorted(expected) if i >= 0 and tick > loop_tick]


def rms(samples):
    return math.sqrt(sum(s * s for s in samples) / len(samples))


@pytest.mark.parametrize("name", ["TEST.HMP", "song 2"])
def test_render_first_seconds_not_silent(lib, gd, music, name):
    if name == "TEST.HMP":
        data = (GAME / "TEST.HMP").read_bytes()
        assert lib.hmp_start(data, len(data), ctypes.cast(lib.fm_send, ctypes.c_void_p)) == OK
        lib.hmp_set_song_volume(127)
        lib.hmp_unmute()
    else:
        lib.music_play_song(SONG_GAME)
        assert music.playing
    frames = SAMPLE_RATE * 3
    out = (ctypes.c_int16 * (2 * frames))()
    lib.music_render(out, frames)
    assert rms(out) > 300


def test_render_without_banks_is_silent(lib, gd, music):
    lib.music_quit()
    out = (ctypes.c_int16 * 200)(*([1] * 200))
    lib.music_render(out, 100)
    assert list(out) == [0] * 200


# Song selection: music_update_for_map (FUN_00010af6) once per game step.

def test_songlist_gives_the_song_of_each_map(music):
    assert music.songlist_loaded and music.songlist_size == 63
    assert list(music.songlist[:len(SONGLIST)]) == SONGLIST
    assert set(music.songlist[len(SONGLIST):]) == {0xFF}


def test_first_map_starts_its_song_now(lib, music):
    lib.music_update_for_map(0)
    assert (music.playing, music.current_song, music.requested_song, music.fade) == (1, 2, 2, 0)
    lib.music_update_for_map(0)
    assert (music.current_song, music.fade) == (2, 0)


def test_new_map_fades_out_then_starts_its_song(lib, music):
    volumes = []
    lib.music_update_for_map(0)
    lib.music_update_for_map(1)
    assert (music.fade, music.current_song, music.requested_song) == (0x7F, 17, 2)
    for _ in range(0x7F - 1):
        volumes.append(music.fade)
        lib.music_update_for_map(1)
    assert volumes == list(range(0x7F, 1, -1))
    assert (music.fade, music.requested_song) == (1, 2)
    lib.music_update_for_map(1)
    assert (music.fade, music.current_song, music.requested_song, music.playing) == (0, 17, 17, 1)


def test_map_change_during_fade_starts_the_song_now(lib, music):
    lib.music_update_for_map(0)
    lib.music_update_for_map(1)
    lib.music_update_for_map(2)
    assert (music.fade, music.current_song, music.requested_song) == (0, 14, 14)


def test_song_zero_keeps_the_old_song_after_the_fade(lib, music):
    lib.music_update_for_map(0)
    lib.music_update_for_map(29)  # SONGLIST.DAT gives song 0
    for _ in range(0x7F):
        lib.music_update_for_map(29)
    assert (music.fade, music.current_song, music.requested_song, music.playing) == (0, 0, 0, 1)


def test_map_outside_songlist_keeps_the_song(lib, music):
    lib.music_update_for_map(0)
    lib.music_update_for_map(1000)
    assert (music.current_song, music.fade) == (2, 0)


def test_pause_until_next_game_step(lib, music):
    lib.music_update_for_map(0)
    lib.music_pause()
    assert music.paused
    lib.music_update_for_map(0)
    assert not music.paused and music.playing


def test_volume_zero_stops_and_volume_up_restarts_the_song(lib, music):
    lib.music_update_for_map(0)
    lib.music_set_volume(0)
    assert (music.playing, music.volume) == (0, 0)
    lib.music_play_song(17)  # no song plays at volume 0, but the game remembers the request
    assert (music.playing, music.requested_song) == (0, 17)
    lib.music_set_volume(9)
    assert (music.playing, music.volume, music.requested_song) == (1, 7, 17)


# The FM driver: the port writes the same OPL registers as fmmidi.com run in the emulator.

class Driver:
    """fmmidi.com at address 0 in a flat 32-bit Unicorn machine; collects its OPL register writes."""

    def __init__(self, blob, melodic, drum):
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0, GDT)
        uc.mem_write(0, blob)
        uc.mem_map(GDT, 0x1000)
        uc.mem_write(GDT, struct.pack("<3Q", 0, 0x00CF9A000000FFFF, 0x00CF92000000FFFF))
        uc.reg_write(UC_X86_REG_GDTR, (0, GDT, 3 * 8 - 1, 0))
        for reg in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS, UC_X86_REG_GS):
            uc.reg_write(reg, 0x10)
        uc.reg_write(UC_X86_REG_CS, 0x08)
        for base in (STACK, DATA, RET):
            uc.mem_map(base, 0x10000)
        self.writes, self.index = [], 0
        uc.hook_add(UC_HOOK_INSN, self._out, aux1=UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INSN, lambda *_: 0, aux1=UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_CODE, self._semitone, begin=DRV_SEMITONE_LOOP, end=DRV_SEMITONE_LOOP)
        uc.mem_write(DATA, struct.pack("<I", OPL_PORT))
        uc.mem_write(MELODIC_AT, melodic)
        uc.mem_write(DRUM_AT, drum)
        self.call(DRV_INIT, 0, 0, DATA, 0x10)
        self.call(DRV_RESET)
        self.call(DRV_SET_BANK, 0, 0, MELODIC_AT, 0x10)
        self.call(DRV_SET_BANK, 0, 0, DRUM_AT, 0x10)
        self.writes.clear()

    def _out(self, uc, port, size, value, _):
        if port == OPL_PORT:
            self.index = value
        elif port == OPL_PORT + 1:
            self.writes.append((self.index, value))

    @staticmethod
    def _semitone(uc, *_):
        # Same result as the loop of subtractions, without its run time.
        addr = uc.reg_read(UC_X86_REG_EBP) - 0x18
        uc.mem_write(addr, struct.pack("<I", struct.unpack("<I", uc.mem_read(addr, 4))[0] % 12))

    def call(self, addr, *args):
        sp = STACK + 0x8000
        self.uc.mem_write(sp, struct.pack(f"<{len(args) + 1}I", RET, *args))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.emu_start(addr, RET, count=10_000_000)
        assert self.uc.reg_read(UC_X86_REG_EIP) == RET

    def send(self, event):
        self.uc.mem_write(EVENT_AT, event)
        self.call(DRV_SEND, 0, 0, EVENT_AT, 0x10)


# Events that the songs do not reach in their first part: unpacked instruments 126 and 127, sustain, notes below 12
# with pitch bend, bend range, drums, voice stealing, controller reset of channels above 8, aftertouch.
EDGE_EVENTS = [
    "c07e", "903c40", "c17f", "913050", "b2407f", "c205", "924060", "824000", "924360", "b24000",
    "e30030", "c310", "930470", "937f70", "e3007f", "e90010", "99237f", "997f40", "892300",
    "b4660c", "e40000", "943c40", "e4007f", "b40720",
    *[f"9{ch:x}{0x30 + ch:02x}50" for ch in range(5, 16)],
    "bc7900", "bd7900", "b57b00", "b60730", "a03c10", "d01000", "e00040",
    "c711", "973c40", "c911", "99233f",  # additive instruments (17) on a melodic and the drum channel
]


def test_fm_driver_writes_like_the_original(lib, gd):
    if not (GAME / "HMIMDRV.386").exists():
        pytest.skip("game/HMIMDRV.386 missing")
    blob = (GAME / "HMIMDRV.386").read_bytes()[DRIVER_FILE_OFFSET:DRIVER_FILE_OFFSET + DRIVER_SIZE]
    melodic, drum = (GAME / "MELODIC.BNK").read_bytes(), (GAME / "DRUM.BNK").read_bytes()
    driver = Driver(blob, melodic, drum)
    lib.fm_reset(SAMPLE_RATE)
    assert lib.fm_set_bank(0, melodic, len(melodic)) == OK
    assert lib.fm_set_bank(1, drum, len(drum)) == OK
    writes = []
    hook = WRITE_HOOK(lambda reg, value: writes.append((reg, value)))
    lib.fm_set_write_hook(ctypes.cast(hook, ctypes.c_void_p))
    try:
        events = [bytes.fromhex(e).ljust(3, b"\0") for e in EDGE_EVENTS]
        for song in range(SONG_COUNT):
            data = song_bytes(lib, gd, song)
            found = [(tick, t, i, bytes(e).ljust(3, b"\0"))
                     for t, (start, end, _) in enumerate(tracks(data)) if t
                     for i, (tick, e) in enumerate(track_events(data, start, end, lambda d: d)) if not is_hmi(e)]
            events += [e for *_, e in sorted(found)[:1000]]
        for event in events:
            driver.send(event)
            lib.fm_send(event)
            assert writes == driver.writes, event.hex()
            writes.clear()
            driver.writes.clear()
    finally:
        lib.fm_set_write_hook(None)
