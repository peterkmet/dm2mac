#!/bin/bash
# Starts Dungeon Master II in DOSBox without the intro movies.
# Builds build/dos-nointro with links to every file of game/ except the movie files,
# writes build/dosbox-nointro.conf from tools/dosbox-dm2.conf, and starts DOSBox in the background.
# DATA is a link, so the saves stay shared with game/. Run from the repo root.
set -euo pipefail

dir=build/dos-nointro
mkdir -p "$dir"
for f in game/*; do
    name=$(basename "$f")
    case "$name" in
        INTRO|INTRO.VGA|FTL|FTL.VGA|SPLASH|INTRPLAY.PCX) continue ;;
    esac
    ln -sfn "$PWD/$f" "$dir/$name"
done

sed 's#^mount c game$#mount c build/dos-nointro#' tools/dosbox-dm2.conf > build/dosbox-nointro.conf
/Applications/dosbox.app/Contents/MacOS/DOSBox -conf build/dosbox-nointro.conf &
