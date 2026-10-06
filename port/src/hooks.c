#include "dungeon.h"
#include "hooks.h"
#include "input.h"

/* Empty until their steps; see hooks.h. */
void game_complete(void) { }
int dungeon_level_digit(int level) { (void)level; return 0; }
void command_execute_later(const Command *c) { (void)c; }
