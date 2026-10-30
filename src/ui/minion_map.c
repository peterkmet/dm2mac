/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The minion map panel in the action area: the grid of square icons around the party, a minion, or a marker. */
#include "minion_map.h"
#include <string.h>
#include "action.h"
#include "action_area.h"
#include "aspect.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "game_text.h"
#include "gdat.h"
#include "ids.h"
#include "item.h"
#include "minion.h"
#include "projectile.h"
#include "random.h"
#include "text.h"
#include "ui.h"
#include "view.h"
#include "zone.h"

/* The label of the first option button; the other buttons take the next letters. */
#define OPTION_LABEL_FIRST 'r'
#define OPTION_LABEL_SIZE  2 /* the label letter and the end */

/* GDAT indices are bytes. */
#define GDAT_INDEX_COUNT 256

/* Icon strips: GDAT image (category, index, GDAT_TYPE_IMAGE, GFX_STRIP), MAP_ICON_W pixels per icon, color ICON_KEY
   transparent; the value (category, index, GDAT_TYPE_VALUE, STRIP_DETAIL_KEY) set: the thing shows only with
   MAP_OPTION_DETAILS. */
#define ICON_KEY         10
#define STRIP_DETAIL_KEY GFX_STRIP

/* The fake wall of a seen square blinks on this bit of game_time. */
#define FAKE_WALL_BLINK_BIT 0x02

/* Offsets added to a door icon of the square strip, and the icons of a missile and an alcove item. */
#define ICON_DOOR_NO_BUTTON 2
#define ICON_DOOR_OPEN      4
#define MISSILE_SIDES_ICON  3
#define ALCOVE_ICON_COUNT   3 /* an item strip with alcove icons: the floor icon, then front and side */

/* Square bits that the map reads. */
#define FAKE_WALL_SEEN    SQUARE_FLAG_0
#define FLOOR_SHOWS_LEVEL SQUARE_FLAG_3 /* a floor square shows the level digit instead */

/* Map 63 of a map item location: no location. */
#define MAP_LOCATION_NONE LOCATION_MAP_MASK

/* The 5 x 5 cells of a square in the view: cells in a row. */
#define VIEW_CELL_ROW 5

/* An icon strip; count: the icons of a strip of square icons (map_icon_strip), 0 for a map item strip
   (map_item_icons), whose callers rely on the image test of show_icon. */
typedef struct {
    const Bitmap *image; /* NULL: no strip */
    const uint8_t *palette;
    int count;
} Strip;

/* A square to draw: the square (sx, sy) of the current map, the screen position (x, y) of its icon, the direction
   that the map looks to, and the map item thing with its MAP_ flags; fake_seen: a fake wall that the party found;
   other_map: the wall of another map of the level; area: the bitmap that the icons go into. */
typedef struct {
    int16_t sx, sy;
    int16_t x, y;
    int16_t dir;
    uint16_t map_thing;
    uint16_t flags;
    bool fake_seen;
    bool other_map;
    Offscreen *area;
} MapSquare;

/* The result of clip_icon_rect: visible false when nothing is left; dx, dy: the pixels cut at the left and top. */
typedef struct {
    bool visible;
    int16_t dx, dy;
} IconClip;

/* The squares that show the minion of a map item: its square and the far end of a teleporter there (map -1 none). */
typedef struct {
    Position minion;
    Position linked;
} MinionSquares;

/* The grid of the minion map: its middle square, the direction it looks to, and its pixel shift. */
typedef struct {
    Position center;
    int16_t dir;
    int16_t x_offset, y_offset;
} GridView;

/* A square of the grid: its position, and the icon set that it shows. */
typedef struct {
    Position at;
    int icon_set;
} GridSquare;

/* Mirror of an icon by the relative direction: icons with 2 sides (front, side), icons with 1 side. */
static const uint8_t mirror_two_sides[DIR_COUNT] = {0, 0, MIRROR_HV, MIRROR_HV};
static const uint8_t mirror_one_side[DIR_COUNT] = {0, MIRROR_H, MIRROR_HV, MIRROR_V};
/* The icon shift of a missile with 3 sides, by its relative direction and its relative cell. */
static const int8_t missile_side_shift[DIR_COUNT][DIR_COUNT] = {
    {0, 0,  2,  2 },
    {0, 2,  2,  0 },
    {0, 0,  -2, -2},
    {0, -2, -2, 0 }
};
/* The pixel shift of the grid by the column (row) of the minion in the 5 x 5 cells of its square. */
static const int8_t cell_shift[VIEW_CELL_ROW] = {-2, -3, 0, -1, -2};
/* The relative direction of a creature by the side it shows: front, right, back, left. */
static const uint8_t creature_side_dir[DIR_COUNT] = {0, TURN_LEFT, TURN_BACK, TURN_RIGHT};

