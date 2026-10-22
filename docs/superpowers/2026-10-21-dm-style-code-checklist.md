# Manual test checklist: dm style code refactor

The refactor renames symbols, files, and build targets, gives each parameter name one type, and moves the engine writes of `ui/` into engine functions. The behavior does not change; the automated tests pass. Start `build/dm2 --save game/DATA/SKSAVE0.DAT` or a new game.

## Smoke test

Run the section "Smoke test after the dm style refactor (2026-10-22)" of `docs/manual-test-checklist.md` (R1-R7).

## Areas of the late changes

- [ ] M1. Party order: drag a champion portrait to another cell and drop it. The champions swap cells; the dragged champion faces the party direction.
- [ ] M2. Party order, cancel: start a drag and cancel it (right click or the cancel key). No champion moves.
- [ ] M3. Party order, death: start a drag of a champion in a back cell, let that champion die (for example from a creature attack or a fall), then go on. No champion changes cell; save and load the game and check the party again.
- [ ] M4. Light: light a torch and put it in a hand, then put it away. The dungeon gets lighter and darker as before.
- [ ] M5. Spells: cast a spell with three or four runes. The rune icons and the spell result are as before.
- [ ] M6. Sound and music volume: change both in the options dialog; save, load, and check that the volumes stay.
- [ ] M7. Sleep: put the party to sleep and wake it. The shown skill raises appear after a level up as before.
- [ ] M8. Push: push a creature or an object; the push arrow shows and the walk steps count as before.
- [ ] M9. Minion map and action area: open the minion map, select a minion, close the map; open and close the action area of a champion.
- [ ] M10. Automap (advanced mode): open the automap and use its keys.
- [ ] M11. Text: read a scroll, a wall text, and a shop price; the text and the colors are as before.

## Build

- [ ] B1. `make check` passes on a clean tree.
- [ ] B2. `make windows` builds `build/dm2.exe` with 0 warnings; `build/dm2.exe` starts a new game under Wine or Windows.
