"""The item reference: the GDAT values and actions of every item, as Markdown or as an HTML page.

The values come from GRAPHICS.DAT through the port library (build/libdm2.dylib): gdat_entry_index for the value
keys (category, index, 0x0b, entry) and gdat_text for the texts (category, index, 5, entry).
"""

import base64
import ctypes
import html
import json
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = ROOT / "build" / "libdm2.dylib"
GRAPHICS = ROOT / "game" / "DATA" / "GRAPHICS.DAT"

TYPE_IMAGE, TYPE_TEXT, TYPE_VALUE = 0x01, 0x05, 0x0b
ENTRY_NAME, ACTION_FIRST, ACTION_LAST = 0x18, 0x08, 0x0b
ENTRY_FLOOR_PICTURE, ENTRY_ICON = 0x00, 0x18  # the picture of the item on the floor; its first inventory icon
IMAGE_4BPP = 4
ICON_KEY = 12  # the transparent local color of a 4 bpp item icon
PICTURE_KEY = 10  # the transparent color of an 8 bpp thing picture
RUNE_GLYPH_FIRST = 0x60
RUNES = ["lo", "um", "on", "ee", "pal", "mon", "ya", "vi", "oh", "ful", "des", "zo",
         "ven", "ew", "kath", "ir", "bro", "gor", "ku", "ros", "dain", "neta", "ra", "sar"]
SKILLS = ["fighter", "ninja", "priest", "wizard", "swing", "thrust", "club", "parry", "steal", "fight", "throw",
          "shoot", "identify", "heal", "influence", "defend", "fire", "air", "earth", "water"]
STATS = ["luck", "strength", "dexterity", "wisdom", "vitality", "anti-magic", "anti-fire"]
MISSILES = {0: "fireball", 1: "slime", 2: "lightning bolt", 3: "harm non-material", 4: "open door", 6: "poison bolt",
            7: "poison cloud", 9: "push", 10: "pull", 13: "close door", 14: "missile wall"}
POTIONS = {6: "raises dexterity", 7: "raises strength", 8: "raises wisdom", 9: "raises vitality", 10: "cures poison",
           11: "stamina", 12: "defense aura", 13: "mana", 14: "health, heals wounds", 15: "water"}
COMMAND_SPELL, COMMAND_MELEE, COMMAND_MELEE_2, COMMAND_CONSUME = 0x03, 0x04, 0x08, 0x10
AURAS = {0x0c: "wisdom aura", 0x0d: "dexterity aura", 0x0e: "vitality aura", 0x0f: "strength aura",
         0x21: "spell shield", 0x22: "fire shield", 0x23: "defense shield"}
COMMANDS = {0x01: "defends", 0x02: "invisibility", 0x05: "frightens the creature in front", 0x06: "light",
            0x07: "explosion at the party", 0x09: "haste", 0x0a: "descends into a pit", 0x0b: "freezes the creatures",
            0x10: "eat or drink", 0x11: "load", 0x20: "shoots", 0x24: "heals with mana", 0x26: "light", 0x27: "light",
            0x2a: "throws", 0x2c: "sets the minion target", 0x2d: "the minion guards, else a guard minion comes",
            0x2e: "the minion fetches, else a fetch minion comes", 0x2f: "an attack minion comes and goes to the target",
            0x30: "dismisses the minion", 0x31: "summons a scout minion", 0x32: "summons a carrier minion",
            0x33: "summons an army minion", 0x36: "warps to the marker"}
POWERED = {0x02, 0x05, 0x06, 0x07, 0x09, 0x0b, 0x26, 0x27}
CHARGES = {0x10: "uses all charges", 0x11: "needs a charge, uses none", 0x12: "needs no charges left"}
WORN = [(0x01, "head"), (0x02, "neck"), (0x04, "torso"), (0x08, "legs"), (0x10, "feet")]
CAT_CLOTHING, CAT_POTION = 0x11, 0x13
CATEGORIES = [(0x10, "Weapons"), (0x11, "Clothing"), (0x12, "Scrolls"), (0x13, "Potions"),
              (0x14, "Containers"), (0x15, "Miscellaneous items")]