/* ---- Icon strips ---- */

/* The icon strip of a GDAT key; count 0 when it has none. */
static Strip map_icon_strip(int category, int index)
{
    Strip s = {0};
    if (!gdat_exists(gdat_current(), category, index, GDAT_TYPE_IMAGE, GFX_STRIP))
        return s;
    s.image = draw_gdat_image(category, index, GFX_STRIP);
    s.palette = draw_gdat_image_palette(category, index, GFX_STRIP);
    s.count = s.image ? s.image->width / MAP_ICON_W : 0;
    return s;
}

/* The map item icons of the container item. */
static Strip party_item_icons(int item)
{
    return (Strip){draw_gdat_image(CAT_CONTAINER, item, GFX_MAP_ITEM_ICONS),
                   draw_gdat_image_palette(CAT_CONTAINER, item, GFX_MAP_ITEM_ICONS), 0};
}

/* The party, minion, and marker icons of the map item; THING_NONE (the automap) takes the first map item image. */
static Strip map_item_icons(uint16_t thing)
{
    if (thing == THING_NONE) {
        int item = mmap_party_icons_item();
        if (item < 0)
            return (Strip){0};
        return party_item_icons(item);
    }
    int category = item_gdat_category(thing);
    int index = item_gdat_index(thing);
    return (Strip){draw_gdat_image(category, index, GFX_MAP_ITEM_ICONS),
                   draw_gdat_image_palette(category, index, GFX_MAP_ITEM_ICONS), 0};
}

/* True when the GDAT key has the detail value: it shows only with MAP_OPTION_DETAILS. */
static bool detail_only(int category, int index)
{
    return gdat_entry_index(gdat_current(), category, index, GDAT_TYPE_VALUE, STRIP_DETAIL_KEY) != 0;
}

bool mmap_shows_floor_ornament(int ornament)
{
    return ornament != ORNAMENT_NONE && !detail_only(CAT_FLOOR_ORNAMENT, ornament);
}

/* Clips rect to clip. */
static IconClip clip_icon_rect(int16_t *rect, const int16_t *clip)
{
    IconClip c;
    int16_t d = (int16_t)(clip[RECT_X] - rect[RECT_X]);
    c.dx = d > 0 ? d : 0;
    if (d > 0) {
        rect[RECT_X] += d;
        rect[RECT_W] -= d;
    }
    d = (int16_t)(clip[RECT_Y] - rect[RECT_Y]);
    c.dy = d > 0 ? d : 0;
    if (d > 0) {
        rect[RECT_Y] += d;
        rect[RECT_H] -= d;
    }
    d = (int16_t)((rect[RECT_X] + rect[RECT_W]) - (clip[RECT_X] + clip[RECT_W]));
    if (d > 0)
        rect[RECT_W] -= d;
    d = (int16_t)((rect[RECT_Y] + rect[RECT_H]) - (clip[RECT_Y] + clip[RECT_H]));
    if (d > 0)
        rect[RECT_H] -= d;
    c.visible = rect[RECT_W] > 0 && rect[RECT_H] > 0;
    return c;
}

/* Draws icon of a strip at the screen position of the square at, inside the grid, mirrored by mirror (MIRROR_H,
   MIRROR_V). */
static void show_icon(const MapSquare *at, const Strip *s, int icon, int mirror)
{
    if (!s->image)
        return;
    if (s->count && icon >= s->count)
        return; /* fix: no icon past the end of a short strip (damaged GRAPHICS.DAT) */
    int16_t r[RECT_SIZE] = {at->x, at->y, MAP_ICON_W, MAP_ICON_H};
    IconClip c = clip_icon_rect(r, g_minion_map.clip);
    if (!c.visible)
        return;
    if (MAP_ICON_W - r[RECT_W] && mirror & MIRROR_H)
        c.dx = c.dx ? 0 : (int16_t)(MAP_ICON_W - r[RECT_W]);
    if (MAP_ICON_H - r[RECT_H] && mirror & MIRROR_V)
        c.dy = c.dy ? 0 : (int16_t)(MAP_ICON_H - r[RECT_H]);
    offscreen_blit(at->area,
                   &(BlitSource){.bitmap = s->image,
                                 .x = (int16_t)(icon * MAP_ICON_W + c.dx),
                                 .y = c.dy,
                                 .transparent = ICON_KEY,
                                 .mirror = mirror,
                                 .colormap = s->palette},
                   r);
}

/* ---- Square layers ---- */

/* True for a square inside its map. */
static bool inside_map(Position at)
{
    return at.x >= 0 && at.x < dungeon_map_width(at.map) && at.y >= 0 && at.y < dungeon_map_height(at.map);
}

