"""The animated icon of an item (src/engine/item.c): a damaged icon animation attribute of GRAPHICS.DAT."""

import ctypes
import struct

import pytest

from dm2test import constants as C
from dm2test.lib import load_lib
from dm2test.save import GRAPHICS

KEYS = 0x2BF6  # the key table: a big-endian row count at +2, the rows at +20
KEY_ROWS, ROW_SIZE, ROW_VALUE = 20, 8, 6
CAT_WEAPON, TYPE_VALUE, ATTR_ICON_ANIMATION = 0x10, 0x0B, 0x06
GFX_ITEM_ICON = 0x18
NO_STEP = 0x0402  # mode 4 (by charges and time), step 0, 2 frames


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.dungeon_unused_thing.restype = ctypes.c_uint16
    lib.gdat_load.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.gdat_select.argtypes = [ctypes.c_void_p]
    lib.gdat_free.argtypes = [ctypes.c_void_p]
    lib.item_icon_index.argtypes = [ctypes.c_uint16, ctypes.c_int16, ctypes.c_bool]
    return lib


def with_icon_animation(data, index, value):
    """GRAPHICS.DAT with the icon animation attribute of weapon index set to value."""
    data = bytearray(data)
    for row in range(struct.unpack_from(">H", data, KEYS + 2)[0]):
        at = KEYS + KEY_ROWS + row * ROW_SIZE
        if tuple(data[at:at + 4]) == (CAT_WEAPON, index, TYPE_VALUE, ATTR_ICON_ANIMATION):
            struct.pack_into(">H", data, at + ROW_VALUE, value)
            return bytes(data)
    raise AssertionError("no icon animation key of the weapon")


def test_an_icon_step_of_0_gives_the_first_icon(lib, gdat, start, tmp_path):
    # regression: an icon animation by charges and time with a step of 0 divided by 0
    path = tmp_path / "SKSAVE0.DAT"
    path.write_bytes(start)
    assert lib.save_load(str(path).encode(), gdat) == 0
    damaged = tmp_path / "GRAPHICS.DAT"
    damaged.write_bytes(with_icon_animation(GRAPHICS.read_bytes(), 0, NO_STEP))
    handle = ctypes.c_void_p()
    assert lib.gdat_load(str(damaged).encode(), ctypes.byref(handle)) == 0
    lib.gdat_select(handle)
    try:
        weapon = lib.dungeon_unused_thing(C.THING_WEAPON)
        assert weapon != C.THING_NONE
        lib.item_set_gdat_index(weapon, 0)
        lib.item_add_charges(weapon, 1)
        assert lib.item_icon_index(weapon, -1, True) == GFX_ITEM_ICON
    finally:
        lib.gdat_select(gdat)
        lib.gdat_free(handle)
