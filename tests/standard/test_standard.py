# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks src/ against docs/coding-standard.md. Accepted exceptions: tests/standard/allowlist.txt.

DM2_STANDARD_DUMP=<file> appends every violation line to the file; DM2_STANDARD_IGNORE_ALLOWLIST=1 reports the
violations that the allowlist accepts too.
"""

import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict, namedtuple
from pathlib import Path

from dm2test.markers import ROOT

SRC = ROOT / "src"
TESTS = ROOT / "tests"
ALLOWLIST = Path(__file__).with_name("allowlist.txt")
LAYERS = ["data", "engine", "ui", "control", "platform"]
MAX_FUNCTION_LINES = 60
# the rules of docs/coding-standard.md that name an exception, and the checks whose allowlist lines may give them
EXCEPTION_RULES = {"1.5": {"engine-write"}, "1.7": {"module"}, "3.11": {"record"}, "3.12": {"decimal", "hex"},
                   "5.8": {"long"}, "10.7": {"byteorder"}}

HEX = re.compile(r"\b0x[0-9a-fA-F]{2,}\b")
DECIMAL = re.compile(r"(?<![\w.])[1-9][0-9]+(?![\w.])|\[\s*[5-9]\s*\]|(?<!<)<=?\s*[2-9]\s*;"
                     r"|(?<=[=,(+\-*/%<>&|?:]\s)[3-9](?![\w.\]])")
BIT_OPERATOR = r"(?:<<|>>|(?<![&|])[&|^](?![&|]))=?"
MASK = r"0x(?:0?[fF]|[fF]{2}|[fF]{4})[uU]?\b"
MASK_OPERAND = re.compile(rf"{BIT_OPERATOR}\s*\(?\s*{MASK}|\b{MASK}\s*\)?\s*{BIT_OPERATOR}")
STRTOL_BASE = re.compile(r"\bstrto\w*\s*\([^;]*?,\s*10\s*\)")
DECOMPILER = re.compile(r"\b(?:[a-z]{1,3}Var\d+|[a-z]{1,2}Stack_[0-9a-f]+|extraout_\w+|unaff_\w+|local_[0-9a-f]+|arg_[0-9a-f]+|param_\d+|in_[AD][0-7]\w*|CONCAT\d+|DAT_[0-9a-f]+|FUN_[0-9a-f]+)\b")
RECORD = re.compile(r"\bdungeon_thing_record\(")
CONST_DEF = re.compile(r"\s*(?:static\s+)?const\s+\w+\s+[A-Z][A-Z0-9_]*\s*=")
ENUM = re.compile(r"\benum\b[^{};()]*\{([^}]*)\}")
DEFINE = re.compile(r"^\s*#define\s+([A-Z][A-Z0-9_]*)\b", re.M)
DECL = re.compile(r"^((?:(?:static|inline|const|struct|enum|unsigned|_Noreturn)\s+)*[A-Za-z_]\w*[\s*]+)([a-z_]\w*)\s*\(", re.M)
MACRO = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)\(", re.M)
PREFIXES = re.compile(r"^/\* Prefixes: ([\w ]+?) \*/$", re.M)
PREDICATE = re.compile(r"(?:^|_)(?:is|has|can)_")
FUNC_START = re.compile(r"^(?:static\s+)?[A-Za-z_][\w *]*?\b([a-z_]\w*)\s*\([^;]*$")


def sources():
    return sorted(p for p in SRC.rglob("*.[ch]") if p.relative_to(SRC).parts[0] != "third_party")


def headers():
    return [p for p in sources() if p.suffix == ".h"]


def rel(path):
    return path.relative_to(SRC).as_posix()


def layer_of(path):
    return path.relative_to(SRC).parts[0]


def public_header(layer, stem):
    """The path of the public header <stem>.h of a layer (it may not exist)."""
    return SRC / layer / "include" / f"{stem}.h"


def public_headers():
    """Each public header name and its paths: src/<layer>/include/<name>.h."""
    names = defaultdict(list)
    for path in sorted(SRC.glob("*/include/*.h")):
        names[path.name].append(path)
    return names


def allowed():
    """The (check, target) pairs of the allowlist; none when DM2_STANDARD_IGNORE_ALLOWLIST is set."""
    if os.environ.get("DM2_STANDARD_IGNORE_ALLOWLIST"):
        return set()
    return {tuple(line.split(maxsplit=2)[:2]) for line in ALLOWLIST.read_text().splitlines()
            if line.strip() and not line.startswith("#")}


STRIP_TOKEN = re.compile(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.S)


def strip(text):
    """The text without comments and string or character literals; line numbers stay."""
    def drop(match):
        token = match.group()
        return "\n" * token.count("\n") if token[0] == "/" else '""'
    return STRIP_TOKEN.sub(drop, text)


def code_lines(path):
    """Numbered lines that are not constant definitions."""
    text = ENUM.sub(lambda m: "\n" * m.group().count("\n"), strip(path.read_text(errors="replace")))
    for number, line in enumerate(text.splitlines(), 1):
        if not line.lstrip().startswith("#") and not CONST_DEF.match(line):
            yield number, line


TABLE_START = re.compile(r"^(?:[^\s#]|[ \t]+static\b)[^\n=;{}()]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]=;]*\]\s*)*=\s*\{"
                         r"|^[ \t]+\}\s*([A-Za-z_]\w*)\s*(?:\[[^\]=;]*\]\s*)*=\s*\{", re.M)


def tables(path):
    """Each 1-based line of a table initializer of the file, from the = to the closing brace: (table name, column
    where the initializer starts on the line); the declaration before the = stays outside."""
    text = strip(path.read_text(errors="replace"))
    found, end = {}, 0
    for m in TABLE_START.finditer(text):
        if m.start() < end:
            continue
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        equals = text.rindex("=", m.start(), m.end())
        first = text.count("\n", 0, equals) + 1
        end = i
        for n in range(first, text.count("\n", 0, i) + 2):
            found.setdefault(n, (m.group(1) or m.group(2), equals - text.rfind("\n", 0, equals) - 1 if n == first else 0))
    return found


def report(check, found):
    """Fails with one allowlist key and the evidence for each violation."""
    if found and (dump := os.environ.get("DM2_STANDARD_DUMP")):
        with open(dump, "a") as out:
            out.writelines(line + "\n" for line in found)
    assert not found, f"{check} violations (allowlist key, evidence):\n" + "\n".join(found)


def line_check(check, pattern, owners=(), ignore=None):
    """Each code line that matches pattern, outside the owner files and the allowed tables (target file:table);
    ignore removes allowed text first."""
    ok = allowed()
    found = []
    for path in sources():
        if (check, rel(path)) in ok or path.name in owners:
            continue
        table_lines = tables(path)
        for n, line in code_lines(path):
            name, column = table_lines.get(n, (None, len(line)))
            for target, part in ((rel(path), line[:column]), (f"{rel(path)}:{name}", line[column:])):
                if part and pattern.search(ignore.sub("", part) if ignore else part) and (check, target) not in ok:
                    found.append(f"{check} {target}  | {n}: {line.strip()}")
    report(check, found)


def declarations(path):
    """(return type, name) of each function that a header declares or defines inline."""
    text = strip(path.read_text(errors="replace"))
    return [(ret.replace("static", "").replace("inline", "").strip(), name) for ret, name in DECL.findall(text)
            if not ret.lstrip().startswith(("typedef", "return", "extern"))]


Function = namedtuple("Function", "name ret line params static definition")


def parameter_list(text, start):
    """The text between the opening parenthesis just before start and its partner."""
    depth, i = 1, start
    while depth and i < len(text):
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        i += 1
    return text[start:i - 1]


def split_parameters(params):
    """The parameters of a parameter list, without void."""
    parts, depth, current = [], 0, ""
    for ch in params:
        if ch == "," and depth == 0:
            parts.append(current)
            current = ""
            continue
        depth += {"(": 1, ")": -1, "[": 1, "]": -1}.get(ch, 0)
        current += ch
    parts.append(current)
    return [p.strip() for p in parts if p.strip() and p.strip() != "void"]


def functions(path):
    """Each function that the file declares or defines at column 0, with its parameters."""
    text = strip(path.read_text(errors="replace"))
    found = []
    for m in DECL.finditer(text):
        ret, name = m.group(1), m.group(2)
        if ret.lstrip().startswith(("typedef", "return", "extern", "else")):
            continue
        params = parameter_list(text, m.end())
        after = text[m.end() + len(params) + 1:].lstrip()
        found.append(Function(name, " ".join(ret.split()), text.count("\n", 0, m.start()) + 1,
                              split_parameters(params), "static" in ret.split(), after.startswith("{")))
    return found


def comment_above(lines, line):
    """The comment block that ends on the line above the 1-based line, or ''."""
    i = line - 2
    if i < 0 or not lines[i].rstrip().endswith("*/"):
        return ""
    j = i
    while j > 0 and "/*" not in lines[j]:
        j -= 1
    return "\n".join(lines[j:i + 1]) if lines[j].lstrip().startswith("/*") else ""


def definitions(path):
    """(name, first line, last line, static) of each function body at column 0 of a .c file; 1-based lines."""
    found, name = [], None
    for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        match = FUNC_START.match(line)
        if match and name is None:
            name, start, static = match.group(1), number, line.startswith("static")
        elif line.startswith("}") and name:
            found.append((name, start, number, static))
            name = None
    return found


def test_no_hex_literals():
    line_check("hex", HEX, ignore=MASK_OPERAND)


def test_no_decimal_literals():
    line_check("decimal", DECIMAL, ignore=STRTOL_BASE)


ENUM_ANY = re.compile(r"\b(?:typedef\s+)?enum\b\s*(\w+)?\s*\{([^}]*)\}\s*(\w+)?\s*;", re.S)
ENUM_MEMBER = re.compile(r"^\s*([A-Za-z_]\w*)")


def test_enums_are_named_types():
    ok = allowed()
    found = []
    for path in sources():
        if ("enum", rel(path)) in ok:
            continue
        text = strip(path.read_text(errors="replace"))
        for m in ENUM_ANY.finditer(text):
            name = m.group(1) or m.group(3)
            members = [hit.group(1) for part in m.group(2).split(",") if (hit := ENUM_MEMBER.match(part))]
            line = text.count(chr(10), 0, m.start()) + 1
            at_file_scope = text.count("{", 0, m.start()) == text.count("}", 0, m.start())
            if not name and at_file_scope:
                found.append(f"enum {rel(path)}  | {line}: anonymous enum")
            elif len({member.split("_")[0] for member in members}) > 1:
                found.append(f"enum {rel(path)}  | {line}: enum {name or '{...}'} members without one first word")
    report("enum", found)


DEFINE_LINE = re.compile(r"^\s*#\s*define\s+([A-Z][A-Z0-9_]*)(?!\()\s+(.*?)\s*(?:/\*.*\*/)?\s*$")


def constant_blocks(path):
    """(first line, comment line above, [(name, value)]) of each run of adjacent #define lines with a value."""
    lines = path.read_text(errors="replace").splitlines()
    i = 0
    while i < len(lines):
        m = DEFINE_LINE.match(lines[i])
        if not m or not m.group(2):
            i += 1
            continue
        start, items = i, []
        while i < len(lines) and (m := DEFINE_LINE.match(lines[i])) and m.group(2):
            items.append((m.group(1), m.group(2)))
            i += 1
        yield start + 1, start > 0 and lines[start - 1].rstrip().endswith("*/"), items