/* The square of a location word. */
static Position unpack_location(uint16_t w)
{
    return (Position){location_map(w), location_x(w), location_y(w)};
}

/* The level digit icons of the current map: what a square outside the maps of a level shows. */
static int level_icon_set(void)
{
    return dungeon_level_digit(dungeon_map_level(g_current_map.map));
}

/* The icons of a wall square: the wall and the ornaments of its 4 faces. */
static void show_wall(const ViewSquare *s, const Strip *base, const MapSquare *at)
{
    if (at->flags & MAP_OPTION_DETAILS && at->fake_seen && g_game.game_time & FAKE_WALL_BLINK_BIT) {
        show_icon(at, base, ICON_FAKE_WALL_BLINK, 0);
        return;
    }
    if (!at->other_map)
        show_icon(at, base, ICON_WALL, 0);
    for (int side = 0; side < VIEW_SIDE_COUNT; side++) {
        int ornament = s->side[side] & ORNAMENT_INDEX_MASK;
        if (ornament == ORNAMENT_NONE)
            continue;
        if (!(at->flags & MAP_OPTION_DETAILS) && detail_only(CAT_WALL_ORNAMENT, ornament))
            continue;
        Strip o = map_icon_strip(CAT_WALL_ORNAMENT, ornament);
        if (!o.count)
            continue;
        int frame = aspect_face_frame(s->side[side]);
        int icon = o.count < frame * 2 + 2 ? side & 1 : frame * 2 + (side & 1);
        show_icon(at, &o, icon, mirror_two_sides[side]);
    }
}

/* The icons of a floor or teleporter square: the floor, its ornament, the teleporter field. */
static void show_floor(const ViewSquare *s, const Strip *base, const MapSquare *at)
{
    show_icon(at, base, ICON_FLOOR, 0);
    int ornament = s->side[SIDE_FLOOR_ORNAMENT] & ORNAMENT_INDEX_MASK;
    if (at->flags & MAP_OPTION_DETAILS ? ornament != ORNAMENT_NONE : mmap_shows_floor_ornament(ornament)) {
        Strip o = map_icon_strip(CAT_FLOOR_ORNAMENT, ornament);
        if (o.count) {
            int frame = aspect_floor_frame(s->side[SIDE_FLOOR_ORNAMENT]);
            show_icon(at, &o, o.count < frame + 1 ? 0 : frame, 0);
        }
    }
    if (s->aspect != ASPECT_TELEPORTER)
        return;
    Strip t = map_icon_strip(CAT_TELEPORTER, 0);
    if (t.count)
        show_icon(at, &t, (int)(g_game.game_time % (uint32_t)t.count), 0);
}

/* The icon of the creature group, and the items that a static creature holds on its square. */
static void show_group(uint16_t group, const MapSquare *at)
{
    int16_t dir = at->dir;
    uint16_t flags = at->flags;
    uint16_t map_thing = at->map_thing;
    uint16_t cflags = creature_group_info(group)->flags;
    if (flags & MAP_MINION) {
        Strip m = map_item_icons(map_thing);
        show_icon(at, &m, ICON_MINION_FIRST + (g_game.game_time & 1), 0);
    } else {
        Strip c = map_icon_strip(item_gdat_category(group), item_gdat_index(group));
        if (c.count) {
            int frame = 0;
            /* fix: a strip of 1 icon divides by zero in the original */
            if (!(cflags & CREATURE_STATIC) && c.count / 2)
                frame = (int)(g_game.game_time % (uint32_t)(c.count / 2)) * 2;
            int side = dir_relative(dir, group_dir(group));
            show_icon(at, &c, frame + (side & 1), mirror_two_sides[side]);
        } else if (!(cflags & CREATURE_STATIC) && !(cflags & POSSESSIONS_FLOOR_CLICKABLE)) {
            Strip m = map_item_icons(map_thing);
            show_icon(at, &m, g_game.game_time & 1, 0);
        }
    }
    if (!(cflags & CREATURE_STATIC) || !(flags & MAP_OPTION_ITEMS))
        return;
    if (!(creature_group_info(group)->possessions & POSSESSIONS_KIND_MASK))
        return;
    for (uint16_t t = dungeon_next_thing(group); t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) < THING_WEAPON || thing_type(t) > THING_MISC)
            continue;
        Strip o = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
        if (o.count)
            show_icon(at, &o, 0, mirror_one_side[dir_relative(thing_cell(t), dir)]);
    }
}

