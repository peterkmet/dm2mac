/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_MOVE_H
#define DM2_ENGINE_MOVE_H
/* Prefixes: move_ dun_ */

/* Party movement and thing movement: the movement commands, the redraw flags, and the doors. */

#include <stdbool.h>
#include <stdint.h>
#include "dungeon.h"

/* Movement commands: turn left and right, then step forward, right, back, and left. */
#define CMD_TURN_LEFT    0x01
#define CMD_TURN_RIGHT   0x02
#define CMD_MOVE_FORWARD 0x03
#define CMD_MOVE_RIGHT   0x04
#define CMD_MOVE_BACK    0x05
#define CMD_MOVE_LEFT    0x06

/* g_view_redraw_flags: the view squares and the creature maps change. */
#define REDRAW_VIEW     0x01
#define REDRAW_SQUARES  0x02
#define REDRAW_ALL_VIEW 0x03

/* The push arrows (g_party.push_arrow) by the part of the viewport under the mouse. */
#define PUSH_ARROW_UP         0
#define PUSH_ARROW_RIGHT      1
#define PUSH_ARROW_DOWN_RIGHT 2
#define PUSH_ARROW_DOWN       3
#define PUSH_ARROW_DOWN_LEFT  4
#define PUSH_ARROW_LEFT       5
#define PUSH_ARROW_COUNT      6
#define PUSH_ARROW_NONE       PUSH_ARROW_COUNT

/* Special source and destination x of move_thing. */
#define MOVE_NEW_ITEM (-1)
#define MOVE_RETRY    (-3) /* move_thing: a group that waited enters its square */
#define MOVE_CREATED  (-4) /* move_thing: the group is new, or it leaves the dungeon */

/* The group move events (EVENT_MOVE_GROUP, EVENT_MOVE_GROUP_2): data bytes 2-3 hold the group. */
#define EVENT_GROUP_WORD 2

/* Results of move_thing. */
typedef enum { MOVE_DONE, MOVE_LEFT_DUNGEON, MOVE_WAIT } MoveResult;

/* Movement state of the party and of the dungeon reads (the original globals). */
typedef struct {
    int16_t cur_map;                        /* the map that the square reads of the original use, -1 none */
    int16_t square_x, square_y, square_map; /* the party position as the current map sees it */
    int16_t ref_dir;                        /* the party direction as the current map sees it */
    int16_t move_steps;                     /* game steps until a slow party move ends */
    uint16_t move_command;                  /* the move that ends the walk */
    int16_t view_x, view_y, view_dir;       /* where the view stands while the party walks */
    int alt_view_active;                    /* the party stands at a seamless passage to another map */
    int16_t alt_view_x, alt_view_y, alt_view_dir, alt_view_map; /* the party position on the other map */
    int16_t new_map;                                       /* the map that the party entered in this step, -1 none */
    int16_t fell;                                          /* the party falls without damage (a levitation action) */
    int16_t dest_x, dest_y, dest_map, dest_cell, dest_dir; /* where move_thing put the last thing */
    int teleporter_flag;                                   /* the last teleporter had its bit 15 set */
    uint32_t redraw;                                       /* REDRAW_* */
    uint16_t push_pending_group;                           /* creature group that a push waits for, THING_NONE none */
    int push_mode;                                         /* the movement arrows show the push arrows */
    int16_t push_arrow;
} Party;

/* The other end of a seamless passage at (x, y) of the current map. */
typedef struct {
    uint8_t side, target_side, x, y, map;
} PassageEnd;

/* The two champions on the side of a bump, -1 none; both can be the same champion. */
typedef struct {
    int16_t first, second;
} BumpedChampions;

/* A thing moves from the square (src_x, src_y) to the square (dst_x, dst_y) of the current map. */
typedef struct {
    int16_t src_x, src_y; /* a negative src_x: the thing is new */
    int16_t dst_x, dst_y;
} MovePath;

extern Party g_party;
/* The square step of each direction. */
extern const int8_t g_dir_dx[DIR_COUNT], g_dir_dy[DIR_COUNT];

/* ---- The current map ---- */

