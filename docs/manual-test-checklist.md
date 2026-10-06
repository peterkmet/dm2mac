# Manual test checklist

Use this checklist for the manual test of the C port (workflow step 6, plan Step 21). Compare the port with the DOS version in DOSBox side by side. Each item gives an action and the expected result. Check the box when the result matches. Write down each mismatch with the item number, the place in the game, and a screenshot.

## Prerequisites

- macOS with SDL3 from Homebrew: `brew install sdl3`.
- The game files of Dungeon Master II (PC DOS, English, 1995) in `game/`, with `game/DATA/GRAPHICS.DAT`, `game/DATA/DUNGEON.DAT`, `game/DATA/SONGLIST.DAT`, `game/MELODIC.BNK`, and `game/DRUM.BNK`.
- DOSBox with the folder `game/` mounted as drive `C:`.
- `make -C port` and `uv run pytest` pass (plan Step 21).
- Optional: a DOS save near the end of the game, for the end sensor (item J16).

## Build and start

1. Build the program and the test library:

   ```sh
   make -C port
   ```

   Expected result: `build/dm2` and `build/libdm2.dylib`, no warnings.

2. Start the port with the start menu:

   ```sh
   build/dm2
   ```

3. Start the DOS version in DOSBox:

   ```text
   C:
   IBMIOP SKULL.EXE
   ```

Command-line options of `build/dm2`:

| Option | Default | Effect |
|---|---|---|
| `--data DIR` | `game` | Folder with the game files. Saves go to `DIR/DATA/SKSAVEn.DAT` (slots 0 to 9) and `.BAK`. |
| `--save PATH` | none | Loads the save `PATH` and starts its game at once, without the start menu and without the dialog after a load. |
| `--filter NAME` | `none` | Upscaling filter: `none`, `scale2x`, or `scale3x`. The `DM2_FILTER` environment variable sets the same value. The option wins. |

The port and DOSBox use the same save files when both use `game/`. Example: start the port with the test save of the plan.

```sh
build/dm2 --save tests/saves/start.DAT
```

## A. Program start and window

- [ ] A1. Run `build/dm2`. Expected: a window at scale 3 shows the start menu title; the log shows `data folder: game`.
- [ ] A2. Run `build/dm2 --filter scale2x`, then `--filter scale3x`. Expected: the image is smoothed by the filter; the game is the same.
- [ ] A3. Run `DM2_FILTER=scale2x build/dm2 --filter none`. Expected: no filter (the option wins).
- [ ] A4. Run `build/dm2 --filter bad`. Expected: an error message and exit code 2.
- [ ] A5. Run `build/dm2 --data /tmp`. Expected: the message `cannot read /tmp/DATA/GRAPHICS.DAT` and exit code 1.
- [ ] A6. Start with `--data` on a copy of `game/` without `MELODIC.BNK`. Expected: the log says the port plays without music; the game runs with sound effects.
- [ ] A7. Close the window during the game. Expected: the program ends without a crash.

## B. Image

### Dungeon view

- [ ] B1. Load `tests/saves/start.DAT` in both (`build/dm2 --save tests/saves/start.DAT`). Expected: the dungeon view at the party position is equal: walls, floor, ceiling, doors, stairs, pits, ornaments (plan Step 12a).
- [ ] B2. Look at squares with objects and creatures in the view. Expected: objects, creatures, and their possessions are at the same places and sizes (plan Step 12b).
- [ ] B3. Watch creatures that move. Expected: the animation frames, mirror, and random shift look like the DOS version.
- [ ] B4. Look at a visible teleporter. Expected: the sparkle field shows as in DOS.
- [ ] B5. Look at alcoves and shelves with objects. Expected: the objects stand in the alcove picture as in DOS.
- [ ] B6. Look at missiles and explosions (thrown objects, spells). Expected: the pictures, sizes, and the tint of an explosion on the party square are as in DOS.
- [ ] B7. Walk forward one square. Expected: the walking frame stretches to the full viewport as in DOS (plan Step 14: the rounding of the stretch is unverified; compare it closely).
- [ ] B8. Step into an open pit. Expected: the fall view shows, then the view of the map below.
- [ ] B9. Walk into a dark area without a light. Expected: the view darkens by the same steps as in DOS.

### Outdoor view and weather

