"""The ISO 9660 reader (src/data/iso.c): finds and reads the files on a CD image."""

import ctypes
import random

import pytest
from dm2test import constants as C
from dm2test.iso import SECTOR, build_iso
from dm2test.lib import load_lib

FILES = {"FTL": b"ftl" * 100, "DATA/GRAPHICS.DAT": bytes(range(256)) * 20}
PRIMARY = 16 * SECTOR  # the primary volume descriptor
ROOT_LENGTH = PRIMARY + 156 + 10  # the data length of the root directory record


class IsoFile(ctypes.Structure):
    """The IsoFile of iso.h: the first sector and the size."""

    _fields_ = [("sector", ctypes.c_uint32), ("size", ctypes.c_uint32)]


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.iso_open.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
    lib.iso_close.argtypes = [ctypes.c_void_p]
    lib.iso_find.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(IsoFile)]
    lib.iso_read.argtypes = [ctypes.c_void_p, ctypes.POINTER(IsoFile), ctypes.c_char_p, ctypes.c_uint32]
    return lib


@pytest.fixture
def open_image(lib, tmp_path):
    opened = []

    def open_image(image):
        path = tmp_path / "game.iso"
        path.write_bytes(image)
        iso = ctypes.c_void_p()
        result = lib.iso_open(str(path).encode(), ctypes.byref(iso))
        assert (result == C.ISO_OPEN_OK) == bool(iso.value)
        opened.append(iso)
        return result, iso

    yield open_image
    for iso in opened:
        lib.iso_close(iso)


def find(lib, iso, path):
    file = IsoFile()
    return lib.iso_find(iso, path.encode(), ctypes.byref(file)), file


def read(lib, iso, file, size):
    data = ctypes.create_string_buffer(max(size, 1))
    return lib.iso_read(iso, ctypes.byref(file), data, size), data.raw[:size]


@pytest.mark.parametrize("raw", [False, True])
def test_read_files(lib, open_image, raw):
    result, iso = open_image(build_iso(FILES, raw))
    assert result == C.ISO_OPEN_OK
    for path, content in FILES.items():
        found, file = find(lib, iso, path)
        assert found and file.size == len(content)
        assert read(lib, iso, file, len(content)) == (True, content)


def test_find_without_case_and_version(lib, open_image):
    _, iso = open_image(build_iso(FILES, raw=False))
    found, file = find(lib, iso, "data/graphics.dat")
    assert found and file.size == len(FILES["DATA/GRAPHICS.DAT"])


def test_missing_file(lib, open_image):
    _, iso = open_image(build_iso(FILES, raw=False))
    assert not find(lib, iso, "DATA/DUNGEON.DAT")[0]


def test_wrong_size_fails(lib, open_image):
    _, iso = open_image(build_iso(FILES, raw=False))
    _, file = find(lib, iso, "FTL")
    assert not read(lib, iso, file, file.size - 1)[0]
    assert not read(lib, iso, file, file.size + 1)[0]


def test_mode2_image(lib, open_image):
    assert open_image(build_iso(FILES, raw=True, mode=2))[0] == C.ISO_OPEN_MODE2


def without_id(image):
    return image[:PRIMARY + 1] + b"XXXXX" + image[PRIMARY + 6:]


def long_root(image):
    return image[:ROOT_LENGTH] + (0x7FFFFFFF).to_bytes(4, "little") + image[ROOT_LENGTH + 4:]


IMAGES = {
    "random bytes": lambda: random.Random(1).randbytes(100),
    "no CD001": lambda: without_id(build_iso(FILES, raw=False)),
    "cut after sector 16": lambda: build_iso(FILES, raw=False)[:PRIMARY + SECTOR],
    "file extent past the end": lambda: build_iso({"FTL": bytes(3 * SECTOR)}, raw=False)[:-SECTOR],
    "root length past the end": lambda: long_root(build_iso(FILES, raw=False)),
}


@pytest.mark.parametrize("image", IMAGES.values(), ids=IMAGES.keys())
def test_not_an_image(lib, open_image, image):
    result, iso = open_image(image())
    if result != C.ISO_OPEN_OK:
        assert result == C.ISO_OPEN_DAMAGED
        return
    found, file = find(lib, iso, "FTL")
    assert not found or not read(lib, iso, file, file.size)[0]
