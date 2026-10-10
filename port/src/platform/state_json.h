#ifndef STATE_JSON_H
#define STATE_JSON_H

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

#endif
