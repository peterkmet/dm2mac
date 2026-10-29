# Champions, inventory, items, money, and shops

The rules of the champions: the leader hand, damage and time effects, the actions, money and shops, item creation by a sensor, merchant trading, the cursor, and the first room of `start.DAT`. The code is in `src/engine/champion.c`, `src/engine/champion_damage.c`, `src/engine/champion_action.c`, `src/engine/champion_inventory.c`, `src/engine/item.c`, `src/engine/money.c`, `src/engine/merchant.c`, `src/engine/recruit.c`, and `src/ui/input.c`. The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## The leader hand

- `g_leader_hand_object` holds the object of the leader hand. Beside it the original keeps its attribute word, its weight, and its 16 × 16 icon. The port keeps them in `g_leader_hand` (`src/engine/include/champion.h`) and draws the icon at the mouse pointer.
- A click on an object zone of the view takes the object (`click_take_object`). With an object in the hand, the click places the object in the cell (`click_place_object`), in an alcove, or into an open chest. A click in the upper view throws the hand object (`click_throw_object`). The commands `0x14` to `0x41` click the inventory slots, `0xea` to `0xed` put the hand object into a free slot.
- `champ_apply_item_effects` adds or removes the stat effects of an item in a slot. Mode 0 is the load, 1 adds, -1 removes, 2 and -2 are the timed effects of eaten items, 3 restores. The load applies mode 0 to all slots. Then `restore_timed_effects` restores the light power and the timed effects.

## Damage and time effects

- `champ_take_damage` adds the damage to `g_pending_damage` and the wounds to `g_pending_wounds`. `champ_apply_pending_damage` applies them at the end of the step: health, the damage star (event `0x0c`), and death (`kill_champion`, bones in the champion cell).
- `champ_apply_time_effects` runs every game step for each living champion: food, water, stamina, health and mana regeneration (mana: maximum mana / 40 + 1 per gain), the decay of raised stats to their base, and the poison.

## Actions

