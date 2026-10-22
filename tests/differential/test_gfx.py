import ctypes
import random

import pytest

from dm2tools.emu import SCRATCH, Emu
from dm2tools.le import load_image
from dm2test import constants as C
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

GFX_BLIT, GFX_BLIT_SCALED_4BPP, GFX_BLIT_SCALED_8BPP = 0x12AD2, 0x1410D, 0x1424B
GFX_BLIT_TILED_4TO8_MASKED = 0x13BE5
SRC, DST, RECT, MAP, MASK = SCRATCH, SCRATCH + 0x10000, SCRATCH + 0x20000, SCRATCH + 0x20100, SCRATCH + 0x30000
OPAQUE = C.GFX_OPAQUE
u8_p = ctypes.POINTER(ctypes.c_uint8)


class Blit(ctypes.Structure):
    _fields_ = [("src", u8_p), ("dst", u8_p), ("src_x", ctypes.c_int), ("src_y", ctypes.c_int),
                ("src_width", ctypes.c_int), ("dst_width", ctypes.c_int), ("src_bpp", ctypes.c_int),
                ("dst_bpp", ctypes.c_int), ("transparent", ctypes.c_int), ("mirror", ctypes.c_int),
                ("colormap", u8_p)]


class TileFill(ctypes.Structure):
    _fields_ = [("tile", u8_p), ("tile_width", ctypes.c_int), ("tile_offset", ctypes.c_int), ("mask", u8_p),
                ("mask_x", ctypes.c_int), ("mask_y", ctypes.c_int), ("mask_width", ctypes.c_int),
                ("transparent", ctypes.c_int), ("colormap", u8_p)]


class Bitmap(ctypes.Structure):
    _fields_ = [("pixels", u8_p), ("width", ctypes.c_int16), ("height", ctypes.c_int16), ("bpp", ctypes.c_uint8)]


def buf(data):
    return (ctypes.c_uint8 * len(data))(*data)


def rect(*values):
    return (ctypes.c_int16 * 4)(*values)


def call_blit(lib, src, dst, r, src_x, src_y, src_width, dst_width, transparent, mirror, src_bpp, dst_bpp, colormap):
    blit = Blit(src, dst, src_x, src_y, src_width, dst_width, src_bpp, dst_bpp, transparent, mirror, colormap)
    lib.gfx_blit(ctypes.byref(blit), r)


def blit(lib, src, dst_size, dst_width, r, src_x=0, src_y=0, src_width=3, transparent=OPAQUE, mirror=C.MIRROR_NONE,
         src_bpp=C.IMAGE_8BPP, dst_bpp=C.IMAGE_8BPP, colormap=None):
    dst = buf([0] * dst_size)
    call_blit(lib, buf(src), dst, rect(*r), src_x, src_y, src_width, dst_width, transparent, mirror, src_bpp,
              dst_bpp, buf(colormap) if colormap else None)
    return list(dst)


SRC_3X2 = [1, 2, 3,
           4, 5, 6]


@pytest.mark.parametrize("transparent, mirror, expected", [
    (OPAQUE, C.MIRROR_NONE, [0, 0, 0, 0,
                 0, 1, 2, 3,
                 0, 4, 5, 6]),
    (2, C.MIRROR_NONE, [0, 0, 0, 0,
            0, 1, 0, 3,
            0, 4, 5, 6]),
    (OPAQUE, C.MIRROR_H, [0, 0, 0, 0,
                 0, 3, 2, 1,
                 0, 6, 5, 4]),
    (OPAQUE, C.MIRROR_V, [0, 0, 0, 0,
                 0, 4, 5, 6,
                 0, 1, 2, 3]),
    (OPAQUE, C.MIRROR_HV, [0, 0, 0, 0,
                 0, 6, 5, 4,
                 0, 3, 2, 1]),
])
def test_blit_8bpp(lib, transparent, mirror, expected):
    assert blit(lib, SRC_3X2, 12, 4, (1, 1, 3, 2), transparent=transparent, mirror=mirror) == expected


