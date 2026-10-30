/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mouse and keyboard input: the cursor, the event ring, clicks in the dungeon view and the command queue. */
#include <string.h>
#include "action.h"
#include "action_area.h"
#include "advanced.h"
#include "creature.h"
#include "dialog.h"
#include "game.h"
#include "game_text.h"
#include "ids.h"
#include "input_internal.h"
#include "item.h"
#include "magic.h"
#include "minion.h"
#include "money.h"
#include "move.h"
#include "music.h"
#include "play.h"
#include "recruit.h"
#include "sensor.h"
#include "session.h"
#include "sound_request.h"
#include "video.h"
#include "view.h"
#include "zone.h"

/* Sizes of the event ring, the command queue, and the key buffer. */
#define EVENT_LIMIT         7
#define EVENT_LIMIT_SPECIAL 9

/* Relative cells of the squares of a view click. */
#define POSSESSION_VIEW_CELL_FIRST HALF_CELLS      /* the possessions of a static creature show in the back half */
#define ALCOVE_CELL                CELL_BACK_RIGHT /* the alcove in front keeps its objects in this relative cell */

/* Sizes and hot spots of the cursor images. */
#define ARROW_W          12
#define HAND_W           16
#define CURSOR_H         16
#define FACE_CURSOR_SKIP 4 /* color 4 of the face keeps the fill color of the dragged champion cursor */
#define FACE_HOT_X       8
#define FACE_HOT_Y       6
#define OBJECT_HOT       8 /* the hot spot of the held object cursor: the center of the icon */

/* Cursor kinds of a cursor zone. */
#define CURSOR_KIND_HAND     1
#define CURSOR_KIND_CHAMPION 2

/* The squares of a wall ornament click: the party square, left, right, front. */
#define WALL_CELL_PARTY 0
#define WALL_CELL_LEFT  1
#define WALL_CELL_RIGHT 2
#define WALL_CELL_FRONT 3
#define WALL_CELL_COUNT 4

/* The push resistance of a creature that never moves: a click on it only sounds. */
#define PUSH_RESISTANCE_FIXED 0xff

/* The PC scan codes that have a character (kbd_key_to_ascii), and the scan code of a key code. */
#define CHAR_SCAN_FIRST 0x02
#define CHAR_SCAN_LAST  0x39
#define SCAN_CODE_MASK  0xff

/* The party direction, the party square, and the square in front of it at the click. */
typedef struct {
    int16_t dir;
    Location party, front;
} ViewSquares;
/* A cell of a square of the view that a click with the leader hand object aims at: the square, its view position,
   the cell relative to the party direction, and the click zone kind. */
typedef struct {
    Location square;
    int16_t pos, cell;
    int kind;
} ViewCell;

/* The arrow and the hand cursors: 4 bpp, color 12 clear. */
static const uint8_t cursor_arrow[ARROW_W * CURSOR_H / 2] = {
    0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x0f, 0x0c, 0xcc, 0xcc, 0xcc, 0xcc, 0x0f, 0xf0, 0xcc, 0xcc,
    0xcc, 0xcc, 0x0f, 0xff, 0x0c, 0xcc, 0xcc, 0xcc, 0x0f, 0xff, 0xf0, 0xcc, 0xcc, 0xcc, 0x0f, 0xff,
    0xff, 0x0c, 0xcc, 0xcc, 0x0f, 0xff, 0xff, 0xf0, 0xcc, 0xcc, 0x0f, 0xff, 0xff, 0xff, 0x0c, 0xcc,
    0x0f, 0xff, 0xff, 0xff, 0xf0, 0xcc, 0x0f, 0xff, 0xff, 0x00, 0x00, 0x0c, 0x0f, 0xf0, 0xff, 0x0c,
    0xcc, 0xcc, 0x0f, 0x0c, 0x0f, 0xf0, 0xcc, 0xcc, 0x00, 0xcc, 0x0f, 0xf0, 0xcc, 0xcc, 0x0c, 0xcc,
    0xc0, 0xff, 0x0c, 0xcc, 0xcc, 0xcc, 0xc0, 0xff, 0x0c, 0xcc, 0xcc, 0xcc, 0xcc, 0x00, 0x0c, 0xcc,
};
static const uint8_t cursor_hand[HAND_W * CURSOR_H / 2] = {
    0xff, 0x0c, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x0f, 0xf0, 0xf0, 0xf0, 0xcc, 0xcc, 0xcc, 0xcc, 0xc0, 0xff, 0x0f,
    0x0f, 0x0f, 0x0c, 0xcc, 0xcc, 0xcc, 0x0f, 0xf0, 0xf0, 0xf0, 0xf0, 0xcc, 0xcc, 0xcc, 0xc0, 0xff, 0x0f, 0x0f, 0x0f,
    0x0c, 0xcc, 0xcc, 0xc0, 0xff, 0xf0, 0xf0, 0xf0, 0xf0, 0xcc, 0x00, 0xcc, 0x0f, 0xff, 0xff, 0xff, 0xf0, 0xcc, 0xff,
    0x0c, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x0f, 0xf0, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x0c, 0x0f, 0xff, 0x0f, 0xff,
    0xff, 0xff, 0xff, 0x0c, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0c, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0c, 0xcc, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xcc, 0xc0, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xcc, 0xcc,
    0xc0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0xc0, 0xff, 0xff, 0xff, 0xff,
};

