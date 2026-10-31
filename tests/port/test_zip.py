"""The ZIP reader (src/data/zip.c): the central directory, the names without case, stored and deflated entries."""

import ctypes
import io
import struct
import zipfile

import pytest

from dm2test import constants as C

MARKER = b"DATA/GRAPHICS.DAT"
TEXT = (b"Dungeon Master II: the legend of Skullkeep. " * 2400)[:100_000]
END_SIZE = 22  # the end of central directory record without its comment


class ZipEntry(ctypes.Structure):
    _fields_ = [("local_offset", ctypes.c_uint32), ("compressed_size", ctypes.c_uint32), ("size", ctypes.c_uint32),
                ("method", ctypes.c_uint16)]


def write_zip(target, entries):
    """Writes (name, data, compression) entries with zipfile into a path or a stream."""
    with zipfile.ZipFile(target, "w") as archive:
        for name, data, compression in entries:
            archive.writestr(zipfile.ZipInfo(name), data, compress_type=compression)


def archive_file(tmp_path, entries, name="test.zip"):
    path = tmp_path / name
    write_zip(path, entries)
    return path


def find(lib, zip_handle, name):
    entry = ZipEntry()
    return entry if lib.zip_find(zip_handle, name, ctypes.byref(entry)) else None


def read(lib, zip_handle, entry, size):
    data = ctypes.create_string_buffer(max(size, 1))
    return data.raw[:size] if lib.zip_read(zip_handle, ctypes.byref(entry), data, size) else None


def read_entry(lib, path, name):
    """The bytes of the entry, or None when the archive does not open, the entry is missing or does not read."""
    zip_handle = lib.zip_open(str(path).encode())
    if not zip_handle:
        return None
    try:
        entry = find(lib, zip_handle, name)
        return read(lib, zip_handle, entry, entry.size) if entry else None
    finally:
        lib.zip_close(zip_handle)


def find_root(lib, path):
    zip_handle = lib.zip_open(str(path).encode())
    assert zip_handle
    root = ctypes.create_string_buffer(256)
    try:
        return root.value if lib.zip_find_root(zip_handle, MARKER, root, len(root)) else None
    finally:
        lib.zip_close(zip_handle)


ENTRIES = [("A", b"stored bytes", zipfile.ZIP_STORED), ("B/C.DAT", TEXT, zipfile.ZIP_DEFLATED)]


def test_stored_and_deflated(lib, tmp_path):
    path = archive_file(tmp_path, ENTRIES)
    zip_handle = lib.zip_open(str(path).encode())
    assert zip_handle
    try:
        a, c = find(lib, zip_handle, b"A"), find(lib, zip_handle, b"B/C.DAT")
        assert a.method == C.ZIP_STORED and c.method == C.ZIP_DEFLATED and c.compressed_size < c.size
        assert read(lib, zip_handle, a, a.size) == b"stored bytes"
        assert read(lib, zip_handle, c, c.size) == TEXT
        assert find(lib, zip_handle, b"B/C") is None
    finally:
        lib.zip_close(zip_handle)


def test_find_without_case(lib, tmp_path):
    path = archive_file(tmp_path, [("dumast2/data/graphics.dat", b"gdat", zipfile.ZIP_DEFLATED)])
    assert read_entry(lib, path, b"DUMAST2/DATA/GRAPHICS.DAT") == b"gdat"


def test_find_root(lib, tmp_path):
    def entries(*names):
        return [(name, b"x", zipfile.ZIP_STORED) for name in names]

    assert find_root(lib, archive_file(tmp_path, entries("x/cd/DM2.img", "x/DATA/GRAPHICS.DAT", "x/FTL"), "a.zip")) == b"x/"
    assert find_root(lib, archive_file(tmp_path, entries("DATA/GRAPHICS.DAT"), "b.zip")) == b""
    assert find_root(lib, archive_file(tmp_path, entries("b/DATA/GRAPHICS.DAT", "a/DATA/GRAPHICS.DAT"), "c.zip")) == b"a/"
    assert find_root(lib, archive_file(tmp_path, entries("x/DATA/OTHER.DAT", "GRAPHICS.DAT"), "d.zip")) is None


class WriteOnly:
    """A stream without seek and tell: zipfile writes a data descriptor after each entry."""

    def __init__(self):
        self.buffer = io.BytesIO()

    def write(self, data):
        return self.buffer.write(data)

    def flush(self):
        pass


def test_entry_with_data_descriptor(lib, tmp_path):
    stream = WriteOnly()
    write_zip(stream, ENTRIES)
    path = tmp_path / "stream.zip"
    path.write_bytes(stream.buffer.getvalue())
    flags = struct.unpack_from("<H", path.read_bytes(), 6)[0]
    assert flags & 0x08  # the data descriptor flag of the local header
    assert read_entry(lib, path, b"A") == b"stored bytes"
    assert read_entry(lib, path, b"B/C.DAT") == TEXT


def test_wrong_size_fails(lib, tmp_path):
    path = archive_file(tmp_path, ENTRIES)
    zip_handle = lib.zip_open(str(path).encode())
    try:
        for name in (b"A", b"B/C.DAT"):
            entry = find(lib, zip_handle, name)
            assert read(lib, zip_handle, entry, entry.size - 1) is None
            assert read(lib, zip_handle, entry, entry.size + 1) is None
    finally:
        lib.zip_close(zip_handle)


def central_offset(data):
    return struct.unpack_from("<I", data, len(data) - END_SIZE + 16)[0]


def short_file(data):
    return data[:10]


def cut(data):
    return data[:len(data) // 2]


def directory_past_end(data):
    out = bytearray(data)
    struct.pack_into("<I", out, len(out) - END_SIZE + 16, len(out) + 100)
    return bytes(out)


def flipped_deflate(data):
    out = bytearray(data)
    start = 30 + len("B/C.DAT")  # the deflate data of the only entry, after its local header and name
    for at in range(start + 1, central_offset(data) - 4):
        out[at] ^= 0xA5
    return bytes(out)


def encrypted(data):
    out = bytearray(data)
    out[6] |= 0x01  # the flags of the local header
    out[central_offset(data) + 8] |= 0x01  # the flags of the central entry
    return bytes(out)


def method_14(data):
    out = bytearray(data)
    struct.pack_into("<H", out, 8, 14)  # the method of the local header
    struct.pack_into("<H", out, central_offset(data) + 10, 14)  # the method of the central entry
    return bytes(out)


@pytest.mark.parametrize("damage, entry", [(short_file, ENTRIES[0]), (cut, ENTRIES[1]),
                                           (directory_past_end, ENTRIES[0]), (flipped_deflate, ENTRIES[1]),
                                           (encrypted, ENTRIES[0]), (method_14, ENTRIES[0])],
                         ids=lambda value: getattr(value, "__name__", ""))
def test_damaged_archive(lib, tmp_path, damage, entry):
    path = archive_file(tmp_path, [entry])
    path.write_bytes(damage(path.read_bytes()))
    assert read_entry(lib, path, entry[0].encode()) is None