def test_blit_source_window(lib):
    # A 2 x 1 window at (1, 1) of the source, mirrored: the window is mirrored, not the whole row.
    assert blit(lib, SRC_3X2, 4, 4, (0, 0, 2, 1), src_x=1, src_y=1, mirror=C.MIRROR_H) == [6, 5, 0, 0]


def test_blit_4bpp_to_8bpp_with_colormap(lib):
    # 3 x 2 pixels, rows of 4 nibbles (even width), high nibble first.
    src = [0x12, 0x30, 0x45, 0x60]
    colormap = [10 + i for i in range(16)]
    assert blit(lib, src, 6, 3, (0, 0, 3, 2), src_bpp=C.IMAGE_4BPP, colormap=colormap) == [11, 12, 13, 14, 15, 16]
    # The transparent value is a source pixel, before the colormap.
    assert blit(lib, src, 6, 3, (0, 0, 3, 2), src_bpp=C.IMAGE_4BPP, colormap=colormap, transparent=2) == [11, 0, 13, 14, 15, 16]
    assert blit(lib, src, 6, 3, (0, 0, 3, 2), src_bpp=C.IMAGE_4BPP, colormap=colormap, mirror=C.MIRROR_HV) == [16, 15, 14, 13, 12, 11]


def test_blit_8bpp_with_colormap(lib):
    colormap = list(range(255, -1, -1))
    assert blit(lib, SRC_3X2, 6, 3, (0, 0, 3, 2), colormap=colormap) == [254, 253, 252, 251, 250, 249]


def test_blit_4bpp_to_4bpp(lib):
    src = [0x12, 0x30, 0x45, 0x60]
    assert blit(lib, src, 4, 3, (0, 0, 3, 2), src_bpp=C.IMAGE_4BPP, dst_bpp=C.IMAGE_4BPP) == [0x12, 0x30, 0x45, 0x60]
    assert blit(lib, src, 4, 3, (1, 0, 2, 1), src_bpp=C.IMAGE_4BPP, dst_bpp=C.IMAGE_4BPP) == [0x01, 0x20, 0, 0]


@pytest.fixture(scope="module")
def emu():
    from dm2test.markers import ROOT
    exe = ROOT / "game" / "SKULL.EXE"
    if not exe.exists():
        pytest.skip("game/SKULL.EXE missing")
    return Emu(load_image(exe))


def bitmap(pixels, width, height, bpp):
    return Bitmap(ctypes.cast(pixels, u8_p), width, height, bpp)


def run_both(lib, emu, func, addr, src, dst_size, args, cargs):
    """Runs the C function and the original on the same source; returns both destinations."""
    emu.write(SRC, bytes(src))
    emu.write(DST, bytes(dst_size))
    emu.call(addr, *args)
    dst = buf([0] * dst_size)
    func(buf(src), dst, *cargs)
    return bytes(dst), emu.read(DST, dst_size)


def test_blit_matches_original(lib, emu):
    rng = random.Random(1)
    for case in range(1000):
        src_bpp = rng.choice([C.IMAGE_4BPP, C.IMAGE_8BPP])
        dst_bpp = C.IMAGE_4BPP if src_bpp == C.IMAGE_4BPP and rng.random() < 0.3 else C.IMAGE_8BPP
        sw, sh = rng.randint(1, 13), rng.randint(1, 9)
        dw = rng.randint(1, 15)
        w, h = rng.randint(1, sw), rng.randint(1, sh)
        sx, sy = rng.randint(0, sw - w), rng.randint(0, sh - h)
        x, y = rng.randint(0, dw - 1), rng.randint(0, 5)
        w = min(w, dw - x)
        stride = (sw + 1) // 2 if src_bpp == C.IMAGE_4BPP else sw
        src = [rng.randrange(256) for _ in range(stride * sh)]
        # The original mirrored 4 to 4 bpp rows with a transparent color have quirks (docs/skull-formats.md).
        transparent = rng.choice([OPAQUE, rng.randrange(16)])
        mirror = rng.randrange(5) if dst_bpp == C.IMAGE_8BPP or transparent == OPAQUE else rng.choice([0, 2, 4])
        colormap = [rng.randrange(256) for _ in range(256)] if src_bpp == C.IMAGE_4BPP or rng.random() < 0.5 else None
        dst_size = 400
        emu.write(RECT, ctypes.string_at(rect(x, y, w, h), 8))
        if colormap:
            emu.write(MAP, bytes(colormap))
        args = [SRC, DST, RECT, sx, sy, sw, dw, transparent, mirror, src_bpp, dst_bpp, MAP if colormap else 0]
        mine, original = run_both(lib, emu, lambda s, d, *a: call_blit(lib, s, d, rect(x, y, w, h), *a), GFX_BLIT,
                                  src, dst_size, args,
                                  [sx, sy, sw, dw, transparent, mirror, src_bpp, dst_bpp,
                                   buf(colormap) if colormap else None])
        assert mine == original, case


