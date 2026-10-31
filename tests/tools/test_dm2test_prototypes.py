# SPDX-License-Identifier: GPL-3.0-or-later
"""The prototype parser of tests/dm2test/lib.py against small C headers and the headers of the port."""

import subprocess
import sys
from ctypes import c_bool, c_byte, c_char_p, c_int, c_int16, c_size_t, c_uint, c_uint16, c_void_p

from dm2test import structs
from dm2test.lib import PROTOTYPES, UNTYPED, parse_prototypes
from dm2test.markers import BUILD, ROOT

HEADER = r"""
typedef enum { A, B } Kind;
/* a comment with a prototype: int hidden(void); */
#define TWICE(x) \
    ((x) * 2)
int16_t two_lines(int16_t a,
                  uint16_t b);
static inline int twice(int x) { return x * 2; }
_Noreturn void stop(void) __attribute__((noreturn));
bool ready(void);
char by_value(char c);
const char *name(Kind k, enum Other o);
uint8_t *bytes(const uint8_t *src, uint8_t rgb[4], char **list, size_t n, unsigned u);
Location step(Location at, int16_t dir);
Unknown make(void);
void each(void (*f)(int));
void say(const char *f, ...);
_Static_assert(1, "x");
"""


def test_prototypes_of_a_header():
    prototypes, untyped = parse_prototypes([HEADER])
    assert prototypes["ready"] == (c_bool, ())
    assert prototypes["stop"] == (None, ())
    assert prototypes["by_value"] == (c_byte, (c_byte,))
    assert prototypes["name"] == (c_char_p, (c_int, c_int))
    assert prototypes["bytes"] == (c_void_p, (c_void_p, c_void_p, c_void_p, c_size_t, c_uint))
    assert prototypes["step"] == (structs.Location, (structs.Location, c_int16))
    assert prototypes["two_lines"] == (c_int16, (c_int16, c_uint16))
    assert "twice" not in prototypes and "twice" not in untyped
    assert "hidden" not in prototypes and "TWICE" not in prototypes
    assert untyped == {"make": "unknown type Unknown", "each": "function pointer parameter", "say": "variadic"}


def test_known_prototypes_of_the_port():
    assert PROTOTYPES["thing_word"] == (c_uint16, (c_uint16, c_int))
    assert PROTOTYPES["dungeon_square_first_thing"] == (c_uint16, (c_int16,) * 3)
    assert PROTOTYPES["gdat_load"] == (c_int, (c_char_p, c_void_p))
    assert PROTOTYPES["gdat_palette_color"] == (c_int, (c_void_p, c_int, c_void_p))
    assert PROTOTYPES["dungeon_thing_record"] == (c_void_p, (c_uint16,))
    assert PROTOTYPES["creature_get_group_at"] == (c_uint16, (structs.Location,))
    assert PROTOTYPES["dungeon_location_on_map"] == (structs.Position, (structs.Position, c_int))
    assert PROTOTYPES["state_json_string"] == (c_char_p, (c_int,))
    assert PROTOTYPES["input_key_down"] == (None, (c_uint16,))


EXPECTED_UNTYPED = {
    "ai_find_matching_item_inside": "unknown type AiInsideItem",
    "ai_find_target_in_lines": "unknown type AiLineTarget",
    "ai_move_test": "unknown type AiMoveTest",
    "ai_read_target": "unknown type AiTarget",
    "ai_set_animation": "unknown type AnimStep",
    "ai_trace_line": "function pointer parameter",
    "ai_try_attack": "unknown type AiCommand",
    "ai_try_door_action": "unknown type AiCommand",
    "ai_try_move": "unknown type AiCommand",
    "ai_try_take_item": "unknown type AiCommand",
    "ai_try_use_wall_actuator": "unknown type AiCommand",
    "ai_turn_toward": "unknown type AiCommand",
    "aspect_cell_to_xy": "unknown type GridPoint",
    "champ_use_action_charge": "unknown type ChampionAction",
    "cheats_get_cursor": "unknown type CheatsCursor",
    "creature_anim_next_random_step": "unknown type AnimStep",
    "creature_anim_next_step": "unknown type AnimStep",
    "creature_anim_start": "unknown type AnimStep",
    "creature_find_group_near_point": "unknown type GroupNear",
    "creature_summon_group": "unknown type SummonedGroup",
    "dungeon_find_level_link": "unknown type LevelLink",
    "explosion_move_events": "unknown type MoveDest",
    "fm_set_write_hook": "function pointer parameter",
    "hmp_start": "unknown type HmpSend",
    "icon_build": "unknown type IconSize",
    "input_cursor_image": "unknown type CursorImage",
    "money_split_amount": "unknown type MoneySplit",
    "move_follow_square_transitions": "unknown type Transition",
    "move_party_bump_damage": "unknown type BumpedChampions",
    "move_thing_dest": "unknown type MoveOutcome",
    "offscreen_draw_shadow_text": "unknown type TextColors",
    "offscreen_draw_text_at": "unknown type TextColors",
    "offscreen_draw_text_in_zone": "unknown type TextColors",
    "os_fatal": "variadic",
    "os_log": "variadic",
    "projectile_move_event": "unknown type MoveDest",
    "settings_load": "unknown type Settings",
    "settings_parse": "unknown type Settings",
    "text_draw": "unknown type TextColors",
    "text_draw_with_shadow": "unknown type TextColors",
    "video_layout": "unknown type VideoLayout",
    "video_map_mouse": "unknown type VideoPoint",
    "view_flight_shift": "unknown type ViewPoint",
    "view_hit_test_object": "unknown type ObjectClickResult",
    "view_offset_toward_party": "unknown type GridPoint",
    "view_relative_offset": "unknown type RelativeOffset",
    "zone_compute_anchored_rect": "unknown type AnchoredZone",
}


def test_the_untyped_functions_are_the_expected_set():
    assert UNTYPED == EXPECTED_UNTYPED


def test_every_export_has_a_prototype_or_a_reason():
    out = subprocess.run(["nm", "-gU", str(BUILD / "libdm2.dylib")], capture_output=True, text=True, check=True).stdout
    exports = {line.split()[2].removeprefix("_") for line in out.splitlines() if len(line.split()) == 3 and line.split()[1] == "T"}
    exports = {name for name in exports if not name.startswith(("OPL3_", "tinf_"))}
    missing = sorted(exports - PROTOTYPES.keys() - UNTYPED.keys())
    assert not missing, f"exports without a prototype or a reason: {missing}"


def test_the_table_does_not_depend_on_the_import_order():
    code = ("from dm2test.lib import PROTOTYPES\n"
            "print(sorted((name, repr(p)) for name, p in PROTOTYPES.items()))")
    fresh = subprocess.run([sys.executable, "-c", code], cwd=ROOT / "tests", capture_output=True, text=True, check=True).stdout
    assert fresh.strip() == str(sorted((name, repr(p)) for name, p in PROTOTYPES.items()))
