#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Prefixes: control_ */

/* Control of the running game over a Unix domain socket: one JSON request per line, one JSON reply per line
   (docs/llm-control.md). Plain POSIX, no SDL. */

/* Listens on the socket path: an old socket there is replaced, the last folder level is made. Returns 0 when path
   is another file (it stays) or the socket cannot open. */
int control_open(const char *path);
/* main.c calls it on every PIT tick of the game loop and once per frame of the modal loops: accepts a client (other
   clients get a busy error), reads requests, and answers them. A reply blocks until the client reads it. A client that
   closes with nothing unread during a wait frees the socket at once; one that sent more lines keeps it until the wait
   ends. */
void control_poll(void);
/* One frame of the turn-based main loop: the requests, and one game step while a turn runs. */
void control_frame(void);
/* A turn of the turn-based mode runs: the game ticks, and the client waits for the reply. */
bool control_turn_pending(void);
/* The text after "key": in a request line; 0 when the key is missing. */
const char *control_find_value(const char *line, const char *key);
/* The JSON string value of key, decoded (escapes, \uXXXX to UTF-8); 0 when it is missing, bad, too long, or has a
   NUL character. */
int control_get_string(const char *line, const char *key, char *out, size_t size);
/* The integer value of key; missing when the key is absent or not a number. */
long control_get_int(const char *line, const char *key, long missing);
#define KEY_CODE_MAX 0xffff          /* the largest key code of a request */
/* The key code of a key name (docs/llm-control.md): a key name, a character of the US layout, with modifier prefixes;
   0 none. */
uint16_t control_key_code(const char *name);
/* Closes the sockets and removes the socket file. */
void control_close(void);

#endif