def test_constant_blocks_have_a_comment():
    ok = allowed()
    report("block", [f"block {rel(path)}  | {line}: {items[0][0]}" for path in sources() if ("block", rel(path)) not in ok
                     for line, commented, items in constant_blocks(path) if not commented])


LVALUE = r"\w+(?:\s*(?:\.|->)\s*\w+|\s*\[[^\]=;]*\])*"
ASSIGN = r"(?:[-+*/%&|^]|<<|>>)?=(?!=)"
STATEMENT = re.compile(rf"(?<![=!<>])=(?!=)\s*{LVALUE}\s*{ASSIGN}"  # a chained assignment
                       # a second initializer in one declaration
                       rf"|^\s*(?!return\b|else\b|case\b)(?:\w+\s+)*\w+[\s*]+{LVALUE}\s*=(?!=)[^;]*?,\s*\**{LVALUE}\s*=(?!=)"
                       rf"|^\s*{LVALUE}\s*(?:{ASSIGN}[^;,(){{}}]*|\+\+|--)\s*,")  # a comma statement
FOR_HEADER = re.compile(r"\bfor\s*\((?:[^()]|\((?:[^()]|\([^()]*\))*\))*\)")


def test_one_statement_per_line():
    """No chained assignment, no second initializer in one declaration, no comma statement; a for header is skipped."""
    line_check("statement", STATEMENT, ignore=FOR_HEADER)


def test_no_long_type():
    line_check("long-type", re.compile(r"\blong\b"))


def test_no_decompiler_names():
    line_check("decompiler", DECOMPILER)


def test_raw_record_access_only_in_accessors():
    line_check("record", RECORD, owners={"dungeon.c", "dungeon.h"})


def test_each_constant_has_one_definition():
    where = defaultdict(set)
    for path in sources():
        text = strip(path.read_text(errors="replace"))
        names = set(DEFINE.findall(text))
        for body in ENUM.findall(text):
            names.update(m.group(1) for item in body.split(",") if (m := re.match(r"\s*([A-Z][A-Z0-9_]*)\b", item)))
        for name in names:
            where[name].add(rel(path))
    ok = allowed()
    report("duplicate", [f"duplicate {name}  | {sorted(paths)}" for name, paths in sorted(where.items())
                         if len(paths) > 1 and ("duplicate", name) not in ok])