InputState g_input;

InputMatch g_input_match = {.zone = ZONE_NONE, .origin = -1};
bool g_input_feedback_busy;
int16_t g_input_mouse_x = -1;
int16_t g_input_mouse_y = -1;

/* ---- The cursor ---- */

enum CursorKind input_cursor_at(int16_t x, int16_t y)
{
    int kind = 0;
    for (int i = 0; i < g_input.cursor_count; i++) {
        const CursorZone *z = &g_input_cursor_zones[g_input.cursor_list[i] - 1];
        int16_t r[RECT_SIZE];
        if (input_get_zone_rect(z->zone, r) && rect_contains_point(r, x, y)) {
            kind = z->kind;
            break;
        }
    }
    if (kind == CURSOR_KIND_CHAMPION)
        return CURSOR_CHAMPION;
    if (kind != CURSOR_KIND_HAND || g_game.leader == LEADER_NONE)
        return CURSOR_ARROW;
    return inv_cursor_object() == THING_NONE ? CURSOR_HAND : CURSOR_OBJECT;
}

void input_set_champion_cursor(int16_t champion_index)
{
    Bitmap *face = draw_temp_bitmap(FACE_SIZE, FACE_SIZE, IMAGE_4BPP);
    if (!face)
        return;
    memset(face->pixels, 0, (FACE_SIZE + 1) / 2 * FACE_SIZE);
    uint8_t colors[FACE_ICON_COLORS];
    ui_draw_champion_face_icon(face, champion_index, colors);
    Bitmap cursor = {g_input.champion_cursor, FACE_SIZE, FACE_SIZE, IMAGE_8BPP};
    memset(g_input.champion_cursor, UI_KEY, sizeof g_input.champion_cursor);
    const int16_t r[RECT_SIZE] = {0, 0, FACE_SIZE, FACE_SIZE};
    ui_blit_bitmap(&(BlitSource){.bitmap = face, .transparent = FACE_CURSOR_SKIP, .colormap = colors}, &cursor, r);
    g_input.champion_cursor_key = colors[UI_KEY];
    draw_free_temp();
}

CursorImage input_cursor_image(enum CursorKind cursor)
{
    switch (cursor) {
    case CURSOR_HAND:
        return (CursorImage){cursor_hand, g_ui_colors, IMAGE_4BPP, HAND_W, CURSOR_H, 0, 0, UI_KEY};
    case CURSOR_OBJECT: {
        uint8_t key = g_leader_hand.palette[HAND_ICON_KEY];
        uint8_t shadow[HAND_ICON_COLORS];
        memset(shadow, g_ui_colors[UI_BLACK], sizeof shadow);
        memset(g_input.object_cursor, key, sizeof g_input.object_cursor);
        const Bitmap icon = {g_leader_hand.cursor, HAND_ICON_SIZE, HAND_ICON_SIZE, IMAGE_8BPP};
        Bitmap image = {g_input.object_cursor, OBJECT_CURSOR_SIZE, OBJECT_CURSOR_SIZE, IMAGE_8BPP};
        const int16_t s[RECT_SIZE] = {OBJECT_SHADOW, OBJECT_SHADOW, HAND_ICON_SIZE, HAND_ICON_SIZE};
        const int16_t r[RECT_SIZE] = {0, 0, HAND_ICON_SIZE, HAND_ICON_SIZE};
        ui_blit_bitmap(&(BlitSource){.bitmap = &icon, .transparent = HAND_ICON_KEY, .colormap = shadow}, &image, s);
        ui_blit_bitmap(&(BlitSource){.bitmap = &icon, .transparent = HAND_ICON_KEY, .colormap = g_leader_hand.palette},
                       &image, r);
        return (CursorImage){g_input.object_cursor, NULL,       IMAGE_8BPP, OBJECT_CURSOR_SIZE,
                             OBJECT_CURSOR_SIZE,    OBJECT_HOT, OBJECT_HOT, key};
    }
    case CURSOR_CHAMPION:
        return (CursorImage){g_input.champion_cursor,    NULL, IMAGE_8BPP, FACE_SIZE, FACE_SIZE, FACE_HOT_X, FACE_HOT_Y,
                             g_input.champion_cursor_key};
    default:
        return (CursorImage){cursor_arrow, g_ui_colors, IMAGE_4BPP, ARROW_W, CURSOR_H, 0, 0, UI_KEY};
    }
}

