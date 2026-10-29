"""GRAPHICS.DAT loaded in the C port."""

import ctypes

import pytest

from dm2test.markers import ROOT


GDAT = ROOT / "game" / "DATA" / "GRAPHICS.DAT"
OK, ERR_IO, ERR_FORMAT, ERR_NOT_IMAGE, ERR_DATA, ERR_SPACE = 0, -1, -2, -4, -5, -6

class GdatKey(ctypes.Structure):
    """The GdatKey of gdat.h: category, index, entry."""

    _fields_ = [("category", ctypes.c_int), ("index", ctypes.c_int), ("entry", ctypes.c_int)]


class ImageInfo(ctypes.Structure):
    """The ImageInfo of gdat.h: width, height, bits per pixel, decoded byte size."""

    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int), ("bpp", ctypes.c_int), ("size", ctypes.c_size_t)]


class GdatSizes(ctypes.Structure):
    """The start of struct gdat of gdat.c: the file, the item count, the item offsets and sizes."""

    _fields_ = [("file", ctypes.c_void_p), ("count", ctypes.c_int), ("offset", ctypes.POINTER(ctypes.c_uint32)),
                ("size", ctypes.POINTER(ctypes.c_uint32))]


def item_sizes(gdat):
    """The byte sizes of the items of a loaded GRAPHICS.DAT, writable: a test shortens an item."""
    return GdatSizes.from_address(gdat.value).size


def image_info(lib, gdat, item):
    """The result of gdat_image_info and the ImageInfo it fills."""
    info = ImageInfo()
    return lib.gdat_image_info(gdat, item, ctypes.byref(info)), info


c_int_p = ctypes.POINTER(ctypes.c_int)
u8_p = ctypes.POINTER(ctypes.c_uint8)


@pytest.fixture(scope="module")
def gd(lib):
    if not GDAT.exists():
        pytest.skip("game/DATA/GRAPHICS.DAT missing")
    lib.gdat_load.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.gdat_free.argtypes = [ctypes.c_void_p]
    lib.gdat_item_count.argtypes = [ctypes.c_void_p]
    lib.gdat_item.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
    lib.gdat_item.restype = u8_p
    lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
    lib.gdat_image_info.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ImageInfo)]
    lib.gdat_decode_image.argtypes = [ctypes.c_void_p, ctypes.c_int, u8_p, ctypes.c_size_t]
    lib.gdat_image_colormap.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.gdat_image_colormap.restype = u8_p
    lib.gdat_palette_color.argtypes = [ctypes.c_void_p, ctypes.c_int, u8_p]
    lib.image_decode_4bpp.argtypes = [u8_p, ctypes.c_size_t, u8_p, u8_p, ctypes.c_size_t]
    lib.image_decode_8bpp.argtypes = [u8_p, ctypes.c_size_t, u8_p, ctypes.c_size_t]
    handle = ctypes.c_void_p()
    assert lib.gdat_load(str(GDAT).encode(), ctypes.byref(handle)) == OK
    yield handle
    lib.gdat_free(handle)
