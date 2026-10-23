# SPDX-License-Identifier: GPL-3.0-or-later
"""Every name of the protocol tables in the C code appears in the Protocol vocabulary section of the docs."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TABLE = re.compile(r"static const char \*const (\w+)\[[^\]]*\] = \{(.*?)\};", re.S)
ACTION = re.compile(r'\{"(\w+)",\s*\w+\s*\}')
# creature_names is skipped: it is empty, so it gives no name.
SKIPPED = {"creature_names"}
# Only this table of src/engine/magic.c is part of the protocol.
MAGIC_TABLES = {"rune_names"}
STATE_TABLES = {
    "mode_names", "direction_names", "party_cell_names", "statistic_names", "skill_names", "wound_names",
    "slot_names", "hand_names", "map_option_names", "amount_names", "side_names", "explosion_names",
    "creature_names",
}  # fmt: skip


def section():
    text = (ROOT / "docs/llm/llm-mode.md").read_text()
    start = text.index("## Protocol vocabulary")
    end = text.find("\n## ", start + 1)
    return text[start : end if end > 0 else None]


def tables(path, only=None):
    found = {}
    for name, body in TABLE.findall((ROOT / path).read_text()):
        if only is not None and name not in only:
            continue
        found[name] = set(re.findall(r'"([^"]+)"', body))
    return found


def protocol_names():
    names = set()
    state = tables("src/control/state_json.c")
    magic = tables("src/engine/magic.c", MAGIC_TABLES)
    assert set(state) == STATE_TABLES, f"state_json.c tables found: {sorted(state)}"
    assert set(magic) == MAGIC_TABLES, f"magic.c tables found: {sorted(magic)}"
    for found in (state, magic):
        for name, table in found.items():
            if name not in SKIPPED:
                names |= table
    actions = set(ACTION.findall((ROOT / "src/control/turn.c").read_text()))
    assert actions, "no action table found in src/control/turn.c"
    names |= actions
    return names


def test_every_protocol_name_is_documented():
    documented = section()
    missing = sorted(name for name in protocol_names() if f"`{name}`" not in documented)
    assert not missing, f"names missing in docs/llm/llm-mode.md, Protocol vocabulary: {missing}"
