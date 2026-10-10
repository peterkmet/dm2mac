# Manual test checklist

Use this checklist for the manual test of the C port (workflow step 6, plan Step 21). Use DOSBox only for the items that name DOS or DOSBox. Each item gives an action and the expected result. Check the box when the result matches. Write down each mismatch with the item number, the place in the game, and a screenshot.

## Results of 2026-10-06

The user and LLM test agents over the control socket (`docs/llm-control.md`) ran the manual test on 2026-10-06. The checklist has 108 items:

- 47 passed in the manual test.
- 51 are covered by automated tests.
- 7 are skipped by the user's decision: the DOSBox comparisons C10, I4, K2, K3, K4, K6, and K7.
- 3 are not run: D15 and J16 need long play; H6 needs a wall ornament with a sound, and these are only on levels 2 and 4, far from the start.

The fixes that the manual test found are in `CHANGELOG.md`.

## Smoke test after the module cleanup (2026-10-07)

The cleanup does not change the behavior, and the automated tests pass. This short test checks the game paths that the cleanup touched most. Start `build/dm2 --save game/DATA/SKSAVE0.DAT` or a new game.

- [ ] S1. Walk, turn, and push against a wall: the view and the bump sound are as before.
- [ ] S2. Open the inventory, eat and drink, sleep with the sleep button, and wake with a key.
- [ ] S3. Press a door button: the button shows pressed for one step, and the door moves.
- [ ] S4. Put an item into a wall alcove and take it back.
- [ ] S5. Fight a creature with a weapon and with a spell: damage, sounds, and the creature reaction.
- [ ] S6. Throw an item and shoot a missile; a projectile hits a door and a creature.
- [ ] S7. Buy and sell at a shop; the money box shows the coins.
- [ ] S8. Summon a minion, open the minion map, and give a command.
- [ ] S9. Open the automap (Tab), then save and load a slot game and the quick save (F5, F9).
- [ ] S10. Music and sound effects play; the volume dialog works.

## Smoke test after the layer cleanup (2026-10-10)

The cleanup moves code between the layers and does not change the behavior; the automated tests pass. This short test checks the paths that now go through `g_hooks` or `ui/play.c`. Start `build/dm2 --save game/DATA/SKSAVE0.DAT` or a new game.

The items marked "control socket" ran over `docs/llm-control.md` with the saves of `tests/saves/`, on this branch and on `main` side by side with the same input; the states and the screenshots are equal.

- [x] L1. Start a new game and load a save from the start menu: the view, the champions, and the music start as before. Passed (control socket, 2026-10-10). The music is not checked (dummy audio).
- [x] L2. Take an object into the leader hand two times: its name prints in the message area, with a new row for the next message. `DUNGEON.DAT` has no visible floor text and no wall text with a message. Passed (control socket, 2026-10-10).
- [x] L3. Fall into a pit and take stairs: the view of the new map shows at once, and no key press from before the move is still pending. Passed (control socket, 2026-10-10).
- [x] L4. Recruit a champion from a mirror: the view hides and shows again, and the recruit buttons work. Passed (control socket, 2026-10-10).
- [x] L5. Sleep and wake (Enter, a click, and damage): "WAKE UP" shows, the view clears, and the move arrows come back after waking. Other keys do not wake the party, as in `main`. Passed (control socket, 2026-10-10).
- [x] L6. Use the action hands and cast a spell; let a cast fail: the hand icons count down, the spell panel shows the runes, and the flask picture shows on the failed cast. Passed (control socket, 2026-10-10).
- [x] L7. Hit and get hit by a creature: the champion damage boxes and the bars update at once. Passed (control socket, 2026-10-10).
- [x] L8. Open a container from the action hand and the minion map of a summoned minion; click a destination on the map. Passed (control socket, 2026-10-10).
- [x] L9. Outdoors, wait for rain and lightning: the sky objects, the thunder sound, and the light change are as before. Passed (control socket, 2026-10-10). The thunder sound is not checked (dummy audio).
- [ ] L10. Walk next to creatures and doors: sounds come from the right side and fade with distance.
- [x] L11. Advanced mode: open the automap (Tab); the seen squares are kept after save and load. Passed (control socket, 2026-10-10).
- [ ] L12. Finish the game with the end sensor (or a DOS save near the end): the end sequence runs.

## Prerequisites

- macOS with SDL3 from Homebrew: `brew install sdl3`.
- The game files of Dungeon Master II (PC DOS, English, 1995) in `game/`, with `game/DATA/GRAPHICS.DAT`, `game/DATA/DUNGEON.DAT`, `game/DATA/SONGLIST.DAT`, `game/MELODIC.BNK`, and `game/DRUM.BNK`.
- DOSBox with the folder `game/` mounted as drive `C:`, for the items C10, I4, and K2 to K7.
- `make -C port` and `uv run pytest` pass (plan Step 21).
- Optional: a DOS save near the end of the game, for the end sensor (item J16).

