#!/bin/bash
# Build build/DM2Mac-macos-arm64.zip: the application DM2Mac.app with SDL3. The player adds the game files.
set -euo pipefail
cd "$(dirname "$0")/../.."
make
out=build/dist/DM2Mac
app="$out/DM2Mac.app/Contents"
rm -rf build/dist build/DM2Mac-macos-arm64.zip
mkdir -p "$app/MacOS" "$app/Frameworks"
cp build/dm2 "$app/MacOS/dm2"
sdl="$(brew --prefix sdl3)/lib/libSDL3.0.dylib"
cp -L "$sdl" "$app/Frameworks/libSDL3.0.dylib"
chmod u+w "$app/Frameworks/libSDL3.0.dylib"
install_name_tool -id @executable_path/../Frameworks/libSDL3.0.dylib "$app/Frameworks/libSDL3.0.dylib"
install_name_tool -change "$(otool -L build/dm2 | awk '/libSDL3/ {print $1}')" \
    @executable_path/../Frameworks/libSDL3.0.dylib "$app/MacOS/dm2"
minimum="$(otool -l build/dm2 | awk '/minos/ {print $2; exit}')"
cat > "$app/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>dm2</string>
  <key>CFBundleIdentifier</key><string>com.github.peterkmet.dm2mac</string>
  <key>CFBundleName</key><string>DM2Mac</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>$minimum</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
plutil -lint "$app/Info.plist"
codesign -f -s - "$app/Frameworks/libSDL3.0.dylib"
codesign -f -s - "$out/DM2Mac.app"
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
   xattr -dr com.apple.quarantine DM2Mac.app
2. Double-click DM2Mac.app.

The game keeps its files in the folder game/ in this folder: the game files, the
saved games, the settings, and the messages in dm2.log. To remove the game,
delete this folder.
EOF
(cd build/dist && ditto -c -k --norsrc --keepParent DM2Mac ../DM2Mac-macos-arm64.zip)
rm -rf build/dist
ls -lh build/DM2Mac-macos-arm64.zip
