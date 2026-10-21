/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_ENGINE_AI_MOVE_INTERNAL_H
#define DM2_ENGINE_AI_MOVE_INTERNAL_H

/* Shared state of the creature movement (ai_move.c): the move directions past the map, the last read square. */

#include <stdint.h>
#include "move.h"

/* The directions of a move test past the four map directions. */
#define DIR_LEVEL_UP 4
#define DIR_LEVEL_DOWN 5
#define DIR_KEEP_FACING 6       /* a move to the given square that keeps the facing */

/* More of the square that the AI read last (0x7f8b2 to 0x7f8bf of SKULL.EXE); ai_cache_square resets it. */
typedef struct {
    uint8_t blocking;           /* the square stops the search: a costly wall, a failed move */
    uint8_t passage;            /* the square is a seamless passage; end is its other side */
    PassageEnd end;
    uint8_t unused;
    int8_t items_elsewhere;     /* -1 not read; 1: items or ornaments that the creature cannot reach */
    uint8_t facing_ornament;    /* a wall: the face toward the creature shows an alcove */
    int8_t cells[4];            /* a floor: the cells that the creature reaches (cells[0] 0xff: not read);
                                   a wall: cells[0] is the face toward the creature */
} AiSquareScan;
extern AiSquareScan ai_square_scan;

#endif /* DM2_ENGINE_AI_MOVE_INTERNAL_H */