- [ ] B10. Go outdoors. Expected: the sky, the sky objects, and the outdoor walls are as in DOS.
- [ ] B11. Wait for rain. Expected: the rain shows with the same picture and wind direction; wet ornaments show rain in their background.
- [ ] B12. Wait for clouds and a storm. Expected: clouds, lightning flashes, and lightning strikes show as in DOS.
- [ ] B13. Wait until the hour changes outdoors (day and night). Expected: the light level outdoors changes at the hour as in DOS.

### Interface panels

- [ ] B14. Compare the screen after a load. Expected: champion status boxes, bars, portraits, hands, movement arrows, and the action area are equal (plan Step 13).
- [ ] B15. Open the inventory of each champion. Expected: slots, statistics, skills, food and water bars, and the mouth picture are equal.
- [ ] B16. Put the eye on a weapon, a piece of clothing, a scroll, a potion, a chest, and a money box. Expected: the object information panel is equal.
- [ ] B17. Open the spell panel and enter runes. Expected: the rune sets and the entered runes are equal.
- [ ] B18. Fill the message area with more than four lines. Expected: the text rows scroll up by 7 pixels as in DOS (plan Step 13: unverified port behavior; confirm or report).
- [ ] B19. Wait without new messages. Expected: old message rows clear after the same time as in DOS.

## C. Input and movement

- [ ] C1. Press the arrow keys and the numeric keypad keys. Expected: the party walks forward, back, left, right and turns left and right as in DOS.
- [ ] C2. Press Shift with the Left or Right arrow. Expected: the party steps sideways.
- [ ] C3. Click each of the six movement arrows. Expected: the arrow shows the pressed picture; the party moves or turns.
- [ ] C4. Hold a movement key. Expected: the move speed (move cooldown) is the same as in DOS.
- [ ] C5. Walk into a wall. Expected: the party does not move; the champions on the wall side take damage; a bump sound plays.
- [ ] C6. Press Esc. Expected: the game pauses; the next key or click resumes it; the music pauses too.
- [ ] C7. Click the sleep button. Expected: the party sleeps (the sleep message shows, time runs faster); a key or click wakes the party.
- [ ] C8. Click a wall button and a lever. Expected: the click feedback shows and the sensor acts as in DOS.
- [ ] C9. Click the champion hands and the action icons. Expected: the action menu opens; the choice runs.
- [ ] C10. Compare the game speed with DOSBox for one minute. Expected: the game time runs at the same rate (a game step every 8 timer ticks at 60 Hz; `docs/skull-formats.md` section 6 marks the real DOS rate as an open measurement).

## D. Champions, inventory, items, money, and shops

- [ ] D1. Take an object from the floor and from a table. Expected: the object shows at the mouse pointer; its name shows in the message area.
- [ ] D2. Place the object on another cell, drop it, and throw it. Expected: the object lands at the same place as in DOS; a thrown object flies.
- [ ] D3. Put objects into inventory slots and take them out. Expected: only fitting slots accept an object; the load and the statistics change.
- [ ] D4. Eat food and drink from a flask or bota. Expected: the food and water bars rise; empty containers stay.
- [ ] D5. Drink potions. Expected: the effect is as in DOS (health, stamina, mana, statistics).
- [ ] D6. Run hand actions with a weapon. Expected: the action name, the busy hand, and the reload countdown are as in DOS.
- [ ] D7. Recruit a champion at a mirror; cancel at the next mirror. Expected: the candidate panel, the join, and the cancel are as in DOS.
- [ ] D8. Push the table of the first room south. Expected: the table moves (plan Step 15 scenario).
- [ ] D9. Put the solid key into the keyhole and press the door button. Expected: the door of the first room opens (the gold coin opens no door, as in DOS).
- [ ] D10. Open a money box; put coins in and take coins out. Expected: the coin counts change as in DOS.
- [ ] D11. Look at each shop display. Expected: the item, its name, and the prices in coins are as in DOS.
- [ ] D12. Buy an item from a merchant and pay with coins. Expected: the merchant takes the payment and gives change as in DOS.
- [ ] D13. Hold a torch until it burns down. Expected: the light falls; the torch burns out.
- [ ] D14. Let a champion get poisoned and cure the poison. Expected: the health loss and the cure are as in DOS.
- [ ] D15. Let a champion die; take the bones to an altar. Expected: the dead champion's objects lie on the square; the altar brings the champion back.