/* The icons of the missiles of a thing list from first. */
static void show_missiles(uint16_t first, const MapSquare *at)
{
    int16_t dir = at->dir;
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_PROJECTILE)
            continue;
        Strip m = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
        if (!m.count)
            continue;
        int icon = 0;
        int cell = dir_relative(thing_cell(t), dir);
        if (m.count > MISSILE_SIDES_ICON) {
            int side = dir_relative(projectile_dir(t), dir);
            icon = MISSILE_SIDES_ICON;
            switch (view_missile_kind(t)) {
            case MISSILE_FRONT_BACK:
                icon = side + MISSILE_SIDES_ICON + missile_side_shift[side][cell];
                break;
            case MISSILE_NO_SIDE:
                break;
            default:
                icon = 0;
                break;
            }
        }
        show_icon(at, &m, icon, mirror_one_side[cell]);
    }
}

/* The things of a square: creatures, items, missiles, explosions. */
static void show_things(const ViewSquare *s, const MapSquare *at)
{
    int16_t dir = at->dir;
    uint16_t flags = at->flags;
    uint16_t group = THING_ENDOFLIST;
    uint16_t missiles = THING_ENDOFLIST;
    uint16_t explosions = THING_ENDOFLIST;
    for (uint16_t t = s->first_thing; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        int type = thing_type(t);
        if (type == THING_GROUP) {
            uint16_t cflags = creature_group_info(t)->flags;
            if (detail_only(CAT_CREATURE, item_gdat_index(t)) ||
                flags & (cflags & CREATURE_STATIC ? MAP_STATIC_CREATURES : MAP_OPTION_CREATURES | MAP_MINION))
                group = t;
        } else if (flags & MAP_OPTION_ITEMS) {
            if (type == THING_PROJECTILE) {
                if (missiles == THING_ENDOFLIST)
                    missiles = t;
            } else if (type == THING_EXPLOSION) {
                if (explosions == THING_ENDOFLIST)
                    explosions = t;
            } else if (type >= THING_WEAPON && type <= THING_MISC) {
                Strip o = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
                if (!o.count)
                    continue;
                int cell = dir_relative(thing_cell(t), dir);
                if (s->aspect == ASPECT_WALL && !at->fake_seen) {
                    /* an item in an alcove */
                    if (o.count < ALCOVE_ICON_COUNT)
                        continue;
                    show_icon(at, &o, (cell & 1) + 1, mirror_two_sides[cell]);
                } else {
                    show_icon(at, &o, 0, mirror_one_side[cell]);
                }
            }
        }
    }
    if (group != THING_ENDOFLIST)
        show_group(group, at);
    show_missiles(missiles, at);
    for (uint16_t t = explosions; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_EXPLOSION)
            continue;
        Strip e = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
        if (e.count)
            show_icon(at, &e, (g_game.game_time & 1) + 1, rand_2bits());
    }
}

/* ---- Party and marker ---- */

/* The red frame around the party square at. */
static void show_party_frame(const MapSquare *at)
{
    int16_t r[RECT_SIZE] = {(int16_t)(at->x - GAP_X), (int16_t)(at->y - GAP_Y), GAP_X * 2 + MAP_ICON_W,
                            GAP_Y * 2 + MAP_ICON_H};
    clip_icon_rect(r, g_minion_map.clip);
    offscreen_fill_rect(at->area, r, g_ui_colors[UI_RED]);
}

/* The white side of the party frame around the square at toward dir, the direction that the party faces. */
static void show_party_facing(const MapSquare *at, int16_t dir)
{
    int16_t r[RECT_SIZE] = {(int16_t)(at->x - GAP_X), (int16_t)(at->y - GAP_Y), GAP_X * 2 + MAP_ICON_W,
                            GAP_Y * 2 + MAP_ICON_H};
    if (dir == DIR_SOUTH)
        r[RECT_Y] = (int16_t)(at->y + MAP_ICON_H);
    else if (dir == DIR_EAST)
        r[RECT_X] = (int16_t)(at->x + MAP_ICON_W);
    if (dir == DIR_NORTH || dir == DIR_SOUTH)
        r[RECT_H] = GAP_Y;
    else
        r[RECT_W] = GAP_X;
    if (clip_icon_rect(r, g_minion_map.clip).visible)
        offscreen_fill_rect(at->area, r, g_ui_colors[UI_WHITE]);
}

/* The icon of each living champion at its cell of the party square at, for a map that looks to at->dir. */
static void show_party_icons(const MapSquare *at, const Strip *p)
{
    for (int i = 0; i < g_game.champion_count; i++) {
        const Champion *c = champion(i);
        if (!c->health)
            continue;
        int icon = g_game.magic_light[MAGIC_INVISIBILITY] ? ICON_PARTY_INVISIBLE : i + ICON_PARTY_FIRST;
        show_icon(at, p, icon, mirror_one_side[dir_relative(c->cell, at->dir)]);
    }
}