def test_scaled_blits_match_original(lib, emu):
    rng = random.Random(2)
    for case in range(200):
        sw, sh, dw, dh = rng.randint(1, 40), rng.randint(1, 30), rng.randint(1, 60), rng.randint(1, 40)
        if rng.random() < 0.5:
            src = [rng.randrange(256) for _ in range(sw * sh)]
            mine, original = run_both(
                lib, emu, lambda s, d, *a: lib.gfx_blit_scaled_8bpp(ctypes.byref(bitmap(s, sw, sh, C.IMAGE_8BPP)),
                                                         ctypes.byref(bitmap(d, dw, dh, C.IMAGE_8BPP))),
                GFX_BLIT_SCALED_8BPP, src, dw * dh, [SRC, DST, sw, sh, dw, dh], [])
        else:
            src = [rng.randrange(256) for _ in range((sw + 1) // 2 * sh)]
            colormap = [rng.randrange(16) for _ in range(16)] if rng.random() < 0.5 else None
            if colormap:
                emu.write(MAP, bytes(colormap))
            mine, original = run_both(
                lib, emu, lambda s, d, *a: lib.gfx_blit_scaled_4bpp(ctypes.byref(bitmap(s, sw, sh, C.IMAGE_4BPP)),
                                                                    ctypes.byref(bitmap(d, dw, dh, C.IMAGE_4BPP)), *a),
                GFX_BLIT_SCALED_4BPP, src, (dw + 1) // 2 * dh, [SRC, DST, sw, sh, dw, dh, MAP if colormap else 0],
                [buf(colormap) if colormap else None])
            if dw & 1:  # the original leaves the pad nibble of a copied row as it was
                mine, original = ([b & ~(0xF if (i + 1) % ((dw + 1) // 2) == 0 else 0) & 0xFF for i, b in enumerate(m)]
                                  for m in (mine, original))
        assert mine == original, case


def test_tiled_blit_matches_original(lib, emu):
    rng = random.Random(3)
    for case in range(200):
        tile_width = rng.randint(1, 20)
        x, y, w, h, dw = rng.randint(0, 4), rng.randint(0, 3), rng.randint(1, 12), rng.randint(1, 6), 20
        src = [rng.randrange(256) for _ in range((tile_width + 1) // 2)]
        colormap = [rng.randrange(256) for _ in range(16)]
        mask_width = w + rng.randint(0, 3)
        mask = [rng.choice([0, 0xFF, 0x0F, 0xF0, rng.randrange(256)]) for _ in range((mask_width + 1) // 2 * 12)]
        use_mask = rng.random() < 0.7
        offset, transparent = rng.randrange(tile_width), rng.choice([OPAQUE, rng.randrange(16)])
        mask_x, mask_y = rng.randint(0, mask_width - w), rng.randint(0, 3)
        emu.write(RECT, ctypes.string_at(rect(x, y, w, h), 8))
        emu.write(MAP, bytes(colormap))
        emu.write(MASK, bytes(mask))
        args = [SRC, DST, MASK if use_mask else 0, RECT, tile_width, offset, mask_x, mask_y, mask_width, dw,
                transparent, MAP]
        mine, original = run_both(
            lib, emu, lambda s, d, *a: lib.gfx_blit_tiled_4to8_masked(
                ctypes.byref(TileFill(s, tile_width, offset, buf(mask) if use_mask else None, mask_x, mask_y,
                                      mask_width, transparent, buf(colormap))), d, dw, rect(x, y, w, h)),
            GFX_BLIT_TILED_4TO8_MASKED, src, dw * 10, args, [])
        assert mine == original, case


PAL_APPLY_DIM_REMAP = 0x1AFC2
G_COLOR_RAMPS, G_COLOR_RAMP_OF_COLOR = 0x7F208, 0x7F20C
IMAGE_CACHE_LOOKUP_OR_ADD, IMAGE_CACHE_ALLOC, IMAGE_CACHE_RELEASE = 0x3F186, 0x3F279, 0x3F3C2
RAMP_DATA, RAMP_RECORDS, TABLE, COUNT, CACHE = (SCRATCH + 0x40000, SCRATCH + 0x50000, SCRATCH + 0x60000,
                                                SCRATCH + 0x60200, SCRATCH + 0x61000)


def test_dim_remap_matches_original(lib, emu):
    from dm2test.markers import ROOT
    graphics = ROOT / "game" / "DATA" / "GRAPHICS.DAT"
    if not graphics.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    gdat = ctypes.c_void_p()
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = u8_p
    assert lib.gdat_load(str(graphics).encode(), ctypes.byref(gdat)) == 0
    assert lib.pal_load_color_ramps(gdat)
    # The original keeps a 9-byte record per ramp: length, brightness pointer, colors pointer.
    size = ctypes.c_uint32()
    data = ctypes.string_at(lib.gdat_item(gdat, lib.gdat_find(gdat, C.CAT_INTERFACE, 0, 7, 2), ctypes.byref(size)), size.value)
    lib.gdat_free(gdat)
    emu.write(RAMP_DATA, data)
    count, lengths = data[0], data[1:1 + data[0]]
    brightness = RAMP_DATA + 1 + count
    colors = brightness + sum(lengths)
    records = b""
    for i, n in enumerate(lengths):
        records += bytes([n]) + (brightness + sum(lengths[:i])).to_bytes(4, "little") \
            + (colors + sum(lengths[:i])).to_bytes(4, "little")
    emu.write(RAMP_RECORDS, records)
    emu.write_u32(G_COLOR_RAMPS, RAMP_RECORDS)
    emu.write_u32(G_COLOR_RAMP_OF_COLOR, colors + sum(lengths))
    emu.stub(IMAGE_CACHE_LOOKUP_OR_ADD, lambda *_: 0)
    emu.stub(IMAGE_CACHE_ALLOC, lambda *_: CACHE)
    emu.stub(IMAGE_CACHE_RELEASE, lambda *_: 0)
    lib.pal_apply_dim_remap.argtypes = [u8_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int16)]
    rng = random.Random(4)
    for case in range(200):
        n = rng.choice([0, -1, 16, 256])
        table = [rng.randrange(256) for _ in range(256)]
        dim, key1, key2 = rng.randrange(80), rng.choice([-1, rng.randrange(256)]), rng.choice([-1, -2, rng.randrange(256)])
        emu.write(TABLE, bytes(table))
        emu.write(COUNT, (n & 0xFFFF).to_bytes(2, "little"))
        emu.call(PAL_APPLY_DIM_REMAP, TABLE, dim, key1, key2, COUNT)
        c_table, c_count = buf(table), ctypes.c_int16(n)
        lib.pal_apply_dim_remap(c_table, dim, key1, key2, ctypes.byref(c_count))
        assert bytes(c_table) == emu.read(TABLE, 256), case
        assert c_count.value & 0xFFFF == int.from_bytes(emu.read(COUNT, 2), "little"), case
