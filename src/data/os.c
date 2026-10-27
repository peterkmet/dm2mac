/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "os.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Message buffer. */
#define OS_MESSAGE_SIZE 512 /* longest log message */

OsLogHook g_os_log_hook;
OsFatalHook g_os_fatal_hook;
OsFullPathHook g_os_full_path_hook;

/* Messages and fatal errors */

/* Sends a formatted message to the hook, or to stderr without a hook. */
static void report(OsLogHook hook, const char *format, va_list args)
{
    char message[OS_MESSAGE_SIZE];
    vsnprintf(message, sizeof message, format, args);
    if (hook)
        hook(message);
    else
        fprintf(stderr, "%s\n", message);
}

void os_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    report(g_os_log_hook, format, args);
    va_end(args);
}

_Noreturn void os_fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    report(g_os_fatal_hook, format, args);
    va_end(args);
    exit(EXIT_FAILURE);
}

/* Full paths */

bool os_get_full_path(const char *path, char *full, size_t size)
{
    return g_os_full_path_hook && g_os_full_path_hook(path, full, size);
}
