"""The PCX decoder (src/data/pcx.c): the splash picture INTRPLAY.PCX of the DOS launcher."""

import ctypes

import pytest
from dm2test import constants as C
from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    return load_lib()

PCX_OK, PCX_ERR_DATA = C.PCX_OK, C.PCX_ERR_DATA
WIDTH, HEIGHT, COLORS, RGB = C.SCREEN_W, C.SCREEN_H, C.PCX_PALETTE_COLORS, C.PCX_RGB
HEADER = 128
PALETTE_SIZE = 1 + COLORS * RGB  # the marker byte and the colors


def decode(lib, data, max_w=WIDTH, max_h=HEIGHT):
    lib.pcx_decode.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                               ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    pixels = (ctypes.c_uint8 * (max_w * max_h))()
    palette = (ctypes.c_uint8 * (COLORS * RGB))()
    w, h = ctypes.c_int(), ctypes.c_int()
    result = lib.pcx_decode(data, len(data), pixels, max_w, max_h, ctypes.byref(w), ctypes.byref(h), palette)
    return result, w.value, h.value, bytes(pixels), bytes(palette)


def reference_decode(data):
    """PCX RLE in Python: a byte with the two high bits set repeats the next byte; rows have the bytes per line of the
    header, and only the first width bytes of a row are pixels."""
    word = lambda at: int.from_bytes(data[at:at + 2], "little")
    w, h, stride = word(8) - word(4) + 1, word(10) - word(6) + 1, word(66)
    out, p = bytearray(), HEADER
    while len(out) < stride * h:
        b = data[p]
        p += 1
        if b >= 0xC0:
            out += bytes([data[p]]) * (b & 0x3F)
            p += 1
        else:
            out.append(b)
    return b"".join(bytes(out[y * stride:y * stride + w]) for y in range(h))


@pytest.fixture
def splash(game_dir):
    return (game_dir / "INTRPLAY.PCX").read_bytes()


def test_splash_decodes_as_the_reference(lib, splash):
    result, w, h, pixels, palette = decode(lib, splash)
    assert (result, w, h) == (PCX_OK, WIDTH, HEIGHT)
    assert pixels == reference_decode(splash)
    assert palette == splash[-COLORS * RGB:]


@pytest.mark.parametrize("cut", [0, HEADER - 1, HEADER, 5000, -PALETTE_SIZE])
def test_short_data_is_an_error(lib, splash, cut):
    assert decode(lib, splash[:cut])[0] == PCX_ERR_DATA


@pytest.mark.parametrize("offset, value", [(0, 0), (1, 3), (3, 4), (65, 3)])
def test_other_pcx_kinds_are_an_error(lib, splash, offset, value):
    # manufacturer, version, bits per pixel, planes
    data = bytearray(splash)
    data[offset] = value
    assert decode(lib, bytes(data))[0] == PCX_ERR_DATA


def test_picture_larger_than_the_buffer_is_an_error(lib, splash):
    assert decode(lib, splash, WIDTH - 1, HEIGHT)[0] == PCX_ERR_DATA
