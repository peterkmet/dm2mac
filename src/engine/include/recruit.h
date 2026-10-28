/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_RECRUIT_H
#define DM2_ENGINE_RECRUIT_H
/* Prefixes: recruit_ */

/* Recruiting a champion from its mirror: the candidate joins the party or goes back. */

#include <stdbool.h>
#include <stdint.h>

/* The commands of the recruit buttons: the candidate joins, or goes back to its mirror. */
#define CMD_RECRUIT        0x92
#define CMD_RECRUIT_CANCEL 0x93

/* Commands */
/* The champion of the mirror in front of (x, y) of map joins as a candidate; false when the party is full, the
   leader holds an object, or no mirror is there. */
bool recruit_start(int16_t x, int16_t y, int16_t dir, int16_t map);
/* The candidate joins the party, or goes back to its mirror (command CMD_RECRUIT_CANCEL). */
void recruit_finish(int16_t x, int16_t y, int16_t dir, int16_t map, int command);
/* The champion of the mirror in front of (x, y) of map joins at once, without the inventory and the message; false
   when it cannot join (recruit_start). */
bool recruit_start_champion(int16_t x, int16_t y, int16_t dir, int16_t map);

#endif /* DM2_ENGINE_RECRUIT_H */
