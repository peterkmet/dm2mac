"""The sources of the game files (src/platform/source.c): a folder, a ZIP archive and a CD image."""

import ctypes
import zipfile

import pytest

from dm2test import constants as C
from dm2test.iso import build_iso
from dm2test.lib import load_lib

FILES = {"FTL": b"ftl " * 300, "DATA/GRAPHICS.DAT": bytes(range(256)) * 20}
FORMS = ["folder", "zip-folder", "zip-root", "iso", "iso-raw"]
MODE2 = 2


@pytest.fixture(scope="module")
def lib():
    lib = load_lib()
    lib.source_open.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
    lib.source_open.restype = ctypes.c_void_p
    lib.source_close.argtypes = [ctypes.c_void_p]
    lib.source_read.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32]
    return lib


def write_zip(path, files, root=""):
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in files.items():
            archive.writestr(root + name, data, compress_type=zipfile.ZIP_DEFLATED)


def make_source(tmp_path, form, files=FILES):
    """Writes the files as a source of the form; the form and the path to open."""
    if form == "folder":
        for name, data in files.items():
            path = tmp_path / "dumast2" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        return C.SOURCE_FOLDER, tmp_path / "dumast2"
    if form.startswith("zip"):
        path = tmp_path / "game.zip"
        write_zip(path, files, "dumast2/" if form == "zip-folder" else "")
        return C.SOURCE_ZIP, path
    path = tmp_path / "game.iso"
    path.write_bytes(build_iso(files, raw=form == "iso-raw"))
    return C.SOURCE_CD, path


def open_source(lib, form, path):
    problem = ctypes.c_char_p()
    source = lib.source_open(form, str(path).encode(), ctypes.byref(problem))
    return source, problem.value


def read(lib, source, name, size):
    data = ctypes.create_string_buffer(max(size, 1))
    return data.raw[:size] if lib.source_read(source, name.encode(), data, size) else None


@pytest.mark.parametrize("form", FORMS)
def test_read_each_form(lib, tmp_path, form):
    source, problem = open_source(lib, *make_source(tmp_path, form))
    assert source, problem
    try:
        for name, data in FILES.items():
            assert read(lib, source, name, len(data)) == data
    finally:
        lib.source_close(source)


def test_folder_names_without_case(lib, tmp_path):
    (tmp_path / "data").mkdir()
    (tmp_path / "ftl").write_bytes(FILES["FTL"])
    (tmp_path / "data" / "graphics.dat").write_bytes(FILES["DATA/GRAPHICS.DAT"])
    source, _ = open_source(lib, C.SOURCE_FOLDER, tmp_path)
    try:
        for name, data in FILES.items():
            assert read(lib, source, name, len(data)) == data
    finally:
        lib.source_close(source)


@pytest.mark.parametrize("form", FORMS)
def test_missing_and_wrong_size(lib, tmp_path, form):
    source, _ = open_source(lib, *make_source(tmp_path, form))
    size = len(FILES["FTL"])
    try:
        assert read(lib, source, "END", size) is None
        assert read(lib, source, "FTL", size + 1) is None
        assert read(lib, source, "FTL", size - 1) is None
    finally:
        lib.source_close(source)


def test_problem_texts(lib, tmp_path):
    junk = tmp_path / "junk.zip"
    junk.write_bytes(b"not an archive " * 10)
    no_marker = tmp_path / "other.zip"
    write_zip(no_marker, {"FTL": FILES["FTL"]})
    junk_image = tmp_path / "junk.iso"
    junk_image.write_bytes(b"not an image " * 400)
    mode2 = tmp_path / "mode2.bin"
    mode2.write_bytes(build_iso(FILES, raw=True, mode=MODE2))
    cases = [
        (C.SOURCE_ZIP, junk, b"not a ZIP archive or damaged"),
        (C.SOURCE_ZIP, no_marker, b"no DATA/GRAPHICS.DAT in the archive"),
        (C.SOURCE_CD, junk_image, b"not a CD image or damaged"),
        (C.SOURCE_CD, mode2, b"a MODE2 CD image: the port reads 2048-byte and MODE1 sectors only"),
    ]
    for form, path, text in cases:
        assert open_source(lib, form, path) == (None, text)


def snapshot(path):
    """The bytes and the modification time of each file of the source."""
    paths = [path] if path.is_file() else sorted(p for p in path.rglob("*") if p.is_file())
    return {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in paths}


@pytest.mark.parametrize("form", FORMS)
def test_source_does_not_change(lib, tmp_path, form):
    form_value, path = make_source(tmp_path, form)
    before = snapshot(path)
    source, _ = open_source(lib, form_value, path)
    for name, data in FILES.items():
        assert read(lib, source, name, len(data)) == data
    lib.source_close(source)
    assert snapshot(path) == before