## Build and start

1. Build the program and the test library:

   ```sh
   make -C port
   ```

   Expected result: `build/dm2` and `build/libdm2.dylib`, no warnings.

2. Start the port: the splash picture, the movies `FTL` and `INTRO`, then the start menu:

   ```sh
   build/dm2
   ```

3. Start the DOS version in DOSBox:

   ```text
   C:
   IBMIOP SKULL.EXE
   ```

   To start DOSBox without the intro movies, run `tools/dosbox-nointro.sh` from the repo root. The script uses the same `game/DATA` folder, so the saves stay shared with the port.

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

Items marked `[x]` with "Covered by" are checked by automated tests against the original and need no manual run.

## A. Program start and window

- [x] A1. Run `build/dm2`. Expected: a window at scale 3 shows the splash picture and the movies, then the start menu title; the log shows `data folder: game`. Passed (manual, 2026-10-06).
- [x] A2. Run `build/dm2 --filter scale2x`, then `--filter scale3x`. Expected: the image is smoothed by the filter; the game is the same. Passed (manual, 2026-10-06).
- [x] A3. Run `DM2_FILTER=scale2x build/dm2 --filter none`. Expected: no filter (the option wins). Passed (manual, 2026-10-06).
- [x] A4. Run `build/dm2 --filter bad`. Expected: an error message and exit code 2. Passed (manual, 2026-10-06).
- [x] A5. Run `build/dm2 --data /tmp`. Expected: the message `cannot read /tmp/DATA/GRAPHICS.DAT` and exit code 1. Passed (manual, 2026-10-06).
- [x] A6. Start with `--data` on a copy of `game/` without `MELODIC.BNK`. Expected: the log says the port plays without music; the game runs with sound effects. Passed (manual, 2026-10-06).
- [x] A7. Close the window during the game. Expected: the program ends without a crash. Passed (manual, 2026-10-06).

## B. Image

### Dungeon view

- [x] B1. Load `tests/saves/start.DAT` in both (`build/dm2 --save tests/saves/start.DAT`). Expected: the dungeon view at the party position is equal: walls, floor, ceiling, doors, stairs, pits, ornaments (plan Step 12a). Covered by `tests/test_view.py::test_view_picture_matches_original`, `tests/test_view.py::test_view_squares_on_all_maps_match_original`.
- [x] B2. Look at squares with objects and creatures in the view. Expected: objects, creatures, and their possessions are at the same places and sizes (plan Step 12b). Covered by `tests/test_view.py::test_thing_grids_match_original`, `tests/test_view.py::test_added_missiles_explosions_creatures_match_original`.
- [x] B3. Watch creatures that move. Expected: the animation frames, mirror, and random shift look like the DOS version. Covered by `tests/test_creature.py::test_animated_creature_views`, `tests/test_view.py::test_added_missiles_explosions_creatures_match_original`.
- [x] B4. Look at a visible teleporter. Expected: the sparkle field shows as in DOS. Covered by `tests/test_view.py::test_view_picture_matches_original`.
- [x] B5. Look at alcoves and shelves with objects. Expected: the objects stand in the alcove picture as in DOS. Covered by `tests/test_view.py::test_view_picture_matches_original`.
- [x] B6. Look at missiles and explosions (thrown objects, spells). Expected: the pictures, sizes, and the tint of an explosion on the party square are as in DOS. Covered by `tests/test_view.py::test_added_missiles_explosions_creatures_match_original`.
- [x] B7. Walk forward one square. Expected: the walking frame fills the full viewport; the step shows no visible jump (the rounding of the stretch is unverified, plan Step 14). The walk rectangle is covered by `tests/test_view.py::test_walking_view_matches_original`. Passed (manual, 2026-10-06).
- [x] B8. Step into an open pit. Expected: the fall view shows, then the view of the map below. Covered by `tests/test_world.py::test_pit_falls_match_original`.
- [x] B9. Walk into a dark area without a light. Expected: the view darkens by the same steps as in DOS. Covered by `tests/test_world.py::test_party_light_matches_original`, `tests/test_view.py::test_view_picture_matches_original`.

### Outdoor view and weather

