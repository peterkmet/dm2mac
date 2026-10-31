#!/bin/bash
# Build build/DM2Mac-macos-arm64.zip: the game and SDL3. The player adds the game files.
set -euo pipefail
cd "$(dirname "$0")/../.."
make
out=build/dist/DM2Mac
rm -rf build/dist build/DM2Mac-macos-arm64.zip
mkdir -p "$out"
cp build/dm2 "$out/dm2"
sdl="$(brew --prefix sdl3)/lib/libSDL3.0.dylib"
cp -L "$sdl" "$out/libSDL3.0.dylib"
chmod u+w "$out/libSDL3.0.dylib"
install_name_tool -id @executable_path/libSDL3.0.dylib "$out/libSDL3.0.dylib"
install_name_tool -change "$(otool -L build/dm2 | awk '/libSDL3/ {print $1}')" @executable_path/libSDL3.0.dylib "$out/dm2"
codesign -f -s - "$out/libSDL3.0.dylib" "$out/dm2"
cat > "$out/README.txt" <<'EOF'
DM2Mac: Dungeon Master II for macOS (Apple Silicon).

The game files are not included. You need your own copy of the PC DOS version of
Dungeon Master II (English, 1995). Copy its folder, its ZIP archive (.zip), or its
CD image (.iso, .img, .bin) into this folder. At the first start the game checks
these 10 files and copies them into game/:
  INTRPLAY.PCX, FTL, INTRO, END, CREDITS, DRUM.BNK, MELODIC.BNK,
  DATA/SONGLIST.DAT, DATA/GRAPHICS.DAT, DATA/DUNGEON.DAT
Other versions of the game do not work: the game names the wrong file.

1. Open Terminal in this folder and remove the quarantine:
   xattr -d com.apple.quarantine dm2 libSDL3.0.dylib
2. Double-click dm2. Terminal opens and runs the game.

The saved games go to game/DATA in this folder.
EOF
(cd build/dist && ditto -c -k --norsrc --keepParent DM2Mac ../DM2Mac-macos-arm64.zip)
rm -rf build/dist
ls -lh build/DM2Mac-macos-arm64.zip
