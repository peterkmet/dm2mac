/* Minion maps: the map panel in the action area, its mana, and the clicks that send a minion. */
#include <string.h>
#include "action.h"
#include "ai.h"
#include "champion.h"
#include "creature.h"
#include "draw.h"
#include "dungeon.h"
#include "hooks.h"
#include "item.h"
#include "minion.h"
#include "move.h"
#include "random.h"
#include "text.h"
#include "timeline.h"
#include "ui.h"
#include "view.h"
#include "zone.h"

/* The grid: 7 x 7 squares (9 x 9 when the grid is shifted) of 7 x 7 pixel icons with 1 pixel gaps. */
#define ICON_W 7
#define ICON_H 7
#define GAP_X 1
#define GAP_Y 1
#define GRID_RADIUS 3
#define ZONE_MAP_GRID 99
#define ZONE_PANEL 0x5c
#define ZONE_OPTION_BUTTON_FIRST 0x65
#define ZONE_OPTION_LABEL_FIRST 0x69
#define MAP_OPTION_COUNT 4
#define MAP_PANEL_PICTURE 0x10
#define OPTION_BUTTON_UP 0x49
#define OPTION_BUTTON_DOWN 0x4a
#define OPTION_LABEL_FIRST 'r'
#define ICON_KEY 10
/* Icon strips: GDAT image (category, index, 1, 0xf9), ICON_W pixels per icon; the value (category, index, 0x0b,
   0xf9) set: the thing shows only with MAP_OPTION_DETAILS. */
#define STRIP_ENTRY 0xf9
#define STRIP_DETAIL_KEY 0xf9
#define CAT_MAP_ICONS 8                 /* by wall set: the square icons */
#define CAT_WALL_ORNAMENT 9
#define CAT_FLOOR_ORNAMENT 10
#define CAT_CREATURE 0x0f
#define CAT_TELEPORTER 0x18
#define MAP_ITEM_ICONS 0x40             /* the image of the map item with the party, minion, and marker icons */
/* Icons of the square strip. */
enum {
    ICON_WALL, ICON_FAKE_WALL_BLINK, ICON_FLOOR, ICON_PIT, ICON_PIT_VARIANT, ICON_PIT_BLINK, ICON_STAIRS_FIRST,
    ICON_DOOR_FRONT = 0x0a, ICON_DOOR_SIDE = 0x0b, ICON_DOOR_NO_BUTTON = 2, ICON_DOOR_OPEN = 4, ICON_PIT_ABOVE = 0x12
};
/* Icons of the map item strip. */
enum { ICON_MINION_FIRST = 3, ICON_PARTY_FIRST = 5, ICON_PARTY_INVISIBLE = 9, ICON_MARKER = 2, ICON_MARKER_ABOVE = 0x0b,
       ICON_MARKER_BELOW = 0x0a };
#define MIRROR_X 1
#define MIRROR_Y 2
#define DOOR_BUTTON 0x0040              /* door record word 2 */
#define DOOR_NO_ANIMATION 0x2000
#define DOOR_STATE_OPEN 0
#define DOOR_STATE_BROKEN 5
#define SQUARE_FLAG_0 0x01             /* a fake wall that blinks with MAP_OPTION_DETAILS; a pit that is not a way down */
#define FAKE_WALL_SEEN SQUARE_FLAG_0
#define FLOOR_SHOWS_LEVEL 0x08          /* a floor square shows the level digit instead */
#define ORN_FRAME_SHIFT 10
#define FLOOR_FRAME_SHIFT 8
#define FLOOR_FRAME_STEP 10
#define ORNAMENT_NONE 0xff
#define ORNAMENT_INDEX_MASK 0xff
enum { SIDE_DOOR_STATE, SIDE_DOOR_THING, SIDE_FLOOR_ORNAMENT, SIDE_STAIRS_UP = 0, SIDE_PIT_VARIANT = 0 };
#define MAP_KIND_SHIFT 13               /* container word 4: the kind of minion map */
enum { MAP_KIND_PLAIN, MAP_KIND_MINION, MAP_KIND_LOCATION, MAP_KIND_NO_OPTIONS };
#define MAP_LOCATION 6                  /* minion map record word: x bits 0-4, y bits 5-9, map bits 10-15 */
#define MAP_LOCATION_NONE 0xfc00        /* map 63: no location */
#define MAP_CONTENTS 2                  /* the minion group of the map */
#define PROJECTILE_LOCATION 4           /* a link projectile: where the minion goes */
#define LOCATION_X(w) ((w) & 0x1f)
#define LOCATION_Y(w) ((w) >> 5 & 0x1f)
#define LOCATION_MAP(w) ((w) >> 10)
#define LOCATION(x, y, map) (uint16_t)(((x) & 0x1f) | ((y) & 0x1f) << 5 | ((map) & 0x3f) << 10)
#define MINION_MAP_MANA_TICKS 25
#define LEVEL_UP (-1)
#define LEVEL_DOWN 1
/* Level transfer modes. */
#define TRANSFER_PIT 0x008
#define TRANSFER_FALL_INTO 0x010
#define TRANSFER_STAIRS 0x100
#define SQUARE_LEVEL_LINK 0x02          /* a square with a link to another level */
#define EVENT_MISSILE_DIR 2             /* projectile event data word: direction in bits 10-11 */
#define MISSILE_DIR_SHIFT 10
#define MISSILE_SIDES_ICON 3
#define POSSESSIONS_SHOWN 0x0f
#define VIEW_CELL_ROW 5
#define CMD_MINION_MAP 0x55