- [x] B10. Go outdoors. Expected: the sky, the sky objects, and the outdoor walls are as in DOS. Covered by `tests/test_view.py::test_outdoor_sky_matches_original`.
- [x] B11. Wait for rain. Expected: the rain shows with the same picture and wind direction; wet ornaments show rain in their background. Covered by `tests/test_view.py::test_outdoor_sky_matches_original`, `tests/test_view.py::test_wet_image_matches_original`, `tests/test_world.py::test_weather_steps_on_outdoor_maps`.
- [x] B12. Wait for clouds and a storm. Expected: clouds, lightning flashes, and lightning strikes show as in DOS. Covered by `tests/test_world.py::test_weather_steps_on_outdoor_maps`, `tests/test_world.py::test_storm_steps_match_original`, `tests/test_world.py::test_lightning_strikes_match_original`.
- [x] B13. Wait until the hour changes outdoors (day and night). Expected: the light level outdoors changes at the hour as in DOS. Covered by `tests/test_world.py::test_weather_steps_on_outdoor_maps`, `tests/test_world.py::test_party_light_matches_original`.

### Interface panels

- [x] B14. Compare the screen after a load. Expected: champion status boxes, bars, portraits, hands, movement arrows, and the action area are equal (plan Step 13). Covered by `tests/test_interface.py::test_game_screen_matches_original`, `tests/test_interface.py::test_party_states_match_original`.
- [x] B15. Open the inventory of each champion. Expected: slots, statistics, skills, food and water bars, and the mouth picture are equal. Covered by `tests/test_interface.py::test_inventory_matches_original`, `tests/test_interface.py::test_inventory_panels_match_original`.
- [x] B16. Put the eye on a weapon, a piece of clothing, a scroll, a potion, a chest, and a money box. Expected: the object information panel is equal. Covered by `tests/test_interface.py::test_inventory_panels_match_original`.
- [ ] B16a. In the inventory, hold the left mouse button down on the eye, with an empty hand and with an object in the hand. Expected: the skills and statistics panel or the object information panel shows, and the pointer hides, while the button is down. The release shows the normal panel again. A short click shows nothing. The real click path is covered by `tests/test_control.py::test_eye_and_mouth_hold_while_the_left_button_is_down`.
- [x] B17. Open the spell panel and enter runes. Expected: the rune sets and the entered runes are equal. Covered by `tests/test_interface.py::test_action_area_panels_match_original`, `tests/test_magic.py::test_runes_and_casts_match_original`.
- [x] B18. Fill the message area with more than three lines. Expected: the rows move up by one row; no part of an old row stays. The 7-pixel move is a port reconstruction (plan Step 13); `tests/test_interface.py::test_message_area_scrolls` checks it in the port only. Passed (manual, 2026-10-06).
- [x] B19. Wait without new messages. Expected: old message rows clear after the same time as in DOS. Covered by `tests/test_interface.py::test_message_area_matches_original`.

## C. Input and movement

- [x] C1. Press the arrow keys and the numeric keypad keys on the Mac keyboard. Expected: the party walks forward, back, left, right and turns left and right. The commands of the keys are covered by `tests/test_input.py::test_clicks_and_keys_match_original`, `tests/test_input.py::test_keys_walk_and_turn_in_the_main_loop`. Passed (manual, 2026-10-06).
- [x] C2. Press Shift with the Left or Right arrow on the Mac keyboard. Expected: the party steps sideways. The commands of the keys are covered by `tests/test_input.py::test_clicks_and_keys_match_original`. Passed (manual, 2026-10-06).
- [x] C3. Click each of the six movement arrows. Expected: the arrow shows the pressed picture. The moves and the click delays are covered by `tests/test_input.py::test_clicks_on_the_movement_arrows`, `tests/test_input.py::test_arrow_clicks_and_feedback_delays_match_original`. Passed (manual, 2026-10-06).
- [x] C4. Hold a movement key. Expected: the party walks on at an even speed; no step is lost or doubled. The move cooldowns are covered by `tests/test_input.py::test_keys_and_move_cooldowns_match_original`. Passed (manual, 2026-10-06).
- [x] C5. Walk into a wall. Expected: the bump sound plays. The party position and the damage to the champions of the wall side are covered by `tests/test_move.py::test_bump_damage_hits_the_champions_of_the_bump_side`. Passed (manual, 2026-10-06).
- [x] C6. Press Esc, then a key. Expected: the music stops during the pause and continues after it. The pause of the game is covered by `tests/test_input.py::test_keys_and_move_cooldowns_match_original`, `tests/test_music.py::test_pause_until_next_game_step`. Passed (manual, 2026-10-06).
- [x] C7. Click the sleep button. Expected: the party sleeps (the sleep message shows, time runs faster); a key or click wakes the party. Covered by `tests/test_input.py::test_sleep_and_wake_match_original`, `tests/test_interface.py::test_party_states_match_original`. Passed (manual, 2026-10-06).
- [x] C8. Click a wall button and a lever. Expected: the click feedback shows and the sensor acts as in DOS. Covered by `tests/test_move.py::test_wall_clicks_on_all_wall_cells`, `tests/test_move.py::test_square_events_on_all_sensors`. Passed (manual, 2026-10-06).
- [x] C9. Click the champion hands and the action icons. Expected: the action menu opens; the choice runs. Covered by `tests/test_input.py::test_champion_clicks_match_original`, `tests/test_interface.py::test_action_area_panels_match_original`, `tests/test_interface.py::test_action_results_match_original`, `tests/test_magic.py::test_icon_click_feedback_matches_original`.
- [ ] C10. Compare the game speed with DOSBox for one minute. Expected: the game time runs at the same rate (a game step every 8 timer ticks at 60 Hz; `docs/skull-formats.md` section 6 marks the real DOS rate as an open measurement). Skipped by the user's decision (DOSBox comparison).