FIELDS = ["SK", "LV", "CM", "BZ", "TR", "ST", "PA", "TA", "NC", "EX", "PB", "DM", "MS", "SD", "RP", "HN", "AT", "WH"]
DAMAGE_FACTOR_SCALE = 32  # the damage factor of an action is in 32nds
# Value entries.
ATTR_FLAGS, ATTR_WEIGHT, ATTR_VALUE, ATTR_FOOD, ATTR_SLOTS, ATTR_AMMO = 0x00, 0x01, 0x02, 0x03, 0x04, 0x05
ATTR_STRENGTH, ATTR_THROW, ATTR_SHOOT_ATTACK, ATTR_ARMOUR, ATTR_EXTRA_DAMAGE = 0x08, 0x09, 0x0a, 0x0b, 0x0d
ATTR_EFFECT_TIME, ATTR_MANA_MAXIMUM, ATTR_STAT_FIRST, ATTR_SKILL_FIRST = 0x13, 0x14, 0x15, 0x1e
ATTR_LIGHT, ATTR_MOVE_SPEED, ATTR_WEIGHT_PER_CHARGE, ATTR_VALUE_PER_CHARGE = 0x32, 0x33, 0x34, 0x35
FLAG_HOLDS_WATER, FLAG_LIGHT, FLAG_USED_UP, FLAG_HAS_EFFECTS, FLAG_COIN = 0x0001, 0x0010, 0x0800, 0x2000, 0x4000
MODIFIER_CONSUMED, MODIFIER_ANY_SLOT = 0x4000, 0x8000
AMMO_LAUNCHER, ARMOUR_DEFENDS, SLOTS_NO_CONTAINER = 0x8000, 0x8000, 0x8000
ARMOUR_MASK, SHARP_SHIFT, SHARP_MASK = 0xff, 8, 0x07
TEXT_SIZE = 512


class GdatKey(ctypes.Structure):
    _fields_ = [("category", ctypes.c_int), ("index", ctypes.c_int), ("entry", ctypes.c_int)]


class ImageInfo(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int), ("bpp", ctypes.c_int), ("size", ctypes.c_size_t)]


