"""GRAPHICS.DAT loaded in the C port."""

import ctypes

import pytest

from dm2test.gamefiles import require
from dm2test.markers import ROOT
from dm2test.structs import ImageInfo


GDAT = ROOT / "game" / "DATA" / "GRAPHICS.DAT"
OK, ERR_IO, ERR_FORMAT, ERR_NOT_IMAGE, ERR_DATA, ERR_SPACE = 0, -1, -2, -4, -5, -6

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
    require(GDAT)
    handle = ctypes.c_void_p()
    assert lib.gdat_load(str(GDAT).encode(), ctypes.byref(handle)) == OK
    yield handle
    lib.gdat_free(handle)
