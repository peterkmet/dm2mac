/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_OS_H
#define DM2_DATA_OS_H
/* Prefixes: os_ */

/* The operating system services that data/, engine/, ui/ and control/ need: the log, fatal errors, and full paths.
   platform/ sets the hooks; tests set their own. */

#include <stdbool.h>
#include <stddef.h>

/* Messages and fatal errors. platform/ sets the hooks to its log; tests set their own. */
typedef void (*OsLogHook)(const char *message);
typedef void (*OsFatalHook)(const char *message);
extern OsLogHook g_os_log_hook;     /* NULL: the message goes to the error output */
extern OsFatalHook g_os_fatal_hook; /* NULL: the message goes to the error output */
/* Formats a message as printf and logs it. */
void os_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
/* Formats a message as printf, reports it, and ends the program: a port bug or a system error of the original. */
_Noreturn void os_fatal(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* The absolute path of an existing file. */
typedef bool (*OsFullPathHook)(const char *path, char *full, size_t size);
extern OsFullPathHook g_os_full_path_hook; /* platform/ sets it; NULL: no full path */
/* Writes the absolute path of the file into full. False when the file is missing, the path does not fit, or no hook is set. */
bool os_get_full_path(const char *path, char *full, size_t size);

#endif /* DM2_DATA_OS_H */