/* Makes map the map of the square reads; updates the party position as seen from it. */
void dun_set_current_map(int16_t map);
/* Puts the party at (x, y) of map and makes map current. */
void dun_set_party_location(int16_t x, int16_t y, int16_t map);
/* Moves (*out_x, *out_y) forward and to the right of dir. */
void dun_move_relative(int16_t dir, int forward, int right, int16_t *out_x, int16_t *out_y);
/* The square byte of (x, y) of the current map, with the edge walls around it. */
uint8_t dun_get_square(int16_t x, int16_t y);
/* True when (x, y) of the current map is the party square. */
bool dun_on_party_square(int16_t x, int16_t y);
/* True when a square stops the party: walls, closed doors, closed fake walls, unknown squares. */
bool dun_square_blocks_movement(uint8_t square);
/* The direction that leads out of the stairs at (x, y) of the current map. */
int dun_get_stairs_exit_direction(int16_t x, int16_t y);
/* Puts the other end of the seamless passage at (x, y) of the current map into *end; false when there is none. */
bool dun_get_linked_teleporter_dest(PassageEnd *end, int16_t x, int16_t y);

/* ---- Champions ---- */

/* The direction from (x1, y1) toward (x2, y2); a random one of two on a diagonal. */
int dun_get_direction_to(int x1, int y1, int x2, int y2);
/* The champion nearest to the side of the party square that faces (x, y), seen from cell (0xff: any); -1 none. */
int move_nearest_champion_at(int16_t x, int16_t y, int16_t cell);
/* Damage of a bump into a wall or a creature at (x, y) for the two champions on that side; returns them. */
BumpedChampions move_party_bump_damage(int16_t x, int16_t y, int16_t dir, int step);

/* ---- Steps ---- */

/* Turns the party left or right (command 1 or 2). */
void move_party_turn(int command);
/* Moves the party one square in a move command; false when the command must wait. */
bool move_party(int command);
/* Puts the party on (x, y) of map, facing dir. */
void move_party_to(int16_t x, int16_t y, int16_t map, int16_t dir);
/* Sets the alternate view position of a party at (x, y) of map beside a seamless passage. */
void move_compute_map_arrival(int16_t x, int16_t y, int16_t map);

/* ---- Moving things ---- */

/* Follows pits, teleporters, and stairs from the destination of route; leaves the end in g_party.dest_*.
   creature_sound: the teleporter sounds, the fall alert, and the drop sound play. True for a flying thing. */
bool move_follow_square_transitions(uint16_t thing, MovePath route, bool creature_sound);
/* Moves a thing (THING_NONE: the party) from (src_x, src_y) to (dst_x, dst_y) of the current map, through
   pits, teleporters, and stairs; a negative src_x adds it, a negative dst_x removes it. */
MoveResult move_thing(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y);

/* ---- Discarded things ---- */

/* Frees a record of type for reuse when the table is full: removes a thing of kind subtype (DISCARD_ANY_KIND:
   any) that may go, away from the party; THING_NONE when there is none. */
uint16_t dun_get_discarded_thing(int type, int subtype);

/* Level links, pits, and teleporters. */
/* The modes of the pit or teleporter change. */
#define PIT_STATE_OPEN   0
#define PIT_STATE_CLOSED 1
#define PIT_STATE_TOGGLE 2
/* The direction of a level link (sensor or stairs marker) beside (x, y) toward level_delta, -1 none. */
int dun_find_adjacent_level_link(int16_t x, int16_t y, int level_delta, uint16_t *out_sensor);
/* Opens or closes the pit or teleporter on (x, y); true when the state changed. */
bool dun_set_pit_teleporter_state(int16_t x, int16_t y, int mode);
/* The things on (x, y) of the current map fall or teleport through its open pit or teleporter. */
void move_square_contents_through(int16_t x, int16_t y);

/* Pushing creature groups. */
/* True when the champions push the group; the push costs stamina. */
bool move_party_can_push_group(uint16_t group);

/* The warp of the party. */
/* The party on a warp marker text moves to the marker of its id; true when it moved. */
bool move_party_warp_to_marker(void);

/* Dungeon reads: alcoves, script variables, and doors. */
/* True when thing on a wall cell is the alcove ornament that the wall face shows; first: the first thing of the square. */
bool dun_is_alcove_shown(uint16_t first, uint16_t thing);
/* The value of the global variable var: a flag bit, a byte, or a word of the game record; 0 for an unknown var. */
int dun_get_global_var(int var);
/* Changes the global variable var by op (GLOBAL_VAR_*) with value; the bytes stay in their range. */
int dun_modify_global_var(int var, int value, int op); /* returns the new value */
/* The door type (0 or 1 of the party map) of a door thing, 0xff for none. */
int dun_get_door_type(uint16_t door);

/* Party state that the interface sets. */
/* The movement arrows show the push arrows (on) or the move arrows: the party and the interface flag. */
void move_set_push_mode(bool on);
/* The push arrow under the mouse, PUSH_ARROW_NONE for none. */
void move_set_push_arrow(int arrow);
/* The game steps until the slow move of the party ends. */
void move_set_walk_steps(int steps);

#endif /* DM2_ENGINE_MOVE_H */