/* The square icon of the view square s at (x, y) of the screen; returns false when the square shows the level digit
   and nothing else. */
static bool show_square_base(const ViewSquare *s, const Strip *base, const MapSquare *at)
{
    switch (s->aspect) {
    case ASPECT_WALL:
    case ASPECT_HIDDEN:
        show_wall(s, base, at);
        break;
    case ASPECT_FLOOR:
        if (s->square & FLOOR_SHOWS_LEVEL) {
            Strip digits = map_icon_strip(CAT_WALLSET, level_icon_set());
            show_icon(at, &digits, 0, 0);
            return false;
        }
        show_floor(s, base, at);
        break;
    case ASPECT_TELEPORTER:
        show_floor(s, base, at);
        break;
    case ASPECT_DOOR_SIDE:
    case ASPECT_DOOR_FRONT: {
        int icon = s->aspect == ASPECT_DOOR_SIDE ? ICON_DOOR_SIDE : ICON_DOOR_FRONT;
        if (!door_has_button(s->side[SIDE_DOOR_THING]))
            icon += ICON_DOOR_NO_BUTTON;
        if (s->side[SIDE_DOOR_STATE] == DOOR_STATE_OPEN || s->side[SIDE_DOOR_STATE] == DOOR_STATE_BROKEN)
            icon += ICON_DOOR_OPEN;
        show_icon(at, base, icon, 0);
        break;
    }
    case ASPECT_STAIRS_FRONT:
    case ASPECT_STAIRS_SIDE: {
        int d = dir_relative(dungeon_stairs_exit_direction((Location){at->sx, at->sy}), at->dir);
        if (s->side[SIDE_STAIRS_UP])
            d = dir_opposite(d);
        show_icon(at, base, d + ICON_STAIRS_FIRST, 0);
        break;
    }
    case ASPECT_PIT: {
        int icon = ICON_PIT;
        if (s->side[SIDE_PIT_UP])
            icon = at->flags & MAP_OPTION_DETAILS && g_game.game_time & 1 ? ICON_PIT_BLINK : ICON_PIT_VARIANT;
        show_icon(at, base, icon, 0);
        break;
    }
    }
    return true;
}

/* The party icons and the marker icon of a square at (x, y) of the screen, by flags. */
static void show_party_and_marker(const MapSquare *at)
{
    if (at->flags & MAP_PARTY) {
        Strip p = map_item_icons(at->map_thing);
        show_party_icons(at, &p);
    }
    if (at->flags & MAP_MARKER) {
        int icon = g_minion_map.marker_state == MARKER_ABOVE   ? ICON_MARKER_ABOVE
                   : g_minion_map.marker_state == MARKER_BELOW ? ICON_MARKER_BELOW
                                                               : ICON_MARKER;
        Strip m = map_item_icons(at->map_thing);
        show_icon(at, &m, icon, 0);
    }
}

/* An open pit in the level above the square (sx, sy) of the current map shows as a hole in the ceiling. */
static void show_pit_above(const Strip *base, const MapSquare *at)
{
    Position above = dungeon_location_on_map((Position){g_current_map.map, at->sx, at->sy}, LEVEL_UP);
    if (above.map < 0 || !inside_map(above))
        return; /* fix: no read outside the map one level up */
    uint8_t square = dungeon_square(above.map, above.x, above.y);
    if (square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN)
        show_icon(at, base, ICON_PIT_ABOVE, 0);
}

/* The square icon (and the things) of the square (sx, sy) of the current map at (x, y) of the screen. */
static void show_square(const MapSquare *at, int icon_set)
{
    MapSquare sq = *at;
    ViewSquare s;
    aspect_get(&s, g_current_map.map, sq.dir, sq.sx, sq.sy);
    Strip base = map_icon_strip(CAT_WALLSET, icon_set);
    if (sq.flags & MAP_PARTY)
        show_party_frame(&sq);
    sq.fake_seen = square_type(s.square) == SQUARE_FAKE_WALL && s.square & FAKE_WALL_SEEN;
    sq.other_map = false;
    Position saved = {0, 0, 0};
    for (;;) {
        if (!show_square_base(&s, &base, &sq))
            return;
        show_party_and_marker(&sq);
        if (!(sq.flags & (MAP_STATIC_CREATURES | MAP_MINION | MAP_OPTION_ITEMS | MAP_OPTION_CREATURES)))
            return;
        show_things(&s, &sq);
        if (s.aspect != ASPECT_WALL || sq.other_map)
            break;
        saved.x = sq.sx;
        saved.y = sq.sy;
        /* a wall square: another map of the level may show a wall there; as in the original, the search leaves
           (sx, sy) on the map that it found */
        int next = 0;
        Position found = {g_current_map.map, sq.sx, sq.sy};
        while (found.map == g_current_map.map) {
            found = dungeon_location_on_map_from((Position){g_current_map.map, sq.sx, sq.sy}, 0, &next);
            sq.sx = found.x;
            sq.sy = found.y;
        }
        /* fix: the original reads the square byte also outside the map (out of its column table) */
        if (found.map < 0 || !inside_map(found) ||
            square_type(dungeon_square(found.map, found.x, found.y)) != SQUARE_WALL)
            break;
        sq.other_map = true;
        saved.map = g_current_map.map;
        dungeon_set_current_map(found.map);
        aspect_get(&s, g_current_map.map, sq.dir, sq.sx, sq.sy);
    }
    if (sq.other_map) {
        sq.sx = saved.x;
        sq.sy = saved.y;
        dungeon_set_current_map(saved.map);
        aspect_get(&s, g_current_map.map, sq.dir, sq.sx, sq.sy);
    }
    show_pit_above(&base, &sq);
}

