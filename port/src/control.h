#ifndef CONTROL_H
#define CONTROL_H

/* Control of the running game over a Unix domain socket: one JSON request per line, one JSON reply per line
   (docs/llm-control.md). Plain POSIX, no SDL. */

/* Listens on the socket path (an old socket file is replaced, the folder is made). Returns 1 on success. */
int control_open(const char *path);
/* Call once per timer tick (game loop and modal loops): accepts a client, reads requests, and answers them. */
void control_poll(void);
/* Closes the sockets and removes the socket file. */
void control_close(void);

#endif