def module_stem(name):
    """The path of a module's public header without .h and include/; <m>_internal.h gives <m>."""
    return name.replace("/include/", "/").removesuffix(".h").removesuffix("_internal")


def first_word(prefix):
    """The text of a prefix up to and including the first _, or the whole prefix without _."""
    return prefix[:prefix.index("_") + 1] if "_" in prefix else prefix


def test_headers_state_unique_prefixes():
    ok = allowed()
    owner, found = {}, []
    for path in sources():
        if path.suffix != ".h" or ("prefix", rel(path)) in ok:
            continue
        names = [name for _, name in declarations(path)] + MACRO.findall(strip(path.read_text(errors="replace")))
        match = PREFIXES.search(path.read_text(errors="replace"))
        if not match:
            if names:
                found.append(f"prefix {rel(path)}  | no Prefixes line")
            continue
        prefixes = match.group(1).split()
        if prefixes == ["none"]:
            found += [f"prefix {rel(path)}  | {name} with Prefixes: none" for name in names]
            continue
        if not names:
            found.append(f"prefix {rel(path)}  | no functions: the line must say none")
        for prefix in prefixes:
            word = first_word(prefix)
            other, other_path = owner.get(word, (None, None))
            if other_path and module_stem(other_path) != module_stem(rel(path)):  # rule 1.7: an internal header shares its module's prefixes
                found.append(f"prefix {rel(path)}  | {prefix} has the first word {word} of {other} of {other_path}")
            owner.setdefault(word, (prefix, rel(path)))
        found += [f"prefix {rel(path)}  | {name}" for name in names if not name.startswith(tuple(prefixes))]
    report("prefix", found)


NUMBERED = re.compile(r"(?i:events?_?\d)|_\d+_to_\d+|(?:^|_)(?:BYTE|OFFSET|AT)_(?=[0-9A-Fa-f]*\d)[0-9A-Fa-f]{2,}(?:_|$)")
IDENTIFIER = re.compile(r"\b[A-Za-z_]\w*\b")


def test_no_event_numbers_or_offsets_in_names():
    ok = allowed()
    where = defaultdict(set)
    for path in sources():
        for name in IDENTIFIER.findall(strip(path.read_text(errors="replace"))):
            if NUMBERED.search(name) and ("numbered", name) not in ok:
                where[name].add(rel(path))
    report("numbered", [f"numbered {name}  | {sorted(paths)}" for name, paths in sorted(where.items())])


TRUTH_LOCAL = re.compile(r"^\s+(?:const\s+)?(?:int|int8_t|int16_t|int32_t|uint8_t|uint16_t|uint32_t|unsigned|short|char)"
                         r"\s+(\w+)\s*=\s*(?:true|false|[^;?]*(?:==|!=|<=|>=|&&|\|\|)[^;?]*|![\w(][^;?]*);")
RETURNS_TRUTH = re.compile(r"\b[Rr]eturns? (?:1|0|true|false)\b(?!\s+(?:on|for)\b)")


def test_truth_values_are_bool():
    """Predicates, locals set from a comparison or true/false, and functions that return 1 or 0 are bool."""
    ok = allowed()
    found = []
    for path in sources():
        lines = path.read_text(errors="replace").splitlines()
        for f in functions(path):
            key = f"{rel(path)}:{f.name}"
            if f.ret.replace("static", "").replace("inline", "").strip() == "bool" or ("bool", key) in ok:
                continue
            if PREDICATE.search(f.name):
                found.append(f"bool {key}  | predicate returns {f.ret}")
            elif RETURNS_TRUTH.search(comment_above(lines, f.line)):
                found.append(f"bool {key}  | the comment says it returns 1 or 0")
        if ("bool", rel(path)) not in ok:
            found += [f"bool {rel(path)}  | {n}: {line.strip()}" for n, line in code_lines(path) if TRUTH_LOCAL.match(line)]
    report("bool", sorted(set(found)))


def all_prefixes():
    """Every prefix that a header states, longest first."""
    found = {p for h in headers() if (m := PREFIXES.search(h.read_text(errors="replace"))) for p in m.group(1).split()}
    return sorted(found - {"none"}, key=len, reverse=True)


MAX_NAME_CHARS, MAX_NAME_WORDS = 40, 4


def test_function_names_are_short():
    """At most four words after the prefix and at most 40 characters for the whole name."""
    prefixes, ok, found, seen = all_prefixes(), allowed(), [], set()
    for path in sources():
        for f in functions(path):
            key = f"{rel(path)}:{f.name}"
            if (rel(path), f.name) in seen or ("name", key) in ok:
                continue
            seen.add((rel(path), f.name))
            prefix = "" if f.static and path.suffix == ".c" else next((p for p in prefixes if f.name.startswith(p)), "")
            words = f.name[len(prefix):].strip("_").split("_")
            if len(f.name) > MAX_NAME_CHARS or len(words) > MAX_NAME_WORDS:
                found.append(f"name {key}  | {len(f.name)} characters, {len(words)} words after {prefix or 'no prefix'}")
    report("name", found)


def test_static_functions_have_no_module_prefix():
    prefixes, ok = all_prefixes(), allowed()
    report("static-prefix", sorted({f"static-prefix {rel(path)}:{f.name}  | {p}" for path in sources() for f in functions(path)
                                    if f.static and path.suffix == ".c" and (p := next((p for p in prefixes if f.name.startswith(p)), None))
                                    and ("static-prefix", f"{rel(path)}:{f.name}") not in ok}))


EXTERN_VARIABLE = re.compile(r"^[ \t]*extern\b([^;]*);", re.M)
FILE_STATIC_G = re.compile(r"^static\b[^(;=]*?\b(g_\w+)\b", re.M)


def extern_names(declaration):
    """The variable names of one extern declaration, also a function pointer variable."""
    if (pointer := re.search(r"\(\s*\*\s*(\w+)\s*\)", declaration)):
        return [pointer.group(1)]
    if "(" in declaration:
        return []
    return re.findall(r"(\w+)\s*(?:\[[^\]]*\])*\s*(?:,|$)", declaration.strip())


def test_globals_have_the_g_prefix():
    ok = allowed()
    found = []
    for path in sources():
        if ("global", rel(path)) in ok:
            continue
        text = strip(path.read_text(errors="replace"))
        found += [f"global {rel(path)}  | extern {name}" for decl in EXTERN_VARIABLE.findall(text)
                  for name in extern_names(decl) if not name.startswith("g_")]
        if path.suffix == ".c":
            found += [f"global {rel(path)}  | static {name}" for name in FILE_STATIC_G.findall(text)]
    report("global", found)


ORDINAL = re.compile(r"(?:^|_)ordinal(?:_|$)|_plus_1(?:_|$)", re.I)


def test_no_ordinals_in_names():
    ok = allowed()
    where = defaultdict(set)
    for path in sources():
        for name in IDENTIFIER.findall(strip(path.read_text(errors="replace"))):
            if ORDINAL.search(name) and ("ordinal", name) not in ok:
                where[name].add(rel(path))
    report("ordinal", [f"ordinal {name}  | {sorted(paths)}" for name, paths in sorted(where.items())])


