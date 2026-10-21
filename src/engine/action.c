/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include "action.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "hooks.h"

ActionArea action;

void action_reset_champion(int champ)
{
    g_hooks.reset_action_champion(champ);
    action.hand_items[champ][0] = action.hand_items[champ][1] = THING_NONE;
}

void action_reset(void)
{
    memset(&action, 0, sizeof action);
    action.mode = ACTION_MODE_NONE;
    action.open_container = THING_NONE;
    for (int i = 0; i < CONTAINER_SLOTS; i++) action.open_items[i] = THING_NONE;
    g_hooks.reset_action_area();
}

uint16_t action_open_hand_item(void)
{
    if (action.champion <= 0 || action.hand < 0 || action.hand > SLOT_ACTION_HAND) return THING_NONE;
    return action.hand_items[action.champion - 1][action.hand];
}

/* Takes the items out of a chest into the open container slots. */
void action_open_container(uint16_t container)
{
    if (container == THING_NONE || container == action.open_container) return;
    int i = 0;
    for (uint16_t t; (t = thing_word(container, CONTAINER_CONTENTS)) != THING_ENDOFLIST && i < CONTAINER_SLOTS; i++) {
        action.open_items[i] = t;
        uint16_t contents = t;
        /* unlink the first item from the contents list */
        set_thing_word(container, CONTAINER_CONTENTS, dungeon_next_thing(contents));
        set_thing_word(contents, THING_NEXT, THING_ENDOFLIST);
    }
    for (; i < CONTAINER_SLOTS; i++) action.open_items[i] = THING_NONE;
    action.open_container = container;
}

/* Puts the items of the open container slots back into the chest. */
static void close_container(void)
{
    uint16_t container = action.open_container;
    if (container == THING_NONE) return;
    action.open_container = THING_NONE;
    for (int i = 0; i < CONTAINER_SLOTS; i++) {
        uint16_t t = action.open_items[i];
        if (t == THING_NONE) continue;
        action.open_items[i] = THING_NONE;
        set_thing_word(t, THING_NEXT, THING_ENDOFLIST);
        uint16_t last = thing_word(container, CONTAINER_CONTENTS);
        if (last == THING_ENDOFLIST) {
            set_thing_word(container, CONTAINER_CONTENTS, t);
            continue;
        }
        while (dungeon_next_thing(last) != THING_ENDOFLIST) last = dungeon_next_thing(last);
        set_thing_word(last, THING_NEXT, t);
    }
}

/* Closes the open hand or spell panel. */
void action_area_close(void)
{
    if (!action.champion) return;
    if (!action.result || action.redraw_delay < 2) action.redraw_delay = 0;
    action.champion = 0;
    action.mode = ACTION_MODE_HANDS;
    action.flags = 0;
    close_container();
    action.dirty = 1;
}

void action_tick_hands(void)
{
    for (int i = 0; i < game.champion_count; i++)
        for (int k = 0; k < ACTION_DELAY_COUNT; k++)
            if (champion(i)->action_delay[k] && !--champion(i)->action_delay[k]) champ_reload_hand(i, k);
}
