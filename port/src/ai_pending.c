/* Weak placeholders of the Step 16 parts that other branches port (ai_move.c); removed at their merge. */
#include "ai_move.h"

#define WEAK __attribute__((weak))
WEAK void projectile_create(uint16_t thing, int x, int y, int cell, int dir, int kinetic, int attack, int step) { (void)thing; (void)x; (void)y; (void)cell; (void)dir; (void)kinetic; (void)attack; (void)step; }
WEAK int creature_attack_champion(uint16_t group, int champ) { (void)group; (void)champ; return 0; }
