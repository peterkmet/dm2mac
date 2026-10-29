"""The CRC-32 of PNG and ZIP (src/data/crc32.c) against zlib."""

import ctypes
import zlib

import pytest

from dm2test.lib import load_lib


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.crc32_update.argtypes = [ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t]
    lib.crc32_update.restype = ctypes.c_uint32
    return lib


@pytest.mark.parametrize("data", [b"", b"a", b"123456789", bytes(range(256)) * 40])
def test_crc_matches_zlib(lib, data):
    assert lib.crc32_update(0, data, len(data)) == zlib.crc32(data)


def test_crc_in_parts(lib):
    data = bytes(range(256)) * 3
    part = lib.crc32_update(0, data[:100], 100)
    assert lib.crc32_update(part, data[100:], len(data) - 100) == zlib.crc32(data)