void input_draw_cursor(uint8_t *screen, int16_t x, int16_t y)
{
    if (g_ui.eye || g_ui.mouth)
        return; /* the original hides the pointer while the eye or the mouth is held */
    CursorImage c = input_cursor_image(input_cursor_at(x, y));
    for (int cy = 0; cy < c.height; cy++)
        for (int cx = 0; cx < c.width; cx++) {
            int sx = x - c.hot_x + cx;
            int sy = y - c.hot_y + cy;
            int i = cy * c.width + cx;
            if (sx < 0 || sy < 0 || sx >= SCREEN_W || sy >= SCREEN_H)
                continue;
            int n = c.bpp == IMAGE_4BPP ? (c.bits[i / 2] >> (i & 1 ? 0 : NIBBLE_BIT_COUNT)) & NIBBLE_MASK : c.bits[i];
            if (n != c.key)
                screen[sy * SCREEN_W + sx] = c.palette ? c.palette[n] : (uint8_t)n;
        }
}

/* ---- Events ---- */

void input_mouse_click(int16_t x, int16_t y, int type)
{
    if (g_input.events_locked) {
        g_input.deferred = true;
        g_input.deferred_x = x;
        g_input.deferred_y = y;
        g_input.deferred_type = type;
        return;
    }
    int limit =
        (type == INPUT_LEFT_RELEASED && !g_input.dropped_left) || type == EVENT_ENTER_AREA || type == EVENT_LEAVE_AREA
            ? EVENT_LIMIT_SPECIAL
            : EVENT_LIMIT;
    g_input.dropped_left = false;
    if (g_input.event_count < limit) {
        g_input.event_tail = (g_input.event_tail + 1) % EVENT_RING;
        g_input.events[g_input.event_tail].type = (int16_t)type;
        g_input.events[g_input.event_tail].x = x;
        g_input.events[g_input.event_tail].y = y;
        g_input.event_count++;
    } else if (type == INPUT_BUTTON_LEFT) {
        g_input.dropped_left = true;
    }
}

void input_mouse_down(int16_t x, int16_t y, int button)
{
    game_set_mouse_buttons(g_input_mouse_buttons | button);
    input_mouse_click(x, y, button);
}

void input_mouse_up(int16_t x, int16_t y, int button)
{
    game_set_mouse_buttons(g_input_mouse_buttons & ~button);
    if (button == INPUT_BUTTON_LEFT && (g_ui.eye || g_ui.mouth || g_advanced_screen))
        input_mouse_click(x, y, INPUT_LEFT_RELEASED);
}

void input_flush_deferred_click(void)
{
    if (!g_input.deferred)
        return;
    g_input.deferred = false;
    input_mouse_click(g_input.deferred_x, g_input.deferred_y, g_input.deferred_type);
}

void input_enqueue_key(uint16_t key)
{
    if (g_input.event_count < EVENT_LIMIT) {
        g_input.event_tail = (g_input.event_tail + 1) % EVENT_RING;
        g_input.events[g_input.event_tail].type = INPUT_KEY;
        g_input.events[g_input.event_tail].x = (int16_t)key;
        g_input.event_count++;
    }
    g_input.events_locked = false;
    input_flush_deferred_click();
}

void input_key_down(uint16_t key)
{
    if (g_input.key_count < KEY_BUFFER)
        g_input.keys[g_input.key_count++] = key;
}

void input_key_repeat(uint16_t key)
{
    if (input_is_idle())
        input_key_down(key);
}

void input_flush(void)
{
    g_input.key_count = 0;
    int pos = (g_input.event_tail + 1) % EVENT_RING;
    if (pos != g_input.event_head || g_input.event_count) {
        /* only area changes and released left buttons stay */
        int write = g_input.event_head;
        int read = g_input.event_head;
        int kept = 0;
        for (int n = g_input.event_count; n > 0; n--) {
            int t = g_input.events[read].type;
            if (t == EVENT_ENTER_AREA || t == EVENT_LEAVE_AREA || t == INPUT_LEFT_RELEASED) {
                g_input.events[write] = g_input.events[read];
                write = (write + 1) % EVENT_RING;
                kept++;
            }
            read = (read + 1) % EVENT_RING;
        }
        g_input.event_count = kept;
        g_input.event_tail = (write + EVENT_RING - 1) % EVENT_RING;
    }
    g_input.disabled = 0;
    g_input.queue_head = 0;
    g_input.queue_tail = 0;
    g_input_match.zone = ZONE_NONE;
    g_input_match.origin = -1;
    g_input_match.x = 0;
    g_input_match.y = 0;
    g_input_match.value = 0;
    g_input_match.choice = 0;
    g_input.events_locked = false;
    input_flush_deferred_click();
}

/* ---- Clicks in the dungeon view ---- */

/* The point (x, y) is in the layout zone. */
static bool point_in_zone(int16_t zone, int16_t x, int16_t y)
{
    int16_t r[RECT_SIZE];
    return zone_get_rect(zone, r) && rect_contains_point(r, x, y);
}