## D. Champions, inventory, items, money, and shops

- [x] D1. Take an object from the floor and from a table. Expected: the object shows at the mouse pointer; its name shows in the message area. Covered by `tests/test_champions.py::test_coin_from_the_table`, `tests/test_champions.py::test_money_box_flags_in_the_view`, `tests/test_input.py::test_cursor_matches_original`, `tests/test_interface.py::test_hand_icon_matches_original`.
- [x] D2. Place the object on another cell, drop it, and throw it. Expected: the object lands at the same place as in DOS; a thrown object flies. Covered by `tests/test_champions.py::test_money_box_flags_in_the_view`, `tests/test_projectile.py::test_champions_throw`, `tests/test_projectile.py::test_thrown_objects`.
- [x] D3. Open the inventory of a champion with the keys 1 to 4 (there is no F-key). Put objects into inventory slots and take them out. Expected: only fitting slots accept an object; the load and the statistics change. Passed (manual, 2026-10-06).
- [x] D4. Eat food and drink from a flask or bota. Expected: the food and water bars rise; empty containers stay. Covered by `tests/test_champion_rules.py::test_eating_drinking_and_actions_match_original`, `tests/test_champions.py::test_drink_and_hand_actions`.
- [x] D5. Drink potions. Expected: the effect is as in DOS (health, stamina, mana, statistics). Covered by `tests/test_champion_rules.py::test_eating_drinking_and_actions_match_original`.
- [x] D6. Run hand actions with a weapon. Expected: the action name, the busy hand, and the reload countdown are as in DOS. Covered by `tests/test_champions.py::test_drink_and_hand_actions`, `tests/test_champion_rules.py::test_eating_drinking_and_actions_match_original`, `tests/test_interface.py::test_action_results_match_original`.
- [x] D7. Recruit a champion at a mirror; cancel at the next mirror. Expected: the candidate panel shows the portrait and the statistics of the candidate. The join and the cancel are covered by `tests/test_champions.py::test_recruit_and_cancel`. Passed (manual, 2026-10-06).
- [x] D8. Push the table of the first room south. Expected: the table moves (plan Step 15 scenario). Covered by `tests/test_champions.py::test_door_out_of_the_first_room`.
- [x] D9. Put the solid key into the keyhole and press the door button. Expected: the door of the first room opens (the gold coin opens no door, as in DOS). Covered by `tests/test_champions.py::test_door_out_of_the_first_room`, `tests/test_champions.py::test_coin_from_the_table`.
- [x] D10. Open a money box; put coins in and take coins out. Expected: the coin counts change as in DOS. Covered by `tests/test_champion_rules.py::test_money_and_prices_match_original`, `tests/test_champions.py::test_money_box_flags_in_the_view`, `tests/test_interface.py::test_containers_in_hand_match_original`.
- [x] D11. Look at each shop display. Expected: the item, its name, and the prices in coins are as in DOS. Covered by `tests/test_view.py::test_shop_displays_match_original`.
- [x] D12. Buy an item from a merchant and pay with coins. Expected: the merchant takes the payment and gives change as in DOS. Covered by `tests/test_ai_move.py::test_merchant_commands`, `tests/test_champion_rules.py::test_money_and_prices_match_original`.
- [x] D13. Hold a torch until it burns down. Expected: the light falls; the torch burns out. Covered by `tests/test_champion_rules.py::test_damage_rules_match_original`, `tests/test_world.py::test_party_light_matches_original`.
- [x] D14. Let a champion get poisoned and cure the poison. Expected: the health loss and the cure are as in DOS. Covered by `tests/test_champion_rules.py::test_damage_rules_match_original`, `tests/test_champion_rules.py::test_eating_drinking_and_actions_match_original`.
- [ ] D15. Take the bones of a dead champion to an altar. Expected: the altar brings the champion back. The death and the objects on the square are covered by `tests/test_champion_rules.py::test_damage_rules_match_original`. Not run (needs long play).

