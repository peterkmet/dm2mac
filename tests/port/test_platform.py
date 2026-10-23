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


def test_layout_fractional(lib):
    lib.video_layout.argtypes = [ctypes.c_int, ctypes.c_int] + [ctypes.POINTER(ctypes.c_float)] * 3
    s, ox, oy = (ctypes.c_float() for _ in range(3))
    lib.video_layout(1000, 700, s, ox, oy)
    assert (s.value, ox.value, oy.value) == pytest.approx((3.125, 0, 37.5))
    lib.video_layout(0, 0, s, ox, oy)  # minimized window
    assert s.value == 1


def test_map_mouse_fractional(lib):
    lib.video_map_mouse.restype = VideoPoint
    # 1000 x 700 output: scale 3.125, image at (0, 37.5)
    for x, y, expected in [(0, 37, (0, 0)), (3, 41, (0, 1)), (4, 44, (1, 2)), (999, 699, (319, 199)), (500, 0, (160, 0))]:
        at = lib.video_map_mouse(1000, 700, x, y)
        assert (at.x, at.y) == expected