/* A click on a wall ornament of the view (cell: a WALL_CELL_* square). */
static void click_wall_sensor(int16_t cell)
{
    /* the turn from the party direction to the wall face it sees, by wall cell */
    static const int8_t wall_side_turn[WALL_CELL_COUNT] = {TURN_RIGHT, TURN_RIGHT, TURN_LEFT, TURN_BACK};
    int16_t dir = (int16_t)g_game.party_dir;
    int16_t x = (int16_t)g_game.party_x;
    int16_t y = (int16_t)g_game.party_y;
    if (cell == WALL_CELL_LEFT) {
        x += g_dir_dx[dir_turn(dir, TURN_LEFT)];
        y += g_dir_dy[dir_turn(dir, TURN_LEFT)];
    } else if (cell == WALL_CELL_RIGHT) {
        x += g_dir_dx[dir_turn(dir, TURN_RIGHT)];
        y += g_dir_dy[dir_turn(dir, TURN_RIGHT)];
    } else if (cell == WALL_CELL_FRONT) {
        x += g_dir_dx[dir];
        y += g_dir_dy[dir];
    }
    if (x < 0 || x >= dungeon_map_width(g_game.party_map) || y < 0 || y >= dungeon_map_height(g_game.party_map))
        return;
    game_set_command_stop(sensor_process_wall_click((Location){x, y}, dir_turn(dir, wall_side_turn[cell]), THING_NONE,
                                                    g_game.leader_hand));
}

/* A click on a static creature in front: a push of its middle starts the push arrows, a side turns it. */
static void click_creature(uint16_t group, int16_t x, int16_t y, Location at)
{
    int side;
    if (!view_hit_test_creature(x, y, at, &side))
        return;
    if (creature_group_info(group)->push_resistance == PUSH_RESISTANCE_FIXED) {
        sndreq_request_with_fallback((SoundCue){CAT_CREATURE, item_gdat_index(group), SOUND_ENTRY_CLICK,
                                                SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL},
                                     SOUND_FALLBACK, at.x, at.y, SOUND_NOW);
    } else if (!side) {
        move_set_push_mode(true);
        ui_draw_movement_arrows();
        move_set_push_arrow(PUSH_ARROW_NONE);
    } else {
        creature_turn_group(group, false, (int16_t)side);
    }
}

/* A click on the wall in front: the wall sounds; the mouse drag through a fake wall is not ported. */
static void click_front_wall(Location front)
{
    if (!g_game.champion_count)
        return;
    sndreq_request_with_fallback((SoundCue){CAT_WALLSET, dungeon_map_wallset(g_game.party_map), SOUND_ENTRY_CLICK,
                                            SOUND_PRIORITY_CLICK, SOUND_VOLUME_LOUD},
                                 SOUND_FALLBACK, front.x, front.y, SOUND_NOW);
    game_set_command_stop(true);
}

/* A door button of the door at the square at: the door toggles, the button shows pressed until the next game step. */
static void click_door_button(Location at, uint16_t door)
{
    sndreq_request((SoundCue){CAT_DUNGEON, 0, SOUND_ENTRY_ACTIVATE, SOUND_PRIORITY_CLICK, SOUND_VOLUME_NORMAL},
                   g_game.party_x, g_game.party_y, SOUND_STEP_END);
    sensor_press_door_button(at, door);
    game_set_command_stop(true);
}

static void clear_moneybox_placed(uint16_t thing)
{
    if (item_is_moneybox(thing))
        moneybox_set_placed(thing, false);
}

/* A click with an empty hand on the object of click zone index: the leader takes the object. */
static void click_take_object(int16_t x, int16_t y, int index)
{
    if (g_game.leader == LEADER_NONE)
        return;
    const ViewClickZone *z = &g_view_click_zones[index];
    Location at = {(int16_t)g_game.party_x, (int16_t)g_game.party_y};
    if (z->pos) {
        if (z->pos != POS_FRONT)
            return;
        at.x += g_dir_dx[g_game.party_dir];
        at.y += g_dir_dy[g_game.party_dir];
    }
    ViewObjectClick click = {x, y, z->pos, at, z->kind};
    ObjectClickResult hit = view_hit_test_object(&click, z->thing, false);
    if (hit.kind == OBJECT_CLICK_HIT) {
        move_thing(hit.thing, at.x, at.y, MOVE_NO_SQUARE, z->kind == CLICK_POSSESSIONS ? MOVE_IN_POSSESSIONS : 0);
        clear_moneybox_placed(hit.thing);
        champ_put_in_leader_hand(hit.thing);
    }
    game_set_command_stop(true);
}

/* True when the party stands in a wall, a closed fake wall, or a closed door (walk-through-walls cheat). */
static bool is_party_in_wall(void)
{
    return game_has_cheat(CHEAT_WALLS) &&
           dungeon_square_blocks_movement(dungeon_square(g_game.party_map, g_game.party_x, g_game.party_y));
}

/* Drops the leader hand object into cell (relative to the party direction) of the square at: a floor cell, or with
   possession a possession cell of a static creature there. True when done. */
