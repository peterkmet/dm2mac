"""The window icon: the mummy graphic of GRAPHICS.DAT as ARGB pixels."""
import ctypes

import pytest

from dm2test.gdat import OK, gd  # noqa: F401
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

ICON_W, ICON_H = 77, 81
ITEM_KEY = (0x0F, 0x26, 1, 9)
TRANSPARENT = 47
FACE = (ICON_W // 2, ICON_H // 3)
MIN_OPAQUE = 1000


class IconSize(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int)]


@pytest.fixture(scope="module")
def icon(lib, gd):
    lib.icon_build.restype = IconSize
    lib.icon_build.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    argb = (ctypes.c_uint32 * (ICON_W * ICON_H))()
    size = lib.icon_build(gd, argb, len(argb))
    return size.width, size.height, argb


def test_size(icon):
    assert icon[:2] == (ICON_W, ICON_H)


def test_corners_transparent(icon):
    _, _, argb = icon
    for x, y in ((0, 0), (ICON_W - 1, 0), (0, ICON_H - 1), (ICON_W - 1, ICON_H - 1)):
        assert argb[y * ICON_W + x] == 0


def test_face_pixel_has_palette_color(lib, gd, icon):
    item = lib.gdat_find(gd, *ITEM_KEY)
    px = (ctypes.c_uint8 * (ICON_W * ICON_H))()
    assert lib.gdat_decode_image(gd, item, px, len(px)) == OK
    x, y = FACE
    index = px[y * ICON_W + x]
    assert index != TRANSPARENT
    rgb = (ctypes.c_uint8 * 3)()
    assert lib.gdat_palette_color(gd, index, rgb) == OK
    vga = [(v << 2) | (v >> 4) for v in rgb]
    assert icon[2][y * ICON_W + x] == 0xFF000000 | vga[0] << 16 | vga[1] << 8 | vga[2]


def test_most_pixels_opaque(icon):
    assert sum(1 for p in icon[2] if p >> 24 == 0xFF) > MIN_OPAQUE


def test_too_small_buffer(lib, gd, icon):
    argb = (ctypes.c_uint32 * 10)()
    size = lib.icon_build(gd, argb, len(argb))
    assert (size.width, size.height) == (0, 0)
