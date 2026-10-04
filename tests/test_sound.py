import ctypes
import random
import struct

import pytest

from conftest import ROOT
from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image

GDAT = ROOT / "game" / "DATA" / "GRAPHICS.DAT"
SOUND_COMPUTE_VOLUME_PAN = 0x10877
# sound_request: from the party direction switch to the end of the distance scaling
OFFSET_BEGIN, OFFSET_END = 0x15E81, 0x16053
CREATURE_GET_PARTY_DISTANCE = 0x2FACC
G_PARTY_DIR, G_PARTY_X, G_PARTY_Y, G_REQUEST_COUNT = 0x7F254, 0x7F26C, 0x7F26E, 0x71690
# sound_request locals, offsets from ESP
L_X, L_Y, L_MODE, L_LATERAL, L_FORWARD = 0x44, 0x48, 0x4C, 0x18, 0x1C
UPSAMPLE = 4
PAN_LEFT, PAN_CENTER, PAN_RIGHT = 0x0800, 0x8000, 0xF800


class Sound(ctypes.Structure):
    _fields_ = [("data", ctypes.POINTER(ctypes.c_int8)), ("length", ctypes.c_uint32)]


@pytest.fixture(scope="module")
def snd(lib):
    c_int_p = ctypes.POINTER(ctypes.c_int)
    lib.sound_load.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 3 + [ctypes.POINTER(Sound)]
    lib.sound_party_offset.argtypes = [ctypes.c_int] * 6 + [c_int_p, c_int_p]
    lib.sound_volume_pan.argtypes = [ctypes.c_uint8, ctypes.c_int8, ctypes.c_int8, c_int_p, c_int_p]
    lib.sound_play.argtypes = [ctypes.POINTER(Sound), ctypes.c_uint8, ctypes.c_int8, ctypes.c_int8]
    lib.sound_mix.argtypes = [ctypes.POINTER(ctypes.c_int16), ctypes.c_int]
    return lib


@pytest.fixture(scope="module")
def emu():
    if not (ROOT / "game" / "SKULL.EXE").exists():
        pytest.skip("game/SKULL.EXE missing")
    return Emu(load_image(ROOT / "game" / "SKULL.EXE"))


def c_volume_pan(lib, volume, lateral, forward):
    loudness, pan = ctypes.c_int(), ctypes.c_int()
    lib.sound_volume_pan(volume, lateral, forward, ctypes.byref(loudness), ctypes.byref(pan))
    return loudness.value, pan.value


def emu_volume_pan(emu, volume, lateral, forward):
    emu.write(SCRATCH, struct.pack("<5xBbb4x", volume, lateral, forward))
    emu.call(SOUND_COMPUTE_VOLUME_PAN, SCRATCH)
    return struct.unpack("<BH", emu.read(SCRATCH + 8, 3))


def ratio_overflows(lateral, forward):
    return lateral and forward and (abs(lateral) << 11) // abs(forward) > 0xFFFF


def test_volume_pan_matches_original(snd, emu):
    rng = random.Random(18)
    cases = [(v, l, f) for v in (0, 1, 255) for l in (-128, -1, 0, 1, 127) for f in (-128, -1, 0, 1, 127)]
    cases += [(rng.randrange(256), rng.randrange(-128, 128), rng.randrange(-128, 128)) for _ in range(5000)]
    cases += [(rng.randrange(256), rng.randrange(-8, 9), rng.randrange(-8, 9)) for _ in range(2000)]
    for case in cases:
        if not ratio_overflows(*case[1:]):
            assert c_volume_pan(snd, *case) == emu_volume_pan(emu, *case), case


def test_volume_pan_fixes_ratio_overflow(snd, emu):
    # The original truncates (32 << 11) / 1 to 0 and pans the sound near the center.
    assert emu_volume_pan(emu, 255, 32, 1)[1] == 0x8F00
    assert c_volume_pan(snd, 255, 32, 1)[1] == PAN_RIGHT
    assert c_volume_pan(snd, 255, -32, 1)[1] == PAN_LEFT


def c_offset(lib, x, y, px, py, direction, path):
    lateral, forward = ctypes.c_int(), ctypes.c_int()
    ok = lib.sound_party_offset(x, y, px, py, direction, path, ctypes.byref(lateral), ctypes.byref(forward))
    return ok, lateral.value, forward.value