def test_functions_are_short():
    ok = allowed()
    found = []
    for path in sources():
        if path.suffix != ".c":
            continue
        for name, start, end, _static in definitions(path):
            key = f"{rel(path)}:{name}"
            if end - start + 1 > MAX_FUNCTION_LINES and ("long", key) not in ok:
                found.append(f"long {key}  | {end - start + 1} lines")
    report("long", found)


LITERAL = re.compile(r"^\(?-?(0x[0-9a-fA-F]+|\d+)[uUlL]*\)?$")
HEX_LAST_PARTS = {"MASK", "MASKS", "FLAG", "FLAGS", "BIT", "BITS"}
DECIMAL_PARTS = {"COUNT", "SIZE", "BYTES", "TICKS", "MIN", "MAX", "LIMIT", "THRESHOLD", "WIDTH", "HEIGHT", "SHIFT"}


def block_notation_error(items):
    """Why a constant block breaks the notation rule (hex or decimal in every line), or None."""
    literals = [m.group(1) for _, value in items if (m := LITERAL.match(value.strip()))]
    hexes = [v for v in literals if v.lower().startswith("0x")]
    decimals = [v for v in literals if not v.lower().startswith("0x")]
    names = [name.split("_") for name, _ in items]
    hex_kind = any(parts[-1] in HEX_LAST_PARTS for parts in names)
    decimal_kind = any(DECIMAL_PARTS & set(parts) for parts in names)
    if hexes and decimals:
        return "mixes hex and decimal"
    if hex_kind and decimal_kind:
        return "mixes flags with counts or sizes"
    if hex_kind and decimals:
        return "a flag or mask block in decimal"
    if decimal_kind and hexes:
        return "a count, size or limit block in hex"
    if len({len(v) for v in hexes}) > 1:
        return "hex values with different digit widths"
    return None


def test_number_notation():
    ok = allowed()
    report("notation", [f"notation {rel(path)}  | {line}: {items[0][0]}: {error}" for path in sources()
                        if ("notation", rel(path)) not in ok
                        for line, _, items in constant_blocks(path) if (error := block_notation_error(items))])


NAMED_SHIFT = re.compile(r"(?:<<|>>)\s*([A-Z][A-Z0-9_]*_SHIFT)\b")
PACKED = re.compile(r"(?:<<|>>)\s*(?:8|16|24)\b|\((?:u?int(?:8|16|32)_t|unsigned|int)\)\s*\((?:u?int(?:8|16|32)_t|unsigned|int)\)")


def test_no_packed_fields_at_call_sites():
    """Also a shift by a *_SHIFT constant of a header in a .c file of another module: the accessor belongs to the owner."""
    ok = allowed()
    owner = {name: module_stem(path.name) for path in sources() if path.suffix == ".h"
             for name in DEFINE.findall(strip(path.read_text(errors="replace"))) if name.endswith("_SHIFT")}
    found = []
    for path in sources():
        if path.suffix == ".c" and ("packed", rel(path)) not in ok:
            module = module_stem(header.name) if (header := base_header(path)) else path.stem
            found += [f"packed {rel(path)}  | {n}: {line.strip()}" for n, line in code_lines(path)
                      if PACKED.search(line) or any(owner.get(name, module) != module for name in NAMED_SHIFT.findall(line))]
    report("packed", found)


BYTE_ORDER_HELPERS = ("le16", "le32", "be16", "put_le16", "put_le32", "put_be32")
BYTE_ORDER_MACRO = re.compile(r"^\s*#\s*define\s+((?:le16|le32|be16|put_le16|put_le32|put_be32)\w*)", re.M)
BYTE_SHIFT = re.compile(r"\]\s*\)?\s*<<\s*(?:8|16|24)\b|>>\s*(?:8|16|24)\s*\)?\s*&\s*0x[fF]{2}\b")
BYTE_CAST = re.compile(r"\(\s*(?:const\s+)?(?:u?int8_t|char)\s*\*\s*\)")


def test_byte_order_helpers_only_in_le_header():
    """The byte order helpers and the shifts that join or split bytes stand only in data/include/le.h; a cast to
    (uint8_t *), (int8_t *) or (char *), also with const, stands only in data/."""
    ok = allowed()
    found = []
    for path in sources():
        if rel(path) == "data/include/le.h":
            continue
        text = strip(path.read_text(errors="replace"))
        found += [f"byteorder {rel(path)}  | {name}" for _, name in declarations(path) if name.startswith(BYTE_ORDER_HELPERS)]
        found += [f"byteorder {rel(path)}  | #define {name}" for name in BYTE_ORDER_MACRO.findall(text)]
        found += [f"byteorder {rel(path)}  | {n}: {line.strip()}" for n, line in code_lines(path) if BYTE_SHIFT.search(line)]
        if layer_of(path) != "data" and ("byteorder", rel(path)) not in ok:
            found += [f"byteorder {rel(path)}  | {n}: {line.strip()}" for n, line in code_lines(path) if BYTE_CAST.search(line)]
    report("byteorder", found)


PACKED_STRUCT = re.compile(r"__attribute__\s*\(\(\s*packed\s*\)\)|^\s*#\s*pragma\s+pack\b", re.M)
STRUCT_TYPE = re.compile(r"\btypedef\s+struct\b[^{;]*\{[^{}]*(?:\{[^{}]*\}[^{}]*)*\}\s*(\w+)\s*;|\bstruct\s+(\w+)\s*\{")
FILE_COPY = re.compile(r"\b(?:fread|fwrite|memcpy)\s*\(\s*&\s*(\w+)\s*[,)]")
TYPEDEF_NAME = re.compile(r"\}\s*(\w+)\s*;")


def struct_types():
    """The names of the struct types of src/: typedef names and struct tags."""
    names = set()
    for path in sources():
        names.update(a or b for a, b in STRUCT_TYPE.findall(strip(path.read_text(errors="replace"))))
    return names


def declared_type(name, text):
    """The type of the last declaration of the variable name in text, '*' for a pointer, or None."""
    found = re.findall(rf"\b(?:struct\s+)?(\w+)\s+(\**)\s*{name}\s*[;=\[,)]", text)
    return (found[-1][1] or found[-1][0]) if found else None


def is_struct_copy(match, text, types, globals_):
    """True when the & operand of a copy is a variable of a struct type: its declaration in the file, or a global."""
    kind = declared_type(match.group(1), text[:match.start()])
    return kind in types if kind else match.group(1) in globals_


