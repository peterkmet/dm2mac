# Weather, day and night, light, and Skullkeep machines

The rules of the world: day and night, the weather, the party light, the object transfer sensors of the Skullkeep machines, and the sound requests. The code is in `src/engine/weather.c`, `src/engine/sensor.c`, and `src/engine/sound_request.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Day and night

- One hour is `0x555` game steps; a day has 24 hours. `dungeon_load` sets the day time offset: GDAT value `(3, 0, 0x0b, 0)` (clamped to 0 to 23) × `0x555`.
- `g_light_by_hour` (`0x760ec`) gives the darkness of each hour: 5 at night, 1 by day. `world_update_day_night(false)` sets the hour light again when the game time reaches `g_next_hour_time` (saved, game record `+0x38`), and then the next hour time is 1 hour later.
- `weather_init` also sets the hour light. A load calls it before the game record is read, so the hour light comes from the game time before the load until the next hour starts. The port keeps this.

## Weather

- The sky state of a map is GDAT value `(8, graphics set, 0x0b, 0x66)`. `g_sky_state_table` (`0x75be2`, 4 bytes per state): byte 0 tells that the weather shows (states 1 and 4 in `GRAPHICS.DAT`), byte 2 that rain shows in the view.
- A weather cycle (`weather_init(0)`) clears script byte `0x40`, selects a storm curve (0 to 3) and a multiplier (1 to 3), and queues the first storm step (event `0x54`) after 500 to 8499 game steps. The first cycle of a run uses curve 3, multiplier 1, and 0 to 499 game steps.
- A storm step (event `0x54`, `world_update_day_night(true)`) adds multiplier × the curve value of the step to the storm intensity (0 to 255) and queues the next step after 50 to 305 game steps. After 32 steps a new cycle starts.
- Each game step (`world_update_day_night(false)`): without a storm, the clouds thin out and lightning comes with a chance of 1 in 64. With a storm, the sky darkness is the storm intensity; rain starts by chance and then follows the storm intensity; rain grows the clouds; lightning comes more often from intensity `0xcd` on.
- Sky objects (`g_sky_objects`, 10-byte records, ended by `0xff`): rain (`0x67` to `0x69` by sky darkness from `0x10`, `0x40`, `0x80`; `0x80` sets heavy rain), clouds (`0x6a` to `0x6c` by cloud cover from `0x40`, `0x80`, `0xc0`), and a lightning bolt (`0x64` to `0x66`) at a random place between its zone and the next zone. The weather update builds them again in each step while the weather shows.
- Lightning: in a storm from intensity 60 (random), it strikes 1 to 8 random floor squares of the map. A creature is struck only when it is near the party or in front of it (creature info byte `+2` not `0xff`). The party square is struck only from game time 12000 on, in a strong storm (`0xb6`), and when script byte `0x40` is above a random threshold (4 to 7). A strike creates explosion `0xffb0` ([creatures.md](creatures.md#projectiles-explosions-and-magic)). A strike near the party, seen while the inventory is open or the party sleeps, adds 1 to script byte `0x41`. The graphics set of map 1 gives a fixed square instead (value key `0x6c`: x high byte, y low byte), which gets a square event. Without a strike, a bolt shows in the sky; every second bolt thunders after a delay. The flash sets the light level to 0 for one step.

## The party light

- `light_update_party_light` sets `g_light_level` (0 bright to 5 dark). A map without bits 12 to 15 in its object counts word is always at level 1.
- The light sum: the torches (items with attribute flag `0x10`) in the leader hand and in the champion hands, by charges through `g_light_power_table`, the first fully and each next one half as much (after one bubble pass, not a full sort); the square light sum; the magic light (`g_magic_light`); the map value `0x67`; and the daylight (`0x7570e`, by hour light + heavy rain) while the weather shows.
- The level is the first of `99, 75, 50, 25, 1` that the sum is above, at least the map value `0x68`. An explosion light sum above 12 makes the view one level darker.
- `light_add_square_light` adds the light of one square around the party: lit wall ornaments and windows (value keys `0xf8` and `0x63`), floor ornaments, explosions and spell missiles, and creatures, less by distance. Its caller is the creature search of `ai_refresh_party_maps`.

## Skullkeep machines: object transfer sensors

- Sensor kinds `0x3b`, `0x40`, `0x47`, `0x48`, `0x49` on walls and floors move objects from the event square to the sensor target (`0x47` and `0x49`: from the target to the event square). `0x40` moves the items of an item class (`money_parse_item_class_set`, [champions.md](champions.md#money-and-shops)). `0x48` and `0x49` move one object only. Floor sensors `0x3b` and `0x40` take objects from all cells.
- An object that matches the item of the sensor data (item `0x1ff`: any object) moves with `transfer_object`: its charges become full, empty, or random (sensor flags bits 3 and 4), and on a floor it lands in a random cell. Objects in the possessions of creatures on the square move too. Bones of a champion bring the champion back (`champ_revive`) unless sensor flag bit 2 is set.

## Sound requests

- `sndreq_request` (`src/engine/sound_request.c`; in the original one delay word: 0, 1, negative, or a longer delay): `SOUND_NOW` plays immediately; `SOUND_STEP_END` goes to the queue of the game step (20 requests), which `sndreq_flush_requests(false)` plays at the end of the step; `SOUND_DEFERRED` goes to the deferred queue (6 requests, `sndreq_flush_requests(true)`). `sndreq_request_after(ticks)` with ticks 0 or 1 is `SOUND_NOW` or `SOUND_STEP_END`; a longer delay keeps the sound in one of 8 slots and queues event `0x15` (priority = sound priority, data word = slot) at game time + ticks - 1. A sound at the end of the step or after a delay plays only on the party map or the map of the other side of a seamless passage. A sleeping party hears half the volume.
- The place of a sound seen by the party (lateral, forward) and the push away by the walking distance are `sndreq_party_offset` of `engine/sound_request.c` ([sound.md](../formats/sound.md#playback-volume-and-stereo-position)). A sound that is already in the queue at the same place is not added again. A map change (`gdat_load_map_graphics`, `sound_wait_and_clear_requests`) drops the queue of the step.
- `start_requests` (`sound_play_requests` in the original) sorts the requests by priority and then loudness (a bubble sort, the port follows it) and starts them in that order while voices are free. Only its HMI driver calls (`hmi_digi_sample_done`, `hmi_digi_start_sample`) are the DOS layer: `sound.c` plays the voices. `sndreq_request_with_fallback` uses the sound of index fallback when index has none.
- The ambient sound of an animated ornament (`start_ambient_sound`) plays immediately only when the animation cycle starts now; a wall sound comes from the square in front of the wall.

## Deviations from the original

- Uninitialized data of the original: a storm step event (`0x54`) keeps 6 data bytes from the stack; the port sets them to 0. A transfer sensor with charge mode 3 adds an uninitialized amount; the port adds 0.
- Bounds: the thunder delay divides by the storm multiplier; the port uses 1 for a multiplier of 0 (the original divides by 0). A sky state past the table shows no weather (`GRAPHICS.DAT` has states 0 to 4 only).
- Fix: an ornament frames value of `0x8000` gives a cycle of 1; the original divides by 0.
- Kept quirk of the original: `light_update_party_light` makes one bubble pass over the hand lights, not a full sort, so the brightest torch moves up by one place only and the half-weight sum depends on the hand order.
- Kept quirk of the original: a floor ornament without light (value key `0xf8` is 0 and not animated-only) hides all other light of its square: `light_add_square_light` adds no missile, explosion or creature light there.
- `view_enter_map` holds the view part of `gdat_load_map_graphics`. Its callers (`game_enter_map` and the pit fall in `move_follow_square_transitions`) then call `world_update_day_night(false)`, as the original does at the end of `gdat_load_map_graphics`. `view_enter_map` builds no sky objects: the weather update builds them.
- The second fall view of a pit below a pit (`move_follow_square_transitions`, with its map change and weather update) runs only with the debug flag `g_debug_flag_g` of the original. In the game it never runs; the port keeps the code, and no test can reach it.
- The port reads the map graphics flags (rain, clouds, lightning pictures) and the sky state from the party map when it needs them; the original keeps them in globals that `gdat_load_map_graphics` sets.
- Sound requests: every sound of `GRAPHICS.DAT` counts as loaded, so a sound can play where the original has not loaded it. The original plays only the sounds that `gdat_load_map_graphics` loads for the map (`sound_find_loaded`, table from `gdat_load_entry_sets`).
- Event `0x15` keeps its data bytes 2 to 5 at 0 (uninitialized in the original). `sndreq_request_delayed` masks the slot to 0 to 7.
- The delay of a sound is a tick count from 0 to 65535 (`sndreq_request_after`). In the original it is a signed 16-bit word, so there an ornament sound delay (ornament value `0x88`) of `0x7fff` to `0xfffe` goes to the deferred queue; the port queues the sound through the timeline. `GRAPHICS.DAT` has the delays 0 and 2 only.
- The port does not check the sound settings of `start_requests` (digital sound on, sound volume not 0). The volume setting scales the mix of the sound voices ([game-flow.md](game-flow.md#deviations-from-the-original)).
- The walking distance of a sound is `creature_get_party_distance` (the party distance map).
- Kept quirk of the original: an audible creature generator (`0x2e`) requests its sound from GDAT category 0 (`CAT_GENERAL`), which holds no sound, so the new group appears without a sound.
- Kept quirk of the original: the repeated ambient sound of a wall sensor (event `0x5a`, `timeline_event_sensor_ambient_sound`): the original takes the step to the square in front of the wall from `g_dir_dx` and `g_dir_dy` with the high byte of the sensor thing as the index, so it reads the constant data after those tables (thing sizes, text escape words, creature info). The port uses the same values (`ambient_wall_dx` and `ambient_wall_dy` in `src/engine/sensor.c`). Most of these sounds come from far away and do not play. `tests/differential/test_world.py` compares the repeated sounds with the original.