int mmap_party_icons_item(void)
{
    for (int index = 0; index < GDAT_INDEX_COUNT; index++)
        if (gdat_exists(gdat_current(), CAT_CONTAINER, index, GDAT_TYPE_IMAGE, GFX_MAP_ITEM_ICONS))
            return index;
    return -1;
}

void mmap_draw_square_icon(Offscreen *target, const int16_t *clip, const SquareIcon *icon)
{
    int16_t saved[RECT_SIZE];
    memcpy(saved, g_minion_map.clip, sizeof saved);
    memcpy(g_minion_map.clip, clip, sizeof saved);
    MapSquare sq = {.sx = icon->sx,
                    .sy = icon->sy,
                    .x = icon->x,
                    .y = icon->y,
                    .dir = DIR_NORTH,
                    .map_thing = THING_NONE,
                    .flags = icon->flags,
                    .area = target};
    if (icon->party_item >= 0) {
        show_party_frame(&sq);
        show_party_facing(&sq, icon->party_dir);
    }
    show_square(&sq, dungeon_map_wallset(g_current_map.map));
    if (icon->party_item >= 0) {
        Strip p = party_item_icons(icon->party_item);
        show_party_icons(&sq, &p);
    }
    memcpy(g_minion_map.clip, saved, sizeof saved);
}

/* ---- Grid ---- */

/* Sets the marker square and the squares that it leads to one level up and down. */
static void set_marker(Position marker)
{
    MinionMap *m = &g_minion_map;
    m->marker_x = marker.x;
    m->marker_y = marker.y;
    m->marker_map = marker.map;
    Position above = dungeon_find_level_transfer(marker, LEVEL_UP, TRANSFER_STAIRS | TRANSFER_FALL_INTO);
    Position below = dungeon_find_level_transfer(marker, LEVEL_DOWN, TRANSFER_STAIRS | TRANSFER_PIT);
    m->above_map = above.map;
    m->above_x = above.x;
    m->above_y = above.y;
    m->below_map = below.map;
    m->below_x = below.x;
    m->below_y = below.y;
}

/* The minion squares of the map item thing (MAP_MINION) and its marker (MAP_MARKER). */
static void find_minion_and_marker(uint16_t thing, uint16_t flags, MinionSquares *ms)
{
    *ms = (MinionSquares){.linked = {.map = -1}};
    uint16_t link =
        flags & MAP_MINION ? item_find_link_projectile(thing_word(thing, MINION_MAP_GROUP), thing) : THING_NONE;
    if (link != THING_NONE) {
        ms->minion = unpack_location(thing_word(link, PROJECTILE_LOCATION));
        dungeon_set_current_map(ms->minion.map);
        PassageEnd end;
        if (dungeon_linked_teleporter_dest(&end, (Location){ms->minion.x, ms->minion.y}))
            ms->linked = (Position){end.map, end.x, end.y};
    }
    if (flags & MAP_MARKER)
        set_marker(unpack_location(thing_word(thing, MINION_MAP_LOCATION)));
}

/* True when the square at is the square (x, y) of map. */
static bool same_square(Position at, int16_t map, int16_t x, int16_t y)
{
    return at.map == map && at.x == x && at.y == y;
}