def emu_offset(emu, x, y, px, py, direction, path):
    sp = emu.frame()
    emu.write(G_PARTY_DIR, struct.pack("<H", direction))
    emu.write(G_PARTY_X, struct.pack("<hh", px, py))
    emu.write(G_REQUEST_COUNT, struct.pack("<H", 0))  # no queued request to compare with
    for offset, value in ((L_X, x), (L_Y, y), (L_MODE, 0)):
        emu.write(sp + offset, struct.pack("<h", value))
    emu.run(OFFSET_BEGIN, OFFSET_END)
    return struct.unpack("<h", emu.read(sp + L_LATERAL, 2))[0], struct.unpack("<h", emu.read(sp + L_FORWARD, 2))[0]


def test_party_offset_matches_original(snd, emu):
    path = 0
    emu.stub(CREATURE_GET_PARTY_DISTANCE, lambda *_: path)
    rng = random.Random(18)
    for _ in range(3000):
        x, y, px, py = (rng.randrange(32) for _ in range(4))
        direction, path = rng.randrange(4), rng.randrange(80)
        case = (x, y, px, py, direction, path)
        assert c_offset(snd, *case) == (1, *emu_offset(emu, *case)), case


def test_party_offset_drops_sound_without_path(snd):
    assert c_offset(snd, 5, 5, 3, 5, 0, -1)[0] == 0
    assert c_offset(snd, 4, 5, 3, 5, 0, -1) == (1, 1, 0)  # next square: no path check
    assert c_offset(snd, 5, 5, 3, 5, 0, 4) == (1, 4, 0)    # two squares, heard around a corner at four


def test_sound_load_sample_count(snd):
    if not GDAT.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    g = ctypes.c_void_p()
    snd.gdat_load.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    snd.gdat_free.argtypes = [ctypes.c_void_p]
    assert snd.gdat_load(str(GDAT).encode(), ctypes.byref(g)) == 0
    s = Sound()
    assert snd.sound_load(g, 3, 0, 129, ctypes.byref(s)) == 0
    assert s.length == 7927
    assert s.data[:4] == [127, 107, 32, -51]  # signed PCM after the 6-byte header
    assert ctypes.string_at(ctypes.addressof(s.data.contents) - 6, 6) == bytes.fromhex("772b08010000")
    assert snd.sound_load(g, 3, 0, 250, ctypes.byref(s)) != 0
    snd.gdat_free(g)


def mix(lib, frames):
    buf = (ctypes.c_int16 * (2 * frames))()
    lib.sound_mix(buf, frames)
    return list(buf)


def test_mix_upsamples_and_pans(snd):
    pcm = (ctypes.c_int8 * 3)(100, -100, 50)
    s = Sound(ctypes.cast(pcm, ctypes.POINTER(ctypes.c_int8)), 3)
    assert snd.sound_play(ctypes.byref(s), 255, 0, 0)  # center, loudness 255, gain 0x7fff
    out = mix(snd, 3 * UPSAMPLE + 1)
    expected = [100 * 256 * 0x7FFF >> 15, -100 * 256 * 0x7FFF >> 15, 50 * 256 * 0x7FFF >> 15]
    assert out[0::2] == out[1::2] == [v for v in expected for _ in range(UPSAMPLE)] + [0]
    assert snd.sound_play(ctypes.byref(s), 255, 1, 0)  # full right: the left side gets 1/16
    left, right = mix(snd, 1)
    loudness = c_volume_pan(snd, 255, 1, 0)[0]
    gain = loudness << 7 | loudness >> 1
    assert (left, right) == (100 * 256 * (gain // 16) >> 15, 100 * 256 * gain >> 15)
    mix(snd, 3 * UPSAMPLE)
    assert mix(snd, 1) == [0, 0]


def test_all_voices_busy(snd):
    pcm = (ctypes.c_int8 * 1)(1)
    s = Sound(ctypes.cast(pcm, ctypes.POINTER(ctypes.c_int8)), 1)
    assert all(snd.sound_play(ctypes.byref(s), 255, 0, 0) for _ in range(64))
    assert not snd.sound_play(ctypes.byref(s), 255, 0, 0)
    mix(snd, UPSAMPLE)
    assert snd.sound_play(ctypes.byref(s), 255, 0, 0)
    mix(snd, UPSAMPLE)
