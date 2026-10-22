import ctypes
import pytest
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()


def test_vga_palette_conversion(lib):
    lib.video_vga_to_8bit.restype = ctypes.c_uint8
    assert [lib.video_vga_to_8bit(v) for v in (0, 32, 63)] == [0, 130, 255]


class VideoPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int)]


def test_map_mouse(lib):
    lib.video_map_mouse.restype = VideoPoint
    # 1000 x 700 output: scale 3, image at (20, 50)
    for x, y, expected in [(20, 50, (0, 0)), (23, 56, (1, 2)), (979, 649, (319, 199)), (0, 0, (0, 0)), (999, 699, (319, 199))]:
        at = lib.video_map_mouse(1000, 700, x, y)
        assert (at.x, at.y) == expected
