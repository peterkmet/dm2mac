"""The test library: a private copy of build/libdm2.dylib per load, so each test module has its own C globals."""

import ctypes
import os
import re
import shutil
import tempfile
from pathlib import Path

from dm2test.markers import BUILD, ROOT

LIBRARY = BUILD / "libdm2.dylib"

# full is a c_void_p: a c_char_p argument reaches the callback as a copy in a bytes object
_FULL_PATH_HOOK = ctypes.CFUNCTYPE(ctypes.c_bool, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t)


@_FULL_PATH_HOOK
def FULL_PATH(path, full, size):
    # the full path hook of data/os.c, which main.c sets with realpath
    resolved = os.path.realpath(path)
    if len(resolved) >= size:
        return False
    ctypes.memmove(full, resolved + b"\0", len(resolved) + 1)
    return True


def load_lib():
    """Loads a new private copy of the library; conftest.py builds the library before the tests start."""
    folder = Path(tempfile.mkdtemp(prefix="libdm2-"))
    copy = folder / LIBRARY.name
    shutil.copy2(LIBRARY, copy)
    lib = ctypes.CDLL(str(copy))
    # a C bool fills only the low byte of the return register
    for header in (ROOT / "src").rglob("*.h"):
        if "third_party" in header.parts:
            continue
        for name in re.findall(r"^bool (\w+)\(", header.read_text(), re.M):
            getattr(lib, name).restype = ctypes.c_bool
    ctypes.c_void_p.in_dll(lib, "g_os_full_path_hook").value = ctypes.cast(FULL_PATH, ctypes.c_void_p).value
    shutil.rmtree(folder)  # the loaded image stays mapped after the file is gone
    return lib