def test_no_packed_structs():
    """No packed struct and no file or memory copy between bytes and a struct, except a layout of SKULL.EXE (rule 10.7)."""
    ok = allowed()
    types = struct_types()
    texts = {path: strip(path.read_text(errors="replace")) for path in sources()}
    globals_ = {name for text in texts.values() for name in re.findall(r"^extern\s+(?:struct\s+)?(?:" + "|".join(sorted(types))
                                                                      + r")\s+(\w+)", text, re.M)}
    found = []
    for path, text in texts.items():
        for m in PACKED_STRUCT.finditer(text):
            end = TYPEDEF_NAME.search(text, m.end())
            target = f"{rel(path)}:{end.group(1) if end else ''}"
            if ("byteorder", target) not in ok:
                found.append(f"byteorder {target}  | {text.count(chr(10), 0, m.start()) + 1}: {m.group().strip()}")
        found += [f"byteorder {rel(path)}  | {text.count(chr(10), 0, m.start()) + 1}: {m.group()}"
                  for m in FILE_COPY.finditer(text) if is_struct_copy(m, text, types, globals_)]
    report("byteorder", found)


def function_bodies(path):
    """(first line, body text) of each function that the file defines; the body keeps its line breaks."""
    text = strip(path.read_text(errors="replace"))
    starts = [0] + [i + 1 for i, ch in enumerate(text) if ch == "\n"]
    for function in functions(path):
        if not function.definition:
            continue
        open_at = text.index("{", starts[function.line - 1])
        depth, i = 0, open_at
        while i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
            if depth == 0:
                break
        yield text.count("\n", 0, open_at) + 1, text[open_at:i]


STATIC_LOCAL = re.compile(r"^\s*static\b(?![^;]*\bconst\b)[^;]*", re.M)


def test_no_mutable_static_locals():
    """A function keeps no mutable static variable (rule 7.6)."""
    ok = allowed()
    found = []
    for path in sources():
        if path.suffix != ".c" or ("static-local", rel(path)) in ok:
            continue
        for first, body in function_bodies(path):
            found += [f"static-local {rel(path)}  | {first + body.count(chr(10), 0, m.start())}: {' '.join(m.group().split())}"
                      for m in STATIC_LOCAL.finditer(body)]
    report("static-local", found)


TABLE = re.compile(r"^(?!.*\bconst\b)(static\s+)?[A-Za-z_][\w\s*]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)+=\s*\{", re.M)


def element_write(name):
    """A write to an element of the array name: an assignment, an increment, a decrement, a memcpy into it, or a return of it."""
    element = rf"\b{name}\s*(?:\[[^;\]\[]*(?:\[[^;\]\[]*\][^;\]\[]*)*\]\s*)+(?:\.\w+\s*)*"
    return re.compile(rf"{element}(?:[-+*/%|&^]|<<|>>)?=(?!=)|{element}(?:\+\+|--)|(?:\+\+|--)\s*{element}"
                      rf"|\bmemcpy\s*\(\s*{name}\b|\breturn\s+{name}\s*;")


def test_tables_are_const():
    """A file-scope array with an initializer list that no function writes is const (rule 3.13)."""
    ok = allowed()
    texts = {path: strip(path.read_text(errors="replace")) for path in sources()}
    found = []
    for path, text in texts.items():
        if ("const", rel(path)) in ok:
            continue
        for m in TABLE.finditer(text):
            static, name = m.group(1), m.group(2)
            rest = text[:m.start()] + text[m.end():]
            scope = [rest] if static else [rest] + [other for other_path, other in texts.items() if other_path != path]
            if not any(element_write(name).search(other) for other in scope):
                found.append(f"const {rel(path)}  | {text.count(chr(10), 0, m.start()) + 1}: {name}")
    report("const", found)


PORTABLE_HEADERS = {"stdbool", "stddef", "stdint", "stdio", "stdlib", "string", "ctype", "math", "stdarg", "inttypes",
                    "opl3/opl3", "tinf/tinf"}
PLATFORM_CODE = re.compile(r"^\s*#\s*(?:el)?if.*\b_WIN32\b|^\s*#\s*include\s*<(?!(?:" + "|".join(sorted(PORTABLE_HEADERS))
                           + r")\.h>)", re.M)
STDERR_OR_EXIT = re.compile(r"\bfprintf\s*\(\s*stderr\b|\b(?:exit|abort)\s*\(")
NONDETERMINISM = re.compile(r"\b(?:s?rand|time|clock)\s*\(")


def test_platform_code_only_in_platform():
    """Headers other than the portable C ones, rand, srand, time and clock only in platform/; stderr, exit and abort
    only in platform/ and data/os.c."""
    ok = allowed()
    found = []
    for path in sources():
        if layer_of(path) == "platform" or ("platform", rel(path)) in ok:
            continue
        text = path.read_text(errors="replace")
        found += [f"platform {rel(path)}  | {m.group().strip()}" for m in PLATFORM_CODE.finditer(text)]
        for n, line in enumerate(strip(text).splitlines(), 1):
            if NONDETERMINISM.search(line) or rel(path) != "data/os.c" and STDERR_OR_EXIT.search(line):
                found.append(f"platform {rel(path)}  | {n}: {line.strip()}")
    report("platform", found)


EXTERN = re.compile(r"\bextern\b([^;]*);")
REDRAW_FLAG_WRITE = re.compile(r"\bredraw\s*(?:[|&]=|=(?!=))$")


def engine_globals():
    names = set()
    for path in (SRC / "engine").rglob("*.h"):
        for decl in EXTERN.findall(strip(path.read_text(errors="replace"))):
            names.update(n for n in re.findall(r"\b(g_\w+)", decl) if n != "g_hooks" and not n.endswith("_hook"))
    return names


def test_ui_does_not_write_engine_variables():
    """ui/ and control/ write no engine global outside the allowed files and functions (target file or file:function):
    no assignment, increment, memcpy, memmove or memset into it, and no pointer to it without const."""
    alt = "|".join(sorted(engine_globals(), key=len, reverse=True)) or "(?!)"
    target = rf"\b(?:{alt})\b(?:\s*\[[^\]]*\]|\s*(?:\.|->)\s*\w+)*"
    write = re.compile(rf"(?:\+\+|--)\s*{target}|{target}\s*(?:\+\+|--|(?:[-+*/|&^]|<<|>>)?=(?!=))")
    copy_or_alias = re.compile(rf"\bmem(?:cpy|move|set)\s*\(\s*&?\s*{target}|^(?![^=]*\bconst\b)[^=]*\*\s*\w+\s*=\s*&\s*{target}")
    ok = allowed()
    found = []
    for path in sorted([*(SRC / "ui").glob("*.c"), *(SRC / "control").glob("*.c")]):
        if ("engine-write", rel(path)) in ok:
            continue
        owner = {n: name for name, first, last, _ in definitions(path) for n in range(first, last + 1)}
        for n, line in code_lines(path):
            if (all(REDRAW_FLAG_WRITE.search(m.group()) for m in write.finditer(line))  # no write, or only redraw flags
                    and not copy_or_alias.search(line)):
                continue
            key = f"{rel(path)}:{owner[n]}" if n in owner else rel(path)
            if ("engine-write", key) not in ok:
                found.append(f"engine-write {key}  | {n}: {line.strip()}")
    report("engine-write", found)


INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def layer_place_error(path):
    """Why a file stands in the wrong place, or None: a public header in include/, all else next to the .c files."""
    parts = path.relative_to(SRC).parts
    if parts[0] not in LAYERS or len(parts) not in (2, 3) or (len(parts) == 3 and parts[1] != "include"):
        return "not in a layer folder"
    public = path.suffix == ".h" and not path.stem.endswith("_internal")
    if public != (len(parts) == 3):
        return "a public header outside include/" if public else "only public headers stand in include/"
    return None


def test_layers():
    """An include names only the file; a file includes only its own layer and the layers before it."""
    ok = allowed()
    found = []
    public = public_headers()
    found += [f"layer {name}  | the header name is not unique: {[rel(p) for p in paths]}"
              for name, paths in sorted(public.items()) if len(paths) > 1]
    for path in sources():
        if (error := layer_place_error(path)):
            found.append(f"layer {rel(path)}  | {error}")
            continue
        layer = layer_of(path)
        text = path.read_text(errors="replace")
        for header in INCLUDE.findall(text):
            key = f"{rel(path)}:{header}"
            if ("layer", key) in ok:
                continue
            private = SRC / layer / header
            if "/" in header:
                found.append(f"layer {key}  | the include names a path, not only the file")
            elif header in public:
                target = layer_of(public[header][0])
                if LAYERS.index(target) > LAYERS.index(layer):
                    found.append(f"layer {key}  | {layer} includes {target}")
            elif not (header.endswith("_internal.h") and private.exists()):
                found.append(f"layer {key}  | no public header and no private header of {layer}/")
        if layer != "platform" and re.search(r"#\s*include\s+<SDL", text) and ("layer", f"{rel(path)}:SDL") not in ok:
            found.append(f"layer {rel(path)}:SDL  | SDL outside platform/")
    report("layer", found)


CLANG_FORMAT_MAJOR = 23


def clang_format():
    """The clang-format of the virtual environment (dev group), else the one on the PATH."""
    exe = Path(sys.executable).with_name("clang-format")
    return str(exe) if exe.exists() else shutil.which("clang-format")


