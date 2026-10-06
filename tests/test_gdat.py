import ctypes
import struct

import pytest

from conftest import ROOT
from dm2tools.emu import Emu
from dm2tools.le import load_image

GDAT = ROOT / "game" / "DATA" / "GRAPHICS.DAT"
IMAGE_DECODE_4BPP = 0x12E4F
IMAGE_DECODE_4BPP_DELTA = 0x13258
IMAGE_DECODE_8BPP = 0x3F71C
SRC, BASE, DST = 0x01000000, 0x01100000, 0x01200000  # extra emulator buffers: item, delta base, output
MARGIN = 64  # bytes after the image that a decoder must not write
POISON = 0xA5
OK, ERR_IO, ERR_FORMAT, ERR_NOT_IMAGE, ERR_DATA, ERR_SPACE = 0, -1, -2, -4, -5, -6
FORMAT_4BPP, FORMAT_8BPP, FORMAT_RAW = 0x00, 0x1F, 0x20

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
    lib.gdat_image_size.argtypes = [ctypes.c_void_p, ctypes.c_int, c_int_p, c_int_p, c_int_p]
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


@pytest.fixture(scope="module")
def emu():
    if not GDAT.exists():
        pytest.skip("game/ missing")
    emu = Emu(load_image(ROOT / "game" / "SKULL.EXE"), max_insns=100_000_000)
    emu.uc.mem_map(SRC, 3 * 0x100000)
    return emu


def item_bytes(lib, gd, item):
    size = ctypes.c_uint32()
    return ctypes.string_at(lib.gdat_item(gd, item, ctypes.byref(size)), size.value)


def image_items(lib, gd):
    """Items of image keys (type 1) with an image format, from the key table parsed here (English rows)."""
    keys = item_bytes(lib, gd, 0)
    rows = struct.unpack_from(">H", keys, 2)[0]
    items = set()
    for i in range(rows):
        _, _, key_type, _, language, _, value = struct.unpack_from(">6BH", keys, 20 + 8 * i)
        if key_type == 1 and language & 0xF0 in (0, 0x10):
            items.add(value & 0x7FFF)
    return sorted(items)


def image_format(raw):
    """8 bpp, raw, or 4 bpp: the format bits of a 4 bpp image can hold its y offset."""
    fmt = raw[3] >> 2
    return fmt if fmt in (FORMAT_8BPP, FORMAT_RAW) else FORMAT_4BPP


def poisoned(size):
    return (ctypes.c_uint8 * size)(*([POISON] * size))


def image_size(lib, gd, item):
    w, h, bpp = ctypes.c_int(), ctypes.c_int(), ctypes.c_int()
    return lib.gdat_image_size(gd, item, ctypes.byref(w), ctypes.byref(h), ctypes.byref(bpp)), w.value, h.value, bpp.value


def original_decode(emu, raw, size, base=None):
    emu.write(SRC, raw)
    emu.write(DST, bytes([POISON]) * (size + MARGIN))
    fmt = image_format(raw)
    if base is not None:
        emu.write(BASE, base)
        emu.call(IMAGE_DECODE_4BPP_DELTA, BASE, SRC, DST)
    elif fmt == FORMAT_8BPP:
        emu.call(IMAGE_DECODE_8BPP, SRC, DST, len(raw))
    else:
        emu.call(IMAGE_DECODE_4BPP, SRC, DST)
    return emu.read(DST, size + MARGIN)


def test_header_and_keys(lib, gd):
    data = GDAT.read_bytes()
    count = struct.unpack_from("<H", data, 2)[0]
    assert lib.gdat_item_count(gd) == count == 5624
    sizes = [struct.unpack_from("<I", data, 4)[0], *struct.unpack_from(f"<{count - 1}H", data, 8)]
    offset = 8 + 2 * (count - 1)
    for item, size in enumerate(sizes):
        if item % 500 == 0 or item == count - 1:
            assert item_bytes(lib, gd, item) == data[offset:offset + size]
        offset += size
    assert lib.gdat_find(gd, 0, 0, 0x0B, 0) == 0x7B  # value key: the GDAT flag word
    assert lib.gdat_find(gd, 1, 0, 9, 0xFE) == 206  # item key: the game palette
    assert lib.gdat_find(gd, 99, 99, 99, 99) == -1


def test_palette(lib, gd):
    raw = item_bytes(lib, gd, 206)
    rgb = (ctypes.c_uint8 * 3)()
    for color in range(256):
        assert lib.gdat_palette_color(gd, color, rgb) == OK
        assert bytes(rgb) == bytes(v >> 2 for v in raw[4 * color + 1:4 * color + 4])
    assert lib.gdat_palette_color(gd, 15, rgb) == OK and bytes(rgb) == bytes((236 >> 2, 183 >> 2, 72 >> 2))
    assert lib.gdat_palette_color(gd, 256, rgb) != OK


