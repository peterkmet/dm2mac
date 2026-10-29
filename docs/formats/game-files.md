# Game files and install

The 10 game files that the port reads from the PC DOS version of Dungeon Master II (English, 1995), the check of each file, and the install of the files from a source at the first start. The port installs the files into `game/` below the working folder. The ZIP reader is in [zip.md](zip.md), the CD image reader in [cd-image.md](cd-image.md).

## The files

The check of a file is its size and its CRC-32 (the standard polynomial `0xedb88320`). Names match without case. The table is `gamefile_table` in `src/data/gamefile.c`.

| File below `game/` | Size | CRC-32 | Read by |
|---|---|---|---|
| `INTRPLAY.PCX` | 14545 | `0x765c31b6` | `movie_start` (`ui/movie.c`) |
| `FTL` | 495046 | `0x7cace77a` | `movie_start` |
| `INTRO` | 1747588 | `0x6a3668c9` | `movie_start` |
| `END` | 5078746 | `0x8cd811cc` | `movie_start` |
| `CREDITS` | 5454542 | `0x9ce23e0c` | `movie_start` |
| `DRUM.BNK` | 5404 | `0x89422dd5` | `music_init` (`ui/music.c`) |
| `MELODIC.BNK` | 5404 | `0x4df263e3` | `music_init` |
| `DATA/SONGLIST.DAT` | 63 | `0xfa6cdaca` | `music_init` |
| `DATA/GRAPHICS.DAT` | 8639757 | `0xc6d130f6` | the session |
| `DATA/DUNGEON.DAT` | 39437 | `0x2530a08a` | the session |

`DATA/DUNGENB.DAT` and `DATA/DUNGEON.FTL` are optional files of the session. The DOS release has neither, and the install does not copy them. The port reads no other file of the DOS installation.

A file fails when it is missing, has another size, or has another CRC-32. A file that is longer than the table size fails.

## Start

1. With `--data DIR`, the port uses `DIR` as the data folder. It does not check, search, or install.
2. Otherwise `main` creates `game/` when it is missing and opens the log.
3. The port checks the 10 files of `game/`. When all pass, it reads no source.
4. Otherwise it searches a source and installs from it. A restart of the game finds a complete `game/` and reads no source.

The search runs in every mode, including the turn-based mode.

## Sources

A source holds all 10 files. The search runs in the working folder in this order:

1. Folders. The search is depth-first in name order (`strcmp`) up to 3 levels; a child of the working folder is level 1. A folder is checked before its subfolders. The folder `game/` at level 1 is skipped. A folder counts as a source only when it holds at least one of the 10 files at its root. Other folders are searched into, but they get no log line and no message line.
2. ZIP archives (`.zip`) in the working folder itself, in name order. The root of the game files is found as in [zip.md](zip.md#names).
3. CD images (`.iso`, `.img`, `.bin`) in the working folder itself, in name order. The files are in the root folder of the image (see [cd-image.md](cd-image.md)).

The extensions match without case. The first source with all 10 files that pass the check is the source of the install. A folder source with at least one game file, a ZIP archive, or a CD image that fails gets a line in the log and a line in the message, and the search goes on.

## Install

1. The install creates `game/` and `game/DATA` when they are missing.
2. It deletes the temporary files of an earlier install (the file name and `.TMP`) in `game/`.
3. The source is complete only when all 10 files pass, even when `game/` holds some of them.
4. For each file that is missing or fails in `game/`, the install writes the file to its temporary name, checks the written file (size and CRC-32), deletes the target, and renames the temporary file to the target. A mismatch or an error deletes the temporary file.
5. Files of `game/` that pass stay as they are.

The install writes only the 10 fixed names of the table, never a path from the source.

## Message

When no source passes, the port stops with `EXIT_FAILURE` and a message. In the turn-based mode the message goes to the log only; in the other modes the port also shows it in a message box without a window. The message has these parts:

1. The game files of Dungeon Master II are missing.
2. The need of an own copy of the PC DOS version (English, 1995).
3. The forms of a source (folder, `.zip`, `.iso`, `.img`, `.bin`) and the full path of the working folder.
4. When `game/` holds a file that exists and fails, a line with the first such file of the table, `game/<file> is missing or wrong.`
5. One line per failed source: `<path>: <reason>.` The reason is a problem of the source (`not a ZIP archive or damaged`, `not a CD image or damaged`, `a MODE2 CD image: the port reads 2048-byte and MODE1 sectors only`, `no DATA/GRAPHICS.DAT in the archive`) or `<file> is missing or wrong` for the first file of the table that fails.

The message has at most 4095 characters; a longer message ends with `...`. When the port cannot write into `game/`, the message is `cannot write the game files into <path>`.

## Log

The progress goes to the log only: the source path, the failed sources with their first bad file, and `installed N game files`. The log is `os_log`: `dm2.log` in `game/` on Windows, the error output on macOS. The port shows no progress.

## Invariants

- The install changes no file of a source. The readers open a source with `"rb"`.
- After a successful install, each of the 10 files of `game/` passes the check, and `game/` has no temporary file.
- The install changes no save, no `QUICK.DAT`, and no `SETTINGS.TXT`.
- With `--data DIR`, the port neither searches nor writes a game file.
- Two programs that start at the same time in one folder can install at the same time. The port does not prevent this.

## Deviations from the original

None: the install is port only.
