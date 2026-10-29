#!/bin/bash
# Build build/DM2Mac-windows-x64.zip: the Windows game (x86-64) and SDL3. The player adds the game files.
# DM_DIR (environment) names the dm folder that holds the SDL3 MinGW package.
set -euo pipefail
cd "$(dirname "$0")/../.."
make windows
sdl_version="$(awk -F' = ' '/^SDL_MINGW_VERSION = / {print $2}' Makefile)"
out=build/dist-win/DM2Mac
rm -rf build/dist-win build/DM2Mac-windows-x64.zip
mkdir -p "$out"
x86_64-w64-mingw32-strip -o "$out/dm2.exe" build/dm2.exe
cp "build/sdl-mingw/SDL3-$sdl_version/x86_64-w64-mingw32/bin/SDL3.dll" "$out/SDL3.dll"
cat > "$out/README.txt" <<'EOF'
DM2Mac: Dungeon Master II for Windows (64-bit, Windows 10 or later).

The game files are not included. You need your own copy of the PC DOS version of
Dungeon Master II (English, 1995). Copy its folder, its ZIP archive (.zip), or its
CD image (.iso, .img, .bin) into this folder. At the first start the game checks
these 10 files and copies them into game\:
  INTRPLAY.PCX, FTL, INTRO, END, CREDITS, DRUM.BNK, MELODIC.BNK,
  DATA\SONGLIST.DAT, DATA\GRAPHICS.DAT, DATA\DUNGEON.DAT
Other versions of the game do not work: the game names the wrong file.

Double-click dm2.exe. Windows can warn about an unknown publisher: click "More info",
then "Run anyway". A problem at the start shows a message box; dm2.log in game\
holds the messages of the game.

The turn-based mode is not available on Windows.

The saved games go to game\DATA in this folder.
EOF
sed -i '' 's/$/\r/' "$out/README.txt"
(cd build/dist-win && zip -qr ../DM2Mac-windows-x64.zip DM2Mac)
rm -rf build/dist-win
ls -lh build/DM2Mac-windows-x64.zip
