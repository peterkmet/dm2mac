/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_PLATFORM_TRANSPORT_H
#define DM2_PLATFORM_TRANSPORT_H
/* Prefixes: transport_ */

/* The control socket: a Unix domain socket with one request line in and one reply line out (POSIX). The protocol is
   control/control.h. */

#include <stdbool.h>

/* Setup */

/* Listens on the socket path: an old socket there is replaced, the last folder level is made. Returns false when path
   is another file (it stays) or the socket cannot open. */
bool transport_open(const char *path);
/* Closes the sockets and removes the socket file. */
void transport_close(void);

/* Queries */

/* True in the POSIX build (macOS), which has the control socket; false in the Windows build, which has no turn-based
   mode. */
bool transport_is_available(void);

/* Requests */

/* main.c calls it on every PIT tick of the game loop and once per frame of the modal loops: accepts a client (other
   clients get a busy error), reads requests, and answers them. A reply blocks until the client reads it. A client that
   closes with nothing unread during a wait frees the socket at once; one that sent more lines keeps it until the wait
   ends. */
void transport_poll(void);

#endif /* DM2_PLATFORM_TRANSPORT_H */
