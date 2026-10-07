#ifndef MOVE_H
#define MOVE_H
#include <stdint.h>
#include "dungeon.h"

/* Movement commands: turn left and right, then step forward, right, back, and left. */
enum move_command {
    CMD_TURN_LEFT = 1, CMD_TURN_RIGHT, CMD_MOVE_FORWARD, CMD_MOVE_RIGHT, CMD_MOVE_BACK, CMD_MOVE_LEFT
};

/* g_view_redraw_flags: the view squares and the creature maps change. */
#define REDRAW_VIEW 0x01
#define REDRAW_SQUARES 0x02
#define REDRAW_ALL_VIEW 0x03

/* Movement state of the party and of the dungeon reads (the original globals). */
/* The push arrows (party.push_arrow) by the part of the viewport under the mouse. */
enum {
    PUSH_ARROW_UP, PUSH_ARROW_RIGHT, PUSH_ARROW_DOWN_RIGHT, PUSH_ARROW_DOWN, PUSH_ARROW_DOWN_LEFT, PUSH_ARROW_LEFT,
    PUSH_ARROW_COUNT, PUSH_ARROW_NONE = PUSH_ARROW_COUNT
};

typedef struct {
    int16_t cur_map;            /* the map that the square reads of the original use, -1 none */
    int16_t square_x, square_y, square_map; /* the party position as the current map sees it */
    int16_t ref_dir;            /* the party direction as the current map sees it */
    int16_t move_steps;         /* game steps until a slow party move ends */
    uint16_t move_command;      /* the move that ends the walk */
    int16_t view_x, view_y, view_dir; /* where the view stands while the party walks */
    int alt_view_active;        /* the party stands at a seamless passage to another map */
    int16_t alt_view_x, alt_view_y, alt_view_dir, alt_view_map; /* the party position on the other map */
    int16_t new_map;            /* the map that the party entered in this step, -1 none */
    int16_t fell;               /* the party falls without damage (a levitation action) */
    int16_t dest_x, dest_y, dest_map, dest_cell, dest_dir; /* where move_thing put the last thing */
    int teleporter_flag;        /* the last teleporter had its bit 15 set */
    uint32_t redraw;            /* REDRAW_* */
    uint16_t push_pending_group;/* creature group that a push waits for, THING_NONE none */
    int push_mode;              /* the movement arrows show the push arrows */
    int16_t push_arrow;
} Party;

extern Party party;
/* The square step of each direction. */
extern const int8_t dir_dx[4], dir_dy[4];

/* Makes map the map of the square reads; updates the party position as seen from it. */
void dun_set_current_map(int map);
/* Puts the party at (x, y) of map and makes map current. */
void dun_set_party_location(int x, int y, int map);
/* True when (x, y) of the current map is the party square. */
int dun_on_party_square(int x, int y);
/* Moves (*x, *y) forward and to the right of dir. */
void dun_move_relative(int dir, int forward, int right, int16_t *x, int16_t *y);
/* The square byte of (x, y) of the current map, with the edge walls around it. */
uint8_t dun_get_square(int x, int y);
/* True when a square stops the party: walls, closed doors, closed fake walls, unknown squares. */
int dun_square_blocks_movement(uint8_t square);
/* The direction that leads out of the stairs at (x, y) of the current map. */
int dun_get_stairs_exit_direction(int x, int y);
/* The other end of a seamless passage at (x, y) of the current map. */
typedef struct {
    uint8_t side, target_side, x, y, map;
} PassageEnd;
int dun_get_linked_teleporter_dest(PassageEnd *end, int x, int y);

/* The direction from (x1, y1) toward (x2, y2); a random one of two on a diagonal. */
int dun_get_direction_to(int x1, int y1, int x2, int y2);
/* The creature group whose body covers the point of cell (0xff: any cell) of the square (*x, *y) of the current
   map or of a square next to it; moves *x, *y to that square. THING_NONE when there is none. */
uint16_t creature_find_group_near_point(int16_t *x, int16_t *y, int cell);
/* Turns the party to dir; the champions turn with it. */
void champ_set_party_direction(int dir);
/* Moves the party one square in a move command; returns 0 when the command must wait. */
int move_party(int command);
/* Turns the party left or right (command 1 or 2). */
void move_party_turn(int command);
/* Puts the party on (x, y) of map, facing dir. */
void move_party_to(int x, int y, int map, int dir);
/* Moves a thing (THING_NONE: the party) from (src_x, src_y) to (dst_x, dst_y) of the current map, through
   pits, teleporters, and stairs; a negative src_x adds it, a negative dst_x removes it. Returns 1 when a
   creature left the dungeon, 2 when a creature must wait. */
int move_thing(uint16_t thing, int src_x, int src_y, int dst_x, int dst_y);
#define MOVE_NEW_ITEM (-1)
#define MOVE_RETRY (-3)                     /* move_thing: a group that waited enters its square */
#define MOVE_CREATED (-4)                   /* move_thing: the group is new, or it leaves the dungeon */
/* Sets the alternate view position of a party at (x, y) of map beside a seamless passage. */
void move_compute_map_arrival(int x, int y, int map);
/* Finds a free square beside (x, y) for thing (THING_NONE: the party); returns 0 when there is none. */
int move_find_free_adjacent_square(uint16_t thing, int x, int y, int16_t *out_x, int16_t *out_y);
/* Marker search over all maps; see the mode values in move.c. */
int dun_find_marker(int mode, int nth, int marker, int16_t *x, int16_t *y);
/* The number of game steps that the champion needs for a move (load, wounds, haste). */
int champ_get_move_delay(int champ);
/* The champion nearest to the square (x, y) next to the party, seen from cell (0xff: any); -1 none. */
int champ_get_nearest_at(int x, int y, int cell);
/* Damage of a bump into (x, y) for the two champions on that side; returns 1 when one was hurt. */
int move_party_bump_damage(int x, int y, int16_t *first, int16_t *second, int dir, int step);
/* Takes stamina from a champion. */
void champ_decrease_stamina(int champ, int amount);
/* The champion of the cell nearest to the side of the party square that faces (x, y) (cell 0xff: any), -1 none. */
int champ_get_nearest_at(int x, int y, int cell);
/* Follows pits, teleporters, and stairs from (x, y) of the current map; leaves the end in party.dest_*.
   Returns 1 for a flying thing. */
int move_follow_square_transitions(uint16_t thing, int src_x, int src_y, int x, int y, int creature_sound);

#endif
