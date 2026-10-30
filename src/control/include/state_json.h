/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_CONTROL_STATE_JSON_H
#define DM2_CONTROL_STATE_JSON_H
/* Prefixes: state_json_ */

/* The state of the game as JSON: party, champions, view, map. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "interface_state.h"
#include "json.h"
#include "seen.h"

/* The parts of the game state that state_json_string gives. */
typedef enum { STATE_JSON_STATE, STATE_JSON_VIEW, STATE_JSON_MAP } StateJsonPart;

/* The limit of the creature groups in view. */
#define STATE_JSON_VISIBLE_GROUPS_MAX SEEN_VISIBLE_MAX

/* A thing on a square of the view: the row, the side (negative to the left), the square, the thing. */
typedef struct {
    int16_t depth, side, x, y;
    uint16_t thing;
} StateJsonSeenThing;

/* The game state */

/* The name of an input mode (the roots of the input tree in input.c), "unknown" for another number. */
const char *state_json_mode_name(enum InputMode mode);
/* The party died or finished the game. */
bool state_json_game_over(void);
/* The count of the actions in the action menu of a hand of a champion, as champions[].hands lists them; the open
   action menu stays as it is. */
int state_json_hand_action_count(int16_t champion_index, int16_t hand);
/* The game state as the player sees it: time, modes, party, champions, open panels, messages, weather. */
void state_json_state(Json *j);

/* The view and the map */

/* The name of a view side: "center", "left", "right", "left_2", "right_2", "left_3", "right_3". */
const char *state_json_side_name(int side);
/* Appends the name of an explosion type, or its number when the name table has none. */
void state_json_explosion_name(Json *j, uint16_t type);
/* Appends a creature type as its number: the game data has no creature names. */
void state_json_creature_name(Json *j, uint16_t type);
/* Writes to things (at most max) the things of a thing type that the view draws on the visible non-wall squares, in
   view order; returns the count. */
int state_json_visible_things(uint16_t type, StateJsonSeenThing *things, int max);
/* Writes the creature groups that the view draws to groups; returns the count. */
int state_json_visible_groups(uint16_t groups[STATE_JSON_VISIBLE_GROUPS_MAX]);
/* The squares that the party sees now (the seen rule), their things, and the wall faces that the view draws. */
void state_json_view(Json *j);
/* The explored squares of the party map as rows of glyphs. */
void state_json_map(Json *j);
/* A part of the game state as a new JSON text (free it with free); an unknown part gives null. */
char *state_json_string(StateJsonPart part); /* exported for the tests */

#endif /* DM2_CONTROL_STATE_JSON_H */