MinionMap minion_map;

/* The mana of each option button. */
static const uint8_t option_mana[MAP_OPTION_COUNT] = {2, 3, 1, 1};
/* Mirror of an icon by the relative direction: icons with 2 sides (front, side), icons with 1 side. */
static const uint8_t mirror_two_sides[DIR_COUNT] = {0, 0, 3, 3};
static const uint8_t mirror_one_side[DIR_COUNT] = {0, 1, 3, 2};
/* The icon shift of a missile with 3 sides, by its relative direction and its relative cell. */
static const int8_t missile_side_shift[DIR_COUNT][DIR_COUNT] = {
    {0, 0, 2, 2}, {0, 2, 2, 0}, {0, 0, -2, -2}, {0, -2, -2, 0}
};
/* The pixel shift of the grid by the column (row) of the minion in the 5 x 5 cells of its square. */
static const int8_t cell_shift[VIEW_CELL_ROW] = {-2, -3, 0, -1, -2};
/* The relative direction of a creature by the side it shows: front, right, back, left. */
static const uint8_t creature_side_dir[DIR_COUNT] = {0, 3, 2, 1};

typedef struct {
    const Bitmap *image;        /* NULL: no strip */
    const uint8_t *palette;
    int count;
} Strip;

static Offscreen *area;

/* The icon strip of a GDAT key; count 0 when it has none. */
static Strip map_icon_strip(int category, int index)
{
    Strip s = {0};
    if (!gdat_exists(draw_gdat(), category, index, GDAT_TYPE_IMAGE, STRIP_ENTRY)) return s;
    s.image = draw_gdat_image(category, index, STRIP_ENTRY);
    s.palette = draw_gdat_image_palette(category, index, STRIP_ENTRY);
    s.count = s.image ? s.image->width / ICON_W : 0;
    return s;
}

/* The party, minion, and marker icons of the map item. */
static Strip map_item_icons(uint16_t thing)
{
    int category = item_gdat_category(thing), index = item_gdat_index(thing);
    return (Strip){draw_gdat_image(category, index, MAP_ITEM_ICONS), draw_gdat_image_palette(category, index, MAP_ITEM_ICONS), 0};
}

/* True when the GDAT key has the detail value: it shows only with MAP_OPTION_DETAILS. */
static int detail_only(int category, int index)
{
    return gdat_entry_index(draw_gdat(), category, index, GDAT_TYPE_VALUE, STRIP_DETAIL_KEY) != 0;
}

/* Clips rect to clip; *dx, *dy: the pixels cut at the left and top. Returns 0 when nothing is left. */
static int rect_clip(int16_t *rect, const int16_t *clip, int16_t *dx, int16_t *dy)
{
    int16_t d = (int16_t)(clip[RECT_X] - rect[RECT_X]);
    *dx = d > 0 ? d : 0;
    if (d > 0) {
        rect[RECT_X] += d;
        rect[RECT_W] -= d;
    }
    d = (int16_t)(clip[RECT_Y] - rect[RECT_Y]);
    *dy = d > 0 ? d : 0;
    if (d > 0) {
        rect[RECT_Y] += d;
        rect[RECT_H] -= d;
    }
    d = (int16_t)((rect[RECT_X] + rect[RECT_W]) - (clip[RECT_X] + clip[RECT_W]));
    if (d > 0) rect[RECT_W] -= d;
    d = (int16_t)((rect[RECT_Y] + rect[RECT_H]) - (clip[RECT_Y] + clip[RECT_H]));
    if (d > 0) rect[RECT_H] -= d;
    return rect[RECT_W] > 0 && rect[RECT_H] > 0;
}

/* Draws icon of a strip at (x, y) of the screen inside the grid, mirrored by mirror (MIRROR_X, MIRROR_Y). */
static void draw_icon(const Bitmap *image, const uint8_t *palette, int icon, int x, int y, int mirror)
{
    if (!image) return;
    int16_t r[RECT_SIZE] = {(int16_t)x, (int16_t)y, ICON_W, ICON_H}, dx, dy;
    if (!rect_clip(r, minion_map.clip, &dx, &dy)) return;
    if (ICON_W - r[RECT_W] && mirror & MIRROR_X) dx = dx ? 0 : (int16_t)(ICON_W - r[RECT_W]);
    if (ICON_H - r[RECT_H] && mirror & MIRROR_Y) dy = dy ? 0 : (int16_t)(ICON_H - r[RECT_H]);
    offscreen_blit(area, image, r, (int16_t)(icon * ICON_W + dx), dy, ICON_KEY, mirror, palette);
}

static void draw_strip(const Strip *s, int icon, int x, int y, int mirror)
{
    draw_icon(s->image, s->palette, icon, x, y, mirror);
}

/* True for a square of map. */
static int inside_map(int map, int x, int y)
{
    return x >= 0 && x < dungeon_map_width(map) && y >= 0 && y < dungeon_map_height(map);
}

