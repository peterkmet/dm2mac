# Manual test checklist

Use this checklist for the manual test of the C port. Use DOSBox only for the items that name DOS or DOSBox. Each item gives an action and the expected result. Check the box when the result matches. Write down each mismatch with the item number, the place in the game, and a screenshot.

## Prerequisites

- macOS with SDL3 from Homebrew: `brew install sdl3`.
- For the Windows build (section W): the tools and Wine settings in [Windows build](development.md#windows-build).
- The game files of Dungeon Master II (PC DOS, English, 1995) in `game/` ([Run the game](manual.md#run-the-game)).
- [DOSBox](development.md#tools) with the folder `game/` mounted as drive `C:`, for the items C10, I4, and K2 to K7.
- `make` and `uv run pytest` pass.
- The default mode is `advanced`. Use `--mode vanilla` for items A to K that compare the port with the original ([Modes](manual.md#modes)).
- Do not report a documented deviation as a bug: see the sections "Deviations from the original" of the pages in `docs/formats/`, `docs/rules/`, and [turn-based.md](llm/turn-based.md#deviations-from-the-original).
- Optional: a DOS save near the end of the game, for the end sensor (item J16).

## Build and start

1. Build and start the port as in [Build](development.md#build). Expected result: no warnings; `build/dm2` and `build/libdm2.dylib`; for `make windows`, `file build/dm2.exe` says `PE32+ executable (GUI) x86-64`. The port shows the mode screen, the splash picture, the movies `FTL` and `INTRO`, then the start menu.

2. Start the DOS version in DOSBox:

   ```text
   C:
   IBMIOP SKULL.EXE
   ```

   To start DOSBox without the intro movies, run `tools/dosbox-nointro.sh` from the repo root. The script uses the same `game/DATA` folder, so the saves stay shared with the port.

Command-line options of `build/dm2`: see [Options](manual.md#options).

The port and DOSBox use the same save files when both use `game/`. Example: start the port with a test save.

```sh
build/dm2 --save tests/saves/start.DAT
```

An item with "Covered by" has no box: automated tests check it against the original, so it needs no manual run.

## A. Program start and window

- [ ] A1. Run `build/dm2 --mode vanilla`. Expected: a window at scale 3 shows the splash picture and the movies, then the start menu title; the log shows `data folder: game`.
- [ ] A2. Run `build/dm2 --filter scale2x`, then `--filter scale3x`. Expected: the image is smoothed by the filter; the game is the same.
- [ ] A3. Run `DM2_FILTER=scale2x build/dm2 --filter none`. Expected: no filter (the option wins).
- [ ] A4. Run `build/dm2 --filter bad`. Expected: an error message and exit code 2.
- [ ] A5. Run `build/dm2 --data /tmp`. Expected: the message `cannot read /tmp/DATA/GRAPHICS.DAT` and exit code 1.
- [ ] A6. Start with `--data` on a copy of `game/` without `MELODIC.BNK`. Expected: the log says the port plays without music; the game runs with sound effects.
- [ ] A7. Close the window during the game. Expected: the program ends without a crash.

## B. Image

### Dungeon view

- B1. Load `tests/saves/start.DAT` in both (`build/dm2 --save tests/saves/start.DAT`). Expected: the dungeon view at the party position is equal: walls, floor, ceiling, doors, stairs, pits, ornaments. Covered by `tests/differential/test_view.py::test_view_picture_matches_original`, `tests/differential/test_view.py::test_view_squares_on_all_maps_match_original`.
- B2. Look at squares with objects and creatures in the view. Expected: objects, creatures, and their possessions are at the same places and sizes. Covered by `tests/differential/test_view.py::test_thing_grids_match_original`, `tests/differential/test_view.py::test_added_missiles_explosions_creatures_match_original`.
- B3. Watch creatures that move. Expected: the animation frames, mirror, and random shift look like the DOS version. Covered by `tests/differential/test_creature.py::test_animated_creature_views`, `tests/differential/test_view.py::test_added_missiles_explosions_creatures_match_original`.
- B4. Look at a visible teleporter. Expected: the sparkle field shows as in DOS. Covered by `tests/differential/test_view.py::test_view_picture_matches_original`.
- B5. Look at alcoves and shelves with objects. Expected: the objects stand in the alcove picture as in DOS. Covered by `tests/differential/test_view.py::test_view_picture_matches_original`.
- B6. Look at missiles and explosions (thrown objects, spells). Expected: the pictures, sizes, and the tint of an explosion on the party square are as in DOS. Covered by `tests/differential/test_view.py::test_added_missiles_explosions_creatures_match_original`.
- [ ] B7. Walk forward one square. Expected: the walking frame fills the full viewport; the step shows no visible jump (the rounding of the stretch is unverified). The walk rectangle is covered by `tests/differential/test_view.py::test_walking_view_matches_original`.
- B8. Step into an open pit. Expected: the fall view shows, then the view of the map below. Covered by `tests/differential/test_world.py::test_pit_falls_match_original`.
- B9. Walk into a dark area without a light. Expected: the view darkens by the same steps as in DOS. Covered by `tests/differential/test_world.py::test_party_light_matches_original`, `tests/differential/test_view.py::test_view_picture_matches_original`.

### Outdoor view and weather

- B10. Go outdoors. Expected: the sky, the sky objects, and the outdoor walls are as in DOS. Covered by `tests/differential/test_view.py::test_outdoor_sky_matches_original`.
- B11. Wait for rain. Expected: the rain shows with the same picture and wind direction; wet ornaments show rain in their background. Covered by `tests/differential/test_view.py::test_outdoor_sky_matches_original`, `tests/differential/test_view.py::test_wet_image_matches_original`, `tests/differential/test_world.py::test_weather_steps_on_outdoor_maps`.
- B12. Wait for clouds and a storm. Expected: clouds, lightning flashes, and lightning strikes show as in DOS. Covered by `tests/differential/test_world.py::test_weather_steps_on_outdoor_maps`, `tests/differential/test_world.py::test_storm_steps_match_original`, `tests/differential/test_world.py::test_lightning_strikes_match_original`.
- B13. Wait until the hour changes outdoors (day and night). Expected: the light level outdoors changes at the hour as in DOS. Covered by `tests/differential/test_world.py::test_weather_steps_on_outdoor_maps`, `tests/differential/test_world.py::test_party_light_matches_original`.

### Interface panels

- B14. Compare the screen after a load. Expected: champion status boxes, bars, portraits, hands, movement arrows, and the action area are equal. Covered by `tests/differential/test_interface.py::test_game_screen_matches_original`, `tests/differential/test_interface.py::test_party_states_match_original`.
- B15. Open the inventory of each champion. Expected: slots, statistics, skills, food and water bars, and the mouth picture are equal. Covered by `tests/differential/test_interface.py::test_inventory_matches_original`, `tests/differential/test_interface.py::test_inventory_panels_match_original`.
- B16. Put the eye on a weapon, a piece of clothing, a scroll, a potion, a chest, and a money box. Expected: the object information panel is equal. Covered by `tests/differential/test_interface.py::test_inventory_panels_match_original`.
- [ ] B16a. In the inventory, hold the left mouse button down on the eye, with an empty hand and with an object in the hand. Expected: the skills and statistics panel or the object information panel shows, and the pointer hides, while the button is down. When the button is released, the normal panel shows again. A short click shows nothing. The real click path is covered by `tests/port/test_control.py::test_eye_and_mouth_hold_while_the_left_button_is_down`.
- B17. Open the spell panel and enter runes. Expected: the rune sets and the entered runes are equal. Covered by `tests/differential/test_interface.py::test_action_area_panels_match_original`, `tests/differential/test_magic.py::test_runes_and_casts_match_original`.
- [ ] B18. Fill the message area with more than three lines. Expected: the rows move up by one row; no part of an old row stays. The 7-pixel move is a port reconstruction; `tests/differential/test_interface.py::test_message_area_scrolls` checks it in the port only.
- B19. Wait without new messages. Expected: old message rows clear after the same time as in DOS. Covered by `tests/differential/test_interface.py::test_message_area_matches_original`.

## C. Input and movement

- [ ] C1. Press the arrow keys and the numeric keypad keys on the Mac keyboard. Expected: the party walks forward, back, left, right and turns left and right. The commands of the keys are covered by `tests/differential/test_input.py::test_clicks_and_keys_match_original`, `tests/differential/test_input.py::test_keys_walk_and_turn_in_the_main_loop`.
- [ ] C2. Press Shift with the Left or Right arrow on the Mac keyboard. Expected: the party steps sideways. The commands of the keys are covered by `tests/differential/test_input.py::test_clicks_and_keys_match_original`.
- [ ] C3. Click each of the six movement arrows. Expected: the arrow shows the pressed picture. The moves and the click delays are covered by `tests/differential/test_input.py::test_clicks_on_the_movement_arrows`, `tests/differential/test_input.py::test_arrow_clicks_and_feedback_delays_match_original`.
- [ ] C4. Hold a movement key. Expected: the party keeps walking at an even speed; no step is lost or doubled. The move cooldowns are covered by `tests/differential/test_input.py::test_keys_and_move_cooldowns_match_original`.
- [ ] C5. Walk into a wall. Expected: the bump sound plays. The party position and the damage to the champions of the wall side are covered by `tests/differential/test_move.py::test_bump_damage_hits_the_champions_of_the_bump_side`.
- [ ] C6. Press Esc, then a key. Expected: the music stops while the game is paused and continues after it. The pause of the game is covered by `tests/differential/test_input.py::test_keys_and_move_cooldowns_match_original`, `tests/differential/test_music.py::test_pause_until_next_game_step`.
- C7. Click the sleep button. Expected: the party sleeps (the sleep message shows, time runs faster); a key or click wakes the party. Covered by `tests/differential/test_input.py::test_sleep_and_wake_match_original`, `tests/differential/test_interface.py::test_party_states_match_original`.
- C8. Click a wall button and a lever. Expected: the click feedback shows and the sensor acts as in DOS. Covered by `tests/differential/test_move.py::test_wall_clicks_on_all_wall_cells`, `tests/differential/test_move.py::test_square_events_on_all_sensors`.
- C9. Click the champion hands and the action icons. Expected: the action menu opens; the choice runs. Covered by `tests/differential/test_input.py::test_champion_clicks_match_original`, `tests/differential/test_interface.py::test_action_area_panels_match_original`, `tests/differential/test_interface.py::test_action_results_match_original`, `tests/differential/test_magic.py::test_icon_click_feedback_matches_original`.
- [ ] C10. Compare the game speed with DOSBox for one minute. Expected: the game time runs at the same rate (a game step every 8 timer ticks at 60 Hz; [Unknown values](reference/runtime.md#unknown-values) marks the real DOS rate as an open measurement).

## D. Champions, inventory, items, money, and shops

- D1. Take an object from the floor and from a table. Expected: the object shows at the mouse pointer; its name shows in the message area. Covered by `tests/differential/test_champions.py::test_coin_from_the_table`, `tests/differential/test_champions.py::test_money_box_flags_in_the_view`, `tests/differential/test_input.py::test_cursor_matches_original`, `tests/differential/test_interface.py::test_hand_icon_matches_original`.
- D2. Place the object on another cell, drop it, and throw it. Expected: the object lands at the same place as in DOS; a thrown object flies. Covered by `tests/differential/test_champions.py::test_money_box_flags_in_the_view`, `tests/differential/test_projectile.py::test_champions_throw`, `tests/differential/test_projectile.py::test_thrown_objects`.
- [ ] D3. Open the inventory of a champion with the keys 1 to 4 (there is no F-key). Put objects into inventory slots and take them out. Expected: only fitting slots accept an object; the load and the statistics change.
- D4. Eat food and drink from a flask or bota. Expected: the food and water bars rise; empty containers stay. Covered by `tests/differential/test_champion_rules.py::test_eating_drinking_and_actions_match_original`, `tests/differential/test_champions.py::test_drink_and_hand_actions`.
- D5. Drink potions. Expected: the effect is as in DOS (health, stamina, mana, statistics). Covered by `tests/differential/test_champion_rules.py::test_eating_drinking_and_actions_match_original`.
- D6. Run hand actions with a weapon. Expected: the action name, the busy hand, and the reload countdown are as in DOS. Covered by `tests/differential/test_champions.py::test_drink_and_hand_actions`, `tests/differential/test_champion_rules.py::test_eating_drinking_and_actions_match_original`, `tests/differential/test_interface.py::test_action_results_match_original`.
- [ ] D7. Recruit a champion at a mirror; cancel at the next mirror. Expected: the candidate panel shows the portrait and the statistics of the candidate. The join and the cancel are covered by `tests/differential/test_champions.py::test_recruit_and_cancel`.
- D8. In the first room, push the movable object south. Expected: it moves. Covered by `tests/differential/test_champions.py::test_door_out_of_the_first_room`.
- D9. Open the door of the first room with its key. Expected: the door opens; a coin does not open it, as in DOS. Covered by `tests/differential/test_champions.py::test_door_out_of_the_first_room`, `tests/differential/test_champions.py::test_coin_from_the_table`.
- D10. Open a money box; put coins in and take coins out. Expected: the coin counts change as in DOS. Covered by `tests/differential/test_champion_rules.py::test_money_and_prices_match_original`, `tests/differential/test_champions.py::test_money_box_flags_in_the_view`, `tests/differential/test_interface.py::test_containers_in_hand_match_original`.
- D11. Look at each shop display. Expected: the item, its name, and the prices in coins are as in DOS. Covered by `tests/differential/test_view.py::test_shop_displays_match_original`.
- D12. Buy an item from a merchant and pay with coins. Expected: the merchant takes the payment and gives change as in DOS. Covered by `tests/differential/test_ai_move.py::test_merchant_commands`, `tests/differential/test_champion_rules.py::test_money_and_prices_match_original`.
- D13. Hold a torch until it burns down. Expected: the light falls; the torch burns out. Covered by `tests/differential/test_champion_rules.py::test_damage_rules_match_original`, `tests/differential/test_world.py::test_party_light_matches_original`.
- D14. Let a champion get poisoned and cure the poison. Expected: the health loss and the cure are as in DOS. Covered by `tests/differential/test_champion_rules.py::test_damage_rules_match_original`, `tests/differential/test_champion_rules.py::test_eating_drinking_and_actions_match_original`.
- [ ] D15. Take the bones of a dead champion to an altar. Expected: the altar brings the champion back. The death and the objects on the square are covered by `tests/differential/test_champion_rules.py::test_damage_rules_match_original`.

## E. Creatures and combat

- E1. Meet creatures. Expected: they move, turn, and attack as in DOS. Covered by `tests/differential/test_creature.py::test_creature_updates`, `tests/differential/test_ai_move.py::test_try_move`, `tests/differential/test_ai_move.py::test_turn_toward`, `tests/differential/test_ai_move.py::test_try_attack`.
- E2. Attack a creature with a melee weapon. Expected: damage numbers, creature reaction, and death (possessions drop) are as in DOS. Covered by `tests/differential/test_champion_rules.py::test_eating_drinking_and_actions_match_original`, `tests/differential/test_interface.py::test_action_results_match_original`, `tests/differential/test_creature.py::test_creature_updates`.
- E3. Push a creature with a movement arrow (the push arrow in the view). Expected: the push succeeds or fails as in DOS. Covered by `tests/differential/test_creature.py::test_push_creatures`.
- E4. Let a creature attack the party. Expected: the damage goes to the champions of the attacked side; a sleeping party wakes. Covered by `tests/differential/test_projectile.py::test_creature_attacks_champions`.
- E5. Stand in front of a missile launcher. Expected: the missiles fly and hit the party as in DOS. Covered by `tests/differential/test_projectile.py::test_missile_launchers`, `tests/differential/test_projectile.py::test_missiles_hit_party_on_moves`.
- E6. Use the minion map of a minion item. Expected: the map panel, the icons, and the mana drain are as in DOS. Covered by `tests/differential/test_magic.py::test_minion_maps_match_original`.

## F. Magic

- F1. Cast a fireball, a lightning bolt, and a poison cloud. Expected: the missiles, the explosions, and the damage are as in DOS. Covered by `tests/differential/test_magic.py::test_every_spell_cast_matches_original`, `tests/differential/test_projectile.py::test_spell_missiles`, `tests/differential/test_projectile.py::test_explosions`.
- F2. Cast a light spell. Expected: the view becomes brighter; the light fades with time. Covered by `tests/differential/test_magic.py::test_magic_light_events_match_original`, `tests/differential/test_world.py::test_party_light_matches_original`.
- F3. Cast a party aura spell (shield, invisibility). Expected: the aura shows in the status boxes and ends after the same time. Covered by `tests/differential/test_magic.py::test_every_spell_cast_matches_original`, `tests/differential/test_interface.py::test_party_states_match_original`.
- F4. Summon a minion. Expected: the minion appears and acts as in DOS. Covered by `tests/differential/test_magic.py::test_every_spell_cast_matches_original`, `tests/differential/test_creature.py::test_creature_updates`.
- F5. Enter a rune set that is not a spell. Expected: the failure message is as in DOS. Covered by `tests/differential/test_magic.py::test_find_spell_matches_original_for_all_rune_sets`, `tests/differential/test_magic.py::test_runes_and_casts_match_original`.
- F6. Cast with too little mana. Expected: the cast fails as in DOS. Covered by `tests/differential/test_magic.py::test_every_spell_cast_matches_original`, `tests/differential/test_magic.py::test_many_casts_match_original`.

## G. World

- [ ] G1. Open and close a door with its button. Expected: the door sound plays. The door animation is covered by `tests/differential/test_move.py::test_step_into_special_squares`, `tests/differential/test_view.py::test_door_states_match_original`.
- G2. Break a door with a weapon or a spell. Expected: the broken door picture shows as in DOS. Covered by `tests/differential/test_view.py::test_door_states_match_original`, `tests/differential/test_projectile.py::test_projectiles_at_doors`.
- G3. Step into an open pit. Expected: the party falls to the map below and takes damage. Covered by `tests/differential/test_move.py::test_step_into_special_squares`, `tests/differential/test_world.py::test_pit_falls_match_original`.
- G4. Step onto an active teleporter. Expected: the party arrives at the same place and direction as in DOS. Covered by `tests/differential/test_move.py::test_step_into_special_squares`.
- G5. Take stairs up and down. Expected: the party arrives on the next map with the same direction as in DOS. Covered by `tests/differential/test_move.py::test_step_into_special_squares`.
- G6. Step on a floor pressure plate and put an object on it. Expected: the target (door, pit, wall) changes as in DOS. Covered by `tests/differential/test_move.py::test_square_events_on_all_sensors`, `tests/differential/test_champions.py::test_floor_sensor_sees_the_leader_hand`.
- G7. Walk through a fake wall. Expected: the party passes. Covered by `tests/differential/test_move.py::test_step_into_special_squares`.
- G8. Put an object onto a Skullkeep machine (object transfer sensor). Expected: the object moves to the same place as in DOS. Covered by `tests/differential/test_world.py::test_object_transfer_sensors_match_original`.
- G9. Stay outdoors for a full day. Expected: day and night change; the outdoor light and sky follow the same hours as in DOS. Covered by `tests/differential/test_world.py::test_weather_steps_on_outdoor_maps`, `tests/differential/test_world.py::test_party_light_matches_original`.

## H. Sound effects

- [ ] H1. Start a game from the start menu. Expected: the test sound plays when the game starts.
- [ ] H2. Open a door at the left and at the right of the party. Expected: the sound comes from the correct side.
- [ ] H3. Walk into a wall. Expected: the bump sound plays.
- [ ] H4. Wait for a storm with thunder. Expected: the thunder plays from the correct side and after a delay.
- [ ] H5. Fight a creature. Expected: the attack and hit sounds play at the moment of the attack and of the hit. The sound requests are covered by `tests/differential/test_world.py::test_sound_requests_match_original`.
- [ ] H6. Stand near an animated wall ornament with a sound. Expected: the ambient sound plays at the start of each animation cycle. The rule is covered by `tests/differential/test_world.py::test_ambient_wall_sounds_match_original`. The audible wall ornaments are only on levels 2 and 4, far from the start.
- [ ] H7. Let the party sleep near a sound. Expected: the sound is quieter.
- [ ] H8. Open the volume dialog (see J11). Set the sound volume to 0, then to 7. Expected: 0 is silent; a new value plays the test sound.

## I. Music

- [ ] I1. Start a game. Expected: the music plays from the first game step; the start menu plays no song.
- [ ] I2. Go to a map with another song. Expected: the old song fades out; the new song starts.
- [ ] I3. Open the volume dialog (see J11). Set the music volume to 0, then to a higher value. Expected: 0 stops the music; a higher value starts it again.
- [ ] I4. Listen to a song in the port and in DOSBox (Sound Blaster 16 with OPL). Expected: the same notes, instruments, and tempo.
- [ ] I5. Let a song play to its end. Expected: the song loops.

## J. Start menu, new game, save and load, end of game

- [ ] J1. Run `build/dm2 --mode vanilla`. Expected: the start menu shows the title and its options.
- [ ] J2. Choose the picture option. Expected: the picture shows until a click or until it times out.
- [ ] J3. Choose a new game. Expected: the party starts on the start square with the champion of the start mirror as leader.
- [ ] J4. Recruit more champions and enter the game. Expected: the party has up to four champions.
- [ ] J5. Press Option+S (Alt+S) to open the save menu. Expected: the dialog shows SAVE, QUIT, RESTART, RESUME.
- [ ] J6. Save into an empty slot and type a name. Expected: the slot shows the name; `game/DATA/SKSAVEn.DAT` exists.
- [ ] J7. Quit, start `build/dm2`, and load the save. Expected: "LOADING GAME...", then "GAME LOADED, READY TO PLAY"; the game continues in the same state.
- [ ] J8. Save with an object in the leader hand, a magic light, and an aura; load. Expected: the object is back in the hand; the light and the aura continue.
- [ ] J9. Choose QUIT more than 100 game steps after the last save. Expected: the program asks before it quits; then the end picture shows until a click.
- [ ] J10. Choose RESTART. Expected: the program starts again with the start menu, even if it started with `--save`.
- [ ] J11. Open a champion's inventory and click the speaker button (zone 0x238, command 0x8d); the dialog has no keyboard shortcut. Expected: the sound and music volumes go from 0 to 7.
- [ ] J12. Rename a champion with the name editor. Expected: the keys edit the name and the title.
- [ ] J13. Let the whole party die. Expected: the scream, then "THE END"; RESTART GAME loads the last save; QUIT GAME ends with exit code 2.
- [ ] J14. Load or save slot 0. While the game runs, rename `SKSAVE0.DAT` and keep `SKSAVE0.BAK`. Let the party die and choose RESTART GAME. Expected: the `.BAK` file loads and is renamed back to `SKSAVE0.DAT`. (The slot list reads only `.DAT` files, as in the original; a slot with only a `.BAK` shows UNUSED.)
- [ ] J15. Load a damaged save. Expected: "THE GAME IS DAMAGED!"; no crash.
- [ ] J16. Optional: load a save near the end and complete the final task (end sensor). Expected: the screen goes black and the program ends with exit code 1 after the movies `END` and `CREDITS` (M6).

## K. DOS cross-checks

- K1. Run the port and DOSBox side by side on the same save. Expected: the image is equal for the start view, a walk of ten squares, and the inventory. Covered by `tests/differential/test_view.py::test_view_picture_matches_original`, `tests/differential/test_view.py::test_walking_view_matches_original`, `tests/differential/test_move.py::test_random_walks_on_all_maps`, `tests/differential/test_interface.py::test_inventory_matches_original`.
- [ ] K2. Compare the sound effects of K1 side by side. Expected: the same sounds at the same moments.
- [ ] K3. Compare the music side by side. Expected: the same song and the same tempo.
- [ ] K4. Compare the input side by side: the same keys and clicks. Expected: the same results.
- [ ] K5. Save in the port, then load the slot in DOS. Expected: DOS loads the game; the party, champions, and objects are equal.
- [ ] K6. Save in DOS, then load the slot in the port. Expected: the port loads the game; the party, champions, and objects are equal.
- [ ] K7. After K5 and K6, play one minute in each. Expected: no crash and no visible difference in the game state.

## L. Advanced mode

Details: [Advanced mode](manual.md#advanced-mode).

- [ ] L1. Run `build/dm2 --save tests/saves/start.DAT`. Press W, S, A, D, Q, and E. Expected: forward, back, strafe left, strafe right, turn left, turn right, like the arrow keys.
- [ ] L2. Run `build/dm2 --mode vanilla --save tests/saves/start.DAT`. Press W, Q, F5, and F9. Expected: nothing happens; no `game/DATA/QUICK.DAT` is written.
- [ ] L3. In advanced mode, open the save dialog (Option+S, SAVE) and type a slot name with W, A, S, D, Q, and E. Expected: the letters go into the name; the party does not move.
- [ ] L4. In advanced mode, open an inventory and rename a champion with the name editor. Expected: W, A, S, D, Q, and E type letters.
- [ ] L5. In the dungeon view, press F5. Expected: the white line "GAME SAVED" in the message area; `game/DATA/QUICK.DAT` exists; no `QUICK.BAK`.
- [ ] L6. Walk some squares, then press F9. Expected: "GAME LOADED"; the party is back on the square of L5; no dialog shows.
- [ ] L7. Delete `game/DATA/QUICK.DAT` and press F9. Expected: "NO QUICK SAVE"; the game continues.
- [ ] L8. Write some bytes of garbage into `game/DATA/QUICK.DAT` and press F9. Expected: "QUICK SAVE DAMAGED"; the game continues unchanged.
- [ ] L9. Press F5 and F9 with an open inventory, while the game is paused (Esc), while the party sleeps, and in the save menu. Expected: nothing happens.
- [ ] L10. Load a slot, play more than 100 game steps, press F5, then choose QUIT in the save menu. Expected: the program asks before it quits (a quick save is not a saved game).
- [ ] L11. Start with `--mode vanilla` and choose RESTART in the save menu. Expected: the program starts again in vanilla mode.
- [ ] L12. Run `build/dm2 --mode turbo`. Expected: the message "unknown mode turbo: use vanilla, advanced, or turnbased" and exit code 2.
- [ ] L13. In the dungeon view, press Tab. Expected: the automap covers the viewport; the party square with a red frame is in the middle; the readout shows the level, X, Y, and the direction. Press Tab, M, and Esc in turn after opening with Tab or M. Expected: each closes the automap and the dungeon view comes back.
- [ ] L14. Walk through some rooms and open the automap. Expected: the squares that the view showed are drawn with the minion map icons; squares behind walls and closed doors stay black.
- [ ] L15. With the automap open, wait 10 seconds and press W and the arrow keys. Expected: the party does not move; the game time stops (no creature moves after the automap closes).
- [ ] L16. Drag the automap with the left mouse button. Expected: the map moves in whole squares and stops at the level edges; after closing and reopening it, the party is in the middle again.
- [ ] L17. Press I, F, and L with the automap open. Expected: nothing changes; the readout shows no cheats.
- [ ] L17a. Open the automap with the reveal map cheat on, then off. Expected: a white 1-pixel frame surrounds the whole level; a floor with a pressure pad (a floor plate that a step triggers) has a small gray square in the middle; a wall that the party can walk through (an imaginary wall) shows as the wall icon with a light gray checkerboard.
- [ ] L17b. On level 6 open the automap and drag to a neighbor map. Expected: the seen squares of the neighbor maps show at their place; one white frame surrounds the whole level; the drag stops at the level edge.
- [ ] L17c. Outdoors, walk to the edge of a map that has no map beyond it, where the view shows trees ahead, and open the automap. Expected: the trees show as walls beyond the last floor squares; the white frame is outside them.
- [ ] L17d. Outdoors, stand in front of a tree on a floor square and open the automap, then a magic map without options. Expected: both show the tree. Near a merchant and a walking creature, open the automap. Expected: neither shows, as on the magic map without options.
- [ ] L17e. Outdoors, stand at the edge of a map where another map of the level covers the squares beyond it only with unknown squares (level 6, map 24, the south edge), with the view showing trees ahead, and open the automap. Expected: the trees show as walls, as on the magic map.
- [ ] L17f. Press M, then C, while the party walks a step. Expected: each screen fills the whole viewport.
- [ ] L18. Press C, turn IMMORTAL on, close the screen, and let a creature hit the party until a champion has 1 health. Expected: the champion does not die.
- [ ] L19. On the cheat screen, turn ENDLESS FOOD AND WATER on and wait some minutes. Expected: the food and water gauges in the inventory do not go down. Eat or drink. Expected: the gauges go up.
- [ ] L20. In a dark place, turn FULL LIGHT on with C and close the screen. Expected: the dungeon view shows full light.
- [ ] L21. Press F5 with all cheats off, turn cheats on with C, then press F9. Expected: the cheat screen shows all cheats OFF. On the SPELLS pages, click NEXT up to page 5, then NEXT (page 1) and BACK (the cheat page). Click FUL IR (X,4,4) FIREBALL. Expected: the screen closes, the fireball flies, the mana and the experience do not change.
- [ ] L22. Explore, press F5, save in a slot, restart, and load the slot; then press F9. Expected: the automap shows the explored squares after each load. Load a vanilla save. Expected: an empty automap except the current view.
- [ ] L23. In vanilla mode, press Tab and M. Expected: no automap.
- [ ] L24. In advanced mode, press C. Expected: the title line shows CHEATS at the left and CLOSE at the right; below it the six cheat rows with their ON and OFF values in one column, the cyan subtitle OPTIONS, SCREEN FILTER NONE (its value in the same column), QUICK SAVE F5, QUICK LOAD F9, and SPELLS, with no blank row. Press Down until CLOSE. Expected: the selection skips OPTIONS and CLOSE is the last. Press Return on SCREEN FILTER and click it. Expected: the filter changes immediately through NONE, SCALE2X, SCALE3X and wraps; `game/DATA/SETTINGS.TXT` holds the filter. Click CLOSE. Expected: the screen closes. Press Esc in the dungeon view. Expected: GAME FROZEN, as in vanilla mode; Esc again resumes the game.
- [ ] L25. Start `build/dm2` again. Expected: the filter from `SETTINGS.TXT` is on. Start with `--filter none`. Expected: no filter; C, then a click on SCREEN FILTER, writes the file again. With `--mode vanilla` Esc pauses and the file is not read. Make `SETTINGS.TMP` a folder and change the filter. Expected: "UNABLE TO SAVE OPTIONS" once; the filter still changes.
- [ ] L26. In advanced mode, open an inventory and press Tab. Expected: the inventory closes. Open the name editor and press Tab. Expected: the editor moves to the next field; the inventory stays. In vanilla mode, press Tab in an inventory. Expected: nothing happens.
- [ ] L27. In advanced mode, hit a creature with a weapon. Expected: the damage star stays visible about twice as long as in vanilla mode. Click in the action area while the star shows. Expected: the hand icons show immediately and the click works as usual (it opens the hand menu). In vanilla mode, the star disappears after one step.
- [ ] L28. In advanced mode, stand still and cast a fireball, or throw a dagger, down a long corridor. Expected: the projectile glides smoothly away, with no jumps or steps back. Turn while it flies. Expected: the view turns immediately; no projectile jumps. In vanilla mode, the projectile moves in steps.
- [ ] L29. In advanced mode, open the automap and the cheat screen in turn, and right-click in each. Expected: each closes and the dungeon view comes back. Open SPELLS on the cheat screen and right-click. Expected: the cheat page shows with SPELLS selected. Open an inventory, press Alt+S, and right-click. Expected: the save menu closes and the inventory shows again. Click SAVE and right-click in the slot list. Expected: the save menu shows again. Wait more than 100 game steps after the last save, click QUIT, and right-click. Expected: the save menu shows again. In vanilla mode, right-click in the save menu. Expected: nothing.

## M. Movies

Start `build/dm2` without `--save` unless an item says otherwise. Use a window with sound.

- [ ] M1. Start and choose a mode. Expected: the Interplay picture for 3 seconds, then `FTL` with sound, then `INTRO` with sound, then the start menu with its colors.
- [ ] M2. Look at the size of the pictures. Expected: `FTL` is small in the middle of a 4:3 area; `INTRO` fills about 90 % of the 4:3 area; black borders on the sides of a wide window.
- [ ] M3. `--mode vanilla`: click during the splash and during `FTL`. Expected: they continue. Click during `INTRO`. Expected: the start menu shows.
- [ ] M4. `--mode advanced`: click during the splash, `FTL`, and `INTRO`. Expected: each stops immediately. Press keys during a movie. Expected: nothing; the start menu does not get the keys.
- [ ] M5. Start with `--save game/DATA/SKSAVE0.DAT`. Expected: no movie; the game starts immediately.
- [ ] M6. Complete the game (the end sensor). Expected: the screen goes black, `END` plays, then `CREDITS`, then the program ends with exit code 1. A click in `END` starts `CREDITS`.
- [ ] M7. Let the party die and choose QUIT GAME. Expected: `CREDITS` plays, then the program ends with exit code 2.
- [ ] M8. Close the window during `INTRO`, and during `CREDITS` of M7. Expected: the program ends immediately (exit code 0 and 2); no start menu. Send `quit` over the control socket during `INTRO`. Expected: the same.
- [ ] M8b. Let the party die. Close the window (or send `quit`) during the "THE END" dialog. Expected: the program ends immediately with exit code 2; no `CREDITS`.
- [ ] M9. Choose RESTART in the save menu. Expected: the start menu, no movie.
- [ ] M10. Resize the window during a movie. Expected: the picture stays 4:3 with black borders.
- [ ] M11. Watch `INTRO`, `END`, and `CREDITS` to the end. Expected: clean pictures without broken blocks; the sound fits the picture.
- [ ] M12. Rename `game/FTL` and start. Expected: the splash, then `INTRO`, then the start menu. Rename it back.
- [ ] M13. With the control socket ([Movies](llm/turn-based.md#movies)): `state` during the splash gives `"mode":"movie"`; `click` skips by the rules of M3 and M4.
- [ ] M14. Optional, DOSBox (`tools/dosbox-dm2.conf`): compare the size and the position of the `INTRO` picture with the port.

## T. Turn-based mode

Details: [Turn-based mode](llm/turn-based.md). Run the game with `SDL_AUDIO_DRIVER=dummy build/dm2 --mode turnbased --saves DIR` (DIR is an existing folder) and send requests with `uv run dm2-play`.

- [ ] T1. Start the game. Expected: no start menu and no movie; the dungeon view shows immediately; the party has one champion; `get_state` gives `game_mode` `turnbased`. Press keys and click in the window. Expected: nothing happens. Send `key '{"key": "up"}'`. Expected: the error `use act in turnbased mode`. Send `quit`. Expected: the program exits with code 0.
- [ ] T2. Send `new_game`. Expected: the party is on the start square (map 0, x 1, y 8, facing north); the reply has `state`.
- [ ] T3. Send `act nothing`. Expected: the error `unknown action`; the game time and the screen do not change.
- [ ] T4. Send `uv run dm2-play act take_object '{"cell": 2}'` and `uv run dm2-play act_many '["turn_left", "turn_right"]'`. Expected: the game gets the action and its args (an error text about the cell or the view, or the turn), and the batch runs two turns (`ran` 2).
- [ ] T5. Watch the window during `act move_forward` and `act turn_left`. Expected: the party walks and turns on the screen; the window ignores the keyboard and the mouse.
- [ ] T6. Open an inventory and send `act look '{"champion": 0, "slot": 0}'`. Expected: `look` has the same name and lines as the eye panel shows for that object.
- [ ] T7. Walk to a dialog or a mirror and use `act click` on its buttons. Expected: the dialog closes; `save`, `load`, and `new_game` give `close the dialog first (act click or act key)` while it is open.
- [ ] T8. Send `save`, walk some squares, then send `load`. Expected: the party is back on the saved square; the game time is the saved time plus 1. Send `new_game`. Expected: a new game.
- [ ] T9. Let the party die. Expected: the reply has `game_over` true; `act`, `wait`, and `save` give `game over`; `load` restores the game.
- [ ] T10. Run `uv run dm2-play --brief get_state`. Expected: a short text with the party, the champions, and the squares ahead.
- [ ] T11. Create `playthroughs/current/coordinator/`, run some `dm2-play` calls without `DM2_PLAY_LOG_DIR`, and read `calls.log`. Expected: one line per call. Remove the folder and call again. Expected: no log file is written.
- [ ] T12. Open a dialog (T7), then run `uv run dm2-play wait '{"ticks": 5000}'` and measure the time until the reply. Expected: the game does not tick, so the turn ends after 16 frames per tick (80000 frames). Note the time and whether `dm2-play` stops with a timeout (its limit is 150 seconds).

## W. Windows

Copy `SDL3.dll` next to `build/dm2.exe` and set up Wine ([Windows build](development.md#windows-build)). Start the program from the repo root.

- [ ] W1. Remove `SDL3.dll` from `build/` and run `"$WINE" build/dm2.exe`. Expected: Windows reports the missing `SDL3.dll`. Copy it back.
- [ ] W2. Run `"$WINE" build/dm2.exe`. Expected: the program starts a new game with `game/`: the splash picture, the movies, and the start menu, with image and sound.
- [ ] W3. Run `"$WINE" build/dm2.exe --mode turnbased`. Expected: the program stops with exit code 1; `game/dm2.log` holds the line `the turn-based mode and --control need macOS`.
