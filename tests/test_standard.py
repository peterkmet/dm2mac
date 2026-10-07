"""Checks port/src against docs/coding-standard.md. Open exceptions: tests/standard_allowlist.txt."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "src"
ALLOWLIST = Path(__file__).with_name("standard_allowlist.txt")
VENDORED = {"opl3.c", "opl3.h"}
LAYERS = ["data", "engine", "ui", "platform"]
INCLUDE = re.compile(r'^\s*#include\s+"([\w./]+)"', re.M)


def sources():
    return sorted(p for p in SRC.rglob("*.[ch]") if p.name not in VENDORED)


def rel(path):
    return path.relative_to(SRC).as_posix()


def allowed():
    """The (check, target) pairs of the allowlist. Each line: check target reason."""
    pairs = set()
    for line in ALLOWLIST.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            check, target, _reason = line.split(maxsplit=2)
            pairs.add((check, target))
    return pairs


def report(check, found):
    """Fails with one allowlist key and the evidence for each violation."""
    assert not found, f"{check} violations (allowlist key, evidence):\n" + "\n".join(found)


def test_layers():
    ok = allowed()
    home = {p.name: p.parent.name for p in SRC.rglob("*.h")}
    found = []
    for path in sources():
        layer = path.parent.name
        assert layer in LAYERS, f"{rel(path)} is not in a layer folder"
        text = path.read_text(errors="replace")
        for header in INCLUDE.findall(text):
            target = home.get(header)
            key = f"{rel(path)}:{header}"
            if target and LAYERS.index(target) > LAYERS.index(layer) and ("layer", key) not in ok:
                found.append(f"layer {key}  | {layer} includes {target}")
        if layer != "platform" and re.search(r"#include\s+<SDL", text):
            found.append(f"layer {rel(path)}:SDL  | SDL outside platform/")
    report("layer", found)
