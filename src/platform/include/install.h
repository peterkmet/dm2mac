/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_PLATFORM_INSTALL_H
#define DM2_PLATFORM_INSTALL_H
/* Prefixes: install_ */

/* The install of the game files at the first start: the check of the game folder, the search for a source below
   the working folder, and the copy into the game folder. It writes only the game files and their temporary names
   into the game folder; it never writes to a source. */

#include <stdbool.h>
#include <stddef.h>
#include "gamefile.h"

/* The game folder below the working folder. */
#define INSTALL_GAME_FOLDER  "game"
/* The size of the message of a failed install. */
#define INSTALL_MESSAGE_SIZE 4096

/* The result of the install. */
typedef enum { INSTALL_READY, INSTALL_DONE, INSTALL_NO_SOURCE, INSTALL_WRITE_FAILED } InstallResult;

/* Setup */
/* Creates the folder when it is missing; false when it cannot. */
bool install_make_folder(const char *path);

/* Commands */
/* Checks folder/game; when a file of files[GAMEFILE_COUNT] is missing or fails, installs it from the first source
   below folder that passes for all files. INSTALL_NO_SOURCE and INSTALL_WRITE_FAILED write the message. */
InstallResult install_game_files(const char *folder, const GameFile *files, char *message, size_t size);

#endif /* DM2_PLATFORM_INSTALL_H */
