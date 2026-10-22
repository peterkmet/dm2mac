/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The build without a control socket (Windows): the turn-based mode cannot start. */
#include "transport.h"

/* Setup */

bool transport_open(const char *path)
{
    (void)path;
    return false;
}

void transport_close(void)
{
}

/* Queries */

bool transport_is_available(void)
{
    return false;
}

/* Requests */

void transport_poll(void)
{
}
