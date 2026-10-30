/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_MOVE_H
#define DM2_ENGINE_MOVE_H
/* Prefixes: move_ */

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

/* The bits of g_party.redraw: the view and the squares around the party change. */
#define PARTY_REDRAW_VIEW    0x01
#define PARTY_REDRAW_SQUARES 0x02
#define PARTY_REDRAW_ALL     0x03

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
#define MOVE_NO_SQUARE          (-1) /* move_thing: as src_x the thing is new, as dst_x it leaves the squares */
#define MOVE_DROPPED_PROJECTILE (-2) /* move_thing: the object of a projectile drops; teleporters keep its cell */
#define MOVE_RETRY              (-3) /* move_thing: a group that waited enters its square */
#define MOVE_CREATED            (-4) /* move_thing: the group is new, or it leaves the dungeon */
/* The y beside a negative src_x or dst_x of move_thing: the thing comes from or goes into the possessions of a static
   creature. */
#define MOVE_IN_POSSESSIONS     (-1)

/* The group move events (EVENT_MOVE_GROUP, EVENT_MOVE_GROUP_2): data bytes 2-3 hold the group. */
#define EVENT_GROUP_WORD 2

/* Results of move_thing. */
typedef enum { MOVE_DONE, MOVE_LEFT_DUNGEON, MOVE_WAIT } MoveResult;

/* Movement state of the party (the original globals); tests/dm2test/structs.py mirrors the layout. */
typedef struct {
    int16_t move_steps;                                    /* game steps until a slow party move ends */
    uint16_t move_command;                                 /* the move that ends the walk */
    int16_t view_x, view_y, view_dir;                      /* where the view stands while the party walks */
    int16_t new_map;                                       /* the map that the party entered in this step, -1 none */
    int16_t fell;                                          /* the party falls without damage (a levitation action) */
    int16_t dest_x, dest_y, dest_map, dest_cell, dest_dir; /* the last end of a move, as in the original */
    int teleporter_flag;                                   /* the last teleporter is TELEPORTER_AUDIBLE */
    uint32_t redraw;                                       /* PARTY_REDRAW_* */
    uint16_t push_pending_group;                           /* creature group that a push waits for, THING_NONE none */
    bool push_mode;                                        /* the movement arrows show the push arrows */
    int16_t push_arrow;
} Party;

/* The two champions on the side of a bump, -1 none; both can be the same champion. */
typedef struct {
    int16_t first, second;
} BumpedChampions;

/* A thing moves from the square (src_x, src_y) to the square (dst_x, dst_y) of the current map. */
typedef struct {
    int16_t src_x, src_y; /* a negative src_x: the thing is new */
    int16_t dst_x, dst_y;
} MovePath;

/* Where a move puts a thing: its square, map, and cell, and the flight direction of a projectile. */
typedef struct {
    int16_t x, y, map, cell, dir;
} MoveDest;

/* The end of move_follow_square_transitions: where the thing ends, and true for a flying thing. */
typedef struct {
    MoveDest dest;
    bool flying;
} Transition;

/* The result of move_thing_dest, and where the thing ends (x -1: no destination; cell -1: no transition). */
typedef struct {
    MoveResult result;
    MoveDest dest;
} MoveOutcome;

extern Party g_party;

/* ---- Champions ---- */

/* The champion nearest to the side of the party square that faces at, seen from cell (0xff: any); -1 none. */
int16_t move_nearest_champion_at(Location at, int16_t cell);
/* Damage of a bump into a wall or a creature on at for the two champions on that side; returns them. */
BumpedChampions move_party_bump_damage(Location at, int16_t dir, int step);

/* ---- Steps ---- */

/* Turns the party left or right (command 1 or 2). */
void move_party_turn(int command);
/* Moves the party one square in a move command; false when the command must wait. */
bool move_party(int command);
/* Puts the party on the square at, facing dir. */
void move_party_to(Position at, int16_t dir);
/* Sets the alternate view position of a party on the square at beside a seamless passage. */
void move_compute_map_arrival(Position at);

/* ---- Moving things ---- */

/* Follows pits, teleporters, and stairs from the destination of route; returns the end (g_party.dest_* keep a copy, as
   in the original). creature_sound: the teleporter sounds, the fall alert, and the drop sound play. */
Transition move_follow_square_transitions(uint16_t thing, MovePath route, bool creature_sound);
/* Moves a thing (THING_NONE: the party) from (src_x, src_y) to (dst_x, dst_y) of the current map, through
   pits, teleporters, and stairs; a negative src_x adds it, a negative dst_x removes it (MOVE_*), and a y of
   MOVE_IN_POSSESSIONS beside it takes it from or puts it into the possessions of a static creature. */
MoveResult move_thing(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y);
/* move_thing, with where the thing ends (g_party.dest_* keep a copy, as in the original). */
MoveOutcome move_thing_dest(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y);

/* ---- Level links, pits, and teleporters ---- */

/* The modes of the pit or teleporter change. */
#define PIT_STATE_OPEN   0
#define PIT_STATE_CLOSED 1
#define PIT_STATE_TOGGLE 2
/* Opens, closes, or toggles (PIT_STATE_*; another mode closes) the pit or the teleporter that is not fixed on at; an
   opened one moves the square contents through it. True when the state changed. */
bool move_set_pit_teleporter_state(Location at, int mode);

/* ---- Pushing creature groups ---- */

/* True when the champions push the group; the push costs stamina. */
bool move_party_can_push_group(uint16_t group);

/* ---- The warp of the party ---- */

/* The party on a warp marker text moves to the marker of its id; true when it moved. */
bool move_party_warp_to_marker(void);

/* ---- Party state that the interface sets ---- */

/* The movement arrows show the push arrows (on) or the move arrows: the party and the interface flag. */
void move_set_push_mode(bool on);
/* The push arrow under the mouse, PUSH_ARROW_NONE for none. */
void move_set_push_arrow(int arrow);
/* The game steps until the slow move of the party ends. */
void move_set_walk_steps(int steps);

#endif /* DM2_ENGINE_MOVE_H */
