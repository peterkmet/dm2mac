/* The rules of the main loop: the timeline, map changes, the wake of the party. */
#include "champ_action.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game.h"
#include "hooks.h"
#include "sound_request.h"
#include "projectile.h"
#include "inventory.h"
#include "le.h"
#include "magic.h"
#include "move.h"
#include "random.h"
#include "save.h"
#include "sensor.h"
#include "weather.h"

MainLoop main_loop = {.tick_threshold = STEP_TICKS, .running = 1};
UiState ui;
int input_command_stop;
int input_drag_state;
int input_mouse_buttons;
Session session = {.slot = SAVE_SLOT_NONE};
int game_mode = GAME_MODE_VANILLA;
const char *const game_mode_names[GAME_MODE_COUNT] = {"vanilla", "advanced"};
int cheat_light, cheat_immortal, cheat_food;

void champ_wake_party(void)
{
    input_command_stop = 1;
    party_sleeping = 0;
    main_loop.tick_threshold = STEP_TICKS;
    g_hooks.clear_viewport();
    g_hooks.set_input_mode(INPUT_MODE_MOVE);
}

void game_enter_map(int x, int y, int map, int leaving)
{
    if (leaving) sensor_process_map_entry(0);
    dun_set_party_location(x, y, map);
    g_hooks.enter_map_view(map);
    world_update_day_night(0);
    sensor_process_map_entry(1);
    creature_activate_groups_on_map();
    party.redraw |= REDRAW_SQUARES;
    creature_refresh_party_maps();
    g_hooks.flush_input();
}

static void timeline_event_hide_damage(int champ)
{
    Champion *c = champion(champ);
    c->damage_event = THING_NONE;
    if (c->health) c->redraw |= REDRAW_BARS;
}

/* The aura of the champions in mask weakens by amount. */
static void timeline_event_aura(int mask, int amount)
{
    for (int i = 0; i < game.champion_count; i++) {
        Champion *c = champion(i);
        if (mask >> i & 1 && c->health) c->aura_value = (int16_t)(c->aura_value - amount);
        if (c->aura_value < 0) c->aura_value = 0;
    }
}

void timeline_process(void)
{
    while (timeline_is_event_due(game.game_time)) {
        g_hooks.poll_input();
        TimelineEvent e;
        timeline_pop_first_event(&e);
        dun_set_current_map((int8_t)timeline_event_map(&e));
        uint16_t word = le16(e.data);
        switch (e.type) {
        case EVENT_DOOR_ANIMATION: timeline_event_door_animation(&e); break;
        case EVENT_DOOR_DESTRUCTION: timeline_event_door_destruction(&e); break;
        case EVENT_SQUARE: timeline_event_square(&e); break;
        case EVENT_HIDE_DAMAGE: timeline_event_hide_damage(e.priority); break;
        case EVENT_ALTAR_REBIRTH: timeline_event_altar_rebirth(&e); break;
        case EVENT_ITEM_EFFECT: timeline_event_apply_item_effect(&e, EFFECT_MODE_CONSUME_END); break;
        case EVENT_DELAYED_SOUND: sound_request_delayed(word); break;
        case EVENT_EXPLOSION: explosion_process_event(&e); break;
        case EVENT_PROJECTILE_MOVE: case EVENT_PROJECTILE_MOVE_2: projectile_process_event(&e); break;
        case EVENT_CREATURE_ANIMATION: case EVENT_CREATURE_THINK: creature_process_event(e.data[EVENT_X], e.data[EVENT_Y], e.type); break;
        case EVENT_MOVE_GROUP: case EVENT_MOVE_GROUP_2: timeline_event_move_group(&e); break;
        case EVENT_MAGIC_LIGHT:
            dun_set_current_map(game.party_map);
            timeline_event_magic_light(&e);
            light_update_party_light();
            break;
        case EVENT_INVISIBILITY:  /* invisibility ends */
            if (!--game.magic_light[MAGIC_INVISIBILITY] && ui.inventory)
                champion(ui.inventory - 1)->redraw |= REDRAW_STATUS_BOX;
            break;
        case EVENT_AURA: timeline_event_aura(e.priority, (int16_t)word); break;
        case EVENT_POISON: {
            Champion *c = champion(e.priority);
            c->poisoned--;
            c->poison = (int16_t)(c->poison - word);
            champ_poison(e.priority, word);
            break;
        }
        case EVENT_WEATHER: world_update_day_night(1); break;
        case EVENT_SENSOR_ANIMATION_STEP: case EVENT_SENSOR_TIMER: case EVENT_SENSOR_IDLE: case EVENT_DOOR_BUTTON_RELEASE:
        case EVENT_SENSOR_ANIMATION_END: case EVENT_SENSOR_AMBIENT_SOUND: case EVENT_SENSOR_READY: case EVENT_SET_MARKER:
            timeline_event_sensor(&e);
            break;
        case EVENT_TELEPORT_PARTY:
            if (e.data[TELEPORT_EVENT_MAP] == game.party_map) {
                move_thing(THING_NONE, game.party_x, game.party_y, location_x(word), location_y(word));
                champ_set_party_direction(location_dir(word));
            }
            break;
        case EVENT_SPAWN_CREATURE: {
            int dir = rand_2bits() ? dun_get_direction_to(e.data[EVENT_X], e.data[EVENT_Y], game.party_x, game.party_y)
                                     : rand_2bits();
            creature_create_group(e.data[SPAWN_EVENT_CREATURE], GROUP_COUNT_FULL_HEALTH, dir, e.data[EVENT_X], e.data[EVENT_Y]);
            break;
        }
        }
    }
    dun_set_current_map(game.party_map);
}
