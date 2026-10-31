/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "json.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os.h"

/* Sizes of the JSON text. */
#define JSON_START_SIZE 1024 /* the first size of the text buffer */
#define ESCAPE_SIZE     8    /* a \u00XX escape and its end mark */
#define ESCAPE_LENGTH   6    /* the length of a \u00XX escape */

/* Appends n bytes; the buffer doubles when they do not fit. */
static void put(Json *j, const char *text, size_t n)
{
    if (j->len + n + 1 > j->cap) {
        size_t cap = j->cap ? j->cap : JSON_START_SIZE;
        while (cap < j->len + n + 1)
            cap *= 2;
        char *s = realloc(j->s, cap);
        if (!s)
            os_fatal("out of memory");
        j->s = s;
        j->cap = cap;
    }
    memcpy(j->s + j->len, text, n);
    j->len += n;
    j->s[j->len] = 0;
}

void json_separate(Json *j)
{
    if (j->len && !strchr("{[:", j->s[j->len - 1]))
        put(j, ",", 1);
}

void json_raw(Json *j, const char *text)
{
    put(j, text, strlen(text));
}
void json_open(Json *j, char bracket)
{
    json_separate(j);
    put(j, &bracket, 1);
}
void json_close(Json *j, char bracket)
{
    put(j, &bracket, 1);
}
void json_bool(Json *j, bool flag)
{
    json_separate(j);
    json_raw(j, flag ? "true" : "false");
}

void json_int(Json *j, int64_t value)
{
    char buf[INT64_TEXT_SIZE];
    json_separate(j);
    snprintf(buf, sizeof buf, "%" PRId64, value);
    json_raw(j, buf);
}

void json_strn(Json *j, const char *text, size_t n)
{
    char buf[ESCAPE_SIZE];
    json_separate(j);
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

void json_str(Json *j, const char *text)
{
    json_strn(j, text, strlen(text));
}

void json_key(Json *j, const char *key)
{
    json_str(j, key);
    put(j, ":", 1);
}

void json_key_int(Json *j, const char *key, int64_t value)
{
    json_key(j, key);
    json_int(j, value);
}

void json_key_pair(Json *j, const char *key, int64_t a, int64_t b)
{
    json_key(j, key);
    json_open(j, '[');
    json_int(j, a);
    json_int(j, b);
    json_close(j, ']');
}
