#ifndef CONTROL_H
#define CONTROL_H

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
/* Closes the sockets and removes the socket file. */
void control_close(void);

#endif