## E. Creatures and combat

- [ ] E1. Meet creatures. Expected: they move, turn, and attack as in DOS.
- [ ] E2. Attack a creature with a melee weapon. Expected: damage numbers, creature reaction, and death (possessions drop) are as in DOS.
- [ ] E3. Push a creature with a movement arrow (the push arrow in the view). Expected: the push succeeds or fails as in DOS.
- [ ] E4. Let a creature attack the party. Expected: the damage goes to the champions of the attacked side; a sleeping party wakes.
- [ ] E5. Stand in front of a missile launcher. Expected: the missiles fly and hit the party as in DOS.
- [ ] E6. Use the minion map of a minion item. Expected: the map panel, the icons, and the mana drain are as in DOS.

## F. Magic

- [ ] F1. Cast a fireball, a lightning bolt, and a poison cloud. Expected: the missiles, the explosions, and the damage are as in DOS.
- [ ] F2. Cast a light spell. Expected: the view becomes brighter; the light fades with time.
- [ ] F3. Cast a party aura spell (shield, invisibility). Expected: the aura shows in the status boxes and ends after the same time.
- [ ] F4. Summon a minion. Expected: the minion appears and acts as in DOS.
- [ ] F5. Enter a rune set that is no spell. Expected: the failure message is as in DOS.
- [ ] F6. Cast with too little mana. Expected: the cast fails as in DOS.

## G. World

- [ ] G1. Open and close a door with its button. Expected: the door animation and its sound are as in DOS.
- [ ] G2. Break a door with a weapon or a spell. Expected: the broken door picture shows as in DOS.
- [ ] G3. Step into an open pit. Expected: the party falls to the map below and takes damage.
- [ ] G4. Step onto an active teleporter. Expected: the party arrives at the same place and direction as in DOS.
- [ ] G5. Take stairs up and down. Expected: the party arrives on the next map with the same direction as in DOS.
- [ ] G6. Step on a floor pressure plate and put an object on it. Expected: the target (door, pit, wall) changes as in DOS.
- [ ] G7. Walk through a fake wall. Expected: the party passes.
- [ ] G8. Put an object onto a Skullkeep machine (object transfer sensor). Expected: the object moves to the same place as in DOS.
- [ ] G9. Stay outdoors for a full day. Expected: day and night change; the outdoor light and sky follow the same hours as in DOS.

## H. Sound effects

- [ ] H1. Start a game from the start menu. Expected: the test sound plays at the game start (plan Step 18).
- [ ] H2. Open a door at the left and at the right of the party. Expected: the sound comes from the correct side (plan Step 18).
- [ ] H3. Walk into a wall. Expected: the bump sound plays (plan Step 18).
- [ ] H4. Wait for a storm with thunder. Expected: the thunder plays from the correct side and after a delay (plan Step 18).
- [ ] H5. Fight a creature. Expected: the attack and hit sounds play at the same moments as in DOS.
- [ ] H6. Stand near an animated wall ornament with a sound. Expected: the ambient sound plays as in DOS.
- [ ] H7. Let the party sleep near a sound. Expected: the sound is quieter.
- [ ] H8. Set the sound volume to 0 in the volume dialog, then to 7. Expected: 0 is silent; a new value plays the test sound.

## I. Music

- [ ] I1. Start a game. Expected: the music plays from the first game step; the start menu plays no song (plan Step 19).
- [ ] I2. Go to a map with another song. Expected: the old song fades out; the new song starts (plan Step 19).
- [ ] I3. Set the music volume to 0 in the volume dialog, then to a higher value. Expected: 0 stops the music; a higher value starts it again (plan Step 19).
- [ ] I4. Listen to a song in the port and in DOSBox (Sound Blaster 16 with OPL). Expected: the same notes, instruments, and tempo.
- [ ] I5. Let a song play to its end. Expected: the song loops.

## J. Start menu, new game, save and load, end of game

