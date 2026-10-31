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

/* Queries */
/* The folder to change to at the start: base when folder has no game entry (a file, a folder, or a link) and base is
   not NULL; NULL to stay in folder. */
const char *install_start_folder(const char *folder, const char *base);
/* Writes the folder of the program into folder: the folder that holds <name>.app when base (SDL_GetBasePath) is
   <name>.app/Contents/Resources/ or <name>.app/Contents/MacOS/ of a macOS bundle, else base. False when it does not
   fit. */
bool install_program_folder(const char *base, char *folder, size_t size);
/* Writes path into full: as it is when it is absolute, else joined to the folder start. False when it does not fit. */
bool install_absolute_path(const char *start, const char *path, char *full, size_t size);

/* Commands */
/* Checks folder/game; when a file of files[GAMEFILE_COUNT] is missing or fails, installs it from the first source
   below folder that passes for all files. INSTALL_NO_SOURCE and INSTALL_WRITE_FAILED write the message. */
InstallResult install_game_files(const char *folder, const GameFile *files, char *message, size_t size);

#endif /* DM2_PLATFORM_INSTALL_H */
