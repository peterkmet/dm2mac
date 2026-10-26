/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "action.h"
#include <string.h>
#include "champion.h"
#include "dungeon.h"
#include "hooks.h"

ActionArea g_action;

/* Setup */

void action_reset(void)
{
    memset(&g_action, 0, sizeof g_action);
    g_action.mode = ACTION_MODE_NONE;
    g_action.open_container = THING_NONE;
    for (int i = 0; i < CONTAINER_SLOTS; i++)
        g_action.open_items[i] = THING_NONE;
    g_hooks.reset_action_area();
}

void action_reset_champion(int champ)
{
    g_hooks.reset_action_champion(champ);
    g_action.hand_items[champ][SLOT_READY_HAND] = THING_NONE;
    g_action.hand_items[champ][SLOT_ACTION_HAND] = THING_NONE;
}

/* Queries */

uint16_t action_open_hand_item(void)
{
    if (g_action.champion <= 0 || g_action.hand < 0 || g_action.hand > SLOT_ACTION_HAND)
        return THING_NONE;
    return g_action.hand_items[g_action.champion - 1][g_action.hand];
}

/* Commands */

/* Puts the items of the open container slots back into the chest. */
static void close_container(void)
{
    uint16_t container = g_action.open_container;
    if (container == THING_NONE)
        return;
    g_action.open_container = THING_NONE;
    for (int i = 0; i < CONTAINER_SLOTS; i++) {
        uint16_t t = g_action.open_items[i];
        if (t == THING_NONE)
            continue;
        g_action.open_items[i] = THING_NONE;
        thing_set_word(t, THING_NEXT, THING_ENDOFLIST);
        uint16_t last = thing_word(container, CONTAINER_CONTENTS);
        if (last == THING_ENDOFLIST) {
            thing_set_word(container, CONTAINER_CONTENTS, t);
            continue;
        }
        while (dungeon_next_thing(last) != THING_ENDOFLIST)
            last = dungeon_next_thing(last);
        thing_set_word(last, THING_NEXT, t);
    }
}

void action_area_close(void)
{
    if (!g_action.champion)
        return;
    if (!g_action.result || g_action.redraw_delay < 2)
        g_action.redraw_delay = 0;
    g_action.champion = 0;
    g_action.mode = ACTION_MODE_HANDS;
    g_action.flags = 0;
    close_container();
    g_action.dirty = 1;
}

void action_open_container(uint16_t container)
{
    if (container == THING_NONE || container == g_action.open_container)
        return;
    int i = 0;
    for (uint16_t t; (t = thing_word(container, CONTAINER_CONTENTS)) != THING_ENDOFLIST && i < CONTAINER_SLOTS; i++) {
        g_action.open_items[i] = t;
        uint16_t contents = t;
        /* unlink the first item from the contents list */
        thing_set_word(container, CONTAINER_CONTENTS, dungeon_next_thing(contents));
        thing_set_word(contents, THING_NEXT, THING_ENDOFLIST);
    }
    for (; i < CONTAINER_SLOTS; i++)
        g_action.open_items[i] = THING_NONE;
    g_action.open_container = container;
}

void action_tick_hands(void)
{
    for (int i = 0; i < g_game.champion_count; i++)
        for (int k = 0; k < ACTION_DELAY_COUNT; k++)
            if (champion(i)->action_delay[k] && !--champion(i)->action_delay[k])
                champ_reload_hand(i, k);
}

void action_request_redraw(void)
{
    if (!g_action.champion)
        g_action.dirty = 1;
    else
        g_action.redraw_delay = 1;
}

void action_set_map_item(int index)
{
    g_action.map_item = (uint8_t)index;
}

void action_set_map_flags(unsigned flags, bool on)
{
    if (on)
        g_action.flags |= flags;
    else
        g_action.flags &= ~flags;
}
