# LLM playbook

You play Dungeon Master II (PC DOS `SKULL.EXE`) in this C port, through the turn-based mode. Your goal: the party completes the game. When the game is complete, the turn response has `"game_over": true` and `state.party.dead` is `false`.

You need no other page to play. A rule on this page that starts with "Hint:" is not checked in play: test it, and write the result in `lessons.md`.

## Your notes

Your files in `playthroughs/current/player/` are your memory. Keep each file short and up to date. Each file starts with a header that says what to write and in which format: keep the header and follow it. For a new level, copy the header of `map_L1.md`. Limit: `lessons.md` and each map have at most 80 lines. When a file is longer, merge and shorten it before the end of the session.

| File | Content |
|---|---|
| `progress.md` | Where the party is, the time of the last save, the current goal, the next steps, open problems, and a milestone log. |
| `lessons.md` | What you learned from play: what happens after an action, what works and what does not. |
| `party.md` | Each champion: role, place, statistics, skill levels. Each mirror: where it is and which champion it shows. |
| `items.md` | Each item you saw: stats, price, and where it is now. |
| `map_L<n>.md` | Your map of level `n`: keys and locks, switches, hidden plates, pits, teleporters, stairs, creatures, merchants and shops, outdoor squares, puzzles. |
| `spells.md` | Each spell you found: runes, mana cost, effect, who can cast it. |
| `bestiary.md` | Each creature type (its number in the view): where you met it, how strong it is, what works against it. |
| `bugs.md` | What looks wrong in the game (this port is under test). |

## The party

- A new game starts with one champion, Torham. The party has up to 4 champions.
- Recruit the other champions from mirrors. Stand in front of a mirror (the wall face has `mirror`) and send `click_wall_ahead`. The champion shows as `state.candidate`. Read its statistics and skills, then send `recruit` with `{"choice":"join"}` or `{"choice":"cancel"}`. Write each mirror and its champion in `party.md`.
- Each champion has health, stamina, and mana, and the statistics strength, dexterity, wisdom, vitality, antimagic, and antifire.
- The party has 2 rows: `front_left`, `front_right`, `back_right`, `back_left` (`champions[].position`). The front row fights hand to hand and takes most of the hits from the front. The back row casts spells, throws objects, or shoots. `swap_champions` swaps the places of two champions.
- The party turns as one. A creature attacks the champions on the side that faces it: a creature behind the party hits the back row.
- The leader carries the object in the hand (`leader_hand`) and uses it on walls, doors, and floors. `set_leader` chooses the leader.
- A dead champion leaves bones. Hint: some places bring a champion back from the bones. Keep the bones in a pack.

## Health, stamina, mana

- **Health:** a champion with 0 health dies. Potions, healing spells, and sleep give health back.
- **Stamina:** fighting, walking, and carrying a load use stamina. A champion at 0 stamina loses health. Sleep gives stamina back.
- **Mana:** spells cost mana. Mana comes back over time, faster during sleep.
- **Poison:** a poisoned champion (`poisoned`) loses health until the poison ends or a cure removes it.
- **Wounds:** `wounds` lists the hurt body parts (`ready_hand`, `action_hand`, `head`, `torso`, `legs`, `feet`). A wound makes the champion weaker.

## Food, water, sleep

- The champions eat and drink all the time. The maximum of `food` and `water` is 2048. While a value is 0 or above, the champion gets stamina back. Between -512 and -1, the stamina stays. Below -512, the champion loses stamina. A champion whose stamina goes down to 0 loses health.
- Sleep makes the champions use food and water four times as fast: the champion clock runs each 16 game steps instead of 64.
- Carry a food reserve in the packs. Take every food item that you find. Eat only when a champion is below half, and keep the rest. Hint: some creatures drop food when they die; note where they live.
- Before you go to a new level, make sure each champion has food above a quarter (512), or carry food in the packs for each champion below that.
- Food is finite, and the champions eat with game time. Save game time: sleep only for low health, stamina, or mana; do not wait long for one creature; go back to an earlier level only for a clear reason.
- To eat or drink: put the food or the drink in the leader hand (`take_from_slot`), send `open_inventory` for the champion who eats, then `eat` or `drink`.
- A fountain (wall face `fountain`): `click_wall_ahead` with an empty leader hand fills the water of all living champions. With an empty flask or a water container in the leader hand, the click fills it.
- Sleep gives back health, stamina, and mana, but time goes on: food and water go down and creatures move. `sleep` opens the inventory of the leader first. Damage or `wake` wakes the party. After `wake`, check `state.inventory`, and send `close_inventory` before you move.

