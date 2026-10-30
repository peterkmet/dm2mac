/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_CONTROL_CONTROL_H
#define DM2_CONTROL_CONTROL_H
/* Prefixes: control_ */

/* The line protocol of the control of the running game: one JSON request per line, one JSON reply per line
   (docs/llm/control-socket.md). platform/transport.c carries the lines over the socket; no socket, no SDL. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "state_json.h"

/* The limits of a request. */
#define KEY_CODE_MAX 65535 /* the largest key code of a request */

/* Setup */

/* The socket closed: a lost game is forgotten. */
void control_reset(void);

/* Requests */

/* The text after "key": in a request line; NULL when the key is missing. */
const char *control_find_value(const char *line, const char *key);
/* The JSON string value of key, decoded (escapes, \uXXXX to UTF-8); false when it is missing, bad, too long, or has
   a NUL character. */
bool control_get_string(const char *line, const char *key, char *out, size_t size);
/* The integer value of key; missing when the key is absent or not a number. */
int64_t control_get_int(const char *line, const char *key, int64_t missing);
/* The key code of a key name (docs/llm/control-socket.md): a key name, a character of the US layout, with modifier
   prefixes; 0 none. */
uint16_t control_key_code(const char *name);

/* Replies and turns */

/* Answers one request line in reply; false when a wait or a turn holds the reply (control_end_turn gives it). */
bool control_handle_line(const char *line, Json *reply);
/* One frame of the wait or the turn that holds the reply: true when it ended and reply holds the answer. */
bool control_end_turn(Json *reply);
/* A turn of the turn-based mode runs: the game ticks, and the client waits for the reply. */
bool control_is_turn_pending(void);
/* The client is gone: a wait ends, a pending turn runs on without a reply. */
void control_drop_client(void);
/* One frame of the turn-based main loop: one game step while a turn runs. */
void control_frame(void);

#endif /* DM2_CONTROL_CONTROL_H */