## E. Creatures and combat

- [x] E1. Meet creatures. Expected: they move, turn, and attack as in DOS. Covered by `tests/test_creature.py::test_creature_updates`, `tests/test_ai_move.py::test_try_move`, `tests/test_ai_move.py::test_turn_toward`, `tests/test_ai_move.py::test_try_attack`.
- [x] E2. Attack a creature with a melee weapon. Expected: damage numbers, creature reaction, and death (possessions drop) are as in DOS. Covered by `tests/test_champion_rules.py::test_eating_drinking_and_actions_match_original`, `tests/test_interface.py::test_action_results_match_original`, `tests/test_creature.py::test_creature_updates`.
- [x] E3. Push a creature with a movement arrow (the push arrow in the view). Expected: the push succeeds or fails as in DOS. Covered by `tests/test_creature.py::test_push_creatures`.
- [x] E4. Let a creature attack the party. Expected: the damage goes to the champions of the attacked side; a sleeping party wakes. Covered by `tests/test_projectile.py::test_creature_attacks_champions`.
- [x] E5. Stand in front of a missile launcher. Expected: the missiles fly and hit the party as in DOS. Covered by `tests/test_projectile.py::test_missile_launchers`, `tests/test_projectile.py::test_missiles_hit_party_on_moves`.
- [x] E6. Use the minion map of a minion item. Expected: the map panel, the icons, and the mana drain are as in DOS. Covered by `tests/test_magic.py::test_minion_maps_match_original`.

## F. Magic

- [x] F1. Cast a fireball, a lightning bolt, and a poison cloud. Expected: the missiles, the explosions, and the damage are as in DOS. Covered by `tests/test_magic.py::test_every_spell_cast_matches_original`, `tests/test_projectile.py::test_spell_missiles`, `tests/test_projectile.py::test_explosions`.
- [x] F2. Cast a light spell. Expected: the view becomes brighter; the light fades with time. Covered by `tests/test_magic.py::test_magic_light_events_match_original`, `tests/test_world.py::test_party_light_matches_original`.
- [x] F3. Cast a party aura spell (shield, invisibility). Expected: the aura shows in the status boxes and ends after the same time. Covered by `tests/test_magic.py::test_every_spell_cast_matches_original`, `tests/test_interface.py::test_party_states_match_original`.
- [x] F4. Summon a minion. Expected: the minion appears and acts as in DOS. Covered by `tests/test_magic.py::test_every_spell_cast_matches_original`, `tests/test_creature.py::test_creature_updates`.
- [x] F5. Enter a rune set that is no spell. Expected: the failure message is as in DOS. Covered by `tests/test_magic.py::test_find_spell_matches_original_for_all_rune_sets`, `tests/test_magic.py::test_runes_and_casts_match_original`.
- [x] F6. Cast with too little mana. Expected: the cast fails as in DOS. Covered by `tests/test_magic.py::test_every_spell_cast_matches_original`, `tests/test_magic.py::test_many_casts_match_original`.

## G. World

- [x] G1. Open and close a door with its button. Expected: the door sound plays. The door animation is covered by `tests/test_move.py::test_step_into_special_squares`, `tests/test_view.py::test_door_states_match_original`. Passed (manual, 2026-10-06).
- [x] G2. Break a door with a weapon or a spell. Expected: the broken door picture shows as in DOS. Covered by `tests/test_view.py::test_door_states_match_original`, `tests/test_projectile.py::test_projectiles_at_doors`.
- [x] G3. Step into an open pit. Expected: the party falls to the map below and takes damage. Covered by `tests/test_move.py::test_step_into_special_squares`, `tests/test_world.py::test_pit_falls_match_original`.
- [x] G4. Step onto an active teleporter. Expected: the party arrives at the same place and direction as in DOS. Covered by `tests/test_move.py::test_step_into_special_squares`.
- [x] G5. Take stairs up and down. Expected: the party arrives on the next map with the same direction as in DOS. Covered by `tests/test_move.py::test_step_into_special_squares`.
- [x] G6. Step on a floor pressure plate and put an object on it. Expected: the target (door, pit, wall) changes as in DOS. Covered by `tests/test_move.py::test_square_events_on_all_sensors`, `tests/test_champions.py::test_floor_sensor_sees_the_leader_hand`.
- [x] G7. Walk through a fake wall. Expected: the party passes. Covered by `tests/test_move.py::test_step_into_special_squares`.
- [x] G8. Put an object onto a Skullkeep machine (object transfer sensor). Expected: the object moves to the same place as in DOS. Covered by `tests/test_world.py::test_object_transfer_sensors_match_original`.
- [x] G9. Stay outdoors for a full day. Expected: day and night change; the outdoor light and sky follow the same hours as in DOS. Covered by `tests/test_world.py::test_weather_steps_on_outdoor_maps`, `tests/test_world.py::test_party_light_matches_original`.

