/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_CONTROL_JSON_H
#define DM2_CONTROL_JSON_H
/* Prefixes: json_ */

/* The JSON text writer of the control replies. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The digits and sign of an int64_t. */
#define INT64_TEXT_SIZE 24

/* A growing JSON text. Start with {0}; free s when done. */
typedef struct {
    char *s;
    size_t len, cap;
} Json;

/* Appends a comma when a value comes before this one. */
void json_separate(Json *j);
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
/* Appends "key":value of an integer. */
void json_key_int(Json *j, const char *key, int64_t value);

#endif /* DM2_CONTROL_JSON_H */
