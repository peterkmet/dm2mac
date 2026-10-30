# Game files and install

The port reads 10 game files of the PC DOS version of Dungeon Master II (English, 1995), checks each file, and on startup installs them from a source into `game/` below the working folder. The ZIP reader is in [zip.md](zip.md), and the CD image reader is in [cd-image.md](cd-image.md).

## The files

The file check compares the size and the CRC-32 (the standard polynomial `0xedb88320`). Names are case-insensitive. The table is `gamefile_table` in `src/data/gamefile.c`.

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

`DATA/DUNGENB.DAT` and `DATA/DUNGEON.FTL` are optional session files. The DOS release has neither, and the install does not copy them. The port does not read any other file of the DOS installation.

A file fails if it is missing, has a different size, or has a different CRC-32.

## Startup

1. With `--data DIR`, the port uses `DIR` as the data folder. It does not check, search, or install.
2. Otherwise, `main` creates `game/` if it is missing and opens the log.
3. The port checks the 10 files in `game/`. If all pass, it does not read a source.
4. Otherwise, it searches for a source and installs from it.

The search runs in every mode, including the turn-based mode.

## Sources

A source is a folder, a ZIP archive, or a CD image that holds all 10 files. The search runs in the working folder in this order:

1. Folders. The search is depth-first in name order (`strcmp`) up to 3 levels; a child of the working folder is level 1. A folder is checked before its subfolders. The folder `game/` at level 1 is skipped. A folder is tried only if it holds at least one of the 10 files at its root. The search enters other folders, but they do not get a log line or a message line.
2. ZIP archives (`.zip`) in the working folder itself, in name order. The root of the game files is found as in [zip.md](zip.md#names).
3. CD images (`.iso`, `.img`, `.bin`) in the working folder itself, in name order. The files are in the root folder of the image ([cd-image.md](cd-image.md#directory-records)).

The extensions are case-insensitive. The install uses the first source whose 10 files all pass the check. A folder source with at least one game file, a ZIP archive, or a CD image that fails gets a line in the log and a line in the message, and the search continues.

## Install

1. The install creates `game/` and `game/DATA` if they are missing.
2. It deletes temporary files (the file name plus `.TMP`) that an earlier install left in `game/`.
3. The source is complete only if all 10 files pass, even if `game/` already holds some of them.
4. For each file in `game/` that is missing or fails, the install writes the file under its temporary name, checks the written file (size and CRC-32), deletes the target, and renames the temporary file to the target. On a mismatch or an error, the install deletes the temporary file.
5. Files in `game/` that pass stay as they are.

The install writes only the 10 fixed names from the table, never a path from the source.

## Message

If no source passes, the port stops with `EXIT_FAILURE` and a message. In the turn-based mode, the message goes to the log only; in the other modes, the port also shows it in a system message box (the game window is not open yet). The message has these parts:

1. A statement that the Dungeon Master II game files are missing.
2. A statement that the user needs their own copy of the PC DOS version (English, 1995).
3. The source forms (folder, `.zip`, `.iso`, `.img`, `.bin`) and the full path of the working folder.
4. If `game/` holds a file that is present but fails, a line for the first such file in the table: `game/<file> is missing or wrong.`
5. One line per failed source: `<path>: <reason>.` The reason is one of: `not a ZIP archive or damaged`; `not a CD image or damaged`; `a MODE2 CD image: the port reads 2048-byte and MODE1 sectors only`; `no DATA/GRAPHICS.DAT in the archive`; `<file> is missing or wrong` (the first failing file in the table).

The message has at most 4095 characters; a longer message ends with `...`. If the port cannot write into `game/`, the message is `cannot write the game files into <path>`.

## Log

Progress goes to the log only: the source path, the failed sources with their first bad file, and `installed N game files`. The log is `os_log`: `dm2.log` in the data folder (`game/` by default) on Windows, and the error output on macOS.

## Invariants

- The install does not change any source file. The readers open a source with `"rb"`.
- After a successful install, each of the 10 files in `game/` passes the check, and `game/` holds no temporary file.
- The install does not change saves, `QUICK.DAT`, or `SETTINGS.TXT`.
- With `--data DIR`, the port neither searches nor writes a game file.
- Two programs that start at the same time in one folder can install at the same time; the port does not prevent this.

## Deviations from the original

None, because the install is only in the port.
