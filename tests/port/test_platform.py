import ctypes
import pytest


@pytest.fixture(scope="module", autouse=True)
def lib_prototypes(lib):
    lib.video_layout.argtypes = [ctypes.c_int, ctypes.c_int]  # VideoLayout is a mirror of this module
    lib.video_layout.restype = VideoLayout  # VideoLayout is a mirror of this module
    lib.video_map_mouse.restype = VideoPoint  # VideoPoint is a mirror of this module


def test_vga_palette_conversion(lib):
    assert [lib.video_vga_to_8bit(v) for v in (0, 32, 63)] == [0, 130, 255]


class VideoPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int)]


class VideoLayout(ctypes.Structure):
    _fields_ = [("scale", ctypes.c_float), ("x", ctypes.c_float), ("y", ctypes.c_float)]


def test_layout_fractional(lib):
    layout = lib.video_layout(1000, 700)
    assert (layout.scale, layout.x, layout.y) == pytest.approx((3.125, 0, 37.5))
    assert lib.video_layout(0, 0).scale == 1  # minimized window


def test_map_mouse_fractional(lib):
    # 1000 x 700 output: scale 3.125, image at (0, 37.5)
    for x, y, expected in [(0, 37, (0, 0)), (3, 41, (0, 1)), (4, 44, (1, 2)), (999, 699, (319, 199)), (500, 0, (160, 0))]:
        at = lib.video_map_mouse(1000, 700, x, y)
        assert (at.x, at.y) == expected
