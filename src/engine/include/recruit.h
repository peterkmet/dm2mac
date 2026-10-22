/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_RECRUIT_H
#define DM2_ENGINE_RECRUIT_H
/* Prefixes: recruit_ */

/* Recruiting a champion from its mirror: the candidate joins the party or goes back. */

#include <stdbool.h>

/* The commands of the recruit buttons: the candidate joins, or goes back to its mirror. */
#define CMD_RECRUIT        0x92
#define CMD_RECRUIT_CANCEL 0x93

/* Commands */
/* The champion of the mirror in front of (x, y) of map joins as a candidate; false when the party is full, the
   leader holds an object, or no mirror is there. */
bool recruit_start(int x, int y, int dir, int map);
/* The candidate joins the party, or goes back to its mirror (command CMD_RECRUIT_CANCEL). */
void recruit_finish(int x, int y, int dir, int map, int command);
/* The champion of the mirror in front of (x, y) of map joins at once, without the inventory and the message. */
void recruit_start_champion(int x, int y, int dir, int map);

#endif /* DM2_ENGINE_RECRUIT_H */