static bool click_drop_object(Location at, int16_t cell, bool possession)
{
    if (g_game.leader == LEADER_NONE)
        return false;
    if (at.x == g_game.party_x && at.y == g_game.party_y && is_party_in_wall())
        return true; /* no drop inside a wall: the object stays in the hand */
    int16_t dir_cell = dir_turn(g_game.party_dir, cell);
    uint16_t group = creature_group_at((Position){g_game.party_map, at.x, at.y});
    if (group != THING_NONE) {
        const CreatureInfo *info = creature_group_info(group);
        if (info->flags & CREATURE_STATIC) {
            if (possession && !(info->possessions & CREATURE_POSSESSIONS_ON_FLOOR))
                return false;
            if (!possession && !(info->possessions & POSSESSIONS_FLOOR_CLICKABLE))
                return false;
        } else {
            group = THING_NONE;
        }
    }
    uint16_t thing = champ_take_from_leader_hand();
    if (possession) {
        if (group != THING_NONE)
            for (uint16_t t = thing_word(group, GROUP_POSSESSIONS); t != THING_ENDOFLIST; t = dungeon_next_thing(t))
                if (thing_cell(t) == dir_cell)
                    clear_moneybox_placed(t);
        move_thing(thing_with_cell(thing, dir_cell), MOVE_NO_SQUARE, MOVE_IN_POSSESSIONS, at.x, at.y);
    } else {
        move_thing(thing_with_cell(thing, dir_cell), MOVE_NO_SQUARE, 0, at.x, at.y);
    }
    game_set_command_stop(true);
    return true;
}

/* A click at (x, y) with the leader hand object on the objects of a square cell: it goes into an open chest there
   (true), or it drops into the cell when the click hits no object; at.pos: the view position of the square (0 or
   3). */
static bool click_place_object(int16_t x, int16_t y, ViewCell at)
{
    uint16_t hand = thing_with_cell(g_game.leader_hand, dir_turn(g_game.party_dir, at.cell));
    ViewObjectClick click = {x, y, at.pos, at.square, at.kind};
    ObjectClickResult hit = view_hit_test_object(&click, hand, true);
    if (hit.kind == OBJECT_CLICK_DONE) {
        champ_take_from_leader_hand();
        return true;
    }
    if (hit.kind == OBJECT_CLICK_DROP)
        return click_drop_object(at.square, at.cell, at.kind == CLICK_POSSESSIONS);
    return false;
}

/* A click in the upper view with the leader hand object: the leader throws it to the left or the right side. */
static void click_throw_object(int16_t x, int16_t y)
{
    /* the original tests for square type 0x11 in front, which never occurs, and then uses zones 0x300 and 0x301 */
    bool right_side = point_in_zone(ZONE_FRONT_WALL_HIGH, x, y);
    if (!right_side && !point_in_zone(ZONE_FRONT_WALL_LOW, x, y))
        return;
    if (champ_throw_leader_hand_object(right_side))
        game_set_command_stop(true);
}

/* A click on a door button zone: a door without a button passes the click to the wall beside it. */
static void click_door_button_zone(const ViewSquares *v, const ViewClickZone *z, uint16_t hand)
{
    Location at = z->pos == POS_FRONT ? v->front : v->party;
    uint16_t door = dungeon_square_first_thing(g_game.party_map, at.x, at.y);
    if (!door_flag(door, DOOR_BUTTON)) {
        int16_t side = dir_turn(v->dir, TURN_RIGHT);
        game_set_command_stop(
            sensor_process_wall_click((Location){(int16_t)(at.x + g_dir_dx[side]), (int16_t)(at.y + g_dir_dy[side])},
                                      dir_turn(v->dir, TURN_LEFT), THING_NONE, hand));
    } else if (hand == THING_NONE) {
        click_door_button(at, door);
    }
}

/* A click on the click zones of the view: objects, door buttons, and wall ornaments; true when it is done. */
static bool click_zones_hit(const ViewSquares *v, int16_t x, int16_t y, uint16_t hand)
{
    for (int i = 0; i < g_view_click_zone_count; i++) {
        const ViewClickZone *z = &g_view_click_zones[i];
        if (!rect_contains_point(z->rect, x, y))
            continue;
        switch (z->kind) {
        case CLICK_OBJECT:
        case CLICK_POSSESSIONS:
        case CLICK_ALCOVE:
            if (z->thing == THING_NONE)
                break;
            if (hand == THING_NONE) {
                click_take_object(x, y, i);
                return true;
            }
            if (z->kind == CLICK_ALCOVE) {
                if (click_place_object(x, y, (ViewCell){v->front, 0, ALCOVE_CELL, CLICK_ALCOVE}))
                    return true;
            } else if (z->kind == CLICK_OBJECT) {
                for (int16_t k = 0; k < HALF_CELLS; k++)
                    if (click_place_object(x, y, (ViewCell){v->party, POS_PARTY, k, CLICK_OBJECT}))
                        return true;
            }
            break;
        case CLICK_DOOR_BUTTON:
            if (g_game.leader != LEADER_NONE)
                click_door_button_zone(v, z, hand);
            break;
        case CLICK_WALL_ORNAMENT:
            if (view_hit_test_wall_ornament(z->pos, x, y)) {
                click_wall_sensor(z->pos);
                return true;
            }
            break;
        }
    }
    return false;
}

