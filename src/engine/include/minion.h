/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_MINION_H
#define DM2_ENGINE_MINION_H
/* Prefixes: minion_ */

/* Minion maps: the grid, the mana of the map panel, and the clicks that send a minion. */

#include <stdbool.h>
#include <stdint.h>

/* The grid: 7 x 7 squares (9 x 9 when the grid is shifted) of 7 x 7 pixel icons with 1 pixel gaps. */
#define MAP_ICON_W       7
#define MAP_ICON_H       7
#define GAP_X            1
#define GAP_Y            1
#define GRID_RADIUS      3
#define MAP_OPTION_COUNT 4
#define MAP_CLIP_COUNT   4

/* Minion map kinds: the container type of a minion map. */
#define MAP_KIND_PLAIN      0
#define MAP_KIND_MINION     1
#define MAP_KIND_LOCATION   2
#define MAP_KIND_NO_OPTIONS 3

/* g_action.flags while the action area shows a minion map. */
#define MAP_OPTION_CREATURES 0x0001 /* option button 0: moving creatures */
#define MAP_OPTION_DETAILS   0x0002 /* option button 1: all ornaments, blinking pits and fake walls */
#define MAP_OPTION_ITEMS     0x0004 /* option button 2: items, missiles, explosions, possessions */
#define MAP_OPTION_HOLD      0x0008 /* option button 3: the map keeps its last center */
#define MAP_OPTIONS          0x000f
#define MAP_PARTY            0x0010 /* the square shows the party */
#define MAP_MINION           0x0020 /* the map shows its minion instead of its creature picture */
#define MAP_MARKER           0x0040 /* the map has a location: its marker shows */
#define MAP_STATIC_CREATURES 0x0080
#define MAP_DIMMED           0x0100
#define MAP_POWERED          0x0200 /* the champion paid the mana: the map is not dimmed */
#define MAP_PANEL_DRAWN      0x0400
#define MAP_HAS_OPTIONS      0x0800
#define MAP_OPEN             0x8000

/* MinionMap.marker_state: the grid shows the marker square, or the square it leads to one level up or down. */
#define MARKER_NONE  0
#define MARKER_SHOWN 1
#define MARKER_ABOVE 2
#define MARKER_BELOW 3

/* The state of the minion map panel (the original globals 0x7fb4a to 0x7fb9f). */
typedef struct {
    int16_t marker_state;       /* 0 none, 1 the marker square shows, 2 its level above, 3 its level below */
    int16_t y_offset, x_offset; /* pixel shift of the grid: the minion stands off the middle of its square */
    int16_t below_y, below_x;   /* where the marker square leads one level down */
    int16_t above_map;          /* the map one level up from the marker square, -1 none */
    int16_t marker_map;
    int16_t below_map;
    int16_t above_x, above_y;
    int16_t marker_x, marker_y;
    int8_t map, x, y, dir;        /* the center of the last grid */
    int16_t mana_divisor;         /* 1 + the mana of the options that are on */
    int16_t mana_timer;           /* game ticks until the next mana point */
    int16_t clip[MAP_CLIP_COUNT]; /* the grid rectangle */
} MinionMap;

/* The command of a click on the minion map grid. */
#define CMD_MINION_MAP 0x55

/* Pixels per square of a map: a 7 x 7 icon and a 1 pixel gap. */
#define MAP_CELL_SIZE 8

extern MinionMap g_minion_map;

/* The minion map opens in the hand of the open action area: the champion pays 1 mana for it. */
void minion_map_open(int champ);
/* One game tick of an open minion map: it takes mana, or it goes dark without mana. */
void minion_map_drain_mana(void);
/* Turns option button option (0 to 3) on (for its mana) or off. */
void minion_toggle_map_option(int option);
/* Sets the location of the minion map thing to (x, y) of map; returns false outside the map. */
bool minion_map_set_location(uint16_t thing, int x, int y, int map);
/* A click (command) at (x, y) of the screen on the grid of a minion location map sends its minion there. */
void minion_map_click_set_destination(int command, int x, int y);
/* Minions of creature.c and item.c. */
void minion_dismiss(uint16_t group);
/* The projectile among the possessions of group that carries filter (THING_NONE: any); 0 when none. */
uint16_t minion_find_link_projectile(uint16_t group, uint16_t filter);

#endif /* DM2_ENGINE_MINION_H */
