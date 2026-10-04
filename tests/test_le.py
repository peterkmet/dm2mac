import struct

from dm2tools.le import load_image

PAGE = 0x1000


def build_le(code, data, fixup):
    """Two objects of one page each; `fixup` is the fixup record list of page 1."""
    le = 0x40
    objtab = le + 0xB0
    pagemap = objtab + 2 * 24
    fixpages = pagemap + 2 * 4
    fixrecs = fixpages + 3 * 4
    pages = fixrecs + len(fixup)
    header = bytearray(0xB0)
    header[:2] = b"LE"
    for off, value in {
        0x14: 2, 0x18: 1, 0x1C: 2, 0x28: PAGE, 0x2C: len(data), 0x40: objtab - le, 0x44: 2,
        0x48: pagemap - le, 0x68: fixpages - le, 0x6C: fixrecs - le, 0x80: pages,
    }.items():
        struct.pack_into("<I", header, off, value)
    mz = bytearray(le)
    mz[:2] = b"MZ"
    struct.pack_into("<I", mz, 0x3C, le)
    tables = (
        struct.pack("<6I", len(code), 0x10000, 0x2005, 1, 1, 0)
        + struct.pack("<6I", len(data), 0x20000, 0x2003, 2, 1, 0)
        + bytes([0, 0, 1, 0, 0, 0, 2, 0])
        + struct.pack("<3I", 0, len(fixup), len(fixup))
    )
    return bytes(mz + header) + tables + fixup + code.ljust(PAGE, b"\0") + data


def test_synthetic_le(tmp_path):
    code = b"\x90\x90\x90\x90\0\0\0\0\xc3"
    data = b"DATA1234"
    # Source type 0x07 at offset 4, target object 2, offset 4.
    fixup = bytes([0x07, 0x00]) + struct.pack("<hBH", 4, 2, 4)
    path = tmp_path / "t.exe"
    path.write_bytes(build_le(code, data, fixup))

    image = load_image(path)

    assert image.base == 0x10000
    assert [(o.base, o.size) for o in image.objects] == [(0x10000, 9), (0x20000, 8)]
    assert image.entry == 0x10002
    assert image.data[:4] == b"\x90" * 4
    assert struct.unpack_from("<I", image.data, 4)[0] == 0x20004
    assert image.data[0x10000:0x10008] == data


def test_skull(game_dir):
    image = load_image(game_dir / "SKULL.EXE")
    code, data = image.objects
    assert code.base <= image.entry < code.base + code.size
    at = image.base + image.data.index(b"SKSAVE")
    assert data.base <= at < data.base + data.size