class Gdat:
    """GRAPHICS.DAT loaded in the port library."""

    def __init__(self, library: Path = LIBRARY, graphics: Path = GRAPHICS):
        lib = ctypes.CDLL(str(library))
        lib.gdat_load.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        lib.gdat_text.argtypes = [ctypes.c_void_p, GdatKey, ctypes.c_char_p, ctypes.c_size_t]
        lib.gdat_entry_index.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
        lib.gdat_entry_index.restype = ctypes.c_uint16
        lib.gdat_exists.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
        lib.gdat_find.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4
        u8_p = ctypes.POINTER(ctypes.c_uint8)
        lib.gdat_image_info.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ImageInfo)]
        lib.gdat_decode_image.argtypes = [ctypes.c_void_p, ctypes.c_int, u8_p, ctypes.c_size_t]
        lib.gdat_image_colormap.argtypes = [ctypes.c_void_p, ctypes.c_int]
        lib.gdat_image_colormap.restype = u8_p
        lib.gdat_palette_color.argtypes = [ctypes.c_void_p, ctypes.c_int, u8_p]
        self.lib = lib
        self.palette = {}
        self.handle = ctypes.c_void_p()
        if lib.gdat_load(str(graphics).encode(), ctypes.byref(self.handle)) != 0:
            raise SystemExit(f"cannot load {graphics}")
        self.buffer = ctypes.create_string_buffer(TEXT_SIZE)

    def has_text(self, cat, index, entry):
        return bool(self.lib.gdat_exists(self.handle, cat, index, TYPE_TEXT, entry))

    def text(self, cat, index, entry):
        self.lib.gdat_text(self.handle, GdatKey(cat, index, entry), self.buffer, TEXT_SIZE)
        return self.buffer.value.decode("latin1")

    def value(self, cat, index, entry):
        if not self.lib.gdat_exists(self.handle, cat, index, TYPE_VALUE, entry):
            return 0
        return self.lib.gdat_entry_index(self.handle, cat, index, TYPE_VALUE, entry)

    def picture(self, cat, index, entry):
        """The image (cat, index, 1, entry) as (width, height, RGBA bytes) with its key color transparent; None if
        absent."""
        lib, handle = self.lib, self.handle
        item = lib.gdat_find(handle, cat, index, TYPE_IMAGE, entry)
        if item < 0:
            return None
        info = ImageInfo()
        if lib.gdat_image_info(handle, item, ctypes.byref(info)) != 0 or info.size == 0:
            return None
        pixels = (ctypes.c_uint8 * info.size)()
        if lib.gdat_decode_image(handle, item, pixels, info.size) != 0:
            return None
        w, h = info.width, info.height
        if info.bpp == IMAGE_4BPP:
            colormap = lib.gdat_image_colormap(handle, item)
            stride = (w + 1) // 2
            local = [pixels[y * stride + x // 2] >> 4 if x % 2 == 0 else pixels[y * stride + x // 2] & 0x0f
                     for y in range(h) for x in range(w)]
            colors = [None if c == ICON_KEY else colormap[c] for c in local]
        else:
            colors = [None if c == PICTURE_KEY else c for c in pixels[: w * h]]
        rgba = bytearray()
        for c in colors:
            rgba += b"\0\0\0\0" if c is None else bytes(self.rgb(c)) + b"\xff"
        return w, h, bytes(rgba)

    def rgb(self, color):
        """The 8-bit red, green and blue of a palette color (6-bit VGA values in the data)."""
        if color not in self.palette:
            v = (ctypes.c_uint8 * 3)()
            self.lib.gdat_palette_color(self.handle, color, v)
            self.palette[color] = tuple(c << 2 | c >> 4 for c in v)
        return self.palette[color]


def png_data_uri(width, height, rgba):
    """An RGBA image as a PNG data URI."""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    return "data:image/png;base64," + base64.b64encode(png).decode()


def rune_names(text):
    """Rune glyphs (0x60 + rune) become upper-case rune names."""
    return re.sub(r"[`a-w]", lambda m: RUNES[ord(m.group()) - RUNE_GLYPH_FIRST].upper(), text).strip()


def action_fields(text):
    """The key-number fields of an action text; the last one of a key counts, as in the game."""
    return {k: int(n) for k, n in re.findall(r"([A-Z]{2})(-?\d+)", text) if k in FIELDS}


def action_parts(text):
    """An action text as (name, kind, effect, skill, charges); None for an action without a name. The kind is melee,
    spell (a missile or an aura), or other."""
    label, _, rest = text.partition(":")
    if not label.strip():
        return None
    f = action_fields(rest)
    cm = f.get("CM", 0)
    kind = "other"
    if cm in (COMMAND_MELEE, COMMAND_MELEE_2):
        kind = "melee"
        effect = f"damage ×{f.get('DM', 0) / DAMAGE_FACTOR_SCALE:.2f}"
        if f.get("AT") == 1:
            effect += ", full damage to half-damage creatures"
        if f.get("HN"):
            effect += ", pierces"
    elif cm == COMMAND_SPELL:
        kind = "spell"
        effect = f"{MISSILES.get(f.get('PA', 0), 'missile %d' % f.get('PA', 0))}, power {f.get('ST', 0)}"
    elif cm in AURAS:
        kind = "spell"
        effect = f"{AURAS[cm]}, power {f.get('ST', 0)}"
    elif cm in COMMANDS:
        effect = COMMANDS[cm] + (f", power {f['ST']}" if cm in POWERED and f.get("ST") else "")
    else:
        effect = f"command {cm}"
    skill = ""
    if "SK" in f and f["SK"] < len(SKILLS):
        skill = SKILLS[f["SK"]] + (f" level {f['LV']}" if f.get("LV") else "")
    nc = f.get("NC")
    charges = CHARGES.get(nc, f"{nc} charge" + ("s" if nc > 1 else "")) if nc else ""
    return rune_names(label), kind, effect, skill, charges


def describe_action(parts):
    """An action as 'NAME (effect, skill level, charges)'."""
    name, _, *details = parts
    return f"{name} ({', '.join(d for d in details if d)})"


def signed(byte):
    n = byte - 0x100 if byte & 0x80 else byte
    return f"+{n}" if n > 0 else str(n)


def specials(g, cat, i, eats):
    """The flags and modifiers of an item as short phrases."""
    v = lambda e: g.value(cat, i, e)  # noqa: E731
    flags = v(ATTR_FLAGS)
    out = []
    if v(ATTR_AMMO) & AMMO_LAUNCHER:
        out.append("launcher")
    if v(ATTR_SHOOT_ATTACK):
        out.append(f"shoot attack {v(ATTR_SHOOT_ATTACK)}")
    if v(ATTR_EXTRA_DAMAGE):
        out.append(f"extra damage {v(ATTR_EXTRA_DAMAGE)}")
    if cat != CAT_CLOTHING and v(ATTR_ARMOUR) & ARMOUR_DEFENDS:
        out.append(f"defends in hand, armour {v(ATTR_ARMOUR) & ARMOUR_MASK}")
    if flags & FLAG_LIGHT:
        out.append("gives light in a hand by its charges")
    if flags & FLAG_COIN:
        out.append("coin")
    if flags & FLAG_HOLDS_WATER:
        out.append("holds water")
    if flags & FLAG_USED_UP:
        out.append("vanishes with its last charge")
    if v(ATTR_FOOD):
        out.append(f"food {v(ATTR_FOOD)}")
    if flags & FLAG_HAS_EFFECTS:
        modifiers = ([(ATTR_MANA_MAXIMUM, "maximum mana")] + [(ATTR_STAT_FIRST + s, n) for s, n in enumerate(STATS)]
                     + [(ATTR_SKILL_FIRST + s, n + " skill") for s, n in enumerate(SKILLS)]
                     + [(ATTR_LIGHT, "light"), (ATTR_MOVE_SPEED, "move speed")])
        timed = False
        for attr, name in modifiers:
            m = v(attr)
            # a consumed modifier of an item that cannot be eaten never counts
            if not m or (m & MODIFIER_CONSUMED and not eats):
                continue
            when = "when eaten" if m & MODIFIER_CONSUMED else "in any slot" if m & MODIFIER_ANY_SLOT else "worn or held"
            out.append(f"{name} {signed(m & 0xff)} {when}")
            timed |= bool(m & MODIFIER_CONSUMED) and attr >= ATTR_SKILL_FIRST
        if timed and v(ATTR_EFFECT_TIME):
            out.append(f"the eaten effect lasts {v(ATTR_EFFECT_TIME)} steps")
    if v(ATTR_WEIGHT_PER_CHARGE):
        out.append(f"weight per charge {v(ATTR_WEIGHT_PER_CHARGE) / 10:g} kg")
    if v(ATTR_VALUE_PER_CHARGE):
        out.append(f"value per charge {v(ATTR_VALUE_PER_CHARGE)}")
    if v(ATTR_SLOTS) & SLOTS_NO_CONTAINER:
        out.append("does not go into a container")
    return out


def items(g, cat):
    """The rows of a category: one dict per item with a name; Actions and Special are lists."""
    for i in range(256):
        if not g.has_text(cat, i, ENTRY_NAME):
            continue
        texts = [g.text(cat, i, e) for e in range(ACTION_FIRST, ACTION_LAST + 1) if g.has_text(cat, i, e)]
        eats = any(action_fields(t).get("CM") == COMMAND_CONSUME for t in texts)
        armour = g.value(cat, i, ATTR_ARMOUR)
        clothing = cat == CAT_CLOTHING and armour
        actions = [a for a in map(action_parts, texts) if a]
        yield {
            "Index": str(i),
            "Name": rune_names(g.text(cat, i, ENTRY_NAME).lstrip("\x01'")),
            "Weight (kg)": f"{g.value(cat, i, ATTR_WEIGHT) / 10:g}",
            "Value": str(g.value(cat, i, ATTR_VALUE)),
            "Strength": str(g.value(cat, i, ATTR_STRENGTH) or ""),
            "Throw": str(g.value(cat, i, ATTR_THROW) or ""),
            "Worn on": ", ".join(n for b, n in WORN if g.value(cat, i, ATTR_SLOTS) & b) if cat == CAT_CLOTHING else "",
            "Armour": str(armour & ARMOUR_MASK) if clothing else "",
            "Sharp": str((armour >> SHARP_SHIFT) & SHARP_MASK) if clothing else "",
            "Effect": POTIONS.get(i, "") if cat == CAT_POTION else "",
            "Actions": [describe_action(a) for a in actions],
            "Special": specials(g, cat, i, eats),
            "action_parts": actions,
        }


def columns(rows):
    return [c for c in rows[0] if c[0].isupper() and (c in ("Index", "Name") or any(r[c] for r in rows))]


def collect(g):
    return [(title, list(items(g, cat))) for cat, title in CATEGORIES]


COLUMN_NOTES = [
    ("Index", "", "The GDAT index of the item in its category."),
    ("Weight (kg)", "`0x01`", "The weight, in tenths of a kilogram in the data."),
    ("Value", "`0x02`", "The value in copper coins (copper coin 1, silver coin 4, gold coin 16). Merchants change it "
     "with their price factors."),
    ("Strength", "`0x08`", "The melee bonus: a melee action adds it to the champion strength."),
    ("Throw", "`0x09`", "The throw power: a throw or a shot adds it."),
    ("Worn on", "`0x04`", "The body slots that the clothing fits."),
    ("Armour", "`0x0b` bits 0–7", "The defense of the clothing."),
    ("Sharp", "`0x0b` bits 8–10", "The sharp factor: against a sharp attack the defense is armour × (sharp + 4) / 8."),
    ("Effect", "", "The effect of a potion when a champion drinks it; the power of the potion sets the amount."),
    ("Actions", "texts `0x08`–`0x0b`", "The actions of the item in the action hand."),
    ("Special", "", "Flags and modifiers."),
]
ACTION_NOTES = [
    "`damage ×n`: the damage factor of a melee action. The hit damage is the champion strength (with the weapon "
    "strength) × n, less the defense of the creature.",
    "A spell action names its missile or its aura and the power.",
    "`level n`: the action shows only from this skill level.",
    "`n charges`: the action needs and uses n charges of the item.",
    "The skills are the 4 base skills (fighter, ninja, priest, wizard) and their 16 sub-skills, 4 for each base "
    "skill: swing, thrust, club, parry; steal, fight, throw, shoot; identify, heal, influence, defend; fire, air, "
    "earth, water. The names thrust, club, identify, defend, fire, air, earth, and water are the sub-skill names of "
    "Dungeon Master; they are unverified for Dungeon Master II, because the port has no names for these skills.",
    "The minion actions summon the minion of their command, as in the original: the ATTACK MINION item summons a "
    "scout minion, the GUARD MINION item a carrier minion.",
]
SPECIAL_NOTES = [
    "`extra damage n`: a hit adds armour-piercing damage against creatures; against champions it is poison.",
    "`gives light in a hand by its charges`: the item lights the party in a hand, and its charges burn down.",
    "`worn or held`: a modifier counts when the item is in a slot that fits it. `in any slot`: in every slot. "
    "`when eaten`: once, when a champion eats or drinks it; a skill or speed change of it ends after the given steps.",
    "`vanishes with its last charge`: the item goes away when an action uses its last charge.",
]
INTRO = ("The values of every item of `GRAPHICS.DAT` (PC DOS, English), by category. The game reads them from the "
         "GDAT value keys `(category, index, 0x0b, entry)` with `item_attr`; the names and the actions are the texts "
         "`(category, index, 5, entry)`. The tables use the GDAT names; a rune glyph of a name shows as the "
         "upper-case rune name (`VI POTION`). `dm2-items` writes this page.")


def markdown(data):
    out = ["# Items", "", INTRO, "", "## Columns", "", "| Column | GDAT entry | Meaning |", "|---|---|---|"]
    out += [f"| {c} | {e} | {m} |" for c, e, m in COLUMN_NOTES]
    out += ["", "## Actions", "", "An action shows its name, then its effect, the skill that it trains and needs, and "
            "its charges.", ""] + [f"- {n}" for n in ACTION_NOTES]
    out += ["", "## Special", ""] + [f"- {n}" for n in SPECIAL_NOTES]
    for title, rows in data:
        cols = columns(rows)
        out += ["", f"## {title}", "", "| " + " | ".join(cols) + " |", "|" + "|".join("---" for _ in cols) + "|"]
        for r in rows:
            cells = ["<br>".join(r[c]) if c == "Actions" else "; ".join(r[c]) if c == "Special" else r[c] for c in cols]
            out.append("| " + " | ".join(x.replace("|", "\\|") for x in cells) + " |")
    return "\n".join(out) + "\n"


def inline_code(text):
    return re.sub(r"`([^`]*)`", r"<code>\1</code>", html.escape(text))


def html_page(g, data):
    """The HTML page: the template with the items and their pictures as JSON; the page script draws the cards."""
    template = Path(__file__).with_name("items_page.html").read_text()
    keys = {"Weight (kg)": "weight", "Value": "value", "Strength": "strength", "Throw": "throw", "Worn on": "worn",
            "Armour": "armour", "Sharp": "sharp", "Effect": "effect", "Special": "special"}
    categories = []
    for (cat, _), (title, rows) in zip(CATEGORIES, data):
        out = []
        for r in rows:
            item = {"index": int(r["Index"]), "name": r["Name"]}
            item.update({k: r[c] for c, k in keys.items() if r[c]})
            for key, entry in (("icon", ENTRY_ICON), ("floor", ENTRY_FLOOR_PICTURE)):
                picture = g.picture(cat, item["index"], entry)
                if picture:
                    item[key] = {"src": png_data_uri(*picture), "w": picture[0], "h": picture[1]}
            item["actions"] = [dict(zip(("name", "kind", "effect", "skill", "charges"), a)) for a in r["action_parts"]]
            out.append(item)
        categories.append({"id": re.sub(r"[^a-z]+", "-", title.lower()).strip("-"), "title": title, "items": out})
    notes = ("<dl>" + "".join(f"<dt>{html.escape(c)}</dt><dd>{inline_code(m)}</dd>" for c, _, m in COLUMN_NOTES)
             + "</dl><h3>Actions</h3><ul>" + "".join(f"<li>{inline_code(n)}</li>" for n in ACTION_NOTES)
             + "</ul><h3>Special</h3><ul>" + "".join(f"<li>{inline_code(n)}</li>" for n in SPECIAL_NOTES) + "</ul>")
    payload = json.dumps(categories, ensure_ascii=False).replace("</", "<\\/")
    total = sum(len(rows) for _, rows in data)
    return template.replace("{{DATA}}", payload).replace("{{NOTES}}", notes).replace("{{TOTAL}}", str(total))


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 1 or Path(args[0]).suffix not in (".md", ".html"):
        raise SystemExit("usage: dm2-items OUT.md|OUT.html")
    out = Path(args[0])
    g = Gdat()
    data = collect(g)
    out.write_text(markdown(data) if out.suffix == ".md" else html_page(g, data))
