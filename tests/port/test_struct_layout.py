"""The registered ctypes mirrors of dm2test (structs.MIRRORS) against the C layout: sizeof and offsetof from a compiled C probe."""

import ctypes
import importlib
import os
import pkgutil
import shutil
import subprocess

import pytest

import dm2test
from dm2test import structs
from dm2test.markers import ROOT

CC = os.environ.get("CC", "clang")
INCLUDES = ["-Isrc/data/include", "-Isrc/engine/include", "-Isrc/ui/include", "-Isrc/control/include",
            "-isystem", "src/third_party"]
# Every dm2test module, so that structs.MIRRORS holds all registered mirrors before a test reads it.
MODULES = [importlib.import_module(f"dm2test.{info.name}") for info in pkgutil.iter_modules(dm2test.__path__)]


def fields(mirror):
    """The mirror fields that stand for C fields (not the _pad* padding)."""
    return [name for name, *_ in mirror._fields_ if not name.startswith("_pad")]


def probe_layout(tmp_path):
    """Compiles and runs a C probe; returns {"Type": sizeof, "Type.field": offsetof} of each mirror."""
    headers = sorted({header for header, _ in structs.MIRRORS.values()})
    lines = ["#include <stddef.h>", "#include <stdio.h>"] + [f'#include "{h}"' for h in headers] + ["int main(void)", "{"]
    for mirror in structs.MIRRORS:
        t = mirror.__name__
        lines.append(f'    printf("{t} %zu\\n", sizeof({t}));')
        lines += [f'    printf("{t}.{f} %zu\\n", offsetof({t}, {f}));' for f in fields(mirror)]
    lines += ["    return 0;", "}"]
    source, binary = tmp_path / "probe.c", tmp_path / "probe"
    source.write_text("\n".join(lines) + "\n")
    subprocess.run([CC, "-std=c11", "-fwrapv", *INCLUDES, str(source), "-o", str(binary)], cwd=ROOT, check=True,
                   capture_output=True, text=True)
    out = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
    return {name: int(value) for name, value in (line.split() for line in out.splitlines())}


@pytest.mark.skipif(shutil.which(CC) is None, reason=f"no C compiler {CC}")
def test_mirrors_match_the_c_layout(tmp_path):
    expected = probe_layout(tmp_path)
    for mirror, (_, prefix) in structs.MIRRORS.items():
        t = mirror.__name__
        if not prefix:
            assert ctypes.sizeof(mirror) == expected[t], t
        for name in fields(mirror):
            assert getattr(mirror, name).offset == expected[f"{t}.{name}"], f"{t}.{name}"


PRIVATE = {"GdatSizes"}  # mirrors of file-private C structs (struct gdat of data/gdat.c)


def test_every_dm2test_mirror_is_registered():
    for module in MODULES:
        for cls in vars(module).values():
            if isinstance(cls, type) and issubclass(cls, ctypes.Structure) and cls.__module__ == module.__name__:
                assert cls in structs.MIRRORS or cls.__name__ in PRIVATE, f"{module.__name__}.{cls.__name__}"
