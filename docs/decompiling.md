# Decompiling

`tools/decompile-skull.sh` uses Ghidra to decompile `SKULL.EXE`, the program of Dungeon Master II (PC DOS), from your own copy of the game. The output is `reference/decomp/SKULL.c`, the reference for the port.

## Prerequisites

- macOS with [Homebrew](https://brew.sh).
- [uv](https://docs.astral.sh/uv/).
- Ghidra and OpenJDK 21 from Homebrew. `tools/ghidra/decompile.sh` uses `brew --prefix ghidra`. It uses `JAVA_HOME` if set, otherwise `brew --prefix openjdk@21`.
- [DOSBox](https://www.dosbox.com) as `/Applications/dosbox.app`, only for `tools/dosbox-nointro.sh` ([Development](development.md#tests)).
- Your own copy of the original game files in `game/`, including `game/SKULL.EXE`.

## Quickstart

1. Install Ghidra. Homebrew also installs OpenJDK 21.

   ```sh
   brew install ghidra
   ```

2. Install the Python tools.

   ```sh
   uv sync
   ```

3. Copy the full DOS installation, with `SKULL.EXE`, into `game/`.

4. Decompile.

   ```sh
   tools/decompile-skull.sh
   ```

   The script writes `build/skull.bin` (the flat image), `reference/decomp/SKULL.c` (decompiled C), and `reference/decomp/SKULL.c.log` (Ghidra log).

Git ignores `game/` and `reference/`; do not commit game content. The build and the tests are in [Development](development.md).

## Tools

| Command | Purpose |
|---|---|
| `tools/decompile-skull.sh` | Run `dm2-le` and `decompile.sh` for `game/SKULL.EXE`. It uses `docs/reference/skull-symbols.csv` as `SYMBOLS`. |
| `uv run dm2-le game/SKULL.EXE <out.bin>` | Load the Linear Executable (LE) file into a flat memory image (`tools/dm2tools/le.py`). Prints one `object N: base ...` line per object and the `entry:` address. The image addresses are in the Conventions section of the [SKULL.EXE map](reference/skull-map.md). |
| `[SYMBOLS=<csv>] tools/ghidra/decompile.sh <image.bin> <base> <data-start> <out.c> <entry> [<entry> ...]` | Decompile a flat 32-bit x86 image (Watcom register calling convention) with Ghidra headless. |

## What decompile.sh does

`decompile.sh` imports the image with the `BinaryLoader` at `<base>`, as `x86:LE:32:default`. The code base is `<base>` and the data base is `<data-start>`. `decompile-skull.sh` reads both from the `object 1:` and `object 2:` lines of `dm2-le`, and the entry from the `entry:` line.

Ghidra runs these scripts in order; all except step 3 are in `tools/ghidra/`:

1. `Layout.java` (before the analysis) splits the image at `<data-start>` into a `code` block (read, execute) and a `data` block (read, write). It marks each `<entry>` as an entry point and a function.
2. `FindFunctions.java` removes the functions that the analysis created outside the `code` block. It creates a function at each 4-byte aligned pointer in the `data` block that points to code outside every function. It skips a target that starts with `NOP` (`0x90`) or a zero byte.
3. `CreateFunctionAfterTerminals.java`, a built-in Ghidra script (`Ghidra/Features/Base/ghidra_scripts/`), creates a function at each instruction that follows a terminator (a return) and is not in a function.
4. `ApplySymbols.java` reads the CSV file in `SYMBOLS` (columns `address,name,signature,subsystem`, with a header line). A row at an instruction, or a row with a signature in an executable block, names a function; the script creates the function if needed. Every remaining row labels a global. The script ignores the signature text. If `SYMBOLS` is empty or missing, the script does nothing.
5. `Watcall.java` adds the Watcom calling convention `__watcall` and sets it on every function. The arguments go in `EAX`, `EDX`, `EBX`, `ECX`, then on the stack. The callee removes the stack arguments.
6. `ExportC.java` decompiles all functions to the output file. A function that fails to decompile gets the line `/* failed: ... */`.

## Known limitations

- The decompiled C does not compile. Some names, types, and calling conventions are incomplete.
- The binary has no symbols. The names come from `docs/reference/skull-symbols.csv`.
