/* Functions of other parts of Step 16 and of Step 17 that ai_move.c calls: weak versions until those parts exist. */
#include "ai_move.h"
#include "draw.h"
#include "dungeon.h"
#include "gdat.h"
#include "move.h"

#define WEAK __attribute__((weak))

WEAK uint16_t projectile_get_attack(uint16_t projectile, uint16_t payload) { (void)projectile; (void)payload; return 0; }
WEAK uint16_t explosion_get_attack(uint16_t explosion, uint16_t target) { (void)explosion; (void)target; return 0; }
WEAK void light_add_square_light(int distance, int dir, int x, int y, int mode) { (void)distance; (void)dir; (void)x; (void)y; (void)mode; }