## H. Sound effects

- [x] H1. Start a game from the start menu. Expected: the test sound plays at the game start (plan Step 18). Passed (manual, 2026-10-06).
- [x] H2. Open a door at the left and at the right of the party. Expected: the sound comes from the correct side (plan Step 18). Passed (manual, 2026-10-06).
- [x] H3. Walk into a wall. Expected: the bump sound plays (plan Step 18). Passed (manual, 2026-10-06).
- [x] H4. Wait for a storm with thunder. Expected: the thunder plays from the correct side and after a delay (plan Step 18). Passed (manual, 2026-10-06).
- [x] H5. Fight a creature. Expected: the attack and hit sounds play at the moment of the attack and of the hit. The sound requests are covered by `tests/test_world.py::test_sound_requests_match_original`. Passed (manual, 2026-10-06).
- [ ] H6. Stand near an animated wall ornament with a sound. Expected: the ambient sound plays at the start of each animation cycle. The rule is covered by `tests/test_world.py::test_ambient_wall_sounds_match_original`. Not run: the audible wall ornaments are only on levels 2 and 4, far from the start (socket test agent, 2026-10-06).
- [x] H7. Let the party sleep near a sound. Expected: the sound is quieter. Passed (manual, 2026-10-06).
- [x] H8. Open the volume dialog (see J11). Set the sound volume to 0, then to 7. Expected: 0 is silent; a new value plays the test sound. Passed (manual, 2026-10-06).

## I. Music

- [x] I1. Start a game. Expected: the music plays from the first game step; the start menu plays no song (plan Step 19). Passed (manual, 2026-10-06).
- [x] I2. Go to a map with another song. Expected: the old song fades out; the new song starts (plan Step 19). Passed (manual, 2026-10-06).
- [x] I3. Open the volume dialog (see J11). Set the music volume to 0, then to a higher value. Expected: 0 stops the music; a higher value starts it again (plan Step 19). Passed (manual, 2026-10-06).
- [ ] I4. Listen to a song in the port and in DOSBox (Sound Blaster 16 with OPL). Expected: the same notes, instruments, and tempo. Skipped by the user's decision (DOSBox comparison).
- [x] I5. Let a song play to its end. Expected: the song loops. Passed (manual, 2026-10-06).

## J. Start menu, new game, save and load, end of game

- [x] J1. Run `build/dm2`. Expected: the start menu shows the title and its options. Passed (manual, 2026-10-06).
- [x] J2. Choose the picture option. Expected: the picture shows until a click or until the time ends. Passed (manual, 2026-10-06).
- [x] J3. Choose a new game. Expected: the party starts on the start square with the champion of the start mirror as leader. Passed (manual, 2026-10-06).
- [x] J4. Recruit more champions and enter the game. Expected: the party has up to four champions (PRD Epic 5). Passed (manual, 2026-10-06).
- [x] J5. Press Option+S (Alt+S) to open the save menu. Expected: the dialog with SAVE, QUIT, RESTART, RESUME. Passed (manual, 2026-10-06).
- [x] J6. Save into an empty slot and type a name. Expected: the slot shows the name; `game/DATA/SKSAVEn.DAT` exists. Passed (manual, 2026-10-06).
- [x] J7. Quit, start `build/dm2`, and load the save. Expected: "LOADING GAME...", then "GAME LOADED, READY TO PLAY"; the game continues in the same state (PRD Epic 5). Passed (manual, 2026-10-06).
- [x] J8. Save with an object in the leader hand, a magic light, and an aura; load. Expected: the object is back in the hand; the light and the aura continue. Passed (manual, 2026-10-06).
- [x] J9. Choose QUIT more than 100 ticks after the last save. Expected: the program asks before the quit; then the end picture shows until a click. Passed (manual, 2026-10-06).
- [x] J10. Choose RESTART. Expected: the program starts again with the start menu, also when it started with `--save`. Passed (manual, 2026-10-06).
- [x] J11. Open a champion's inventory and click the speaker button (zone 0x238, command 0x8d); the dialog has no key. Expected: the sound and music volumes go from 0 to 7. Passed (manual, 2026-10-06).
- [x] J12. Rename a champion with the name editor. Expected: the keys edit the name and the title. Passed (manual, 2026-10-06).
- [x] J13. Let the whole party die. Expected: the scream, then "THE END"; RESTART GAME loads the last save; QUIT GAME ends with exit code 2. Passed (manual, 2026-10-06).
- [x] J14. Load or save slot 0. While the game runs, rename `SKSAVE0.DAT` and keep `SKSAVE0.BAK`. Let the party die and choose RESTART GAME. Expected: the `.BAK` file loads and is renamed back to `SKSAVE0.DAT`. (The slot list reads only `.DAT` files, as in the original; a slot with only a `.BAK` shows UNUSED.) Passed (manual, 2026-10-06).
- [x] J15. Load a damaged save. Expected: "THE GAME IS DAMAGED!"; no crash. Passed (manual, 2026-10-06).
- [ ] J16. Optional: load a save near the end and complete the final task (end sensor). Expected: the screen goes black and the program ends with exit code 1 after the movies `END` and `CREDITS` (M6). Not run (needs long play).

