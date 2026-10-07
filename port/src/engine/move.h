#ifndef MOVE_H
#define MOVE_H
#include <stdint.h>
#include "dungeon.h"

/* Prefixes: move_ dun_ */

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
/* Marker modes of dun_find_marker. */
enum { MARKER_FIND_NTH = 2, MARKER_COUNT = 3, MARKER_CLEAR_ALL = 4, MARKER_FIND_SET = 5 };
int dun_find_marker(int mode, int nth, int marker, int16_t *x, int16_t *y);
/* Damage of a bump into (x, y) for the two champions on that side; returns 1 when one was hurt. */
int move_party_bump_damage(int x, int y, int16_t *first, int16_t *second, int dir, int step);
/* Follows pits, teleporters, and stairs from (x, y) of the current map; leaves the end in party.dest_*.
   Returns 1 for a flying thing. */
int move_follow_square_transitions(uint16_t thing, int src_x, int src_y, int x, int y, int creature_sound);

/* Dungeon squares of ai.c. */
int dun_find_adjacent_level_link(int x, int y, int level_delta, uint16_t *sensor);
int dun_set_pit_or_teleporter_state(int x, int y, int mode);

/* Moves of creature groups (creature.c). */
void move_schedule_group_retry(uint16_t group, int x, int y, int map, int audible);
void move_update_carried_missiles(uint16_t holder, int x, int y, int map, int flag);
bool move_party_can_push_group(uint16_t group);
void move_push_creature(int arrow);

/* Dungeon reads of dungeon.c and move.c. */
/* Frees a record of type for reuse when the table is full: removes a thing of kind subtype (DISCARD_ANY_KIND:
   any) that may go, away from the party; THING_NONE when there is none. */
uint16_t dun_get_discarded_thing(int type, int subtype);
/* The map one level up (level_delta -1) or down (1) that the square (*x, *y) of map leads to by its stairs, its open
   pit (TRANSFER_PIT), or a level link; *x, *y become the square there. -1 none. Leaves map current. */
int dun_get_level_transfer_destination(int16_t *x, int16_t *y, int map, int level_delta, unsigned flags);

/* The warp of the party (magic.c). */
/* The party on a warp marker text moves to the marker of its id; returns 1 when it moved. */
int move_party_warp_to_marker(void);

/* Missiles that the party walks into (projectile.c). */
/* The party moves from (from_x, from_y) to (to_x, to_y): the projectiles there hit the champions. */
void move_check_missiles_hit_party(int from_x, int from_y, int to_x, int to_y);

/* Moves through squares of sensor.c. */
/* The things on (x, y) of the current map fall or teleport through its open pit or teleporter. */
void move_square_contents_through(int x, int y);

/* Dungeon reads of sensor.c. */
/* True when thing on a wall cell is the alcove ornament that the wall face shows; first: the first thing of the square. */
bool dun_is_wall_thing_ornament_shown(uint16_t first, uint16_t thing);
int dun_get_global_var(int var);
int dun_modify_global_var(int var, int value, int op);   /* returns the new value */
/* The door type (0 or 1 of the party map) of a door thing, 0xff for none. */
int dun_get_door_type(uint16_t door);

#endif
