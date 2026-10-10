#ifndef STATE_JSON_H
#define STATE_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "seen.h"

/* Prefixes: json_ state_json_ */

/* A growing JSON text. Start with {0}; free s when done. */
typedef struct {
    char *s;
    size_t len, cap;
} Json;

/* Appends text as it is (no comma). */
void json_raw(Json *j, const char *text);
void json_rawn(Json *j, const char *text, size_t n);
/* Appends "key": (a comma first when a value comes before). */
void json_key(Json *j, const char *key);
void json_int(Json *j, long value);
void json_bool(Json *j, int value);
void json_null(Json *j);
void json_str(Json *j, const char *text);
void json_strn(Json *j, const char *text, size_t n);   /* the first n bytes, up to an end mark */
void json_open(Json *j, char bracket);
void json_close(Json *j, char bracket);

/* The parts of the game state that state_json_string gives. */
enum { STATE_JSON_STATE, STATE_JSON_VIEW, STATE_JSON_MAP };

/* The game state as the player sees it: time, modes, party, champions, open panels, messages, weather. */
void state_json_state(Json *j);
/* A part of the game state as a new JSON text (free it with free); an unknown part gives null. */
char *state_json_string(int part);
/* The party died or finished the game. */
bool state_json_game_over(void);
/* The name of an input mode (the roots of the input tree in input.c), "unknown" for another number. */
const char *state_json_mode_name(int mode);
/* The squares that the party sees now (the seen rule), their things, and the wall faces that the view draws. */
void state_json_view(Json *j);
/* The explored squares of the party map as rows of glyphs. */
void state_json_map(Json *j);

/* A thing on a square of the view: the row, the side (negative to the left), the square, the thing. */
typedef struct {
    int16_t depth, side, x, y;
    uint16_t thing;
} StateJsonSeenThing;
/* Writes to out (at most max) the things of a thing type that the view draws on the visible non-wall squares, in
   view order; returns the count. */
int state_json_visible_things(uint16_t type, StateJsonSeenThing *out, int max);
/* The name of a view side: "center", "left", "right", "left_2", "right_2", "left_3", "right_3". */
const char *state_json_side_name(int side);
#define STATE_JSON_VISIBLE_GROUPS_MAX SEEN_VISIBLE_MAX
/* Writes the creature groups that the view draws to groups; returns the count. */
int state_json_visible_groups(uint16_t groups[STATE_JSON_VISIBLE_GROUPS_MAX]);
/* Appends the name of an explosion type, or its number when the name table has none. */
void state_json_explosion_name(Json *j, uint16_t type);
/* Appends the name of a creature type, or its number when the name table has none. */
void state_json_creature_name(Json *j, uint16_t type);

#endif
