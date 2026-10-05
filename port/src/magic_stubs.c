/* Placeholders of other Step 16 parts that the spells call; their modules replace them. */
#include "hooks.h"
#include "magic.h"

__attribute__((weak)) void champ_launch_projectile(Champion *c, uint16_t thing, int power, int attack, int step)
{
    hook_record(HOOK_LAUNCH_PROJECTILE, (int)(c - champion(0)), thing, power, attack, step);
}
