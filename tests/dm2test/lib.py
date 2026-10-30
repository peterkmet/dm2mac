"""The test library: a private copy of build/libdm2.dylib per load, so each test module has its own C globals.

load_lib sets the ctypes prototypes of every exported function from the headers; a test module sets one only for
another type on purpose."""

import ctypes
import os
import re
import shutil
import tempfile
from pathlib import Path

import pytest

from dm2test.markers import BUILD, ROOT
from dm2test.structs import MIRRORS

LIBRARY = BUILD / "libdm2.dylib"

# the public headers of the library; the SDL side (platform.h) and the vendored code are not in it
HEADERS = sorted(h for h in (ROOT / "src").rglob("*.h")
                 if "third_party" not in h.relative_to(ROOT / "src").parts
                 and h != ROOT / "src" / "platform" / "include" / "platform.h")

SCALARS = {
    "void": None, "bool": ctypes.c_bool, "char": ctypes.c_byte, "int": ctypes.c_int, "unsigned": ctypes.c_uint,
    "size_t": ctypes.c_size_t, "int8_t": ctypes.c_int8, "uint8_t": ctypes.c_uint8, "int16_t": ctypes.c_int16,
    "uint16_t": ctypes.c_uint16, "int32_t": ctypes.c_int32, "uint32_t": ctypes.c_uint32, "int64_t": ctypes.c_int64,
    "uint64_t": ctypes.c_uint64,
}
QUALIFIERS = {"const", "struct", "_Noreturn"}
_TOKEN = re.compile(r"\.\.\.|\w+|\S")


class _Untyped(Exception):
    """A type that the prototype table does not map; the argument is the reason."""


def _strip(text):
    """The header text without comments, preprocessor lines and __attribute__((...))."""
    text = re.sub(r'"(?:\\.|[^"\\\n])*"|/\*.*?\*/|//[^\n]*', lambda m: m[0] if m[0][0] == '"' else " ", text, flags=re.S)
    text = re.sub(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", "", text, flags=re.M)
    while (start := text.find("__attribute__")) >= 0:
        depth, end = 0, text.index("(", start)
        while True:
            depth += {"(": 1, ")": -1}.get(text[end], 0)
            end += 1
            if depth == 0:
                break
        text = text[:start] + text[end:]
    return text


def _statements(text):
    """The statements at depth 0: each ends at a ';' or at the end of a {...} body."""
    out, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
            if ch == "}" and depth == 0:
                out.append(text[start:i + 1])
                start = i + 1
        elif ch == ";" and depth == 0:
            out.append(text[start:i])
            start = i + 1
    return [s.strip() for s in out if s.strip()]


def _ctype(tokens, enums):
    """The ctypes type of a C type without its name: char * is c_char_p, every other pointer c_void_p."""
    tokens = [t for t in tokens if t not in QUALIFIERS]
    stars = tokens.count("*")
    base = " ".join(t for t in tokens if t != "*")
    if stars:
        return ctypes.c_char_p if stars == 1 and base == "char" else ctypes.c_void_p
    if base.startswith("enum ") or base in enums:
        return ctypes.c_int
    if base in SCALARS:
        return SCALARS[base]
    for cls in MIRRORS:
        if cls.__name__ == base and cls.__module__ == "dm2test.structs":
            return cls
    raise _Untyped(f"unknown type {base}")


def _parameter(text, enums):
    """The ctypes type of one parameter: it loses its name, and an array is its pointer."""
    tokens = _TOKEN.findall(re.sub(r"\[[^]]*\]", " * ", text))
    if len(tokens) > 1 and re.fullmatch(r"\w+", tokens[-1]) and tokens[-2] != "enum" and tokens[-1] not in SCALARS:
        tokens = tokens[:-1]
    return _ctype(tokens, enums)


def parse_prototypes(texts):
    """Header texts to (prototypes, untyped): name to (restype, argtypes), and name to the reason it has none."""
    texts = [_strip(t) for t in texts]
    enums = {name for t in texts for name in re.findall(r"\btypedef\s+enum\s*\w*\s*\{[^}]*\}\s*(\w+)\s*;", t)}
    prototypes, untyped = {}, {}
    for text in texts:
        for statement in _statements(text):
            statement = " ".join(statement.split())
            if statement.startswith(("typedef", "extern")) or "(" not in statement or "=" in statement:
                continue
            match = re.match(r"([^(]*?)\b(\w+)\s*\((.*)$", statement)
            if not match or not match[1].strip():
                continue
            head, name, rest = match[1], match[2], match[3]
            if "static" in head.split() or "{" in head:
                continue
            depth, end = 1, 0
            while depth:
                depth += {"(": 1, ")": -1}.get(rest[end], 0)
                end += 1
            params = rest[:end - 1].strip()
            try:
                if "(" in params:
                    raise _Untyped("function pointer parameter")
                if "..." in params:
                    raise _Untyped("variadic")
                restype = _ctype(_TOKEN.findall(head), enums)
                argtypes = () if params in ("", "void") else tuple(_parameter(p, enums) for p in params.split(","))
            except _Untyped as reason:
                untyped[name] = str(reason)
                continue
            prototypes[name] = (restype, argtypes)
    return prototypes, untyped


PROTOTYPES, UNTYPED = parse_prototypes([h.read_text() for h in HEADERS])

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
    for name, (restype, argtypes) in PROTOTYPES.items():
        if hasattr(lib, name):
            function = getattr(lib, name)
            function.restype, function.argtypes = restype, argtypes
    ctypes.c_void_p.in_dll(lib, "g_os_full_path_hook").value = ctypes.cast(FULL_PATH, ctypes.c_void_p).value
    shutil.rmtree(folder)  # the loaded image stays mapped after the file is gone
    return lib


@pytest.fixture(scope="module")
def lib():
    """One library copy per test module (rule 11.5)."""
    return load_lib()
