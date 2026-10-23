# SPDX-License-Identifier: GPL-3.0-or-later
"""Every name of the protocol tables in the C code appears in the Protocol vocabulary section of the docs."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TABLE = re.compile(r"static const char \*const (\w+)\[[^\]]*\] = \{(.*?)\};", re.S)
ACTION = re.compile(r'\{"(\w+)",\s*\w+\s*\}')
# Tables that hold no protocol name (creature_names is empty) or none of their own.
SKIPPED = {"creature_names"}
# Only this table of src/engine/magic.c is part of the protocol.
MAGIC_TABLES = {"rune_names"}


def section():
    text = (ROOT / "docs/llm/llm-mode.md").read_text()
    start = text.index("## Protocol vocabulary")
    end = text.find("\n## ", start + 1)
    return text[start : end if end > 0 else None]


def tables(path, only=None):
    found = {}
    for name, body in TABLE.findall((ROOT / path).read_text()):
        if name in SKIPPED or (only is not None and name not in only):
            continue
        found[name] = set(re.findall(r'"([^"]+)"', body))
    return found


def protocol_names():
    names = set()
    for found in (tables("src/control/state_json.c"), tables("src/engine/magic.c", MAGIC_TABLES)):
        for table in found.values():
            names |= table
    names |= set(ACTION.findall((ROOT / "src/control/turn.c").read_text()))
    return names


def test_every_protocol_name_is_documented():
    documented = section()
    missing = sorted(name for name in protocol_names() if f"`{name}`" not in documented)
    assert not missing, f"names missing in docs/llm/llm-mode.md, Protocol vocabulary: {missing}"
