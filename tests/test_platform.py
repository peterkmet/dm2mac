import ctypes


def test_vga_palette_conversion(lib):
    lib.video_vga_to_8bit.restype = ctypes.c_uint8
    assert [lib.video_vga_to_8bit(v) for v in (0, 32, 63)] == [0, 130, 255]
