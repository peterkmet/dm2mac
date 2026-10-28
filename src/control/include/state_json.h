/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_CONTROL_STATE_JSON_H
#define DM2_CONTROL_STATE_JSON_H
/* Prefixes: json_ state_json_ */

/* The JSON text writer of the control replies, and the state of the game as JSON: party, champions, view, map. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game.h"
#include "seen.h"

/* The parts of the game state that state_json_string gives. */
typedef enum { STATE_JSON_STATE, STATE_JSON_VIEW, STATE_JSON_MAP } StateJsonPart;

/* The limit of the creature groups in view. */
#define STATE_JSON_VISIBLE_GROUPS_MAX SEEN_VISIBLE_MAX

/* A growing JSON text. Start with {0}; free s when done. */
typedef struct {
    char *s;
    size_t len, cap;
} Json;

/* A thing on a square of the view: the row, the side (negative to the left), the square, the thing. */
typedef struct {
    int16_t depth, side, x, y;
    uint16_t thing;
} StateJsonSeenThing;

/* The JSON writer */

/* Appends text as it is (no comma). */
void json_raw(Json *j, const char *text);
/* Opens an object or a list with its bracket (a comma first when a value comes before). */
void json_open(Json *j, char bracket);
/* Closes an object or a list with its bracket. */
void json_close(Json *j, char bracket);
/* Appends true or false. */
void json_bool(Json *j, bool flag);
/* Appends an integer. */
void json_int(Json *j, int64_t value);
/* Appends a JSON string of the first n bytes of text (up to its end mark); bytes from 0x80 go as they are (UTF-8). */
void json_strn(Json *j, const char *text, size_t n);
/* Appends a JSON string of text. */
void json_str(Json *j, const char *text);
/* Appends "key": (a comma first when a value comes before). */
void json_key(Json *j, const char *key);

/* The game state */

/* The name of an input mode (the roots of the input tree in input.c), "unknown" for another number. */
const char *state_json_mode_name(enum InputMode mode);
/* The party died or finished the game. */
bool state_json_game_over(void);
/* The count of the actions in the action menu of a hand of a champion, as champions[].hands lists them; the open
   action menu stays as it is. */
int state_json_hand_action_count(int champ, int hand);
/* The game state as the player sees it: time, modes, party, champions, open panels, messages, weather. */
void state_json_state(Json *j);

/* The view and the map */

/* The name of a view side: "center", "left", "right", "left_2", "right_2", "left_3", "right_3". */
const char *state_json_side_name(int side);
/* Appends the name of an explosion type, or its number when the name table has none. */
void state_json_explosion_name(Json *j, uint16_t type);
/* Appends the name of a creature type, or its number when the name table has none. */
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
