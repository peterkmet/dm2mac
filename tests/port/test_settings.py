"""The settings file SETTINGS.TXT (src/data/settings.c): the parser, the loader, and the writer."""

import ctypes

import pytest

from dm2test import constants as C

NONE, SCALE2X, SCALE3X = C.SETTINGS_FILTER_NONE, C.SETTINGS_FILTER_SCALE2X, C.SETTINGS_FILTER_SCALE3X


class Settings(ctypes.Structure):
    _fields_ = [("filter", ctypes.c_int)]


@pytest.fixture(scope="module", autouse=True)
def lib_prototypes(lib):
    lib.settings_parse.argtypes = [ctypes.c_char_p, ctypes.c_size_t]  # Settings is a mirror of this module
    lib.settings_parse.restype = Settings  # Settings is a mirror of this module
    lib.settings_load.argtypes = [ctypes.c_char_p]  # Settings is a mirror of this module
    lib.settings_load.restype = Settings  # Settings is a mirror of this module


def parse(lib, text):
    return lib.settings_parse(text, len(text)).filter


@pytest.mark.parametrize("text, filter", [
    (b"filter=scale2x\n", SCALE2X),
    (b"filter=scale3x", SCALE3X),  # no final newline
    (b"filter=scale2x\r\nfilter=scale3x\r\n", SCALE3X),  # CR stripped, the last line wins
    (b"", NONE),
    (b"music=fm-towns\nfilter=scale2x\nvolume=3\n", SCALE2X),  # unknown keys
    (b"filter=scale4x\n", NONE),  # bad value
    (b"filter=scale2x\nfilter=bad\n", SCALE2X),  # a bad line keeps the earlier value
    (b"filter\nfilter =scale2x\n=scale2x\n", NONE),
    (b"filter=" + b"x" * 80 + b"\nfilter=scale2x\n", SCALE2X),  # an overlong line is skipped, the next one counts
    (b"filter=scale3x" + b" " * 60 + b"\n", NONE),  # a valid start of an overlong line is not read
])
def test_parse(lib, text, filter):
    assert parse(lib, text) == filter


def test_a_file_over_the_limit_gives_the_defaults(lib):
    line = b"filter=scale2x\n"
    assert parse(lib, line + b"#" * (C.SETTINGS_MAX_SIZE - len(line))) == SCALE2X
    assert parse(lib, line + b"#" * (C.SETTINGS_MAX_SIZE - len(line) + 1)) == NONE


def test_save_and_load(lib, tmp_path):
    folder = str(tmp_path).encode()
    assert lib.settings_load(folder).filter == NONE, "a missing file"
    for filter, name in ((SCALE3X, "scale3x"), (SCALE2X, "scale2x"), (NONE, "none")):
        assert lib.settings_save(folder, ctypes.byref(Settings(filter)))
        assert (tmp_path / "SETTINGS.TXT").read_text() == f"filter={name}\n"
        assert lib.settings_load(folder).filter == filter
    assert [p.name for p in tmp_path.iterdir()] == ["SETTINGS.TXT"], "no temporary file stays"


def test_failed_save_keeps_the_old_file(lib, tmp_path):
    folder = str(tmp_path).encode()
    lib.settings_save(folder, ctypes.byref(Settings(SCALE2X)))
    (tmp_path / "SETTINGS.TMP").mkdir()  # the temporary file cannot be created
    assert not lib.settings_save(folder, ctypes.byref(Settings(SCALE3X)))
    assert (tmp_path / "SETTINGS.TXT").read_text() == "filter=scale2x\n"
    assert not lib.settings_save(str(tmp_path / "missing").encode(), ctypes.byref(Settings(SCALE3X)))
    assert not lib.settings_save(folder, ctypes.byref(Settings(0)))