/* The square of the grid at row and col from the middle of view, and its icon set; makes its map the current map. */
static GridSquare grid_square(const GridView *view, int row, int col)
{
    GridSquare g;
    Location at = dungeon_move_relative((Location){view->center.x, view->center.y}, view->dir, row, col);
    g.at = dungeon_location_on_map((Position){view->center.map, at.x, at.y}, 0);
    if (g.at.map < 0) {
        dungeon_set_current_map(view->center.map);
        g.icon_set = square_type(dungeon_square_at((Location){g.at.x, g.at.y})) == SQUARE_UNKNOWN
                         ? level_icon_set()
                         : dungeon_map_wallset(g_current_map.map);
        g.at.map = view->center.map;
        return g;
    }
    dungeon_set_current_map(g.at.map);
    g.icon_set = dungeon_map_wallset(g_current_map.map);
    return g;
}

/* The party, minion, and marker flags of the grid square g; show: the ones that the grid shows. */
static uint16_t grid_square_flags(const MinionSquares *ms, const GridSquare *g, uint16_t flags, uint16_t show)
{
    MinionMap *mm = &g_minion_map;
    Position at = g->at;
    flags &= ~(MAP_PARTY | MAP_MINION | MAP_MARKER);
    if (show & MAP_MINION && (same_square(at, ms->minion.map, ms->minion.x, ms->minion.y) ||
                              same_square(at, ms->linked.map, ms->linked.x, ms->linked.y)))
        flags |= MAP_MINION;
    if (show & MAP_MARKER && !mm->marker_state) {
        if (same_square(at, mm->marker_map, mm->marker_x, mm->marker_y)) {
            mm->marker_state = MARKER_SHOWN;
        } else if (same_square(at, mm->above_map, mm->above_x, mm->above_y)) {
            mm->marker_state = MARKER_ABOVE;
            set_marker(at);
        } else if (same_square(at, mm->below_map, mm->below_x, mm->below_y)) {
            mm->marker_state = MARKER_BELOW;
            set_marker(at);
        }
        if (mm->marker_state)
            flags |= MAP_MARKER;
    }
    if (show & MAP_PARTY && same_square(at, g_current_map.square_map, g_current_map.square_x, g_current_map.square_y))
        flags |= MAP_PARTY;
    return flags;
}

/* Draws the grid of squares around the middle of view; MAP_OPTION_HOLD draws the last grid again. */
static void show_grid(uint16_t thing, const GridView *view, uint16_t flags)
{
    MinionMap *mm = &g_minion_map;
    GridView v = *view;
    if (flags & MAP_OPTION_HOLD) {
        v = (GridView){
            {mm->map, mm->x, mm->y},
            mm->dir, mm->x_offset, mm->y_offset
        };
    } else {
        mm->x = (int8_t)v.center.x;
        mm->y = (int8_t)v.center.y;
        mm->map = (int8_t)v.center.map;
        mm->dir = (int8_t)v.dir;
        mm->x_offset = v.x_offset;
        mm->y_offset = v.y_offset;
    }
    mm->marker_state = MARKER_NONE;
    int16_t saved_map = g_current_map.map;
    Offscreen *area = area_begin_draw(false);
    offscreen_fill_rect(area, zone_get_rect(ZONE_MAP_GRID, mm->clip), g_ui_colors[UI_BLACK]);
    mm->clip[RECT_X] += GAP_X;
    mm->clip[RECT_Y] += GAP_Y;
    mm->clip[RECT_W] -= GAP_X * 2;
    mm->clip[RECT_H] -= GAP_Y * 2;
    int16_t left = (int16_t)(mm->clip[RECT_X] + v.x_offset);
    int16_t row_y = (int16_t)(mm->clip[RECT_Y] + v.y_offset);
    uint16_t show = flags & (MAP_PARTY | MAP_MINION | MAP_MARKER);
    MinionSquares ms;
    find_minion_and_marker(thing, flags, &ms);
    dungeon_set_current_map(v.center.map);
    int first_row = v.y_offset < 0 ? GRID_RADIUS + 1 : GRID_RADIUS;
    int first_col = v.x_offset < 0 ? -GRID_RADIUS - 1 : -GRID_RADIUS;
    for (int row = first_row; row >= -GRID_RADIUS; row--) {
        int16_t col_x = left;
        for (int col = first_col; col <= GRID_RADIUS; col++) {
            GridSquare g = grid_square(&v, row, col);
            flags = grid_square_flags(&ms, &g, flags, show);
            MapSquare sq = {.sx = g.at.x,
                            .sy = g.at.y,
                            .x = col_x,
                            .y = row_y,
                            .dir = v.dir,
                            .map_thing = thing,
                            .flags = flags,
                            .area = area};
            show_square(&sq, g.icon_set);
            col_x = (int16_t)(col_x + MAP_ICON_W + GAP_X);
        }
        row_y = (int16_t)(row_y + MAP_ICON_H + GAP_Y);
    }
    dungeon_set_current_map(saved_map);
}