/* The level digit icons of the current map: what a square outside the maps of a level shows. */
static int level_icon_set(void) { return dungeon_level_digit(dungeon_map_level(party.cur_map)); }

/* The map icon set of the current map. */
static int map_icon_set(void) { return dungeon_map_graphics(party.cur_map) >> 4 & 0xf; }

/* The icons of a wall square: the wall and the ornaments of its 4 faces. */
static void draw_wall(const ViewSquare *s, const Strip *base, int x, int y, int flags, int fake_seen, int other_map)
{
    if (flags & MAP_OPTION_DETAILS && fake_seen && game.game_time & 2) {
        draw_strip(base, ICON_FAKE_WALL_BLINK, x, y, 0);
        return;
    }
    if (!other_map) draw_strip(base, ICON_WALL, x, y, 0);
    for (int side = 0; side < VIEW_SIDE_COUNT; side++) {
        int ornament = s->side[side] & ORNAMENT_INDEX_MASK;
        if (ornament == ORNAMENT_NONE) continue;
        if (!(flags & MAP_OPTION_DETAILS) && detail_only(CAT_WALL_ORNAMENT, ornament)) continue;
        Strip o = map_icon_strip(CAT_WALL_ORNAMENT, ornament);
        if (!o.count) continue;
        int frame = (int16_t)s->side[side] >> ORN_FRAME_SHIFT;
        int icon = o.count < frame * 2 + 2 ? side & 1 : frame * 2 + (side & 1);
        draw_strip(&o, icon, x, y, mirror_two_sides[side]);
    }
}

/* The icons of a floor or teleporter square: the floor, its ornament, the teleporter field. */
static void draw_floor(const ViewSquare *s, const Strip *base, int x, int y, int flags)
{
    draw_strip(base, ICON_FLOOR, x, y, 0);
    int ornament = s->side[SIDE_FLOOR_ORNAMENT] & ORNAMENT_INDEX_MASK;
    if (ornament != ORNAMENT_NONE && (flags & MAP_OPTION_DETAILS || !detail_only(CAT_FLOOR_ORNAMENT, ornament))) {
        Strip o = map_icon_strip(CAT_FLOOR_ORNAMENT, ornament);
        if (o.count) {
            int frame = (int8_t)(s->side[SIDE_FLOOR_ORNAMENT] >> FLOOR_FRAME_SHIFT) / FLOOR_FRAME_STEP;
            draw_strip(&o, o.count < frame + 1 ? 0 : frame, x, y, 0);
        }
    }
    if (s->aspect != ASPECT_TELEPORTER) return;
    Strip t = map_icon_strip(CAT_TELEPORTER, 0);
    if (t.count) draw_strip(&t, (int)(game.game_time % (uint32_t)(int16_t)t.count), x, y, 0);
}

/* The icon of the creature group, and the items that a static creature holds on its square. */
static void draw_group(uint16_t group, uint16_t map_thing, int dir, int x, int y, int flags)
{
    uint16_t cflags = creature_group_info(group)->flags;
    if (flags & MAP_MINION) {
        Strip m = map_item_icons(map_thing);
        draw_strip(&m, ICON_MINION_FIRST + (game.game_time & 1), x, y, 0);
    } else {
        Strip c = map_icon_strip(item_gdat_category(group), item_gdat_index(group));
        if (c.count) {
            int frame = 0;
            /* fix: a strip of 1 icon divides by zero in the original */
            if (!(cflags & CREATURE_STATIC) && c.count / 2) frame = (int)(game.game_time % (uint32_t)(c.count / 2)) * 2;
            int side = (dir - (thing_word(group, GROUP_DIR) >> GROUP_DIR_SHIFT & 3)) & 3;
            draw_strip(&c, frame + (side & 1), x, y, mirror_two_sides[side]);
        } else if (!(cflags & CREATURE_STATIC) && !(cflags & POSSESSIONS_HIDE_CLICKS)) {
            Strip m = map_item_icons(map_thing);
            draw_strip(&m, game.game_time & 1, x, y, 0);
        }
    }
    if (!(cflags & CREATURE_STATIC) || !(flags & MAP_OPTION_ITEMS)) return;
    if (!(creature_group_info(group)->possessions & POSSESSIONS_SHOWN)) return;
    for (uint16_t t = dungeon_next_thing(group); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) < THING_WEAPON || thing_type(t) > THING_MISC) continue;
        Strip o = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
        if (o.count) draw_strip(&o, 0, x, y, mirror_one_side[((t >> THING_CELL_SHIFT) - dir) & 3]);
    }
}

/* The missile kinds of item_get_image_kind: which pictures a missile has. */
enum { MISSILE_NONE = -1, MISSILE_FRONT_MIRRORED, MISSILE_THREE_SIDES, MISSILE_FRONT_ONLY, MISSILE_NO_SIDE };

/* How the pictures of a flying object show its sides: from the images that exist in its GDAT entry. */
static int missile_image_kind(uint16_t thing)
{
    int category = item_gdat_category(thing), index = item_gdat_index(thing);
    const gdat_t *g = draw_gdat();
    if (!gdat_exists(g, category, index, GDAT_TYPE_IMAGE, 8)) return MISSILE_NONE;
    if (!gdat_exists(g, category, index, GDAT_TYPE_IMAGE, 12)) return MISSILE_NO_SIDE;
    if (gdat_exists(g, category, index, GDAT_TYPE_IMAGE, 10)) return MISSILE_THREE_SIDES;
    if (gdat_exists(g, category, index, GDAT_TYPE_IMAGE, 9)) return MISSILE_FRONT_MIRRORED;
    return MISSILE_FRONT_ONLY;
}

