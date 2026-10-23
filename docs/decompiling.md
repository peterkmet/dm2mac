# Decompiling

How to decompile `SKULL.EXE` of Dungeon Master II (PC DOS) from your own copy of the game with Ghidra. The output is `reference/decomp/SKULL.c`, the reference for the port.

## Prerequisites

- macOS with [Homebrew](https://brew.sh).
- [uv](https://docs.astral.sh/uv/).
- Ghidra and OpenJDK 21 from Homebrew. `tools/ghidra/decompile.sh` uses `brew --prefix ghidra`. It uses `JAVA_HOME` if set, else `brew --prefix openjdk@21`.
- The original game files in `game/`, with `game/SKULL.EXE`. You provide them. The folder is git-ignored.

## Quickstart

1. Install Ghidra. Homebrew also installs OpenJDK 21.

   ```sh
   brew install ghidra
   ```

2. Install the Python tools.

   ```sh
   uv sync
   ```

3. Put the original game files in `game/`.

4. Decompile.

   ```sh
   tools/decompile-skull.sh
   ```

   The script writes `build/skull.bin` (the flat image), `reference/decomp/SKULL.c` (decompiled C), and `reference/decomp/SKULL.c.log` (Ghidra log).

The `.gitignore` file excludes `game/` and `reference/`. Do not commit game content. For the build and the tests, see [Development](development.md).

## Tools

| Command | Purpose |
|---|---|
| `tools/decompile-skull.sh` | Run `dm2-le` and `decompile.sh` for `game/SKULL.EXE`. It uses `docs/reference/skull-symbols.csv` as `SYMBOLS`. |
| `uv run dm2-le game/SKULL.EXE <out.bin>` | Load the LE executable into a flat memory image (`tools/dm2tools/le.py`). Prints one `object N: base ...` line per object and the `entry:` address. The addresses of the image are in the Conventions of the [SKULL.EXE map](reference/skull-map.md). |
| `[SYMBOLS=<csv>] tools/ghidra/decompile.sh <image.bin> <base> <data-start> <out.c> <entry> [<entry> ...]` | Decompile a flat 32-bit x86 image (Watcom register calling convention) with Ghidra headless. |
| `dm2tools.emu` (Python) | Run a function of `SKULL.EXE` in an x86 emulator (`tools/dm2tools/emu.py`). `Emu.call(addr, *args)` runs one function. A hardware access stops the call with `EmuError`; `allow_io=True` permits port access. |
| `tools/dosbox-nointro.sh` | Start the original game in DOSBox without the intro movies, for reference runs. It links every file of `game/` except the movie files into `build/dos-nointro` and uses `tools/dosbox-dm2.conf` (`SB16`, 20000 cycles, `ibmiop skull.exe`). |

## What decompile.sh does

`decompile.sh` imports the image with the `BinaryLoader` at `<base>`, as `x86:LE:32:default`. The code base is the first argument after the image and the data base is `<data-start>`. `decompile-skull.sh` reads both from the `object 1:` and `object 2:` lines of `dm2-le`, and the entry from the `entry:` line.

Ghidra runs these scripts of `tools/ghidra/` in this order:

1. `Layout.java` (before the analysis) splits the image at `<data-start>` into a `code` block (read, execute) and a `data` block (read, write). It marks each `<entry>` as an entry point and a function.
2. `FindFunctions.java` removes the functions that the analysis created outside the `code` block. It creates a function at each 4-byte aligned pointer in the `data` block that points to code outside all functions. It skips a target that starts with `NOP` (`0x90`) or a zero byte.
3. `ApplySymbols.java` reads the CSV file in `SYMBOLS` (columns `address,name,signature,subsystem`, with a header line). A row with a signature in an executable block, or at an instruction, names a function; the script creates the function when needed. Any other row labels a global. The script ignores the signature text. With `SYMBOLS` empty or missing, the script does nothing.
4. `Watcall.java` adds the Watcom calling convention `__watcall` and sets it on every function. The arguments go in `EAX`, `EDX`, `EBX`, `ECX`, then on the stack. The callee removes the stack arguments.
5. `ExportC.java` decompiles all functions to the output file. A function that fails has the line `/* failed: ... */`.

## Known limitations

- The decompiled C does not compile. Some names, types, and calling conventions are incomplete.
- The binary has no symbols. The names come from `docs/reference/skull-symbols.csv`.

## Related pages

- [SKULL.EXE map](reference/skull-map.md)
- [Development](development.md)
