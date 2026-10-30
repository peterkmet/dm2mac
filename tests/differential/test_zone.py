import ctypes
import random
import struct

import pytest

from dm2test import constants as C
from dm2test.markers import ROOT
from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image
from dm2test.structs import Party
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

GRAPHICS = ROOT / "game" / "DATA" / "GRAPHICS.DAT"
ZONE_UNPACK_LAYOUT, ZONE_ALLOC_RECORD, ZONE_GET_RECT_CLIPPED = 0x19D0E, 0x19F29, 0x19BF2
MEM_ALLOC = 0x3BBBA
G_ZONE_LIST = 0x71796
LAYOUT, RECT, HEAP = SCRATCH, SCRATCH + 0x8000, SCRATCH + 0x10000
SCALES = [C.ZONE_SCALE_FULL, 5000, 1, 0, 2500, 7777, 12000, 65535]


@pytest.fixture(scope="module")
def gdat(lib):
    if not GRAPHICS.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    g = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = ctypes.POINTER(ctypes.c_uint8)
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == 0
    yield g
    lib.gdat_free(g)


def layout_bytes(lib, gdat):
    size = ctypes.c_uint32()
    return ctypes.string_at(lib.gdat_item(gdat, lib.gdat_find(gdat, 1, 0, 4, 0), ctypes.byref(size)), size.value)


@pytest.fixture(scope="module")
def emu(lib, gdat):
    emu = Emu(load_image(ROOT / "game" / "SKULL.EXE"))
    heap = [HEAP]

    def alloc(size, *_):
        heap[0] += size + 3 & ~3
        return heap[0] - (size + 3 & ~3)

    emu.stub(MEM_ALLOC, alloc)
    data = layout_bytes(lib, gdat)
    emu.write(LAYOUT, data)
    emu.call(ZONE_UNPACK_LAYOUT, LAYOUT, len(data), G_ZONE_LIST, ZONE_ALLOC_RECORD)
    return emu


def zone_ids(data):
    count = int.from_bytes(data[2:4], "little")
    ranges = [(int.from_bytes(data[4 + 4 * i:6 + 4 * i], "little", signed=True),
               int.from_bytes(data[6 + 4 * i:8 + 4 * i], "little", signed=True)) for i in range(count)]
    return sorted({z for first, last in ranges for z in range(first, last + 1)})


G_ZONE_CLIP_RECT = 0x7F1C8
MARGINS = [None, (0, 0, 0, 0), (10, 5, 20, 30)]


@pytest.mark.parametrize("margins", MARGINS)
def test_zone_rect_matches_original(lib, gdat, emu, margins):
    assert lib.zone_load(gdat) == 1
    Party.in_dll(lib, "g_party").move_steps = 0  # a walk clips the zones to g_zone_walk_view_rect; earlier tests leave one
    rng = random.Random(5)
    zones = zone_ids(layout_bytes(lib, gdat))
    assert len(zones) > 1000
    clip = (ctypes.c_int16 * 4).in_dll(lib, "g_zone_clip_rect")
    if margins:
        lib.zone_set_clip_margins(*margins)
    else:
        clip[2] = 0
    emu.write(G_ZONE_CLIP_RECT, bytes(clip))
    lib.zone_get_rect_clipped.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_int16), ctypes.c_uint16, ctypes.c_uint16]
    found_count = 0
    for zone in zones + [0, -1, 0x7FFF]:
        for w_scale, h_scale in [(C.ZONE_SCALE_FULL, C.ZONE_SCALE_FULL)] + [(rng.choice(SCALES), rng.choice(SCALES)) for _ in range(3)]:
            emu.write(RECT, bytes(8))
            found = emu.call(ZONE_GET_RECT_CLIPPED, zone, RECT, w_scale, h_scale)
            rect = (ctypes.c_int16 * 4)()
            mine = lib.zone_get_rect_clipped(zone, rect, w_scale, h_scale)
            assert bool(mine) == bool(found), (zone, w_scale, h_scale)
            if found:
                found_count += 1
                assert bytes(rect) == emu.read(RECT, 8), (zone, w_scale, h_scale)
    assert found_count > 500
    clip[2] = 0


def test_icon_rect_of_unknown_zone(lib, gdat):
    # An icon in a zone that the layout does not place (a damaged GRAPHICS.DAT) gets the zero rect.
    assert lib.zone_load(gdat) == 1
    lib.zone_get_icon_rect.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_int16)]
    rect = (ctypes.c_int16 * 4)(0x55, 0x55, 0x55, 0x55)
    lib.zone_get_icon_rect(0x7FFF, rect)
    assert tuple(rect) == (0, 0, 0, 0)


def test_zone_records_past_the_item(lib, gdat):
    # A damaged GRAPHICS.DAT whose zone range holds more zone records than the layout item is rejected. The test
    # damages its own copy; the gdat fixture sets the argument types.
    g = ctypes.c_void_p()
    assert lib.gdat_load(str(GRAPHICS).encode(), ctypes.byref(g)) == 0
    try:
        size = ctypes.c_uint32()
        item = lib.gdat_item(g, lib.gdat_find(g, 1, 0, 4, 0), ctypes.byref(size))
        assert size.value < 8 * 0x7FFF
        # one range count, then the range of zones 1 to 0x7FFF
        ctypes.memmove(ctypes.addressof(item.contents) + 2, struct.pack("<3h", 1, 1, 0x7FFF), 6)
        assert lib.zone_load(g) == 0
    finally:
        lib.gdat_free(g)