/* The icons of the missiles of a thing list from first. */
static void draw_missiles(uint16_t first, int dir, int x, int y)
{
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_PROJECTILE) continue;
        Strip m = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
        if (!m.count) continue;
        int icon = 0, cell = ((t >> THING_CELL_SHIFT) - dir) & 3;
        if (m.count > MISSILE_SIDES_ICON) {
            const TimelineEvent *e = &timeline.events[thing_word(t, PROJECTILE_EVENT)];
            uint16_t w = (uint16_t)(e->data[EVENT_MISSILE_DIR] | e->data[EVENT_MISSILE_DIR + 1] << 8);
            int side = ((w >> MISSILE_DIR_SHIFT & 3) - dir) & 3;
            icon = MISSILE_SIDES_ICON;
            switch (missile_image_kind(t)) {
            case MISSILE_THREE_SIDES: icon = side + MISSILE_SIDES_ICON + missile_side_shift[side][cell]; break;
            case MISSILE_NO_SIDE: break;
            default: icon = 0; break;
            }
        }
        draw_strip(&m, icon, x, y, mirror_one_side[cell]);
    }
}

/* The things of a square: creatures, items, missiles, explosions. */
static void draw_things(const ViewSquare *s, uint16_t map_thing, int dir, int x, int y, int flags, int fake_seen)
{
    uint16_t group = THING_ENDOFLIST, missiles = THING_ENDOFLIST, explosions = THING_ENDOFLIST;
    for (uint16_t t = s->first_thing; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (type == THING_GROUP) {
            uint16_t cflags = creature_group_info(t)->flags;
            if (gdat_entry_index(draw_gdat(), CAT_CREATURE, item_gdat_index(t), GDAT_TYPE_VALUE, STRIP_DETAIL_KEY)
                || flags & (cflags & CREATURE_STATIC ? MAP_STATIC_CREATURES : MAP_OPTION_CREATURES | MAP_MINION))
                group = t;
        } else if (flags & MAP_OPTION_ITEMS) {
            if (type == THING_PROJECTILE) {
                if (missiles == THING_ENDOFLIST) missiles = t;
            } else if (type == THING_EXPLOSION) {
                if (explosions == THING_ENDOFLIST) explosions = t;
            } else if (type >= THING_WEAPON && type <= THING_MISC) {
                Strip o = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
                if (!o.count) continue;
                int cell = ((t >> THING_CELL_SHIFT) - dir) & 3;
                if (s->aspect == ASPECT_WALL && !fake_seen) {
                    /* an item in an alcove */
                    if (o.count <= 2) continue;
                    draw_strip(&o, (cell & 1) + 1, x, y, mirror_two_sides[cell]);
                } else {
                    draw_strip(&o, 0, x, y, mirror_one_side[cell]);
                }
            }
        }
    }
    if (group != THING_ENDOFLIST) draw_group(group, map_thing, dir, x, y, flags);
    draw_missiles(missiles, dir, x, y);
    for (uint16_t t = explosions; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_EXPLOSION) continue;
        Strip e = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
        if (e.count) draw_strip(&e, (game.game_time & 1) + 1, x, y, rand_2bits());
    }
}