## Light and load

- `party.darkness` is 0 (bright) to 5 (dark). In the dark the view shows less: creatures, pits, and wall features in the far rows stay hidden.
- Torches and light spells give light. Both fade with time. A torch gives light from a hand of a champion or from the leader hand.
- Outdoors, the daylight gives light by day; the night and heavy rain make it darker (see [Outside and weather](#outside-and-weather)).
- Each champion can carry a limited weight (`load_kg`, `[current, maximum]`). Strength sets the maximum; stamina below half of its maximum lowers it. A heavy load uses more stamina.

## Skills and training

- Each champion has 4 base skills: `fighter`, `ninja`, `priest`, and `wizard`. `skills` gives the `level` and the `rank` text of each. A skill gets a new level when the champion uses it. A new level prints a message.
- Each base skill has 4 sub-skills. The state does not show them; a sub-skill grows with its base skill. Fighter has swing and parry; ninja has steal, fight, throw, and shoot. Hint: the other sub-skills are as in Dungeon Master: fighter thrust and club; priest identify, heal, influence, and defend; wizard fire, air, earth, and water.
- Fighter: hand-to-hand attacks. Ninja: fast attacks, throwing, shooting, stealing. Priest: healing, potions, protection. Wizard: attack spells.
- A new level gives more health, stamina, or mana and better statistics.
- Before you go to a new level, follow the training rule in `progress.md`.

## Magic

- A spell is a row of up to 4 runes: a power rune, then a rune of each of the next three sets. The rune names:

  | Set | Runes (symbol 0 to 5) |
  |---|---|
  | 1 power | `lo`, `um`, `on`, `ee`, `pal`, `mon` |
  | 2 | `ya`, `vi`, `oh`, `ful`, `des`, `zo` |
  | 3 | `ven`, `ew`, `kath`, `ir`, `bro`, `gor` |
  | 4 | `ku`, `ros`, `dain`, `neta`, `ra`, `sar` |

- To cast: `set_caster` with the `champion`, then `add_rune` with `symbol` 0-5 for each rune, then `cast`. The symbol is the place of the rune in the set of the next rune. `state.spell` gives `caster`, `chosen` (the rune names), and `available[]` (`rune`, `name`, `mana`): the runes of the next set and the mana that each costs this caster.
- A stronger power rune makes a stronger spell and costs more mana.
- Chosen runes stay after a stopped or failed cast. Before you add runes, check `state.spell.chosen` and send `delete_rune` until it is empty.
- The spell kinds are potions, projectiles, party auras, summons, and others. Hint: a potion spell needs an empty flask in a hand.
- Scrolls (`text` of the object) and wall texts teach spells. Write each spell in `spells.md`.
- `set_caster`, `set_leader`, and `swap_champions` need the action menu or panel closed: send `pass` first.

## Fights

- Each champion attacks with the object in a hand. `hands` gives the action menu of the `ready` hand and of the `action` hand. `choose_action` with `champion`, `hand`, and `index` (the place in the list) runs the action.
- `action_damage` gives the result: above 0 the damage, 0 no damage, -1 nothing (for example a champion is in front), -2 no ammunition, -6 the action cannot work. -3 to -5 is a failed spell.
- `busy` gives `[ready hand, action hand, spell]`: `true` while the champion must wait before it acts with it again.
- Hint: some creatures take damage only from magic or special weapons.
- A closed door blocks creatures. Hint: a door that closes on a creature hurts it.
- Before a fight with a creature type that is in `bestiary.md`, read its section again. Use what works against it, and avoid what does not work.
- Before a timed action (a timed switch, a door that closes, a run past a trap), check `wounds`, health, and stamina of each champion. Heal first.

## The dungeon

- **Doors:** a door opens with a button, a key in a lock, a switch, or a spell. Some doors break under attacks (`door` is `broken`).
- **Keys and locks:** each key fits locks of its own kind. A `lock` face gives the number of its picture: record which key fits which number.
- **Switches and plates:** wall switches and floor plates open doors, close doors, open pits, move walls, or start traps. Some are hidden.
- **Walls:** some walls are illusions that the party can walk through.
- **Pits:** a pit can be the way down or a trap.
- **Teleporters:** a teleporter moves the party or objects to another square. A teleporter shows only while it is on and visible; otherwise it looks like floor.
- **Stairs:** stairs lead to the level above or below.
- **Alcoves:** an alcove holds objects. `view.alcove` lists them. Some alcoves react to the object that you put in.
- **Machines:** some wall and floor places move objects to another place.
- A cell can hold a pile of many objects. `view.piles[].objects` lists every object of the pile, top first. Take every object of a pile: send `take_object` for the cell until the pile is gone, then decide what to keep.

## Money

- Coins are objects. A money box holds coins.
- To open a money box: hold it in a hand of a champion and send `choose_action` with that `champion` and `hand` (give `index` 0). `state.money_box` lists each coin slot: `slot` (0-5), `coin` (the coin name), and `count`.
- `take_coin` with `slot` moves one coin of that slot to the leader hand. The leader hand must be free for the coin. `pass` closes the money box.
- `look` on a money box gives the coin counts and the `total`.
- Hint: put a coin back into the money box with the coin in the leader hand and the money box open. Test it and write the result in `lessons.md`.
- Write the coin names and their values (when you learn them) in `items.md`.

## Merchants

- A merchant is a creature in the view (`creatures[].type`, a number). It stands behind a counter. The counter is the square between the party and the merchant.
- Trade: the merchant shows its goods on its side of the counter. Put your offer (coins, or goods to sell) on your side of the counter: `drop_object` with cell 2 or 3 (the near cells of the square ahead). The merchant compares the value of the offer with the price of the goods. It accepts, refuses, or haggles, and gives change in coins. When `drop_object` gives `no floor to drop on ahead`, the square ahead takes no object: write it in `lessons.md`.
- Hint: take the bought goods and the change with `take_object` on cells 2 and 3. When `take_object` gives `no object at this cell` for an object that you see, note it in `bugs.md`.
- A merchant needs time to answer: send `wait` with some ticks after the offer, then look at the counter again.
- **Shops on walls:** a wall face with `shop` is the display of a shop. `shop` gives the name of the item on display (`true` before the display chose an item). A click above the center of the display shows the next item, a click below the center shows the item before. Hint: `click_wall_ahead` clicks the center; use `act click` with `x` and `y` to page. Check with a screenshot where the display is.
- Write each merchant and shop, its goods, and its prices in the map of the level and in `items.md`.

## Minions

- Some items and spells give minions: creatures that work for the party.
- A minion map is an item. Hold it in a hand and send `choose_action` for that hand: the minion map opens as a panel. `state.minion_map` gives `powered` (whether its mana is paid) and `options[]` (`option` 0-3, `name`: `creatures`, `details`, `items`, `hold`; `on`).
- `minion_map_option` with `option` 0-3 turns an option on or off. `pass` closes the map.
- The minion map takes 1 mana of the champion when it opens; a champion without mana opens it unpowered. While it is open and powered, it takes 1 mana every 25 game steps. Each option that is on costs mana when you turn it on (`creatures` 2, `details` 3, `items` 1, `hold` 1) and makes the map take mana faster: the interval is 25 game steps divided by (1 plus the mana of the options that are on). When the champion has no mana left, the map loses its power. Close it when you do not use it.
- A click on the grid of the map sends the minion there. Use `act click` with `x` and `y`; check the grid in a screenshot first.
- Hint: the action menu of a minion item (`hands`) gives the minion commands. Test each command and write it in `lessons.md`.

## Outside and weather

- `state.outdoor` is `true` when the party sees the sky. Only then the state has `weather` and `day_time`.
- `weather.clouds` and `weather.rain` are `none`, `light`, `medium`, or `heavy`. `weather.wind` (`north`, `east`, `south`, `west`) is present only while rain falls in the view.
- `day_time` is `day`, `night`, or `dawn_dusk`. One hour is 1365 game steps; a day has 24 hours. The night is dark: carry a light.
- A storm comes and goes. In a strong storm, lightning can strike floor squares, even the party square. Get under a roof in a heavy storm.

## Saves

- A save keeps the game. After a death or a mistake, `load` goes back to the last save.
- There is one save slot (`QUICK.DAT`). `save` overwrites it. Save after each milestone (key, door, puzzle, new level, new champion) and before each risk: a pit, a fight with a new creature type, a timed switch, a teleporter.
- Do not save while a champion is poisoned, starving, or below 25 health, or when the party is in a fight: you cannot undo a bad save.
- Use only `save` and `load` of `dm2-play`. Do not click the save button of the game: it opens the save menu of the game.
- `save` gives `answer the candidate first (act recruit)` while a candidate shows.
- The save does not keep the sleep or the pause: a game saved while the party sleeps or while the game is paused loads with an awake party in move mode.
- The coordinator keeps a checkpoint after each session. When the save is bad or the party cannot survive from it, ask in your report for the checkpoint of an earlier game time.

## When you are stuck

1. Stop moving. Save.
2. Read your own notes again: every lock, key, door, merchant, and shop in the map of the level and in `items.md`. Try each key that you carry in each lock that you know, even in locks that you did not list as exits.
3. Walk over each square of the area once and look at the objects on the floor in the view. Turn to each wall of a dead end.
4. Write a list into `progress.md` under "Not tried yet": wall features you have not clicked, keys and items you have not tried on each lock or alcove, piles you did not empty, wall texts you did not read, goods you did not buy, unexplored edges of the map, walls you did not test for an illusion, doors you could not open.
5. Rank the list. Visible things come first: keys, piles, alcoves, switches, locks, doors, shops. Hidden switches and illusory walls come last.
6. Try the most probable item first. After each result, rank again.

## Bugs

This port is under test. When something looks wrong, write it in `bugs.md` with the steps and the state. If the game stops answering or a bug blocks progress, stop and report.

## How to play

### Start of a session

1. Read this file.
2. Read your files in `playthroughs/current/player/`: `progress.md`, `lessons.md`, the map of the current level, `party.md`, `spells.md`.
3. Run `uv run dm2-play --brief get_state`. If the position or the time does not match `progress.md`, run `uv run dm2-play load` and check again.
4. When the coordinator gives you a goal, it replaces the goal in `progress.md`. Write it into `progress.md` first.
5. Count the calls of the run: `wc -l < playthroughs/current/coordinator/calls.log`. Note the number. The call limit of the coordinator counts from it.

### Rules

- Play only through `uv run dm2-play`, in the repository root. Do not read or change source code, game data files, or the decompiled code. Do not search the internet for tips, maps, or walkthroughs.
- Your general knowledge of Dungeon Master II is allowed.
- Do not start or stop the game program. The coordinator runs it. Do not send `quit` or `new_game`.
- The user watches the window. Before each `dm2-play` call, write one short line of text: what you do next and why.
- Each call costs tokens. Use `--brief` for each call. Plan several steps per call: a whole route or a whole fight in one `act_many`, a long rest in one `wait`. Keep your text and your thinking per call short: do not repeat the state, decide the next steps.
- `act_many` stops early when something happens (see [Batches](#batches)). Check why it stopped before you continue.
- A script may compute a route or an action list from `get_map` and send it as one `act_many`.
- A script must not loop over `get_state`, `get_view`, `wait`, or `act`: each turn is your decision.
- Ask for `get_map` only when you plan a route: one `get_map` per route plan, not one per step. The `act`, `act_many`, and `wait` responses already contain the state and the view.
- The view gives wall texts (`text`), locks (`lock`), and shops (`shop`). Use screenshots to read the minion map, a shop display, or a dialog, and to check a suspected bug.

### Commands you use most

| Goal | Command |
|---|---|
| State in short form | `uv run dm2-play --brief get_state` |
| What is ahead | `uv run dm2-play --brief get_view` |
| Explored map | `uv run dm2-play --brief get_map` |
| Several actions | `uv run dm2-play --brief act_many '["move_forward", "turn_left", "move_forward"]'` |
| Pass time | `uv run dm2-play --brief wait '{"ticks": 50}'` |
| Save, load | `uv run dm2-play save`, `uv run dm2-play load` |
| Sleep | `uv run dm2-play --brief act sleep`, then `wait`, then `uv run dm2-play --brief act wake`, then `uv run dm2-play --brief act close_inventory` |
| See the screen | `uv run dm2-play screenshot '{"path": "playthroughs/current/coordinator/shot.png"}'`, then read the PNG |

### View a screenshot as an LLM

1. Save the screenshot to a file.

   ```sh
   uv run dm2-play screenshot '{"path": "playthroughs/current/coordinator/shot.png"}'
   ```

2. Open the path of the reply with the Read tool. The Read tool shows a PNG file as an image.

The image pixels are the game pixels, so a position in the image is the `x` and `y` of `act click`.

### dm2-play

`dm2-play` sends one request to the game and prints the answer as compact JSON on one line. The exit code is 1 when the answer has `"ok": false`.

```sh
uv run dm2-play <cmd> [json-fields]
uv run dm2-play act <action> [json-args]
uv run dm2-play act_many '<json-list>'
```

Examples:

```sh
uv run dm2-play act take_object '{"cell": 2}'
uv run dm2-play act choose_action '{"champion": 0, "hand": "action", "index": 0}'
uv run dm2-play act look '{"champion": 0, "slot": 1}'
```

`--brief` (before the command) prints a short summary instead of the JSON:

- The turn line: `ticks`, `ran`, `stopped`, `error`, `game_over`, `action_damage`, `health_lost`, and `BUMPED` after a bump.
- `look: <name> | <line> | ...` after `look`, and a `seen:` line with the explosions and the killed creatures of the turn.
- The time, the level, `x`, `y`, the facing, the darkness, and the sky when outdoors; the leader and the leader hand.
- `CANDIDATE (not recruited)` while a mirror shows a champion.
- One line per champion: health (`HP`), stamina (`ST`), mana (`MA`), food, water, `poisoned`, `DEAD`.
- The open action menu, spell panel, money box, minion map, and dialog; the messages.
- The view: `here`, `ahead`, `ahead 2` to `ahead 4`, the side squares that hold something and the side passages (`left` or `right`, then `far left` or `far right` at 2 squares from the lane, `outer left` or `outer right` at 3 squares), the piles (top first), the alcove, and `wall ahead: clickable`.
- For `get_map`: the `grid`.

Each call appends a line to `playthroughs/current/coordinator/calls.log`.

### Batches

A batch (`act_many`) runs many `act` turns with one request, one turn per item, in order. After each turn the batch stops early when something happens, so that you can look again. An item is an action name (`"move_forward"`) or an object `{"action": "take_object", "args": {"cell": 2}}`. Separate the items with commas and spaces. A string must not contain a `"` character.

```sh
uv run dm2-play --brief act_many '["turn_left", "move_forward", {"action": "take_object", "args": {"cell": 2}}]'
```

The game does not find paths. Plan the path from your own map and send it as `act_many`.

The response has `ran` (the number of turns that ran) and `stopped`. The checks run in this order:

| `stopped` | Reason |
|---|---|
| `game_over` | The party died, or the game is complete. |
| `bumped` | A move did not change the party square ([Bump](#bump)). |
| `moved`, `turned` | The dungeon moved or turned the party ([Displacement](#displacement)). |
| `damage` | A champion lost health. |
| `creature` | A creature group is in view that was not in view before the turn. A group that stays in view does not stop the batch again. |
| `message` | The game printed a new message. |
| `failed` | An item cannot run. `error` gives the reason. The failed item is not in `ran`, and it changed nothing. |
| `done` | All items ran. |

After a batch, `action_damage` is a list with one value for each `choose_action`; `look` is in the response only when the last item was `look`.

`health_lost`: a list with one value for each champion (the health that the champion lost since the start of the request). It is only in the response when a champion lost health.

### Bump

When a move action (`move_forward`, `move_back`, `move_left`, `move_right`) does not change the party square, the response has `"bumped": true`. A wall, a closed door, or a creature is in the way. Without a bump, the response has no `bumped` field. A teleporter that sends the party back to its own square also counts as a bump.

### Displacement

After each `act` turn and each batch step, the game compares the party facing, square, and map with what the action does:

- A move action changes the square by one step in its direction, or nothing (a bump).
- `turn_left` and `turn_right` change the facing by 90 degrees.
- All other actions change neither.

When the dungeon changes the party in a different way, the turn has a displacement:

| `stopped` | Reason |
|---|---|
| `moved` | The square or the map changed in a different way: a teleporter, a pit, stairs. |
| `turned` | The facing changed in a different way: a spinner. |

`moved` comes before `turned` when both happen. An `act` response has `stopped` only after a displacement.

### Wait

A `wait` turn runs `ticks` game steps (1-5000, default 1). It checks for news after each tick and ends immediately when it finds news: `game_over`, `damage`, `creature`, `message`. The response has `stopped`: `done` when all ticks ran, otherwise the news. `ticks` gives the ticks that ran.

Example: rest until something happens, at most 5000 ticks.

```sh
uv run dm2-play --brief wait '{"ticks": 5000}'
```

### Seen

An explosion lasts only a few ticks. It can be gone before the turn ends, so the `view` of the response does not show it. A creature can die in the same way. Thus an `act`, `act_many`, or `wait` request records what the party sees after each tick.

The response has `seen: {explosions, creature_changes}` when the record is not empty. Without a record, the response has no `seen` field.

- `explosions[]`: `{name, x, y, depth, side}`, one entry for each explosion type on each square. `depth` and `side` give the place in the view where the party first saw it. Here `side` is a word: `center`, `left`, `right`, `left_2`, `right_2`, `left_3`, `right_3`.
- `creature_changes[]`: `{type, x, y, depth, side, before, after}` for each creature group in view that was killed. In this game, a group is one creature: `before` is 1, `after` is 0. A group that only leaves the view is not in the list.

### Game over

When the party dies or the game is complete during a turn, the turn ends. The response has `"game_over": true`. `state.party.dead` is `true` after a death and `false` when the game is complete. After that, `act`, `act_many`, `wait`, and `save` give `game over`. The other commands still work. After a death, send `load`.

### Actions

A champion index is the `index` of `champions[]`; the champion must be alive. While a candidate shows, only `recruit`, `click`, and `key` run. While the party sleeps, only `wake`, `click`, and `key` run; `wait` always runs. An action that cannot run gives an error and changes nothing.

| `action` | `args` | Notes |
|---|---|---|
| `move_forward`, `move_back`, `move_left`, `move_right` | none | Left and right step to the side. The inventory must be closed. |
| `turn_left`, `turn_right` | none | The inventory must be closed. |
| `click_wall_ahead` | none | Clicks the switch, lock, mirror, door button, or alcove ahead (Hint: also a shop display); otherwise touches the plain wall. With an object in the leader hand, the click uses the object (a key in a lock). The inventory must be closed. |
| `take_object` | `cell` 0-3 | The leader takes the top object of the cell. The leader hand must be empty. See `view.piles`. |
| `drop_object` | `cell` 0-4 | The leader drops the hand object: 0-1 own square, 2-3 square ahead, 4 the alcove ahead. |
| `open_inventory` | `champion` | |
| `close_inventory` | none | |
| `put_in_slot`, `take_from_slot` | `champion`, `slot` 0-37 | A click on the slot swaps the leader hand with the slot object: `put_in_slot` needs an object in the hand, `take_from_slot` an empty hand. Slots 0 and 1 work without the inventory; the other slots need the open inventory of the champion. See [Slots](#slots). |
| `eat`, `drink` | none | The champion of the open inventory eats or drinks the leader hand object. |
| `choose_action` | `champion`, `hand` (`ready` or `action`), `index` | Runs action `index` of the action menu of the hand (`champions[].hands`). A money box, a chest, or a minion map opens its panel instead. |
| `pass` | none | Closes the open action menu or panel. |
| `set_caster` | `champion` | Opens the spell panel of the champion. |
| `add_rune` | `symbol` 0-5 | See [Magic](#magic). |
| `delete_rune`, `cast` | none | |
| `recruit` | `choice`: `join` or `cancel` | Only while a candidate shows. |
| `set_leader` | `champion` | |
| `sleep`, `wake` | none | `sleep` opens the inventory of the leader first. |
| `swap_champions` | `a`, `b` | The two champions swap their places. |
| `look` | `champion`, `slot` (both optional) | Needs an open inventory. Without `slot`: the object in the leader hand. With `slot`: the empty leader hand takes the object of the slot (`champion`, default the open inventory), the eye looks, and the hand puts it back. See [Look](#look). |
| `take_coin` | `slot` 0-5 | One coin of the open money box to the leader hand. |
| `minion_map_option` | `option` 0-3 | Turns an option of the open minion map on or off. |
| `click` | `x` 0-319, `y` 0-199, `button` (`left` or `right`, optional) | A raw click on the screen. Use it only when no action does the job (a dialog, the minion map grid, a shop display). |
| `key` | `key` | A raw key: one character, `esc`, `enter`, `space`, `backspace`, `tab`, `f1` to `f10`, `up`, `down`, `left`, `right`. |

`click` and `key` also run while a dialog shows. `state.dialog` gives the `id` and the `buttons` of an open dialog.

### Slots

`state.champions[].slots[]` lists only the slots that hold an object: `slot` (the number), `where` (the name), and `object`.

| Slot | `where` |
|---|---|
| 0 | `ready_hand` |
| 1 | `action_hand` |
| 2 | `head` |
| 3 | `torso` |
| 4 | `legs` |
| 5 | `feet` |
| 6 | `ready_quiver_2` |
| 7, 8, 9 | `action_quiver_2`, `action_quiver_3`, `action_quiver_4` |
| 10 | `neck` |
| 11 | `ready_quiver_1` |
| 12 | `action_quiver_1` |
| 13 | `backpack_1` |
| 14-29 | `backpack_2` to `backpack_17` |
| 30-37 | the slots of the open chest |

The ready hand loads ammunition from `ready_quiver_1`, then `ready_quiver_2`. The action hand loads from `action_quiver_1`, then `action_quiver_2` to `action_quiver_4`.

### Chest

A chest in a hand opens with `choose_action` on that hand. `state.chest` lists the chest slots that hold an object (`slot` 30-37, `object`). `take_from_slot`, `put_in_slot`, and `look` with `slot` use these numbers. The game rules apply: an object that does not fit in a chest stays in the leader hand. A chest slot without an open chest gives `no open chest`. `pass` closes the chest.

### Look

The response of `look` has `look: {name, lines}`: the texts of the eye panel. `name` is the object name; `lines` are the description lines, for example the weight and the gauges as `<word> <value>/<maximum>`: `charges`, `strength`, `throw`, `armour`, `power`. A money box gives the coin counts and `total`; a chest gives its items. A scroll gives its name, its weight, and its text; the scroll text is also in the state (`text` of the object).

### Cells

`take_object` and `drop_object` use cells relative to the party direction:

| Cell | Where | Name in `view.squares[].objects[].cell` |
|---|---|---|
| 0 | own square, front left | `front_left` |
| 1 | own square, front right | `front_right` |
| 2 | square ahead, near right | `back_right` |
| 3 | square ahead, near left | `back_left` |
| 4 | the alcove ahead (`drop_object` only) | |

`view.piles` lists the objects of cells 0-3 that `take_object` can reach, and `view.alcove` the objects of the alcove ahead (`cell` 4). Hint: to take an object out of an alcove, try `click_wall_ahead` with an empty leader hand.

### State fields

- `time`: game steps. `game_mode`: `turnbased`. `mode`: the input mode (`move`, `inventory`, `recruit`, `sleep`, `dialog`, `menu`, `picture`, `pause`, `push`, `name`).
- `dialog`: `{id, buttons}` while a dialog shows, otherwise `null`. Answer it with `act click` or `act key`.
- `party`: `map`, `level`, `x`, `y`, `facing`, `darkness` (0 bright to 5 dark), `can_move`, `sleeping`, `dead`.
- `leader` (index, or `null`), `leader_hand` (object or `null`).
- `candidate`: the champion that a mirror shows, with the fields of a champion, or `null`. The candidate is not in `champions[]` and is never the `leader`.
- `champions[]`: `index`, `name`, `title`, `alive`, `health`, `stamina`, `mana` (each `[current, maximum]`), `food`, `water`, `load_kg` (`[current, maximum]` in kg), `position`, `poisoned`, `statistics` (each `[current, maximum]`), `skills` (`{level, rank}` for `fighter`, `ninja`, `priest`, `wizard`), `wounds`, `slots[]`, `hands` (`{ready, action}`: the action names of each hand), `runes` (the chosen rune names), `busy` (`[ready, action, spell]`).
- The values use the units of the game screen. `stamina` is the stored value divided by 10. `food` and `water` are the stored values (the screen shows them only as bars).
- `rank` is the rank text of the skills panel, for example `LO MASTER FIGHTER`. It is `null` below level 2.
- `action_menu`: `{champion, hand, actions}` while a menu is open, otherwise `null`.
- `spell`: `{caster, chosen, available[{rune, name, mana}]}`, or `null`.
- `inventory`: `{champion, panel}`, or `null`. `panel` is `food`, `skills`, `object`, `scroll`, `mouth`, or `none`.
- `chest`, `money_box`, `minion_map`: the open panel, otherwise `null`.
- `messages`: the rows of the message area that hold text.
- `outdoor`; outdoors also `weather` (`clouds`, `rain`, `wind`) and `day_time`.

An object is `{name, kind}`. `kind` is `weapon`, `armour`, `scroll`, `potion`, `container`, or `misc`. A scroll also has `text`.

### View fields

- `facing`, `darkness`.
- `squares[]`: the visible squares. `depth` is 0 (the party row) to 4. `side` is a number from -3 (outer left) to 3 (outer right); 0 is the lane of the party. Each square has `x`, `y`, and `element` (`wall`, `floor`, `door`, `pit`, `stairs_up`, `stairs_down`, `teleporter`).
- A square that is not a wall has `creatures[]` (`type`, `count`), `objects[]` (`cell`, `name`), and `flying[]`. A door square has `door` (`open`, `closed`, `partly_open`, `broken`) and `button` (`true` when it has a door button). A floor square can have `floor_ornament`: the number of its picture (for example a pressure pad). Record what each number means.
- `creatures[].type` is the number of the creature type: the game data has no creature names. Give each number a name in `bestiary.md`. A merchant is a creature too.
- A `flying` entry has `kind` and `name`. A projectile has `kind` `projectile` and, as `name`, the object name, or `spell` for a spell. An explosion has `kind` `explosion`, `type` (the number), and `name`: `fireball`, `slime`, `lightning`, `harm_non_material`, `door_toggle`, `poison_bolt`, `poison_cloud`, `push`, `pull`, `door_set`, `missile_wall`, `death_cloud`, `storm_lightning`, `storm_blast`, or `smoke`. A different type gives its number as `name`.
- A wall has `front_face` (the side that faces the party) and `side_face` (the side that faces the lane of the party) when the face shows something. A face is `{facing, ...}` with one of: `text` (a wall text; `null` where the view does not draw the text), `mirror`, `alcove`, `shop`, `fountain`, `switch`, `lock` (the number of the lock picture), or `ornament` (the number of a different picture). `facing` is the direction that the face points to.
- `piles[]`: `{cell, top, objects}` for each pile that `take_object` can reach now. `objects` lists every object of the pile, top first. `take_object` takes `objects[0]`, the same object as `top`.
- `alcove`: `{cell, top, objects}` of the alcove ahead, or `null`.
- `wall_ahead_clickable`: `click_wall_ahead` reaches a wall ornament (a switch, a lock, a mirror; Hint: also a shop display), a door button, or an alcove.
- While the inventory is open, `piles` is empty, `alcove` is `null`, and `wall_ahead_clickable` is `false`.

Visibility rule: the party square is in the view. A square is in the view when the square beside it toward the center is in the view and open, or when the square in front of it (one row nearer) is in the view and open. The rows end at the first center square that blocks sight (a wall, a closed fake wall, or a closed door). Nothing behind it is in the view. A square outside the map is a wall.

The view shows what a player sees. It shows no creature health.

### Map fields

`map`, `level`, `width`, `height`, `party` (`[x, y]`), `facing`, `legend`, `rows`, and `grid`. Row index is `y` (north at the top). Column is `x`.

- `rows`: one string per `y`, with exactly `width` characters, one per `x`. A square that is not explored is a space.
- `grid`: the same rows with indexes, for reading by eye. The first line is three spaces and then `x mod 10` for each column. Each next line is `y` in two characters, a space, and the row. The character of square (`x`, `y`) is at position `3 + x` of line `y + 1`. `dm2-play --brief get_map` prints `grid`.

| Character | Square |
|---|---|
| `@` | the party |
| `#` | wall |
| `.` | floor |
| `_` | floor with an ornament that the map draws, for example a pressure pad |
| `D`, `d` | closed door, open or broken door |
| `<`, `>` | stairs up, stairs down |
| `O` | open pit |
| `T` | teleporter |

```text
   0123456789
 0 ##########
 1 #...@....#
 2 ####D#####
```

The map shows the squares that the party saw. It shows no wall features: a wall is `#`. What is on a wall (switches, locks, alcoves, fountains, shops, texts) is in the view; keep it in your own map. The map shows the squares as they are now, even when they changed after you saw them.

### End of a session

Stop when the coordinator's limit is reached, when the game stops answering, or when a bug blocks progress. Count the calls with `wc -l < playthroughs/current/coordinator/calls.log` from time to time. 20 calls before the limit, save the game and update your files, then stop. The report: where the party is, party status, what you achieved, the current goal, bugs found, and a request for an earlier checkpoint when the save is bad.
