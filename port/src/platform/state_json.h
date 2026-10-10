#ifndef STATE_JSON_H
#define STATE_JSON_H

#include <stdbool.h>
#include <stddef.h>

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

#endif