/* A click with the leader hand object outside the click zones: it goes onto a cell of the party square, of the
   square in front (unless front_blocked), or of the possessions of a static creature there; else it is thrown. */
static void click_with_object(const ViewSquares *v, int16_t x, int16_t y, bool front_blocked)
{
    for (int16_t k = 0; k < HALF_CELLS; k++) {
        if (click_place_object(x, y, (ViewCell){v->party, POS_PARTY, k, CLICK_OBJECT}))
            return;
        if (point_in_zone(ZONE_DROP_CELL_FIRST + k, x, y) && click_drop_object(v->party, k, false))
            return;
    }
    if (front_blocked)
        return;
    for (int16_t k = HALF_CELLS; k < CELL_COUNT; k++) {
        if (click_place_object(x, y, (ViewCell){v->front, POS_FRONT, k, CLICK_OBJECT}))
            return;
        if (point_in_zone(ZONE_DROP_CELL_FIRST + k, x, y) && click_drop_object(v->front, k, false))
            return;
    }
    uint16_t g = creature_group_at((Position){g_game.party_map, v->front.x, v->front.y});
    if (g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC) {
        const CreatureInfo *info = creature_group_info(g);
        for (int16_t k = 0; k < (info->possessions & POSSESSIONS_KIND_MASK); k++) {
            int16_t cell = k + POSSESSION_VIEW_CELL_FIRST;
            if (click_place_object(x, y, (ViewCell){v->front, POS_FRONT, cell, CLICK_POSSESSIONS}))
                return;
            if (point_in_zone(info->possession_zone + k, x, y) && click_drop_object(v->front, cell, true))
                return;
        }
    }
    click_throw_object(x, y);
}

/* A click in the dungeon view: walls, doors, objects, creatures. */
static void click_in_viewport(int16_t x, int16_t y)
{
    dungeon_set_current_map(g_game.party_map);
    if (g_party.push_mode) {
        move_set_push_mode(false);
        creature_push_creature(g_party.push_arrow);
        ui_draw_movement_arrows();
        return;
    }
    int16_t origin[RECT_SIZE];
    if (zone_get_rect(ZONE_VIEWPORT, origin)) {
        x -= origin[RECT_X];
        y -= origin[RECT_Y];
    }
    int16_t dir = (int16_t)g_game.party_dir;
    Location party = {(int16_t)g_game.party_x, (int16_t)g_game.party_y};
    Location ahead = {(int16_t)(party.x + g_dir_dx[dir]), (int16_t)(party.y + g_dir_dy[dir])};
    ViewSquares v = {dir, party, ahead};
    uint8_t front = dungeon_square_at(v.front);
    uint16_t hand = g_game.leader_hand;
    if (click_zones_hit(&v, x, y, hand))
        return;
    /* as in the original, which also tests the passable fake wall flags of the front square: a wall square never
       has them, so a wall in front always blocks and always sounds */
    if (hand != THING_NONE) {
        click_with_object(&v, x, y, square_type(front) == SQUARE_WALL);
        return;
    }
    if (square_type(front) == SQUARE_WALL &&
        (point_in_zone(ZONE_FRONT_WALL_LOW, x, y) || point_in_zone(ZONE_FRONT_WALL_HIGH, x, y))) {
        click_front_wall(v.front);
        return;
    }
    uint16_t g = creature_group_at((Position){g_game.party_map, v.front.x, v.front.y});
    if (g != THING_NONE && creature_group_info(g)->flags & CREATURE_STATIC)
        click_creature(g, x, y, v.front);
}

/* ---- Commands ---- */

/* Shows the pause message and waits for the resume command. */
static void pause_game(void)
{
    char text[TEXT_SIZE];
    game_set_running(false);
    play_hide_view();
    gfx_fill_bitmap(&g_view_viewport, g_ui_colors[UI_BLACK]);
    gdat_text(gdat_current(), (GdatKey){CAT_INTERFACE, 0, TEXT_PAUSED}, text, sizeof text);
    text_draw_in_zone(ZONE_VIEW_TEXT, g_ui_colors[UI_CYAN], text);
    play_present_viewport();
    input_push_mode(INPUT_MODE_PAUSE);
    input_flush();
    music_pause();
}

/* The commands of the save, volume, and start menus. */
static void execute_session_command(int command)
{
    switch (command) {
    case CMD_SAVE:
        if (g_game.champion_count && !g_ui.candidate) {
            g_input.disabled--;
            session_save();
            g_input.disabled++;
        }
        break;
    case CMD_VOLUME:
        dialog_volume();
        if (g_ui.inventory)
            ui_draw_champion_state(g_ui.inventory - 1);
        break;
    case CMD_NEW_GAME:
        game_set_new_game(1);
        break;
    case CMD_NEW_GAME_B:
        game_set_new_game(1);
        game_set_dungenb(true);
        break;
    case CMD_LOAD_GAME:
        game_set_new_game(0);
        break;
    case CMD_MENU_PICTURE:
        menu_show_picture_and_wait();
        break;
    case CMD_QUIT:
        session_quit();
        break;
    }
}

