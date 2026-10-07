#!/bin/bash
# Decompile game/SKULL.EXE to reference/decomp/SKULL.c with Ghidra headless.
# docs/skull-symbols.csv, if it exists, names functions and globals.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build reference/decomp
info=$(uv run dm2-le game/SKULL.EXE build/skull.bin)
base=$(echo "$info" | awk '/^object 1:/ {print $4}')
data=$(echo "$info" | awk '/^object 2:/ {print $4}')
entry=0x$(echo "$info" | awk -F: '/^entry:/ {print $3}')
SYMBOLS=docs/skull-symbols.csv tools/ghidra/decompile.sh build/skull.bin "$base" "$data" reference/decomp/SKULL.c "$entry"