void mmap_draw_option_buttons(int pressed)
{
    Offscreen *area = area_begin_draw(false);
    if (g_action.champion <= 0 || !(g_action.flags & MAP_HAS_OPTIONS))
        return;
    int down = (pressed ^ g_action.flags) & MAP_OPTIONS;
    for (int i = 0; i < MAP_OPTION_COUNT; i++, down >>= 1) {
        offscreen_draw_gdat_image(
            area, (GdatKey){CAT_CONTAINER, g_action.map_item, down & 1 ? GFX_OPTION_BUTTON_DOWN : GFX_OPTION_BUTTON_UP},
            ZONE_OPTION_BUTTON_FIRST + i, GFX_OPAQUE);
        char label[OPTION_LABEL_SIZE] = {(char)(OPTION_LABEL_FIRST + i), '\0'};
        offscreen_draw_text_in_zone(area, ZONE_OPTION_LABEL_FIRST + i,
                                    (TextColors){g_ui_colors[UI_BLACK], g_ui_colors[UI_GRAY] | TEXT_TRANSPARENT},
                                    label);
    }
}

/* The grid follows the minion of link: its square, shifted by where the minion stands in its square. */
static void follow_minion(uint16_t link, uint16_t group, GridView *view)
{
    view->center = unpack_location(thing_word(link, PROJECTILE_LOCATION));
    int16_t dir = g_game.party_dir;
    int16_t cell = aspect_creature_cell(group, dir_turn(creature_side_dir[group_dir(group)], dir));
    int col = cell % VIEW_CELL_ROW;
    int row = cell / VIEW_CELL_ROW;
    if (col > VIEW_CELL_ROW / 2) {
        view->center.x += g_dir_dx[dir_turn(dir, TURN_RIGHT)];
        view->center.y += g_dir_dy[dir_turn(dir, TURN_RIGHT)];
    }
    if (row > VIEW_CELL_ROW / 2) {
        view->center.x += g_dir_dx[dir_opposite(dir)];
        view->center.y += g_dir_dy[dir_opposite(dir)];
    }
    view->x_offset = (GAP_X * 2 + MAP_ICON_W + 1) / VIEW_CELL_ROW * cell_shift[col];
    view->y_offset = (GAP_Y * 2 + MAP_ICON_H + 1) / VIEW_CELL_ROW * cell_shift[row];
}

void mmap_draw(uint16_t thing)
{
    action_set_map_item(item_gdat_index(thing));
    action_set_map_flags(MAP_STATIC_CREATURES | MAP_PARTY, true);
    int kind = container_type(thing);
    uint16_t group = thing_word(thing, MINION_MAP_GROUP);
    uint16_t link = THING_NONE;
    if (kind != MAP_KIND_NO_OPTIONS) {
        action_set_map_flags(MAP_HAS_OPTIONS, true);
        if (kind != MAP_KIND_PLAIN) {
            link = item_find_link_projectile(group, thing);
            if (link != THING_NONE)
                action_set_map_flags(MAP_MINION, true);
            else if (g_action.flags & MAP_MINION)
                action_set_map_flags(MAP_MINION | MAP_PANEL_DRAWN, false);
        }
    }
    Offscreen *area = area_begin_draw(false);
    if (!(g_action.flags & MAP_PANEL_DRAWN)) {
        offscreen_draw_gdat_image(area, (GdatKey){CAT_CONTAINER, g_action.map_item, GFX_CONTAINER_PICTURE}, ZONE_PANEL,
                                  GFX_OPAQUE);
        for (int i = 0; i < g_champ_action_count; i++)
            area_draw_name(i, false);
        mmap_draw_option_buttons(0);
        action_set_map_flags(MAP_PANEL_DRAWN, true);
    }
    GridView view = {
        {g_game.party_map, g_game.party_x, g_game.party_y},
        g_game.party_dir, 0, 0
    };
    if (kind != MAP_KIND_PLAIN && kind != MAP_KIND_NO_OPTIONS) {
        if (location_map(thing_word(thing, MINION_MAP_LOCATION)) != MAP_LOCATION_NONE)
            action_set_map_flags(MAP_MARKER, true);
        if (kind == MAP_KIND_MINION) {
            if (link == THING_NONE)
                action_set_map_flags(MAP_MARKER, false);
            else
                follow_minion(link, group, &view);
        } else if (g_action.flags & MAP_MARKER) {
            view.center = unpack_location(thing_word(thing, MINION_MAP_LOCATION));
        }
    }
    show_grid(thing, &view, g_action.flags);
    if (g_action.flags & MAP_DIMMED || !(g_action.flags & MAP_POWERED)) {
        int16_t r[RECT_SIZE];
        offscreen_fill_rect_dithered(area, zone_get_rect(ZONE_MAP_GRID, r), UI_BLACK);
    }
}