/* The square icon (and the things) of the square (sx, sy) of the current map at (x, y) of the screen. */
static void ui_map_draw_square(uint16_t map_thing, int sx, int sy, int dir, int x, int y, int icon_set, int flags)
{
    ViewSquare s;
    dun_get_square_aspect(&s, party.cur_map, dir, sx, sy);
    Strip base = map_icon_strip(CAT_MAP_ICONS, icon_set);
    if (flags & MAP_PARTY) {
        int16_t r[RECT_SIZE] = {(int16_t)(x - GAP_X), (int16_t)(y - GAP_Y), GAP_X * 2 + ICON_W, GAP_Y * 2 + ICON_H}, dx, dy;
        rect_clip(r, minion_map.clip, &dx, &dy);
        offscreen_fill_rect(area, r, ui_colors[UI_RED]);
    }
    int fake_seen = square_type(s.square) == SQUARE_FAKE_WALL && s.square & FAKE_WALL_SEEN;
    int other_map = 0, saved_map = 0, saved_x, saved_y;
    for (;;) {
        switch (s.aspect) {
        case ASPECT_WALL: case ASPECT_HIDDEN: draw_wall(&s, &base, x, y, flags, fake_seen, other_map); break;
        case ASPECT_FLOOR:
            if (s.square & FLOOR_SHOWS_LEVEL) {
                Strip digits = map_icon_strip(CAT_MAP_ICONS, level_icon_set());
                draw_strip(&digits, 0, x, y, 0);
                return;
            }
            draw_floor(&s, &base, x, y, flags);
            break;
        case ASPECT_TELEPORTER: draw_floor(&s, &base, x, y, flags); break;
        case ASPECT_DOOR_SIDE: case ASPECT_DOOR_FRONT: {
            int icon = s.aspect == ASPECT_DOOR_SIDE ? ICON_DOOR_SIDE : ICON_DOOR_FRONT;
            const uint8_t *door = dungeon_thing_record(s.side[SIDE_DOOR_THING]);
            if (!(door[2] & DOOR_BUTTON) && !(door[3] & DOOR_NO_ANIMATION >> 8)) icon += ICON_DOOR_NO_BUTTON;
            if (s.side[SIDE_DOOR_STATE] == DOOR_STATE_OPEN || s.side[SIDE_DOOR_STATE] == DOOR_STATE_BROKEN) icon += ICON_DOOR_OPEN;
            draw_strip(&base, icon, x, y, 0);
            break;
        }
        case ASPECT_STAIRS_FRONT: case ASPECT_STAIRS_SIDE: {
            int d = (dun_get_stairs_exit_direction(sx, sy) - dir) & 3;
            if (s.side[SIDE_STAIRS_UP]) d = (d + 2) & 3;
            draw_strip(&base, d + ICON_STAIRS_FIRST, x, y, 0);
            break;
        }
        case ASPECT_PIT: {
            int icon = ICON_PIT;
            if (s.side[SIDE_PIT_VARIANT]) icon = flags & MAP_OPTION_DETAILS && game.game_time & 1 ? ICON_PIT_BLINK : ICON_PIT_VARIANT;
            draw_strip(&base, icon, x, y, 0);
            break;
        }
        }
        if (flags & MAP_PARTY) {
            Strip p = map_item_icons(map_thing);
            for (int i = 0; i < game.champion_count; i++) {
                const Champion *c = champion(i);
                if (!c->health) continue;
                int icon = game.magic_light[MAGIC_INVISIBILITY] ? ICON_PARTY_INVISIBLE : i + ICON_PARTY_FIRST;
                draw_strip(&p, icon, x, y, mirror_one_side[(c->cell + 4 - dir) & 3]);
            }
        }
        if (flags & MAP_MARKER) {
            int icon = minion_map.marker_state == 2 ? ICON_MARKER_ABOVE : minion_map.marker_state == 3 ? ICON_MARKER_BELOW : ICON_MARKER;
            Strip m = map_item_icons(map_thing);
            draw_strip(&m, icon, x, y, 0);
        }
        if (!(flags & (MAP_STATIC_CREATURES | MAP_MINION | MAP_OPTION_ITEMS | MAP_OPTION_CREATURES))) return;
        draw_things(&s, map_thing, dir, x, y, flags, fake_seen);
        if (s.aspect != ASPECT_WALL || other_map) break;
        saved_x = sx;
        saved_y = sy;
        /* a wall square: another map of the level may show a wall there; like the original, the search leaves
           (sx, sy) on the map that it found */
        int next = 0, m = party.cur_map;
        while (m == party.cur_map) m = dungeon_location_on_map_from(party.cur_map, 0, &sx, &sy, &next);
        /* fix: the original reads the square byte also outside the map (out of its column table) */
        if (m < 0 || !inside_map(m, sx, sy) || square_type(dungeon_square(m, sx, sy)) != SQUARE_WALL) break;
        other_map = 1;
        saved_map = party.cur_map;
        dun_set_current_map(m);
        dun_get_square_aspect(&s, party.cur_map, dir, sx, sy);
    }
    if (other_map) {
        sx = saved_x;
        sy = saved_y;
        dun_set_current_map(saved_map);
        dun_get_square_aspect(&s, party.cur_map, dir, sx, sy);
    }
    /* an open pit in the level above shows as a hole in the ceiling */
    int ax = sx, ay = sy;
    int m = dungeon_location_on_map(party.cur_map, LEVEL_UP, &ax, &ay);
    if (m < 0 || !inside_map(m, ax, ay)) return;  /* fix: as above, no read outside the map */
    uint8_t square = dungeon_square(m, ax, ay);
    if (square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN) draw_strip(&base, ICON_PIT_ABOVE, x, y, 0);
}

/* The map one level up (level_delta -1) or down (1) that the square (*x, *y) of map leads to by its stairs, its open
   pit (mode TRANSFER_PIT), or a level link; *x, *y become the square there. -1 none. Leaves map current. */
static int level_transfer_destination(int16_t *x, int16_t *y, int map, int level_delta, int mode)
{
    int fall_into = 0;
    dun_set_current_map(map);
    uint8_t square = dun_get_square(*x, *y);
    int type = square_type(square);
    if (type == SQUARE_STAIRS) {
        if (!(mode & TRANSFER_STAIRS)) return -1;
        if (square & SQUARE_STAIRS_UP ? level_delta != LEVEL_UP : level_delta != LEVEL_DOWN) return -1;
    } else if (!(type == SQUARE_PIT && mode & TRANSFER_PIT && level_delta == LEVEL_DOWN && square & SQUARE_PIT_OPEN
                 && !(square & SQUARE_FLAG_0))) {
        if (!(square & SQUARE_LEVEL_LINK) || type == SQUARE_WALL || type == SQUARE_UNKNOWN || type == SQUARE_DOOR) return -1;
        uint16_t sensor;
        if (!(mode & TRANSFER_STAIRS) || dun_find_adjacent_level_link(*x, *y, level_delta, &sensor) < 0) {
            if (!(mode & TRANSFER_FALL_INTO) || level_delta != LEVEL_UP) return -1;
            fall_into = 1;
        }
    }
    int lx = *x, ly = *y;
    int found = dungeon_location_on_map(map, level_delta, &lx, &ly);
    *x = (int16_t)lx;
    *y = (int16_t)ly;
    if (found >= 0 && fall_into) {
        /* the square above must be an open pit that leads here */
        dun_set_current_map(found);
        square = dun_get_square(*x, *y);
        if (square_type(square) != SQUARE_PIT || !(square & SQUARE_PIT_OPEN) || square & SQUARE_FLAG_0) found = -1;
        dun_set_current_map(map);
    }
    return found;
}