## K. DOS cross-checks

- [x] K1. Run the port and DOSBox side by side on the same save. Expected: the image is equal for the start view, a walk of ten squares, and the inventory. Covered by `tests/test_view.py::test_view_picture_matches_original`, `tests/test_view.py::test_walking_view_matches_original`, `tests/test_move.py::test_random_walks_on_all_maps`, `tests/test_interface.py::test_inventory_matches_original`.
- [ ] K2. Compare the sound effects of K1 side by side. Expected: the same sounds at the same moments. Skipped by the user's decision (DOSBox comparison).
- [ ] K3. Compare the music side by side. Expected: the same song and the same tempo. Skipped by the user's decision (DOSBox comparison).
- [ ] K4. Compare the input side by side: the same keys and clicks. Expected: the same results. Skipped by the user's decision (DOSBox comparison).
- [x] K5. Save in the port, then load the slot in DOS. Expected: DOS loads the game; the party, champions, and objects are equal. Passed (manual, 2026-10-06).
- [ ] K6. Save in DOS, then load the slot in the port. Expected: the port loads the game; the party, champions, and objects are equal. Skipped by the user's decision (DOSBox comparison).
- [ ] K7. After K5 and K6, play one minute in each. Expected: no crash and no visible difference in the game state. Skipped by the user's decision (DOSBox comparison).

## L. Advanced mode

Details: `docs/advanced-mode.md`.

- [ ] L1. Run `build/dm2 --save tests/saves/start.DAT`. Press W, S, A, D, Q, and E. Expected: forward, back, strafe left, strafe right, turn left, turn right, as the arrow keys.
- [ ] L2. Run `build/dm2 --mode vanilla --save tests/saves/start.DAT`. Press W, Q, F5, and F9. Expected: nothing happens; no `game/DATA/QUICK.DAT` is written.
- [ ] L3. In the advanced mode, open the save dialog (Option+S, SAVE) and type a slot name with W, A, S, D, Q, and E. Expected: the letters go into the name; the party does not move.
- [ ] L4. In the advanced mode, open an inventory and rename a champion with the name editor. Expected: W, A, S, D, Q, and E type letters.
- [ ] L5. In the dungeon view, press F5. Expected: the white line "GAME SAVED" in the message area; `game/DATA/QUICK.DAT` exists; no `QUICK.BAK`.
- [ ] L6. Walk some squares, then press F9. Expected: "GAME LOADED"; the party is back on the square of L5; no dialog shows.
- [ ] L7. Delete `game/DATA/QUICK.DAT` and press F9. Expected: "NO QUICK SAVE"; the game goes on.
- [ ] L8. Write some bytes of garbage into `game/DATA/QUICK.DAT` and press F9. Expected: "QUICK SAVE DAMAGED"; the game goes on unchanged.
- [ ] L9. Press F5 and F9 with an open inventory, during the pause (Esc), during the sleep, and in the save menu. Expected: nothing happens.
- [ ] L10. Load a slot, play more than 100 ticks, press F5, then choose QUIT in the save menu. Expected: the program asks before the quit (a quick save is not a saved game).
- [ ] L11. Start with `--mode vanilla` and choose RESTART in the save menu. Expected: the program starts again in the vanilla mode.
- [ ] L12. Run `build/dm2 --mode turbo`. Expected: the message "unknown mode turbo: use vanilla or advanced" and exit code 2.
- [ ] L13. In the dungeon view, press Tab. Expected: the automap covers the viewport; the party square with a red frame is in the middle; the readout shows the level, X, Y, the direction, and the three cheats OFF. Press Tab, M, and Esc in turn after opening with Tab or M. Expected: each closes the automap and the dungeon view comes back.
- [ ] L14. Walk through some rooms and open the automap. Expected: the squares that the view showed are drawn with the minion map icons; squares behind walls and closed doors stay black.
- [ ] L15. With the automap open, wait 10 seconds and press W and the arrow keys. Expected: the party does not move; the game time stops (no creature moves after the close).
- [ ] L16. Drag the automap with the left mouse button. Expected: the map moves in whole squares and stops at the map edges; after a close and a new open, the party is in the middle again.
- [ ] L17. Press I, F, and L with the automap open. Expected: each readout line changes to ON and back to OFF. Press I, F, and L in the dungeon view. Expected: no cheat changes.
- [ ] L18. Turn I on, close the automap, and let a creature hit the party until a champion has 1 health. Expected: the champion does not die.
- [ ] L19. Turn F on and wait some minutes. Expected: the food and water gauges in the inventory do not go down. Eat or drink. Expected: the gauges go up.
- [ ] L20. In a dark place, turn L on and close the automap. Expected: the dungeon view shows full light.
- [ ] L21. Turn the cheats on, then press F9 after a quick save. Expected: the readout shows all cheats OFF.
- [ ] L22. Explore, press F5, save in a slot, restart, and load the slot; then press F9. Expected: the automap shows the explored squares after each load. Load a vanilla save. Expected: an empty automap except the current view.
- [ ] L23. In the vanilla mode, press Tab and M. Expected: no automap.