- [ ] J1. Run `build/dm2`. Expected: the start menu shows the title and its options.
- [ ] J2. Choose the picture option. Expected: the picture shows until a click or until the time ends.
- [ ] J3. Choose a new game. Expected: the party starts on the start square with the champion of the start mirror as leader.
- [ ] J4. Recruit more champions and enter the game. Expected: the party has up to four champions (PRD Epic 5).
- [ ] J5. Press Option+S (Alt+S) to open the save menu. Expected: the dialog with SAVE, QUIT, RESTART, RESUME.
- [ ] J6. Save into an empty slot and type a name. Expected: the slot shows the name; `game/DATA/SKSAVEn.DAT` exists.
- [ ] J7. Quit, start `build/dm2`, and load the save. Expected: "LOADING GAME...", then "GAME LOADED, READY TO PLAY"; the game continues in the same state (PRD Epic 5).
- [ ] J8. Save with an object in the leader hand, a magic light, and an aura; load. Expected: the object is back in the hand; the light and the aura continue.
- [ ] J9. Choose QUIT more than 100 ticks after the last save. Expected: the program asks before the quit; then the end picture shows until a click.
- [ ] J10. Choose RESTART. Expected: the program starts again with the start menu.
- [ ] J11. Open the volume dialog. Expected: the sound and music volumes go from 0 to 7.
- [ ] J12. Rename a champion with the name editor. Expected: the keys edit the name and the title as in DOS.
- [ ] J13. Let the whole party die. Expected: the scream, then "THE END"; RESTART GAME loads the last save; QUIT GAME ends with exit code 2.
- [ ] J14. Rename `SKSAVE0.DAT` and keep `SKSAVE0.BAK`; load slot 0. Expected: the port loads the `.BAK` file and renames it back.
- [ ] J15. Load a damaged save. Expected: "THE GAME IS DAMAGED!"; no crash.
- [ ] J16. Optional: load a save near the end and complete the final task (end sensor). Expected: the screen goes black and the program ends with exit code 1 (PRD Epic 5; the ending movie is out of scope).

## K. DOS cross-checks

- [ ] K1. Run the port and DOSBox side by side on the same save. Expected: the image is equal for the start view, a walk of ten squares, and the inventory.
- [ ] K2. Compare the sound effects of K1 side by side. Expected: the same sounds at the same moments.
- [ ] K3. Compare the music side by side. Expected: the same song and the same tempo.
- [ ] K4. Compare the input side by side: the same keys and clicks. Expected: the same results.
- [ ] K5. Save in the port, then load the slot in DOS. Expected: DOS loads the game; the party, champions, and objects are equal.
- [ ] K6. Save in DOS, then load the slot in the port. Expected: the port loads the game; the party, champions, and objects are equal.
- [ ] K7. After K5 and K6, play one minute in each. Expected: no crash and no visible difference in the game state.

## Known deviations

Do not report these documented deviations as bugs. Details are in `docs/skull-formats.md`.

- No intro, ending movie, or credits; a completed game ends the program with exit code 1 (PRD scope; section 14).
- No DOS layer: no disk, drive, or memory dialogs; menus and dialogs wait without busy loops (section 14).
- `--save PATH` starts the game at once, without the start menu and without the dialog after a load (section 14).
- RESTART starts the program again; the dialog texts show the `--data` folder as the drive path (section 14).
- The leader hand object shows at the mouse pointer, not as the system cursor; menus and dialogs show no object icon (section 13).
- The mouth or eye held with an empty hand is not ported (section 13).
- No cursor shape changes and no mouse drag through a fake wall (section 10).
- The walking frame stretch uses nearest neighbour; its exact rounding is unverified (section 10).
- The pit fall view shows at the next screen update, not in the middle of the step (section 10).
- The message area scroll by 7 pixels is a port reconstruction (section 9).
- Sound: the pan curve is a linear balance, not the unknown SOS curve; the mix runs at 44100 Hz; the pan overflow of the original near the center is fixed; every sound counts as loaded, so a sound can play where DOS has not loaded it (sections 1 and 11).
- Music: Nuked-OPL3 in OPL2 mode, the same signal on both sides (section 5, "Deviations of the C port (Step 19)").
- Fixed technical bugs of the original (uninitialized values, reads past tables, divisions by 0, endless loops in the shop display) can make a rare case differ from DOS (sections 8, 11, 12, 13, 14).
- A creature AI keeps its plan data; the DOS image cache can make a group plan again (section 12).
- With no free active group, a creature group stays inactive; DOS discards a group instead (section 12).
- No SVGA mode: the SVGA option changes only the movies (section 3).
- The real game rate of DOS is an open measurement (section 6).