/* Sets the marker square (x, y) of map and the squares that it leads to one level up and down. */
static void ui_map_set_marker(int x, int y, int map)
{
    MinionMap *m = &minion_map;
    m->marker_x = m->below_x = m->above_x = (int16_t)x;
    m->marker_y = m->below_y = m->above_y = (int16_t)y;
    m->marker_map = (int16_t)map;
    m->above_map = (int16_t)level_transfer_destination(&m->above_x, &m->above_y, map, LEVEL_UP,
                                                       TRANSFER_STAIRS | TRANSFER_FALL_INTO);
    m->below_map = (int16_t)level_transfer_destination(&m->below_x, &m->below_y, m->marker_map, LEVEL_DOWN,
                                                       TRANSFER_STAIRS | TRANSFER_PIT);
}

/* Draws the grid of squares around (x, y) of map looking to dir, shifted by (x_offset, y_offset) pixels. */
static void ui_map_draw_grid(uint16_t thing, int x, int y, int map, int dir, int x_offset, int y_offset, int flags)
{
    MinionMap *mm = &minion_map;
    if (flags & MAP_OPTION_HOLD) {
        x = mm->x;
        y = mm->y;
        map = mm->map;
        dir = mm->dir;
        x_offset = mm->x_offset;
        y_offset = mm->y_offset;
    } else {
        mm->x = (int8_t)x;
        mm->y = (int8_t)y;
        mm->map = (int8_t)map;
        mm->dir = (int8_t)dir;
        mm->x_offset = (int16_t)x_offset;
        mm->y_offset = (int16_t)y_offset;
    }
    mm->marker_state = 0;
    int saved_map = party.cur_map;
    area = ui_action_area_begin_draw(0);
    offscreen_fill_rect(area, zone_get_rect(ZONE_MAP_GRID, mm->clip), ui_colors[UI_BLACK]);
    mm->clip[RECT_X] += GAP_X;
    mm->clip[RECT_Y] += GAP_Y;
    mm->clip[RECT_W] -= GAP_X * 2;
    mm->clip[RECT_H] -= GAP_Y * 2;
    int left = mm->clip[RECT_X] + (int16_t)x_offset;
    int16_t row_y = (int16_t)(mm->clip[RECT_Y] + y_offset);
    int minion_x = 0, minion_y = 0, minion_map_index = 0, linked_x = 0, linked_y = 0, linked_map = -1;
    int show_party = flags & MAP_PARTY, show_minion = flags & MAP_MINION, show_marker = flags & MAP_MARKER;
    if (flags & (MAP_MINION | MAP_MARKER)) {
        if (flags & MAP_MINION) {
            uint16_t w = thing_word(minion_find_link_projectile(thing_word(thing, MAP_CONTENTS), thing), PROJECTILE_LOCATION);
            minion_x = LOCATION_X(w);
            minion_y = LOCATION_Y(w);
            minion_map_index = LOCATION_MAP(w);
            dun_set_current_map(minion_map_index);
            PassageEnd end;
            if (dun_get_linked_teleporter_dest(&end, minion_x, minion_y)) {
                linked_x = end.x;
                linked_y = end.y;
                linked_map = end.map;
            }
        }
        if (flags & MAP_MARKER) {
            uint16_t w = thing_word(thing, MAP_LOCATION);
            ui_map_set_marker(LOCATION_X(w), LOCATION_Y(w), LOCATION_MAP(w));
        }
    }
    dun_set_current_map(map);
    int first_row = y_offset < 0 ? GRID_RADIUS + 1 : GRID_RADIUS;
    int first_col = x_offset < 0 ? -GRID_RADIUS - 1 : -GRID_RADIUS;
    for (int row = first_row; row >= -GRID_RADIUS; row--) {
        int16_t col_x = (int16_t)left;
        for (int col = first_col; col <= GRID_RADIUS; col++) {
            int16_t sx = (int16_t)x, sy = (int16_t)y;
            dun_move_relative(dir, row, col, &sx, &sy);
            int lx = sx, ly = sy;
            int square_map = dungeon_location_on_map(map, 0, &lx, &ly);
            sx = (int16_t)lx;
            sy = (int16_t)ly;
            int icon_set;
            if (square_map < 0) {
                square_map = map;
                dun_set_current_map(map);
                icon_set = square_type(dun_get_square(sx, sy)) == SQUARE_UNKNOWN ? level_icon_set() : map_icon_set();
            } else {
                dun_set_current_map(square_map);
                icon_set = map_icon_set();
            }
            flags &= ~(MAP_PARTY | MAP_MINION | MAP_MARKER);
            int16_t m16 = (int16_t)square_map;
            if (show_minion && ((sx == minion_x && sy == minion_y && m16 == minion_map_index)
                                || (m16 == linked_map && sx == linked_x && sy == linked_y)))
                flags |= MAP_MINION;
            if (show_marker && !mm->marker_state) {
                if (sx == mm->marker_x && sy == mm->marker_y && m16 == mm->marker_map) {
                    mm->marker_state = 1;
                } else if (sx == mm->above_x && sy == mm->above_y && m16 == mm->above_map) {
                    mm->marker_state = 2;
                    ui_map_set_marker(sx, sy, m16);
                } else if (sx == mm->below_x && sy == mm->below_y && m16 == mm->below_map) {
                    mm->marker_state = 3;
                    ui_map_set_marker(sx, sy, m16);
                }
                if (mm->marker_state) flags |= MAP_MARKER;
            }
            if (show_party && m16 == party.square_map && sx == party.square_x && sy == party.square_y) flags |= MAP_PARTY;
            ui_map_draw_square(thing, sx, sy, dir, col_x, row_y, icon_set, flags);
            col_x = (int16_t)(col_x + ICON_W + GAP_X);
        }
        row_y = (int16_t)(row_y + ICON_H + GAP_Y);
    }
    dun_set_current_map(saved_map);
}