- `g_action_repeat` has 2 records of 8 bytes for each champion: count, steps left, a fresh flag, the action, and the thing. `champ_process_repeated_actions` runs them each step.
- `champ_perform_action` runs the command of an action ([GDAT](../formats/graphics.md#gdat-items) text field `CM`) with its skill, stamina cost, delay, and charges. A melee action at a closed door plays the door sound `(0x0e, door type, 0x8d)`; a hit creature plays `(0x0f, creature, 0x8d)`. Then the action plays its sound (text field 13) at the party, except after a reload and after a shot. The commands of creatures, spells, minions, and missiles call the rules of [creatures.md](creatures.md).
- `champ_add_experience` uses the last combat time and the experience factor of the map. A level gain prints a message in the color of the skill class (green, yellow, red, blue).

## Money and shops

- The coin types are the miscellaneous items with the coin flag (`ATTR_FLAGS` bit 14), sorted by value. `money_split_amount` takes the most valuable coins first.
- A merchant creature has price lists 0 to 4. `money_parse_item_class_set(creature, n)` reads GDAT text `(0x0f, creature, 5, 0x10 + n)`: letters choose the item kind (`W` weapons, `A` clothing, `J` miscellaneous, `P` potions, `C` containers, `S` scroll), numbers and ranges choose the items. The shop display uses n = `0x10` + list × 3 (the items of the list; GDAT entry `0x20` + list × 3). The prices use n = `0x11` and `0x12` + list × 3, the item sets of two price factors (creature values `0xa1` and `0xa2` + list × 3, in percent). The margin is creature value `0xa0` + list × 3 (default 10 %).
- A shop is a wall sensor of kind `0x3f` with a shop ornament (ornament value `0x0a` = 2). Its data byte is the merchant creature. Its flags: bit 0 "item chosen", bit 2 "step forward", bits 7 to 10 the price list. Its target square holds the display sensor (kind `0x30`). The data of the display sensor is the shown item index.
- `show_shop_display` chooses the item when bit 0 is clear or bit 2 is set: the next item (bit 2, or a click above the center of the ornament) or the last item (a click below the center) of the item order table `(9, ornament, 8, 0x13)` that is in the item set of the list. A unique item (`ATTR_FLAGS` bit 12) shows only when it lies on the square, when a creature there holds it, or when the party has it. A table word with bit 15 makes a new item on the target square when the square does not have it. Then the function draws the item (offset value `0xfd`), the empty display (entry `0x11`) when no item is there, and the overlay (entry `0x0f`). In front of the party it also draws the item name (offset `0xf2`) and the sell and buy prices as coin pictures (entry `0x10`, 8 × 7 pixels for each coin type, offsets `0xf1` and `0xf0`).
- The map graphics load has all entries of the map ornaments, including the item order table. `gdat_get_entry_data_or_null` gives entries of other categories only when they are loaded.
- `g_view_hit_offset` (`0x802d4`) is the place of the last click in a view picture: the vertical and the horizontal distance from the picture center, in percent of half the picture size. The port keeps the vertical value in `g_view_hit_offset_y`.

## Item creation by a sensor

- A wall sensor of kind `0x3c` makes a new item when a square event reaches it: with the effect set, or with the effect clear when the sensor is inverted (flags bit 5). Its data is the item index. A new record of the thing type gets the GDAT index. Flags bits 3 and 4 both set keep the item from reuse (`dun_set_thing_do_not_discard`).
- `transfer_object` sets the charges by flags bits 3 and 4: 0 full, 1 empty, 2 random up to full, 3 an uninitialized word. Miscellaneous item 0 keeps its charges. The item goes to the target square (target word: x bits 6 to 10, y bits 11 to 15, cell bits 4 and 5), to a random cell when the target is not a wall. Flags bit 2 selects the move mode of `move_thing`.

## Merchant trading

- A merchant faces a counter: a group in front of it. Coins (item filter `0x10`) and goods (filter `7`) on the counter are the offer. The AI plan of the merchant runs four commands: `shop_cmd_wait_for_offer` waits for coins or goods in the customer cells; `shop_cmd_evaluate_offer` compares the value of the offer with the price of the goods in the merchant cells (in whole coins, at most 18 coins, with a random discount of up to 15 % for a price above 16); `shop_cmd_count_payment` checks the payment and keeps the change in the active group (`position` word); `shop_cmd_pay_change` normalizes the coins of its money boxes and gives the change in coins (into its money box, or into its possessions).
- The active group words `counters[0]` and `counters[1]` hold the patience and the last offer. The actions are `0x1b` refuse, `0x1c` accept, `0x1d` wait, `0x1e` idle, `0x1f` offer, `0x20` haggle.
- `sum_items_value` adds the merchant values (`money_get_item_value`, price list from group word 8) of the items of a filter in a cell, even inside chests; -1 when none matches.

## The cursor

- The hand cursor. `input_set_mode`, `input_pop_mode`, and `input_refresh_mode` mark the input zones of the input tree of the mode whose conditions hold (`input_mark_active_zones`). Byte 6 of an input zone (bits 0 to 5) is its cursor zone, from 1. `mouse_area_add` shows the cursor zone of an input zone that becomes active; `mouse_area_remove` hides the cursor zone of an input zone that becomes inactive. With both changes in one mode change, it stays as it is.
- The 18 cursor zones (mouse areas in the original; `g_cursor_zones`, `0x722d4`, 10 bytes): layout zone (bit 15: in the viewport, `g_viewport_origin` (0, 40)), cursor kind, flags (bits 0 to 3: priority), enter event, leave event, list node. Kind 1: zones `0x5c`, `0xa1` to `0xa4`, and `0x07` (the viewport, 2 zones). Kind 2: zone `0x2f` (the party icons, active only while a champion is dragged). The other zones have kind 0. The shown zones are a list: a new zone goes in front of the first zone of the same or a lower priority.
- `mouse_move_event` (`0x14483`) takes the first shown zone that holds the pointer. Kind 2 gives cursor image 3 (the dragged champion). Kind 1 with a leader gives image 2 (the leader hand object, `g_cursor_has_item`) or image 1 (the hand). All other positions give image 0 (the arrow). A menu or a dialog (input mode 4) shows no cursor zone of kind 1.
- The cursor images (`mouse_set_cursor_image`, `0x14743`: image, bitmap, hot spot, size, bpp, palette, clear color). `input_init` (`0x147d1`) sets image 0, the arrow (`g_cursor_arrow_bitmap`, `0x7155a`, 12 × 16), and image 1, the hand (`g_cursor_hand_bitmap`, `0x715ba`, 16 × 16). Both are 4 bpp (high nibble first) with the UI colors (`g_ui_colors`) and clear color 12, hot spot (0, 0). `mouse_update_hand_cursor` (`0x14889`) sets image 2, the leader hand object: 18 × 18, 8 bpp, no palette, hot spot (8, 8). `mouse_draw_item_cursor_bitmap` (`0x14375`) fills it with the hand icon palette color of color number 12. It draws the 16 × 16 icon at (2, 2) with all colors black (`g_ui_colors[0]`), the shadow, and then the icon at (0, 0) with its palette. Both skip color number 12. The clear color is the palette color of color number 12 (`0x7fbc6`). `champ_drag_click` (`0x149ba`) sets image 3 when a drag starts: the face of the champion (`ui_draw_champion_face_icon`), 17 × 17, 8 bpp, hot spot (8, 6). The face goes through its palette onto a fill of value 12, without its color 4 pixels. The clear color is the face palette color of color number 12.

## The first room of `start.DAT`

- The table of the first room can be pushed. The shelf face (1, 5) behind it shows a sun crest holder (text `0x885c`); while the holder is full, it hides the cabinet with the solid key (text `0x8854`, ornament `0x86`). When the party takes the sun crest, the face shows the cabinet, and its clicks give its items, the solid key last. No other trigger is needed.
- The gold coin on the table of the first room does not open the door. No wall slot takes it, as in the original. The door opens with the solid key in the keyhole (4, 5) cell 3 and the door button (4, 4) (sensor kind `0x46`), which toggles the door (3, 4).

## Deviations from the original

- Kept quirk: `money_normalize_coins` (`0x29598`) takes the coins out of the box one by one and creates each new coin item.
- Kept quirk: the item ranges of a money box text (`money_coin_type_for_slot`) and of a merchant class set (`money_parse_item_class_set`) use base -1 for a range without a letter before it, so such a number gives the item index number - 1.
- Kept quirk: `item_name` formats the item name twice: `gtext_gdat` formats it, then `gtext_format` formats the result again.
- Kept quirk: the fourth rune writes the end mark of the runes into the byte after the rune field (`unknown_26[0]` of the champion record).
- Kept quirk: `add_to_party` (`0x49242`) ends the name at a space or after 7 characters and drops the next character; the title starts after it. The 8th character of a longer name is lost.
- Kept quirk: `recruit_finish` (`0x49a17`) with command `0x93` (cancel) takes the object out of the leader hand and does not put it anywhere: the object is lost.
- Kept quirk: `champ_cure_poison` (`0x475d3`) reads only as many timeline slots from the start as the timeline holds events. A poison event in a later slot (the events before it were deleted, for example the events of creatures that died or stayed behind) gets no cure: an antidote does nothing. The advanced mode reads all slots.
- Kept quirk: bit 15 of the attack value of a melee action (`FIELD_ATTACK`) also makes the hit pierce, and the hit chance ignores the bit (`melee_attack_front`).
- Kept quirk: when the quiver holds no throwing item, a thrower reloads its hand with what lies in the quiver slot (slot 12), even an item that cannot be thrown (`take_next_missile`).
- Not ported: `click_throw_object` (`0x22942`) tests the zones `0x300` and `0x301` instead of `0x2fd` and `0x2fe` when the type of the square in front is `0x11`. The type is bits 5 to 7 of the square byte (0 to 7, `0x7f52c`), so the original never takes this branch.
- Fix: `melee_hit_damage` reads its remainder uninitialized for a strength of 0; the port starts the remainder at 0.
- Fix: `item_icon_index` divides by the step of the icon animation attribute (bits 5 to 7 of value key `0x06`) in the modes by charges and time; the shipped GRAPHICS.DAT has no step of 0. For a step of 0 the port shows the first icon.
- Fix: the time effects (`champ_apply_time_effects`) lower a statistic above its maximum by the current value divided by the maximum; the original divides by 0 for a maximum of 0 (a damaged save). The port leaves such a statistic unchanged.
- Fix: `item_add_charges` returns a register of no meaning for a thing type without charges; the port returns 0.
- Fix: the third action of the Kalan Gauntlet (`SK18SK6`, skill 186) reads the experience of another champion record or the data after the champion array in the original; the port treats a skill outside the skill table as untrained, so the action (level 4) never shows and gives no experience.
- Fix: `money_normalize_coins` skips a coin type with a value below 1 (a damaged GRAPHICS.DAT); the original loops forever when the remainder of the amount reaches that coin type.
- Fixes: the original leaves data bytes of some events uninitialized (the timed effects of eaten items, poison, the hide damage event, invisibility). The port sets them to 0.
- Fix: `add_to_party` reads the champion data without a test that the GDAT entry exists; without the entry, or with an entry shorter than its 52 bytes (a damaged GRAPHICS.DAT; the original reads past it), the port leaves the health, stamina, mana, statistics, skills, food and water of the champion at 0.
- Fix: `recruit_start` without a mirror on the wall takes an uninitialized portrait; the port does not recruit, and the start champion of a new game does not join.
- Fix: `money_parse_item_class_set` writes outside the set for bad numbers; the port writes only inside it.
- Fix: `show_shop_display` loops for ever when the merchant sells no item of the list, and reads a NULL record without a display sensor; the port stops after 512 tries, and draws nothing without a display sensor.
- Fix: `shop_cmd_evaluate_offer` divides by the price in coins, 0 when the merchant cells hold no goods; the port takes 100 %.
- Fix: The save writes the container order of a minion link that is no container (the attack and guard minion items) from past its table; the port writes 0.
- Fix: `frighten_front_group` passes a leftover register (EDX) as the damage of `creature_alert`; the port passes 0.
- Fix: `item_create_by_index` and the item creation sensor ask for a record of type `0xffff` for an item index without a thing type; the port gives no item.
- Fix: The item creation sensor with charge mode 3 adds an uninitialized stack word to the charges; the port adds 0, as for the transfer sensors ([world.md](world.md#deviations-from-the-original)), and the oracle (the original in the emulator `dm2tools.emu`) sets the word to 0.
- Fix: A coin type below 0 reads before the coin table; the port gives no coin.
- The port hides the system cursor. Each present (the game loop and the modal loops of menus and dialogs) draws the image of `input_cursor_at` at the mouse pointer (`present_with_cursor`, `input_draw_cursor`). The original cursor in menus and dialogs is unverified: the driver (`IBMIOP.EXE`) is not analyzed.
- The load and a new game clear the pending damage and wounds, the action repeats, the raised skills, the regeneration counter, the leader hand, and the recruit candidate of the last game (`champ_reset_step_state`).
- The leader hand icon (`g_leader_hand_icon`) starts at 0 in the original (BSS), and a load keeps it. The port starts it at `0xff` (no icon) and sets `0xff` on each load. The screen does not change.
- Not ported: the mouth animation of `champ_consume_object` when the leader hand feeds a champion (4 frames of GDAT `(7, 0, 0x25/0x26)` in zone `0x221`, 8 vertical retraces each). The food takes effect immediately.
- The eye (command `0x47`, zone `0x222`, `input_hold_right_button`) and the mouth with an empty leader hand (command `0x46`, zone `0x221`) start the mouse capture: they set their flag (`0x7f224`, `0x7f21c`) only while the left button (button bit `0x02`) is down, and hide the pointer. The release of the left button (event type 4) gives command `0xe3`, `input_cancel_mouse_capture`, which clears both flags. The port sends event type 4 only while the eye or the mouth is held, because the `IBMIOP` handler that makes the event is not analyzed. After `input_flush` and `mouse_hide`, the original eye waits 8 vertical retraces (`wait_vblanks(8)`); the port does not wait.
