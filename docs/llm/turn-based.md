# Turn-based mode

The commands, turns, batches, reply fields, and actions of the turn-based mode, and the requests during a movie. The start of the game, `dm2-play`, the protocol, and the errors of the real-time commands are in [Control socket](control-socket.md).

## Turn-based commands

| `cmd` | Fields | Reply |
|---|---|---|
| `get_state` | none | `state` |
| `get_view` | none | `view` |
| `get_map` | none | `map` |
| `act` | `action`, `args` | After the turn: `ticks`, `stopped` (after a displacement), `game_over`, `bumped`, `action_damage`, `health_lost`, `look`, `seen`, `state`, `view` |
| `act_many` | `actions` | After the batch: `ticks`, `ran`, `stopped`, `error`, `game_over`, `bumped`, `action_damage` (a list), `health_lost`, `look`, `seen`, `state`, `view` |
| `wait` | `ticks` (1-5000, default 1) | After the turn: `ticks`, `stopped`, `game_over`, `health_lost`, `seen`, `state`, `view` |
| `save` | none | `{"ok":true}`. The game writes `QUICK.DAT`. If the write fails, the old file stays. |
| `load` | none | `state`. The game loads `QUICK.DAT`. |
| `new_game` | none | `state`. The game starts a new game from `DUNGEON.DAT`, on the start square. |
| `state` | none | The control state of the real-time modes ([state](control-socket.md#state)). |
| `screenshot` | `path` | `path`. |
| `quit` | none | `{"ok":true}` |

- A failed request changes nothing in the game, except a failed `load` (see [Deviations from the original](#deviations-from-the-original)).
- `key`, `click`, `press`, `release`, `command`, and `move` are refused: `use act in turnbased mode`. Use `act click` and `act key`.
- An unknown `cmd` gives `unknown cmd`. A request without `cmd` gives `missing cmd`.
- `act`, `act_many`, `wait`, and `save` give `game over` after a game over. The other commands still work.
- `save` gives `answer the candidate first (act recruit)` while a champion candidate is shown, and `cannot write the save file` if the write fails. `save` is allowed with no champion.
- `load` gives `no saved game (use save first)` if `QUICK.DAT` does not exist, `the saved game is damaged` if it does not load, and `cannot write QUICK.TMP: nothing loaded` if the game cannot keep a copy of the running game. The game continues unchanged, except after `the saved game is damaged`: then the game is restored from `QUICK.TMP`, one game step later (see [Deviations from the original](#deviations-from-the-original)). If the copy does not load either, the game is lost.
- `load` and `new_game` also work after a game over. Each ends the old game without asking. After `load`, the game time is the time of the save plus 1 (see [Deviations from the original](#deviations-from-the-original)).
- `new_game` gives `cannot start the new game` if `DUNGEON.DAT` does not load; then no game is running.
- While no game is running (a failed restore or a failed `new_game`), every command except `load`, `new_game`, and `quit` gives `no game is running: use load or new_game`.
- `save`, `load`, and `new_game` give `close the dialog first (act click or act key)` while the game runs a modal loop (a menu, a picture, a dialog, or the name editor).

## Turns

An `act` turn does these steps:

1. It queues the action's inputs one by one (at most 4) on the same path as a player's input: a key, a click on a command zone, or a click at a screen point. The next input is sent once the input pipeline is idle.
2. It runs the game one game step per frame. One tick of the turn protocol is one game step.
3. It ends when at least 2 steps ran, all inputs are sent, the input is idle, and the party can move again (no move cooldown, no walk in progress, no input wait). It also ends after 64 steps, and immediately when the game is over.

If a dialog is open and the game does not tick, a turn ends after 16 frames for each step of its maximum: 64 steps for `act`, `ticks` steps for `wait`.

A `wait` turn runs `ticks` game steps (at most 5000). It ends early on news (the `stopped` values below). The real-time [wait](control-socket.md#wait) keeps its own limit of 6000 timer ticks.

`stopped` in a `wait` reply:

| Value | Meaning |
|---|---|
| `done` | All `ticks` ran. |
| `game_over` | The party died or the game is complete. |
| `damage` | A champion lost health. |
| `creature` | A creature group came into view that was not in view before. |
| `message` | The game printed a new message. |

The checks run in this order: `game_over`, `damage`, `creature`, `message`.

An `act` reply has `stopped` only if the dungeon displaced the party:

| Value | Meaning |
|---|---|
| `moved` | The party is not on the target square of the step, or it is on a different map (a teleporter, a pit, stairs). |
| `turned` | The party does not face the direction that the action gives (a spinner). |

`bumped` is `true` after a step that left the party on its square: a wall, a closed door, or a creature is in the way. A teleporter that sends the party back to its own square counts as a bump (see [Deviations from the original](#deviations-from-the-original)).

## Batches

`act_many` takes `actions`, a list. An item is an action name (`"turn_left"`) or an object (`{"action":"take_object","args":{"cell":2}}`). Only spaces and commas separate the items. A string value cannot contain a `"` character.

The batch runs the items in order, one turn each. After each turn it stops if one of these is true. The checks run in this order:

| `stopped` | Cause |
|---|---|
| `game_over` | The game is over. |
| `bumped` | The step bumped. |
| `moved`, `turned` | The dungeon displaced the party. |
| `damage`, `creature`, `message` | News, as for `wait`. |
| `failed` | An item cannot run: `error` has the reason. The game does not change for this item. |
| `done` | All items ran. |

A batch reply has `ran` (the turns that ran), `stopped`, `error` (only with `failed`), and `ticks` (the game steps of all turns). `action_damage` is a list: one value for each `choose_action` that ran. `look` has the text of the last item only. A bad first item gives `ok` true, `ran` 0, `stopped` `failed`, and the `error`. An item that is not a name or an object gives `an item of actions is not an action name or object`. If `actions` is not a list, the reply is `actions must be a list` (`ok` false).

## Reply fields

| Field | When | Content |
|---|---|---|
| `ticks` | always | The game steps that ran. |
| `stopped` | see above | Why the turn, the batch, or the wait stopped. |
| `ran` | batch | The turns that ran. |
| `error` | batch, `failed` | The reason. |
| `game_over` | always | `true` if the party died or the game is complete. |
| `bumped` | after a bump | `true`. |
| `action_damage` | `choose_action` | The action's damage (a number; a list in a batch). |
| `health_lost` | a champion lost health | A list: the health that each champion lost since the start of the request. |
| `look` | `look` | `{"name": "...", "lines": ["...", ...]}`: the text of the eye panel. |
| `seen` | see below | What the party saw during the request. |
| `state`, `view` | always | The state and the view at the end. |

The `look` text:

- An object's eye panel is given as text lines. The panel shows some parts only as pictures; the text uses these English words for them: `charges` (charges gauge), `strength` (weapon strength gauge), `throw` (weapon throw power gauge), `armour` (armour class gauge), `power` (potion power gauge), and `total` (the total value of a money box). A gauge line is `<word> <value>/<maximum>`.
- In these lines, the items of a chest and the coin counts of a money box (`<coin name> <count>`) are separated by a comma, as many to a line as fit. The panel shows them as icons and numbers in slots. A scroll gives its name and weight before its text; the panel shows only the text. More than 8 lines are cut off.

`seen` is present only if the party saw an explosion or a creature group lose creatures during the request. It shows things that are gone at the end.

- `explosions`: a list of `{name, x, y, depth, side}`. `name` is a name such as `fireball`, or the explosion type number.
- `creature_changes`: a list of `{type, x, y, depth, side, before, after}`. `after` is 0 if the group was killed. Each group has one creature, so `before` is 1.
- `side` is `center`, `left`, `right`, `left_2`, `right_2`, `left_3`, or `right_3`.
- The seen record of a turn (`seen.creature_changes`) gives `before` 1 and `after` 0 for a creature group that the party saw and that the game killed; a group that left the view and whose record is free counts as killed.

## State, view, and map

The fields of `get_state`, `get_view`, and `get_map` are in the [LLM playbook](llm-playbook.md): [State fields](llm-playbook.md#state-fields), [View fields](llm-playbook.md#view-fields), [Map fields](llm-playbook.md#map-fields), [Slots](llm-playbook.md#slots), and [Cells](llm-playbook.md#cells). The JSON shows only what a player sees. This section gives the edge cases that the player does not need.

`get_state`:

- The game state JSON gives object, coin, and action names without the trailing spaces of their [GDAT](../formats/graphics.md#gdat-items) texts (`FINE ROBE ` gives `FINE ROBE`). Other characters stay as GDAT gives them.
- The JSON skill ranks are the texts of the skills panel. The panel shows the master ranks with a power rune glyph of the font (`` ` `` and `a` to `e`, characters `0x60` to `0x65`); the JSON gives the upper-case rune name for it: `LO`, `UM`, `ON`, `EE`, `PAL`, `MON` (`LO MASTER FIGHTER`).
- The slot dialogs (`0x80` load, `0x81` save) do not set the dialog id; the JSON gives the id and the button texts of the last dialog that the game drew.
- [Protocol vocabulary](control-socket.md#protocol-vocabulary) lists the slot names (`slots[].where`) in slot number order.
- The JSON weather uses the same thresholds as the sky pictures:
  - `clouds` from the cloud cover: `light` from 64, `medium` from 128, `heavy` from 192;
  - `rain` from the sky darkness: `light` from 16, `medium` from 64, `heavy` from 128;
  - `wind`: the wind direction of the game.
- Rain or clouds are `none` if the map's sky set has no rain or cloud picture.
- `day_time` comes from the darkness of the hour (1 to 5): 1 is `day`, 5 is `night`, otherwise `dawn_dusk`.

`get_view` gives the squares that the party sees now ([seen squares](../rules/view.md#seen-squares)).

- The view JSON gives the squares of the seen rule, from the walk position while the party walks. It gives only what the view draws there:
  - wall faces (`side_face`, `front_face`) as in the wall face table of the view;
  - creatures, objects, missiles, and explosions only on the view positions and square aspects that draw them: nothing at depth 2 side 2; at depth 4 only creatures, and only on a floor, pit, teleporter, or door seen from the front, nothing at side 3; nothing on a door seen from the front beside the party; no creature on the party square while the party walks;
  - a floor ornament from depth 0 to 3 (the party row too, which the view draws), not ornament 0, and not at depth 3 side 2 when the ornament has the "not far side" key;
  - a door button only to depth 3;
  - no limit: the element, door state, and map glyph show at every depth, as the automap shows them.
- A wall face gives what the ornament on that face holds: `mirror` (portrait kind), `alcove`, `shop`, or `fountain` (the touch key). Otherwise it gives `switch` (a click sensor with an ornament, or a hand switch text) and `lock` (the ornament index of a sensor or an item holder text that takes an object); otherwise `ornament` (the index). A text plate gives `text` on the front face and `null` on a side face or for a hidden text, as the view draws text only on a front plate.
- The JSON has no creature names, wall ornament names, or floor ornament names (the game data has none); it gives the index.
- `piles` and `alcove` are the cells of the last drawn view.
- The view JSON lists things that the view does not draw in these cases: the objects and missiles in the back cells of the party square (the view draws only its two front cells); the things in the far cells of a closed door, which the door hides. `button` shows even for a door seen from the side (as in the minion map).

`get_map` gives the explored squares of the party's map. The explored squares are the seen squares of the automap ([view.md](../rules/view.md#seen-squares)).

- The map JSON marks a floor square with `_` when the minion map draws its floor ornament without the details option.

## Game over and the modal loops

- After a game over, the reply has `game_over` true. Use `load` or `new_game`.
- A save, a load, or a new game from the game's save menu runs a modal loop. A raw click on the game's save button opens the save menu; `save` writes the file without it.
- While a dialog is shown, `act click` and `act key` are the way out. `state.dialog` has the id and the button texts.

## Actions

The actions, their `args`, and what they do are in [Actions](llm-playbook.md#actions) of the LLM playbook. While a candidate is shown, an action other than `recruit`, `click`, and `key` gives `a candidate is shown: answer with recruit first`. While the party sleeps, an action other than `wake`, `click`, and `key` gives `the party sleeps: use wake or wait`. An unknown name gives `unknown action`; a missing name gives `missing action`. `key` takes a name of [key](control-socket.md#key) or a key code.

| Action | Errors |
|---|---|
| `move_forward`, `move_back`, `move_left`, `move_right` | `close the inventory first (close_inventory)` |
| `turn_left`, `turn_right` | same |
| `click_wall_ahead` | `the view takes no click now`, `nothing to click on the wall ahead`, `the leader hand is not empty: nothing to use it on ahead` |
| `take_object` | `cell must be 0-3 (see get_view piles)`, `the leader hand is not empty: drop or store the object first`, `no leader`, `no object at this cell` |
| `drop_object` | `cell must be 0-4`, `the leader hand is empty`, `no floor ahead`, `no floor to drop on ahead`, `no alcove ahead` |
| `open_inventory` | `no living champion with this index`, `the inventory cannot open now` |
| `close_inventory` | `the inventory cannot close now` |
| `put_in_slot` | `the leader hand is empty`, `slot must be 0-37`, `open the inventory of this champion first (open_inventory)`, `no open chest`, `this slot cannot be clicked now` |
| `take_from_slot` | `the leader hand is not empty`, and as `put_in_slot` |
| `eat`, `drink` | `open the inventory of the champion who eats or drinks first`, `take the food or drink in the leader hand first` |
| `choose_action` | `hand must be ready or action`, `this champion cannot act now`, `index out of range of the action menu (see state champions hands)`, `another champion's action menu is open: use pass first`, `close the open panel first (pass)` |
| `pass` | `the open panel has no close button` |
| `set_caster` | `no living champion with this index`, `close the open panel first (pass)`, `this champion cannot cast now`, `the spell icons take no click now` |
| `set_leader` | `no living champion with this index`, `close the open panel first (pass)`, `the spell icons take no click now` |
| `add_rune` | `symbol must be 0-5`, `no spell panel (set_caster)`, `the spell has four runes`, `not enough mana for this rune` |
| `delete_rune` | `no spell panel (set_caster)`, `no rune` |
| `cast` | same |
| `recruit` | `no candidate: click a champion mirror first (click_wall_ahead)`, `choice must be join or cancel` |
| `sleep` | `no leader`, `the inventory cannot open now` |
| `wake` | `the party cannot wake now` |
| `swap_champions` | `a and b must be living champions`, `a and b must be different champions`, `close the open panel first (pass)` |
| `look` | `open an inventory first (open_inventory): the eye is in the inventory`, `the leader hand is empty: take an object, or give champion and slot`, `the leader hand must be empty to look at a slot`, `no object in this slot` |
| `take_coin` | `open a money box first (choose_action on the hand that holds it)`, `slot must be 0-5`, `no coin in this slot` |
| `minion_map_option` | `open a minion map first`, `this minion map has no options`, `option must be 0-3` |
| `click` | `x must be 0-319 and y 0-199`, `button must be left or right` |
| `key` | `unknown key` |

## Movies

The program plays the start and end movies ([manual.md](../manual.md#start-screens), [movies.md](../formats/movies.md)). While one of them plays:

- `state` gives `"mode": "movie"` and `"dialog": null`.
- `click` and `press` skip the movie, as a mouse click in the window does ([movies.md](../formats/movies.md#deviations-from-the-original)). `release`, `key`, and `command` reply `{"ok":true}` and do nothing.
- `wait` counts game timer ticks. These ticks do not advance during a movie, so the reply comes after the movies.
- `screenshot` saves the game screen (320 × 200), not the movie.
- `quit` stops the movies and ends the program.

To reach the start menu quickly: `click` until `state` gives a different mode. In the `vanilla` mode, the splash picture and the `FTL` movie take about 8.5 seconds.

## Deviations from the original

- The names of the inventory zones eye (`0x221`) and mouth (`0x222`) are swapped relative to the input table: the input table gives zone `0x221` to the mouth command `0x46` and zone `0x222` to the eye command `0x47` ([champions.md](../rules/champions.md#deviations-from-the-original)). The port keeps the names. The `look` action presses the zone of the eye command in the input table.
- A turn gives a command as a click on the top left pixel of its first zone in the current input mode. The zone of the leader inventory command (`0x52`) is the whole screen, and a click there hits the status box of champion 0 first. The `sleep` action therefore opens the leader's inventory with its status box command (`0x07` + leader).
- The port keeps the text of each message row next to its pixels. The original has only the pixels and the expiry time of each row. The text moves up and clears with the row.
- A turn distinguishes a bump (`bumped`) from a displacement (`stopped` `moved` or `turned`) by the party square, map, and facing before and after the action. Dungeon Master (DM) has a teleport counter for a teleporter that sends the party back to its square; DM2 has none, so such a teleporter counts as a bump.
- The turn-based `load` starts the loaded game, which runs one game step: the game time after a `load` is the time of the save plus 1. A damaged `QUICK.DAT` restores the running game from `QUICK.TMP` the same way, so the failed `load` also adds one game step.