void ui_draw_map_option_buttons(int pressed)
{
    area = ui_action_area_begin_draw(0);
    if (action.champion <= 0 || !(action.flags & MAP_HAS_OPTIONS)) return;
    int down = (pressed ^ action.flags) & MAP_OPTIONS;
    for (int i = 0; i < MAP_OPTION_COUNT; i++, down >>= 1) {
        offscreen_draw_gdat_image(area, GDAT_CONTAINER_CATEGORY, action.map_item, down & 1 ? OPTION_BUTTON_DOWN : OPTION_BUTTON_UP,
                                  ZONE_OPTION_BUTTON_FIRST + i, UI_OPAQUE);
        char label[2] = {(char)(OPTION_LABEL_FIRST + i), '\0'};
        offscreen_draw_text_in_zone(area, ZONE_OPTION_LABEL_FIRST + i, ui_colors[UI_BLACK], ui_colors[UI_GRAY] | TEXT_TRANSPARENT,
                                    label);
    }
}

void ui_draw_minion_map(uint16_t thing)
{
    action.map_item = item_gdat_index(thing);
    action.flags |= MAP_STATIC_CREATURES | MAP_PARTY;
    int kind = thing_word(thing, CONTAINER_KIND) >> MAP_KIND_SHIFT;
    uint16_t group = thing_word(thing, MAP_CONTENTS), link = 0;
    if (kind != MAP_KIND_NO_OPTIONS) {
        action.flags |= MAP_HAS_OPTIONS;
        if (kind != MAP_KIND_PLAIN) {
            link = minion_find_link_projectile(group, thing);
            if (link) action.flags |= MAP_MINION;
            else if (action.flags & MAP_MINION) action.flags &= ~(MAP_MINION | MAP_PANEL_DRAWN);
        }
    }
    area = ui_action_area_begin_draw(0);
    if (!(action.flags & MAP_PANEL_DRAWN)) {
        offscreen_draw_gdat_image(area, GDAT_CONTAINER_CATEGORY, action.map_item, MAP_PANEL_PICTURE, ZONE_PANEL, UI_OPAQUE);
        for (int i = 0; i < champ_action_count; i++) ui_draw_action_name(i, 0);
        ui_draw_map_option_buttons(0);
        action.flags |= MAP_PANEL_DRAWN;
    }
    int x = game.party_x, y = game.party_y, map = game.party_map, x_offset = 0, y_offset = 0;
    if (kind != MAP_KIND_PLAIN && kind != MAP_KIND_NO_OPTIONS) {
        if ((thing_word(thing, MAP_LOCATION) & MAP_LOCATION_NONE) != MAP_LOCATION_NONE) action.flags |= MAP_MARKER;
        if (kind == MAP_KIND_MINION) {
            if (!link) {
                action.flags &= ~MAP_MARKER;
            } else {
                /* the grid follows the minion; it shifts by where the minion stands in its square */
                uint16_t w = thing_word(link, PROJECTILE_LOCATION);
                x = LOCATION_X(w);
                y = LOCATION_Y(w);
                map = LOCATION_MAP(w);
                int dir = game.party_dir;
                int cell = creature_get_view_cell_zone(group, (creature_side_dir[thing_word(group, GROUP_DIR) >> GROUP_DIR_SHIFT & 3] + dir) & 3);
                int col = (int16_t)cell % VIEW_CELL_ROW, row = (int16_t)cell / VIEW_CELL_ROW;
                if (col > 2) {
                    x += dir_dx[(dir + 1) & 3];
                    y += dir_dy[(dir + 1) & 3];
                }
                if (row > 2) {
                    x += dir_dx[(dir + 2) & 3];
                    y += dir_dy[(dir + 2) & 3];
                }
                x_offset = (GAP_X * 2 + ICON_W + 1) / VIEW_CELL_ROW * cell_shift[col];
                y_offset = (GAP_Y * 2 + ICON_H + 1) / VIEW_CELL_ROW * cell_shift[row];
            }
        } else if (action.flags & MAP_MARKER) {
            uint16_t w = thing_word(thing, MAP_LOCATION);
            x = LOCATION_X(w);
            y = LOCATION_Y(w);
            map = LOCATION_MAP(w);
        }
    }
    ui_map_draw_grid(thing, (int16_t)x, (int16_t)y, map, game.party_dir, (int16_t)x_offset, (int16_t)y_offset, action.flags);
    if (action.flags & MAP_DIMMED || !(action.flags & MAP_POWERED)) {
        int16_t r[RECT_SIZE];
        offscreen_fill_rect_dithered(area, zone_get_rect(ZONE_MAP_GRID, r), UI_BLACK);
    }
}

