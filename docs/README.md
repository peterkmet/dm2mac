# Documentation

## Players

- [manual.md](manual.md): the options, the modes, the controls, and the cheats.
- [items.md](items.md): the values of every item.

## Developers

- [development.md](development.md): build, tests, checks, and how to add a game rule.
- [architecture.md](architecture.md): the layers, the modules, the frame loop, and the test library.
- [coding-standard.md](coding-standard.md): the coding standard of the C port.
- [manual-test-checklist.md](manual-test-checklist.md): the checks that no test covers.
- [todo.md](todo.md): the open work.
- [CHANGELOG.md](../CHANGELOG.md): the changes.

Data formats:

- [formats/graphics.md](formats/graphics.md): the graphics data files.
- [formats/movies.md](formats/movies.md): the movie files and their player.
- [formats/music.md](formats/music.md): the songs and the HMP player.
- [formats/sound.md](formats/sound.md): the digital sound effects.
- [formats/save.md](formats/save.md): the save files, the quick saves, and the settings file.
- [formats/install.md](formats/install.md): the game files and the install.
- [formats/zip.md](formats/zip.md): the ZIP archive reader.
- [formats/cd-image.md](formats/cd-image.md): the CD image reader.

Game rules:

- [rules/game-flow.md](rules/game-flow.md): the start menu, save and load, dialogs, and the end of the game.
- [rules/movement.md](rules/movement.md): the main loop, the input, and the movement.
- [rules/view.md](rules/view.md): the dungeon view.
- [rules/interface.md](rules/interface.md): the interface panels and the text.
- [rules/rendering.md](rules/rendering.md): the blitters, the layout zones, the font, and the offscreen bitmaps.
- [rules/champions.md](rules/champions.md): the champions, items, money, and shops.
- [rules/creatures.md](rules/creatures.md): the creatures, projectiles, magic, and minions.
- [rules/world.md](rules/world.md): the weather, day and night, light, and the Skullkeep machines.
- [rules/advanced-mode.md](rules/advanced-mode.md): the exact rules of the advanced mode.

## Research of the original

- [decompiling.md](decompiling.md): how to decompile `SKULL.EXE` with Ghidra.
- [reference/skull-map.md](reference/skull-map.md): the map of `SKULL.EXE`: structure, subsystems, and data.
- [reference/skull-symbols.csv](reference/skull-symbols.csv): the original addresses and names.
- [reference/symbols/ranges.md](reference/symbols/ranges.md): the index of the range notes `range-1.md` to `range-8.md`, with the module map.
- [reference/reference-crosscheck.md](reference/reference-crosscheck.md): the comparison with the reDM2 project.
- [reference/runtime.md](reference/runtime.md): the DOS runtime layer and the timing.

## LLM play

- [llm/control-socket.md](llm/control-socket.md): start the game with the control socket and the protocol.
- [llm/turn-based.md](llm/turn-based.md): the commands, turns, and actions of the turn-based mode.
- [llm/llm-playbook.md](llm/llm-playbook.md): the instructions for the LLM player.
- [llm/llm-runs.md](llm/llm-runs.md): how an LLM run works.
