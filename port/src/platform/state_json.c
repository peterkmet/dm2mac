#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state_json.h"

#define JSON_START_SIZE 1024          /* the first size of the text buffer */
#define LONG_TEXT_SIZE 24             /* the digits and sign of a long */
#define ESCAPE_SIZE 8                 /* a \u00XX escape and its end mark */
#define ESCAPE_LENGTH 6               /* the length of a \u00XX escape */

/* Appends n bytes; the buffer doubles when they do not fit. */
static void put(Json *j, const char *text, size_t n)
{
    if (j->len + n + 1 > j->cap) {
        size_t cap = j->cap ? j->cap : JSON_START_SIZE;
        while (cap < j->len + n + 1) cap *= 2;
        char *s = realloc(j->s, cap);
        if (!s) abort();
        j->s = s;
        j->cap = cap;
    }
    memcpy(j->s + j->len, text, n);
    j->len += n;
    j->s[j->len] = 0;
}

/* A comma when a value comes before this one. */
static void separate(Json *j)
{
    if (j->len && !strchr("{[:", j->s[j->len - 1])) put(j, ",", 1);
}

void json_raw(Json *j, const char *text) { put(j, text, strlen(text)); }
void json_rawn(Json *j, const char *text, size_t n) { put(j, text, n); }
void json_open(Json *j, char bracket) { separate(j); put(j, &bracket, 1); }
void json_close(Json *j, char bracket) { put(j, &bracket, 1); }
void json_null(Json *j) { separate(j); json_raw(j, "null"); }
void json_bool(Json *j, int value) { separate(j); json_raw(j, value ? "true" : "false"); }

void json_int(Json *j, long value)
{
    char buf[LONG_TEXT_SIZE];
    separate(j);
    snprintf(buf, sizeof buf, "%ld", value);
    json_raw(j, buf);
}

/* A JSON string of the first n bytes of text (up to its end mark); bytes from 0x80 go as they are (UTF-8). */
void json_strn(Json *j, const char *text, size_t n)
{
    char buf[ESCAPE_SIZE];
    separate(j);
    put(j, "\"", 1);
    for (size_t i = 0; i < n && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            buf[0] = '\\';
            buf[1] = (char)c;
            put(j, buf, 2);
        } else if (c == '\n') {
            put(j, "\\n", 2);
        } else if (c < ' ') {
            snprintf(buf, sizeof buf, "\\u%04x", c);
            put(j, buf, ESCAPE_LENGTH);
        } else {
            put(j, text + i, 1);
        }
    }
    put(j, "\"", 1);
}

void json_str(Json *j, const char *text) { json_strn(j, text, strlen(text)); }

void json_key(Json *j, const char *key)
{
    json_str(j, key);
    put(j, ":", 1);
}