/* The inventory command of status box champion_index (CHAMPION_MAX: the close command); with a candidate, it cancels the
   recruit. */
static void execute_inventory_command(int16_t champion_index)
{
    if (g_ui.candidate)
        recruit_finish((Position){g_game.party_map, g_game.party_x, g_game.party_y}, g_game.party_dir,
                       CMD_RECRUIT_CANCEL);
    else if (champion_index == CHAMPION_MAX)
        ui_close_inventory(true);
    else if (champion_index < g_game.champion_count)
        ui_toggle_inventory(champion_index);
}

/* The commands of the movement, the leader, the inventory, the spells, and the pause and the sleep; true when
   command is one of them. */
static bool execute_party_command(const Command *c, bool idle)
{
    int command = c->command;
    if (command == CMD_CANCEL_CAPTURE) {
        game_release_eye_and_mouth();
    } else if (command == CMD_CANCEL_PUSH) {
        input_cancel_push_mode();
    } else if (command == CMD_TURN_LEFT || command == CMD_TURN_RIGHT) {
        move_party_turn(command);
    } else if (command >= CMD_MOVE_FORWARD && command <= CMD_MOVE_LEFT) {
        move_party(command);
    } else if (command == CMD_ROTATE_LEFT || command == CMD_ROTATE_RIGHT) {
        if (g_action.champion) {
            int16_t champion_index = g_action.champion - 1;
            int16_t turn = command == CMD_ROTATE_LEFT ? TURN_LEFT : TURN_RIGHT;
            champ_set_direction(champion_index, dir_turn(champion(champion_index)->dir, turn));
        }
        area_update(false);
    } else if (command >= CMD_LEADER_FIRST && command <= CMD_LEADER_LAST) {
        champ_set_leader(input_party_cell_champion(command - CMD_LEADER_FIRST));
        ui_draw_all_champion_states(false);
    } else if (command >= CMD_INVENTORY_FIRST && command <= CMD_INVENTORY_LEADER) {
        execute_inventory_command(command - CMD_INVENTORY_FIRST);
    } else if (command == CMD_LEADER_INVENTORY) {
        if (g_game.leader != LEADER_NONE)
            ui_toggle_inventory(g_game.leader);
    } else if (command == CMD_VIEWPORT) {
        click_in_viewport(c->x, c->y);
    } else if (command == CMD_CAST_SPELL) {
        game_set_command_stop(magic_cast_entered_runes_failed());
    } else if (command == CMD_REMOVE_RUNE) {
        magic_remove_last_rune();
    } else if (command >= CMD_RUNE_FIRST && command <= CMD_RUNE_LAST) {
        magic_add_rune(command - CMD_RUNE_FIRST);
    } else if (command == CMD_MINION_MAP) {
        minion_map_click_set_destination(command, c->x, c->y);
    } else if (command >= CMD_MAP_OPTION_FIRST && command <= CMD_MAP_OPTION_LAST) {
        minion_toggle_map_option(command - CMD_MAP_OPTION_FIRST);
    } else if (command == CMD_PAUSE) {
        pause_game();
    } else if (command == CMD_RESUME) {
        if (!g_main_loop.running) {
            game_set_running(true);
            play_show_view();
            input_pop_mode();
            input_flush();
        }
    } else if (command == CMD_SLEEP && idle) {
        play_sleep();
    } else if (command == CMD_WAKE && idle) {
        champ_wake_party();
    } else {
        return false;
    }
    return true;
}

/* The commands of the champion actions, the hands, the slots, the money box, the drag, the recruiting, and the
   advanced mode; true when command is one of them. */
static bool execute_champion_command(int command, bool idle)
{
    if (command == CMD_ACTION_CANCEL) {
        champ_execute_selected_action(-1);
    } else if (command >= CMD_ACTION_FIRST && command <= CMD_ACTION_LAST) {
        game_set_command_stop(champ_execute_selected_action(command - CMD_ACTION_FIRST));
    } else if (command >= CMD_HAND_FIRST && command <= CMD_HAND_LAST) {
        action_area_open_hand((command - CMD_HAND_FIRST) / SLOT_HAND_COUNT,
                              (command - CMD_HAND_FIRST) % SLOT_HAND_COUNT);
    } else if (command >= CMD_PARTY_ICON_FIRST && command <= CMD_PARTY_ICON_LAST) {
        int16_t champion_index = input_party_cell_champion(command - CMD_PARTY_ICON_FIRST);
        if (champion_index >= 0)
            action_area_open_spells(champion_index);
    } else if (command >= CMD_COIN_FIRST && command <= CMD_COIN_LAST) {
        money_box_click_coin(command - CMD_COIN_FIRST);
    } else if ((command == CMD_RECRUIT || command == CMD_RECRUIT_CANCEL) && idle) {
        recruit_finish((Position){g_game.party_map, g_game.party_x, g_game.party_y}, g_game.party_dir, command);
    } else if (command == CMD_MOUTH) {
        champ_consume_object(-1, THING_NONE, -1);
    } else if (command == CMD_EYE) {
        game_set_eye_held((g_input_mouse_buttons & INPUT_BUTTON_LEFT) != 0);
        if (g_ui.eye)
            input_flush();
    } else if (command >= CMD_SLOT_FIRST && command <= CMD_SLOT_LAST) {
        inv_click_slot(command - CMD_SLOT_FIRST);
    } else if (command >= CMD_STORE_FIRST && command <= CMD_STORE_LAST) {
        inv_store_leader_hand_object(command - CMD_STORE_FIRST);
    } else if (command >= CMD_DRAG_FIRST && command <= CMD_DRAG_CANCEL) {
        if (command == CMD_DRAG_CANCEL)
            champ_drag_cancel();
        else
            champ_drag_click(command - CMD_DRAG_FIRST);
        input_refresh_mode();
        area_update(false);
    } else if (command >= CMD_QUICK_SAVE && command <= CMD_OPTIONS) {
        if (g_game.champion_count && !g_ui.candidate && idle) {
            if (command == CMD_QUICK_SAVE)
                advanced_quick_save();
            else if (command == CMD_QUICK_LOAD)
                advanced_quick_load();
            else if (command == CMD_AUTOMAP)
                advanced_open_automap();
            else if (command == CMD_OPTIONS)
                advanced_open_options();
            else
                advanced_open_cheats();
        }
    } else if (command == CMD_EDIT_CHAMPION_NAME) {
        if (g_ui.inventory)
            session_edit_champion_name(g_ui.inventory - 1);
    } else {
        return false;
    }
    return true;
}