def test_sources_are_formatted():
    exe = clang_format()
    assert exe, "clang-format is missing: uv sync"
    version = subprocess.run([exe, "--version"], capture_output=True, text=True).stdout
    assert re.search(rf"version {CLANG_FORMAT_MAJOR}\.", version), f"clang-format {CLANG_FORMAT_MAJOR} needed: {version.strip()}"
    result = subprocess.run([exe, "--dry-run", "--Werror", *map(str, sources())], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr[-4000:]


def allowlist_lines():
    """(line number, check, target, reason) of each allowlist line."""
    return [(number, *line.split(maxsplit=2)) for number, line in enumerate(ALLOWLIST.read_text().splitlines(), 1)
            if line.strip() and not line.startswith("#")]


def test_allowlist_names_documented_reasons():
    """Each allowlist line names an exception rule of the standard that its check may give."""
    found = []
    for number, check, target, reason in allowlist_lines():
        rule = re.match(r"rule (\d+\.\d+): \S", reason)
        if not (rule and check in EXCEPTION_RULES.get(rule.group(1), ())):
            found.append(f"allowlist {check}:{target}  | {number}: {reason}")
    report("allowlist", found)


def test_allowlist_lines_match_violations(monkeypatch):
    """Each allowlist line accepts at least one violation: its target or a <target>:<part> of a violation of its check."""
    hits = []
    skip = {"test_allowlist_lines_match_violations", "test_allowlist_names_documented_reasons", "test_sources_are_formatted"}
    with monkeypatch.context() as patch:
        patch.setenv("DM2_STANDARD_IGNORE_ALLOWLIST", "1")
        patch.setattr(sys.modules[__name__], "report", lambda check, found: hits.extend(found))
        for name, check_function in list(globals().items()):
            if name.startswith("test_") and name not in skip:
                check_function()
    keys = {tuple(hit.split("  |")[0].split(" ", 1)) for hit in hits}
    report("allowlist", [f"allowlist {check}:{target}  | {number}: matches no violation"
                         for number, check, target, _ in allowlist_lines()
                         if not any(c == check and (t == target or t.startswith(target + ":")) for c, t in keys)])


def module_files(header):
    """The public header, its _internal.h and the .c files of its module (rule 1.7: the longest header prefix)."""
    base = header.stem.removesuffix("_internal")
    public = public_header(layer_of(header), base)
    layer = SRC / layer_of(header)
    return {p for p in [*layer.iterdir(), *(layer / "include").iterdir()]
            if p == public or p.name == f"{base}_internal.h" or (p.suffix == ".c" and base_header(p) == public)}


def base_header(path):
    """The public header <m>.h with the longest stem m that is the stem of the file or prefixes it as <m>_, or None."""
    return max((h for h in (SRC / layer_of(path) / "include").glob("*.h") if h != path
                and (path.stem == h.stem or path.stem.startswith(h.stem + "_"))), key=lambda h: len(h.stem), default=None)


def main_suffixes():
    """The part file suffixes that IncludeIsMainRegex of .clang-format lists."""
    regex = re.search(r"^IncludeIsMainRegex: '(.*)'", (ROOT / ".clang-format").read_text(), re.M).group(1)
    return set(re.search(r"\(_\(([^)]*)\)\)\?", regex).group(1).split("|"))


def test_one_public_header_per_module():
    """A .c file belongs to a public header; a part file has a suffix that IncludeIsMainRegex lists; a module has
    one public header."""
    ok = allowed()
    suffixes = main_suffixes()
    found = []
    for path in sources():
        if ("module", rel(path)) in ok:
            continue
        own = public_header(layer_of(path), path.stem.removesuffix("_internal"))
        if path.suffix == ".c" and not base_header(path):
            found.append(f"module {rel(path)}  | no header")
        elif path.suffix == ".c" and path.stem != (main := base_header(path).stem) and (
                part := path.stem.removeprefix(main + "_")) not in suffixes:
            found.append(f"module {rel(path)}  | the suffix _{part} is not in IncludeIsMainRegex")
        elif path.suffix == ".h" and path.stem.endswith("_internal"):
            if not own.exists():
                found.append(f"module {rel(path)}  | an internal header without its public header")
        elif path.suffix == ".h" and (base := base_header(path)) and (
                (SRC / layer_of(path) / f"{base.stem}_internal.h").exists()  # <m>_<x>.h of a module m of several files
                or not (SRC / layer_of(path) / f"{path.stem}.c").exists() and any(not f.static for f in functions(path))):
            found.append(f"module {rel(path)}  | a second public header of {base.name}")
    report("module", found)


SPDX_C = "/* SPDX-License-Identifier: GPL-3.0-or-later */"


def header_error(path):
    """Why the head of a header is out of order (SPDX, guard, prefix line, purpose paragraph), or None."""
    lines = path.read_text(errors="replace").splitlines()
    guard = f"DM2_{layer_of(path).upper()}_{path.stem.upper()}_H"
    if lines[:1] != [SPDX_C]:
        return "line 1 is not the SPDX line"
    if lines[1:3] != [f"#ifndef {guard}", f"#define {guard}"]:
        return f"the guard is not {guard}"
    if len(lines) < 4 or not PREFIXES.match(lines[3]):
        return "line 4 is not the prefix line"
    first = next((line for line in lines[4:] if line.strip()), "")
    if not first.startswith("/*"):
        return "no purpose paragraph after the prefix line"
    return None


def test_header_heads():
    ok = allowed()
    report("header", [f"header {rel(path)}  | {error}" for path in headers()
                      if ("header", rel(path)) not in ok and (error := header_error(path))])


FUNCTION_TYPEDEF = re.compile(r"\btypedef\b[^;{(]*?(?:\(\s*\*\s*(\w+)\s*\)\s*\(|\b(\w+)\s*\((?!\s*\*))[^;{]*\)\s*;")


def defined_names(header, cache={}):
    """The names that a header makes known: macros, types (also function and function pointer types), tags,
    functions, variables, enum members."""
    if header not in cache:
        text = strip(header.read_text(errors="replace"))
        names = set(re.findall(r"^\s*#\s*define\s+(\w+)", text, re.M))
        names |= set(re.findall(r"\}\s*(\w+)\s*;", text)) | set(re.findall(r"\btypedef\b[^;{]*?(\w+)\s*;", text))
        names |= set(re.findall(r"\b(?:struct|enum|union)\s+(\w+)", text))
        names |= {a or b for a, b in FUNCTION_TYPEDEF.findall(text)}
        names |= {f.name for f in functions(header)}
        names |= {n for decl in EXTERN_VARIABLE.findall(text) for n in extern_names(decl)}
        for body in ENUM.findall(text):
            names |= set(re.findall(r"(?:^|,)\s*([A-Z][A-Z0-9_]*)", body))
        cache[header] = names
    return cache[header]


def test_includes_are_used():
    """A file uses at least one name of each project header it includes; its own header is always allowed."""
    by_name = {h.name: h for h in headers()}
    ok = allowed()
    found = []
    for path in sources():
        raw = path.read_text(errors="replace")
        used = set(IDENTIFIER.findall(INCLUDE.sub("", strip(raw))))
        own = f"{path.stem}.h"
        for header in INCLUDE.findall(raw):
            key = f"{rel(path)}:{header}"
            if header == own or header not in by_name or ("include", key) in ok:
                continue
            if not defined_names(by_name[header]) & used:
                found.append(f"include {key}  | no name of {header} is used")
    report("include", found)


TRIVIAL_BODY_LINES = 5


def is_field_accessor(f):
    """A getter <type>_<field> with one parameter, or a setter <type>_set_<field> with two that returns void."""
    if "_set_" in f.name:
        return len(f.params) == 2 and f.ret == "void"
    return len(f.params) == 1 and f.ret != "void"


def grouped_accessors(lines, prototypes):
    """The names of the field accessors that follow a commented prototype without a blank or comment line between."""
    found, previous, grouped = set(), None, False
    for f in sorted(prototypes, key=lambda f: f.line):
        between = lines[previous.line - 1:f.line - 1] if previous else []
        joined = previous and all(line.strip() and "/*" not in line for line in between)
        grouped = bool(joined and (grouped or comment_above(lines, previous.line)) and is_field_accessor(f))
        if grouped:
            found.add(f.name)
        previous = f
    return found


def test_function_comments():
    """A public function has its comment above the prototype (a grouped field accessor needs none), a non-trivial static
    function above its definition; never both."""
    ok = allowed()
    found = []
    for header in headers():
        hlines = header.read_text(errors="replace").splitlines()
        bodies = {}
        for c in sorted(module_files(header)):
            if c.suffix == ".c":
                clines = c.read_text(errors="replace").splitlines()
                bodies.update({name: (c, clines, start) for name, start, _, static in definitions(c) if not static})
        prototypes = functions(header)
        grouped = grouped_accessors(hlines, prototypes)
        for f in prototypes:
            if ("comment", rel(header)) in ok:
                continue
            in_header = comment_above(hlines, f.line)
            body = bodies.get(f.name)
            in_source = body and comment_above(body[1], body[2])
            if in_header and in_source:
                found.append(f"comment {rel(header)}  | {f.name}: a comment above the prototype and the definition in {rel(body[0])}")
            elif not in_header and f.name not in grouped:
                found.append(f"comment {rel(header)}  | {f.name}: no comment above the prototype")
    for path in sources():
        if path.suffix != ".c" or ("comment", rel(path)) in ok:
            continue
        lines = path.read_text(errors="replace").splitlines()
        found += [f"comment {rel(path)}  | {name}: no comment above the static definition"
                  for name, start, end, static in definitions(path)
                  if static and end - start - 2 > TRIVIAL_BODY_LINES and not comment_above(lines, start)]
    report("comment", found)


FORWARD_STATIC = re.compile(r"^static\b[^=;{]*?\b(\w+)\s*\([^;{]*\)\s*;", re.M)
SECTIONS = ["constant", "type", "state", "function"]
SECTION_START = [("constant", re.compile(r"^\s*#\s*define\b|^(?:typedef\s+)?enum\b[^=;(]*(?:\{|$)")),
                 ("type", re.compile(r"^typedef\b|^(?:struct|union)\s+\w+\s*\{")),
                 ("state", re.compile(r"^(?!extern\b)[A-Za-z_][^(]*?(?:=|;|\{)\s*$|^(?!extern\b)[A-Za-z_][^(;]*?="))]


def first_sections(path):
    """The 1-based line of the first #define or enum, type, file-scope variable and function definition of a .c file;
    a static const table counts as a variable."""
    starts = {start for _, start, _, _ in definitions(path)}
    first, depth = {}, 0
    for number, line in enumerate(strip(path.read_text(errors="replace")).splitlines(), 1):
        if depth == 0:
            kind = "function" if number in starts else next((k for k, p in SECTION_START if p.search(line)), None)
            first.setdefault(kind, number)
        if not line.lstrip().startswith("#"):
            depth += line.count("{") - line.count("}")
    return {kind: line for kind, line in first.items() if kind}


def test_file_order():
    """Public definitions follow the header order; a static forward declaration names its recursion; the first
    constant, type, file-scope variable and function stand in that order (target <file>:sections)."""
    ok = allowed()
    found = []
    for path in sources():
        if path.suffix != ".c" or ("order", rel(path)) in ok:
            continue
        if ("order", f"{rel(path)}:sections") not in ok:
            first = first_sections(path)
            present = sorted(first, key=SECTIONS.index)
            found += [f"order {rel(path)}:sections  | {first[b]}: the first {b} stands before the first {a} ({first[a]})"
                      for a, b in zip(present, present[1:]) if first[b] < first[a]]
        header = base_header(path)
        if header:
            order = [f.name for f in functions(header) if not f.static]
            defined = [name for name, _, _, static in definitions(path) if not static and name in order]
            expected = sorted(defined, key=order.index)
            if defined != expected:
                first = next(a for a, b in zip(defined, expected) if a != b)
                found.append(f"order {rel(path)}  | {first} stands before its place in {header.name}")
        lines = path.read_text(errors="replace").splitlines()
        text = strip("\n".join(lines))
        for m in FORWARD_STATIC.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            if "recursion" not in comment_above(lines, line).lower():
                found.append(f"order {rel(path)}  | {line}: forward declaration of {m.group(1)} without a recursion comment")
    report("order", found)


TYPE_TABLE = {"champion_index": "int16_t", "cell": "int16_t", "dir": "int16_t", "map": "int16_t",
              "slot": "int16_t", "x": "int16_t", "y": "int16_t", "thing": "uint16_t", "ticks": "uint16_t",
              "square": "uint8_t"}


def parameter(text):
    """(type, name) of one parameter; const does not change the type."""
    if (pointer := re.search(r"\(\s*\*\s*(\w+)\s*\)", text)):
        return "function pointer", pointer.group(1)
    m = re.match(r"(.*?)(\w+)\s*((?:\[[^\]]*\])*)$", text)
    if not m:
        return text, ""
    kind = " ".join(m.group(1).replace("const", " ").split()) + ("[]" if m.group(3) else "")
    return kind.replace(" *", "*"), m.group(2)


def test_one_type_per_parameter_name():
    """A reserved parameter name of a game value kind has the type of the table in the headers of a layer and in
    the static functions of each .c file."""
    ok = allowed()
    found = []
    for path in sources():
        if path.suffix != ".c":
            continue
        for f in functions(path):
            key = f"{rel(path)}:{f.name}"
            if not f.static or ("type", key) in ok:
                continue
            for text in f.params:
                kind, name = parameter(text)
                if (expected := TYPE_TABLE.get(name)) and kind not in (expected, f"{expected}*"):
                    found.append(f"type {key}  | {name}: {kind}; the table says {expected}")
    found = sorted(set(found))
    for layer in LAYERS:
        types = defaultdict(set)
        for header in (SRC / layer).rglob("*.h"):
            for f in functions(header):
                for text in f.params:
                    kind, name = parameter(text)
                    if name:
                        types[name].add(kind)
        for name, kinds in sorted(types.items()):
            key, expected = f"{layer}:{name}", TYPE_TABLE.get(name)
            if ("type", key) in ok:
                continue
            if expected and not kinds <= {expected, f"{expected}*"}:
                found.append(f"type {key}  | {sorted(kinds)}; the table says {expected}")
    report("type", found)


RESULT_COMMENT = re.compile(r"\b0 on success\b|-1 (?:on|for) (?:an? )?(?:error|failure)|\b[Rr]eturns? -1\b|\b1/0/-1\b|\b[Rr]eturns? 0 or -1\b")
MAX_PARAMETERS = 5


def test_results():
    ok = allowed()
    found = []
    for path in sources():
        lines = path.read_text(errors="replace").splitlines()
        found += [f"result {rel(path)}:{f.name}  | the comment gives 0 or -1 as a result" for f in functions(path)
                  if ("result", f"{rel(path)}:{f.name}") not in ok and RESULT_COMMENT.search(comment_above(lines, f.line))]
    report("result", sorted(set(found)))


def test_at_most_five_parameters():
    ok = allowed()
    report("params", sorted({f"params {rel(path)}:{f.name}  | {len(f.params)} parameters" for path in sources()
                             for f in functions(path) if len(f.params) > MAX_PARAMETERS
                             and ("params", f"{rel(path)}:{f.name}") not in ok}))


TEST_MARKER = "exported for the tests"


def defines(path, name):
    """True when the .c file defines the function or the variable name at column 0."""
    text = strip(path.read_text(errors="replace"))
    return (any(f.name == name and f.definition for f in functions(path))
            or re.search(rf"^(?!extern\b)\w[^\n;(=]*\b{name}\s*(?:\[[^\n;=]*\])*\s*[=;]", text, re.M) is not None)


def test_exports_have_users():
    """An exported function or global has a use outside its module and outside the file that defines it, or the test
    marker on its declaration; a name of an _internal.h header has a use outside the file that defines it."""
    used_in = {path: set(IDENTIFIER.findall(strip(path.read_text(errors="replace")))) for path in sources()}
    ok = allowed()
    found = []
    for header in headers():
        internal = header.stem.endswith("_internal")
        lines = header.read_text(errors="replace").splitlines()
        text = strip("\n".join(lines))
        names = [(f.name, f.line) for f in functions(header) if not f.static]
        names += [(n, text.count("\n", 0, m.start()) + 1) for m in EXTERN_VARIABLE.finditer(text) for n in extern_names(m.group(1))]
        for name, line in names:
            key = f"{rel(header)}:{name}"
            defining = {p for p in (module_files(header) if internal else sources()) if p.suffix == ".c" and defines(p, name)}
            own = {header} | defining if internal else module_files(header) | defining
            if ("dead-api", key) in ok or any(name in ids for p, ids in used_in.items() if p not in own):
                continue
            if TEST_MARKER not in lines[line - 1] + comment_above(lines, line):
                where = "the file that defines it" if internal else header.stem
                found.append(f"dead-api {key}  | no use outside {where} and no test marker")
    report("dead-api", found)


COMMENT_TEXT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
EVENT_NUMBER = re.compile(r"(?i)\bevents?\s+#?\d+")
TODO_MARKER = re.compile(r"\b(?:TODO|FIXME|XXX)\b|ponytail:")


def comment_check(check, pattern):
    ok = allowed()
    found = []
    for path in sources():
        if (check, rel(path)) in ok:
            continue
        text = path.read_text(errors="replace")
        for m in COMMENT_TEXT.finditer(text):
            if (hit := pattern.search(m.group())):
                found.append(f"{check} {rel(path)}  | {text.count(chr(10), 0, m.start()) + 1}: {hit.group()}")
    report(check, found)


def test_no_event_numbers_in_comments():
    comment_check("event-comment", EVENT_NUMBER)


def test_no_todo_markers():
    comment_check("todo", TODO_MARKER)


TEST_IMPORT = re.compile(r"^\s*(?:from\s+(?:\S+\.)?(?:test_\w+|conftest)\s+import|import\s+(?:\S+\.)?(?:test_\w+|conftest)\b)", re.M)
MACHINE_PATH = re.compile("/" + "Users/|/" + "private/tmp/")
CLANG_CALL = re.compile(r"""["']clang["']\s*,""")


def test_tests_follow_the_rules():
    """No test module imports another test module or conftest, names a machine path or compiles the library."""
    ok = allowed()
    found = []
    for path in sorted(TESTS.rglob("*.py")):
        target = path.relative_to(ROOT).as_posix()
        if path.resolve() == Path(__file__).resolve() or ("tests", target) in ok:
            continue
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if TEST_IMPORT.match(line) or MACHINE_PATH.search(line) or CLANG_CALL.search(line):
                found.append(f"tests {target}  | {number}: {line.strip()}")
    report("tests", found)