## Movies (2026-10-10)

Start `build/dm2` without `--save` unless an item says otherwise. Use a window with sound.

- [ ] M1. Start. Expected: the Interplay picture for 3 seconds, then `FTL` with sound, then `INTRO` with sound, then the start menu with its music and its colors.
- [ ] M2. Look at the size of the pictures. Expected: `FTL` small in the middle of a 4:3 area; `INTRO` fills about 90 % of the 4:3 area; black borders on the sides of a wide window.
- [ ] M3. `--mode vanilla`: click during the splash and during `FTL`. Expected: they go on. Click during `INTRO`. Expected: the start menu shows.
- [ ] M4. `--mode advanced`: click during the splash, `FTL`, and `INTRO`. Expected: each stops at once. Press keys during a movie. Expected: nothing; the start menu does not get the keys.
- [ ] M5. Start with `--save game/DATA/SKSAVE0.DAT`. Expected: no movie; the game starts at once.
- [ ] M6. Complete the game (the end sensor). Expected: the screen goes black, `END` plays, then `CREDITS`, then the program ends with exit code 1. A click in `END` starts `CREDITS`.
- [ ] M7. Let the party die and choose QUIT GAME. Expected: `CREDITS` plays, then the program ends with exit code 2.
- [ ] M8. Close the window during `INTRO`, and during `CREDITS` of M7. Expected: the program ends at once (exit code 0 and 2); no start menu. Send `quit` over the control socket during `INTRO`. Expected: the same.
- [ ] M8b. Let the party die. Close the window (or send `quit`) during the "THE END" dialog. Expected: `CREDITS` plays; a second close stops it; the exit code is 2.
- [ ] M9. Choose RESTART in the save menu. Expected: the start menu, no movie.
- [ ] M10. Resize the window during a movie. Expected: the picture stays 4:3 with black borders.
- [ ] M11. Watch `INTRO`, `END`, and `CREDITS` to the end. Expected: clean pictures without broken blocks; the sound fits the picture.
- [ ] M12. Rename `game/FTL` and start. Expected: the splash, then `INTRO`, then the start menu. Rename it back.
- [ ] M13. With the control socket (`docs/llm-control.md`, section "Movies"): `state` during the splash gives `"mode":"movie"`; `click` skips by the rules of M3 and M4.
- [ ] M14. Optional, DOSBox (`tools/dosbox-dm2.conf`): compare the size and the position of the `INTRO` picture with the port.

## Known deviations

Do not report these documented deviations as bugs. Details are in `docs/skull-formats.md`.

- The default mode is `advanced`. Use `--mode vanilla` for the items A to K that compare the port with the original (`docs/advanced-mode.md`).
- Only the SVGA movies; the movie screen is the largest 4:3 rectangle of the window with linear filtering; a key does not skip a movie; RESTART plays no movies (section 15).
- No DOS layer: no disk, drive, or memory dialogs; menus and dialogs wait without busy loops (section 14).
- `--save PATH` starts the game at once, without the start menu and without the dialog after a load (section 14).
- RESTART starts the program again; the dialog texts show the last folder of the `--data` path in capitals as the drive path (section 14).
- The port hides the system cursor and draws the original cursor images: the arrow, the hand, the held object with its shadow, and the dragged champion. The held object shows only over the cursor zones where the original shows it; menus and dialogs show no object icon. The original cursor in menus and dialogs is unverified (section 13).
- The enter and leave events of the cursor zones and the mouse drag through a fake wall are not ported (section 10).
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
