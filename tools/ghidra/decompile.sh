#!/bin/bash
# Decompile a flat 32-bit x86 image (Watcom register calling convention) with Ghidra headless.
# Usage: [SYMBOLS=<symbols.csv>] decompile.sh <image.bin> <base-address> <data-start> <output.c> <entry> [<entry> ...]
set -euo pipefail
GHIDRA=$(brew --prefix ghidra)/libexec
export JAVA_HOME="${JAVA_HOME:-$(brew --prefix openjdk@21)/libexec/openjdk.jdk/Contents/Home}"
out="$(cd "$(dirname "$4")" && pwd)/$(basename "$4")"
proj=$(mktemp -d)
trap 'rm -rf "$proj"' EXIT
"$GHIDRA/support/analyzeHeadless" "$proj" p -import "$1" \
  -loader BinaryLoader -loader-baseAddr "$2" -processor x86:LE:32:default -cspec gcc \
  -scriptPath "$(realpath "$(dirname "$0")")" -preScript Layout.java "$3" "${@:5}" \
  -postScript FindFunctions.java -postScript CreateFunctionAfterTerminals.java \
  -postScript ApplySymbols.java "${SYMBOLS:-}" -postScript Watcall.java -postScript ExportC.java "$out" >"$out.log" 2>&1 \
  || { tail -20 "$out.log"; exit 1; }