void ui_toggle_map_option(int option)
{
    int mana = option_mana[option], bit = 1 << option;
    if (!(action.flags & bit)) {
        Champion *c = champion(action.champion - 1);
        if (c->mana < mana) return;
        c->mana = (int16_t)(c->mana - mana);
        c->redraw |= REDRAW_BARS;
    } else {
        mana = -mana;
    }
    action.flags ^= (uint16_t)bit;
    minion_map.mana_divisor = (int16_t)(minion_map.mana_divisor + mana);
}

void minion_map_open(int champ)
{
    Champion *c = champion(champ);
    minion_map.mana_divisor = 1;
    minion_map.mana_timer = MINION_MAP_MANA_TICKS;
    if (!c->mana) {
        action.flags = MAP_OPEN;
    } else {
        c->mana--;
        c->redraw |= REDRAW_BARS;
        action.flags = MAP_OPEN | MAP_POWERED;
    }
    action.mode = ACTION_MODE_MINION_MAP;
}

void minion_map_drain_mana(void)
{
    Champion *c = champion(action.champion - 1);
    if (!(action.flags & MAP_POWERED)) {
        if (c->mana) {
            /* the champion has mana again: the map opens anew */
            action.redraw_delay = 1;
            action.flags = 0;
        }
        return;
    }
    if (--minion_map.mana_timer) return;
    if (!c->mana) {
        action.redraw_delay = 1;
        action.flags = 0;
        return;
    }
    c->mana--;
    c->redraw |= REDRAW_BARS;
    minion_map.mana_timer = (int16_t)(MINION_MAP_MANA_TICKS / minion_map.mana_divisor);
}

int minion_map_set_location(uint16_t thing, int x, int y, int map)
{
    int saved = party.cur_map;
    dun_set_current_map(map);
    int inside = x >= 0 && x < dungeon_map_width(party.cur_map) && y >= 0 && y < dungeon_map_height(party.cur_map);
    if (inside) set_thing_word(thing, MAP_LOCATION, LOCATION(x, y, map));
    dun_set_current_map(saved);
    return inside;
}

void minion_map_click_set_destination(int command, int x, int y)
{
    uint16_t thing = champion(action.champion - 1)->slots[action.hand];
    if (command != CMD_MINION_MAP || (thing_word(thing, CONTAINER_KIND) >> MAP_KIND_SHIFT) != MAP_KIND_MINION) return;
    int16_t r[RECT_SIZE];
    zone_get_rect(ZONE_MAP_GRID, r);
    int16_t px = (int16_t)(x - (r[RECT_X] - minion_map.x_offset)), py = (int16_t)(y - (r[RECT_Y] - minion_map.y_offset));
    if (px % (ICON_W + GAP_X) < GAP_X || py % (ICON_H + GAP_Y) < GAP_Y) return;
    uint16_t group = thing_word(thing, MAP_CONTENTS);
    if (!minion_find_link_projectile(group, thing)) return;
    int col = px / (ICON_W + GAP_X), row = py / (ICON_H + GAP_Y);
    int16_t sx = minion_map.x, sy = minion_map.y;
    dun_move_relative(minion_map.dir, GRID_RADIUS - row, col - GRID_RADIUS, &sx, &sy);
    int lx = sx, ly = sy;
    int map = dungeon_location_on_map(minion_map.map, 0, &lx, &ly);
    if (map < 0) return;
    sx = (int16_t)lx;
    sy = (int16_t)ly;
    int saved = party.cur_map;
    dun_set_current_map(map);
    uint8_t square = dun_get_square(sx, sy);
    int type = square_type(square);
    if (type != SQUARE_WALL && type != SQUARE_UNKNOWN && (type != SQUARE_FAKE_WALL || square & (SQUARE_FAKE_WALL_OPEN | SQUARE_FLAG_0))) {
        if (sx == minion_map.marker_x && sy == minion_map.marker_y && map == minion_map.marker_map) {
            /* a click on the marker of a stairs or a pit goes to the level that it leads to */
            if (minion_map.marker_state == 1 && minion_map.above_map >= 0) {
                sx = minion_map.above_x;
                sy = minion_map.above_y;
                map = minion_map.above_map;
            } else if (minion_map.below_map >= 0 && (minion_map.marker_state == 1 || minion_map.marker_state == 3)) {
                sx = minion_map.below_x;
                sy = minion_map.below_y;
                map = minion_map.below_map;
            }
        }
        minion_map_set_location(thing, sx, sy, map);
        creature_free_cached_ai_data(group);
        ui_update_action_area(0);
    }
    dun_set_current_map(saved);
}