def test_colormap(lib, gd):
    items = image_items(lib, gd)
    four = next(i for i in items if item_bytes(lib, gd, i)[3] >> 2 == FORMAT_4BPP)
    eight = next(i for i in items if item_bytes(lib, gd, i)[3] >> 2 == FORMAT_8BPP)
    assert ctypes.string_at(lib.gdat_image_colormap(gd, four), 16) == item_bytes(lib, gd, four)[-16:]
    assert not lib.gdat_image_colormap(gd, eight)


def test_every_image_matches_original(lib, gd, emu):
    counts = {FORMAT_4BPP: 0, FORMAT_8BPP: 0, FORMAT_RAW: 0}
    for item in image_items(lib, gd):
        raw = item_bytes(lib, gd, item)
        size, w, h, bpp = image_size(lib, gd, item)
        assert size >= 0, item
        dst = poisoned(size + MARGIN)
        assert lib.gdat_decode_image(gd, item, dst, size) == OK, item
        fmt = image_format(raw)
        counts[fmt] += 1
        if fmt == FORMAT_RAW:
            assert bytes(dst[:size]) == raw[10:10 + size], item
        else:
            assert bytes(dst) == original_decode(emu, raw, size), item
    assert counts == {FORMAT_4BPP: 827, FORMAT_8BPP: 3166, FORMAT_RAW: 6}  # 24 of the 4 bpp images have a y offset


def test_delta_decoder_matches_original(lib, gd, emu):
    # No shipped item is a delta image; plain 4 bpp streams still exercise every delta command.
    matched = 0
    for item in image_items(lib, gd)[:300]:
        raw = item_bytes(lib, gd, item)
        if raw[3] >> 2 != FORMAT_4BPP:
            continue
        size = image_size(lib, gd, item)[0]
        base = bytes((i * 7 + 3) & 0xFF for i in range(size))
        dst = poisoned(size + MARGIN)
        src = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw)
        if lib.image_decode_4bpp(src, len(raw), (ctypes.c_uint8 * size).from_buffer_copy(base), dst, size) != OK:
            continue
        assert bytes(dst) == original_decode(emu, raw, size, base), item
        matched += 1
    assert matched >= 20


def test_no_item_crashes(lib, gd):
    # Sounds, texts, and tables go through the image decoder too; they decode or give an error.
    for item in range(1, lib.gdat_item_count(gd)):
        size = image_size(lib, gd, item)[0]
        if size >= 0:
            assert lib.gdat_decode_image(gd, item, poisoned(size), size) in (OK, ERR_DATA)
        else:
            assert size == ERR_NOT_IMAGE


@pytest.mark.parametrize("cut", [0, 1, 4, 8, 50])
def test_truncated_streams(lib, gd, cut):
    items = image_items(lib, gd)
    for fmt, decode in ((FORMAT_4BPP, lib.image_decode_4bpp), (FORMAT_8BPP, lib.image_decode_8bpp)):
        item = next(i for i in items if item_bytes(lib, gd, i)[3] >> 2 == fmt)
        raw = item_bytes(lib, gd, item)[:cut + (0 if cut < 8 else 100)]
        size = image_size(lib, gd, item)[0]
        src = (ctypes.c_uint8 * max(len(raw), 1)).from_buffer_copy(raw.ljust(1, b"\0"))
        args = (src, len(raw), None, poisoned(size), size) if fmt == FORMAT_4BPP else (src, len(raw), poisoned(size), size)
        assert decode(*args) == ERR_DATA


def test_small_destination(lib, gd):
    item = image_items(lib, gd)[0]
    size = image_size(lib, gd, item)[0]
    assert lib.gdat_decode_image(gd, item, poisoned(size), size - 1) == ERR_SPACE


def test_bad_files(lib, gd, tmp_path):
    data = GDAT.read_bytes()
    keys = 8 + 2 * (5624 - 1)
    cases = {
        "empty": b"",
        "truncated": data[:100_000],
        "one_byte_short": data[:-1],
        "longer": data + b"\0",
        "bad_signature": b"\0\0" + data[2:],
        "bad_key_table": data[:keys] + b"\0\0" + data[keys + 2:],
        "huge_count": data[:2] + b"\xff\xff" + data[4:100],
    }
    for name, content in cases.items():
        path = tmp_path / name
        path.write_bytes(content)
        handle = ctypes.c_void_p(1)
        assert lib.gdat_load(str(path).encode(), ctypes.byref(handle)) == ERR_FORMAT, name
        assert not handle.value
    handle = ctypes.c_void_p()
    assert lib.gdat_load(str(tmp_path / "missing").encode(), ctypes.byref(handle)) == ERR_IO