bool command_execute(Command *c)
{
    int command = c->command;
    bool moving = command >= CMD_MOVE_FORWARD && command <= CMD_MOVE_LEFT;
    if ((moving &&
         (g_game.party_move_cooldown ||
          (g_game.throw_cooldown && g_game.throw_dir == dir_turn(g_game.party_dir, command - CMD_MOVE_FORWARD)))) ||
        (g_party.move_steps && (command == CMD_TURN_LEFT || command == CMD_TURN_RIGHT || command == CMD_VIEWPORT)) ||
        (command == CMD_VIEWPORT && !view_shows_party()) || g_input.wait) {
        game_set_command_stop(true);
        return false;
    }
    game_set_command_stop(false);
    bool idle =
        !g_ui.eye && !g_ui.mouth && !g_party.push_mode && !g_input_drag_state; /* no eye, mouth, drag or push mode */
    if (!execute_party_command(c, idle) && !execute_champion_command(command, idle) &&
        (command == CMD_SAVE || command == CMD_VOLUME || (command >= CMD_NEW_GAME && command <= CMD_MENU_PICTURE) ||
         command == CMD_QUIT) &&
        idle)
        execute_session_command(command);
    if (g_input_match.value >= CMD_TEXT_FIRST && g_input_match.value <= CMD_TEXT_LAST)
        game_set_command_stop(true);
    return true;
}

void input_set_wait(int steps)
{
    g_input.wait = steps;
}

void command_process_queue(void)
{
    if (g_input.wait)
        g_input.wait--;
    g_input.feedback_waiting = false;
    do {
        input_poll();
        if (g_input.queue_head == g_input.queue_tail || g_advanced_screen)
            return;
        int head = g_input.queue_head;
        g_input.queue_head = (head + 1) % COMMAND_QUEUE;
        if (!command_execute(&g_input.queue[head]))
            g_input.queue_head = head;
        else if (g_input.disabled)
            g_input.disabled--;
    } while (!g_input_command_stop && !g_main_loop.quit);
}

/* ---- Simulated clicks and keys ---- */

void input_simulate_command_click(uint16_t command)
{
    const InputCommand *e = input_find_command_entry(&g_input_mode_roots[g_input.mode], command);
    int16_t r[RECT_SIZE];
    if (e && input_get_zone_rect(e->zone, r))
        input_mouse_click(r[RECT_X], r[RECT_Y], e->buttons);
}

bool input_command_rect(uint16_t command, int16_t *rect)
{
    const InputCommand *e = input_find_command_entry(&g_input_mode_roots[g_input.mode], command);
    return e && input_get_zone_rect(e->zone, rect);
}

uint16_t input_read_key(void)
{
    if (!g_input.key_count)
        return 0;
    uint16_t key = g_input.keys[0];
    memmove(g_input.keys, g_input.keys + 1, --g_input.key_count * sizeof g_input.keys[0]);
    return key;
}

char kbd_key_to_ascii(uint16_t key)
{
    /* the characters of the PC scan codes 0x02 to 0x39 */
    static const char chars[] = "1234567890-=\0\0qwertyuiop[]\0\0asdfghjkl;'`\0\\zxcvbnm,./\0\0\0 ";
    static const char shifted[] = "!@#$%^&*()_+\0\0QWERTYUIOP{}\0\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0\0\0 ";
    int scan = key & SCAN_CODE_MASK;
    if (scan < CHAR_SCAN_FIRST || scan > CHAR_SCAN_LAST)
        return 0;
    return (key & KEY_SHIFTED ? shifted : chars)[scan - CHAR_SCAN_FIRST];
}
