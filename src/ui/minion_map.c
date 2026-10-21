/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The minion map panel in the action area: the grid of square icons around the party, a minion, or a marker. */
#include "minion_map.h"
#include "gdat.h"
#include "ids.h"
#include "action.h"
#include "aspect.h"
#include "champion.h"
#include "creature.h"
#include "dungeon.h"
#include "item.h"
#include "minion.h"
#include "move.h"
#include "projectile.h"
#include "random.h"
#include "action_area.h"
#include "text.h"
#include "ui.h"
#include "view.h"
#include "zone.h"
#include <string.h>

#define MAP_PANEL_PICTURE   0x10
#define OPTION_BUTTON_UP    0x49
#define OPTION_BUTTON_DOWN  0x4a
#define OPTION_LABEL_FIRST  'r'
#define ICON_KEY            10
/* Icon strips: GDAT image (category, index, 1, 0xf9), MAP_ICON_W pixels per icon; the value (category, index, 0x0b,
   0xf9) set: the thing shows only with MAP_OPTION_DETAILS. */
#define STRIP_ENTRY         0xf9
#define STRIP_DETAIL_KEY    0xf9
#define MAP_ITEM_ICONS      0x40 /* the image of the map item with the party, minion, and marker icons */
#define GDAT_INDEX_COUNT    256  /* GDAT indices are bytes */
/* Offsets added to a door icon of the square strip. */
#define ICON_DOOR_NO_BUTTON 2
#define ICON_DOOR_OPEN      4
#define MIRROR_X            1
#define MIRROR_Y            2
#define FAKE_WALL_SEEN      SQUARE_FLAG_0
#define FLOOR_SHOWS_LEVEL   0x08              /* a floor square shows the level digit instead */
#define MAP_LOCATION_NONE   LOCATION_MAP_MASK /* map 63: no location */
#define MISSILE_SIDES_ICON  3
#define ALCOVE_ICON_COUNT   3 /* an item strip with alcove icons: the floor icon, then front and side */
#define VIEW_CELL_ROW       5

/* Mirror of an icon by the relative direction: icons with 2 sides (front, side), icons with 1 side. */
static const uint8_t mirror_two_sides[DIR_COUNT] = {0, 0, 3, 3};
static const uint8_t mirror_one_side[DIR_COUNT] = {0, 1, 3, 2};
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
static const uint8_t creature_side_dir[DIR_COUNT] = {0, 3, 2, 1};

typedef struct {
    const Bitmap *image; /* NULL: no strip */
    const uint8_t *palette;
    int count;
} Strip;

static Offscreen *area;

/* The icon strip of a GDAT key; count 0 when it has none. */
static Strip map_icon_strip(int category, int index)
{
    Strip s = {0};
    if (!gdat_exists(gdat_current(), category, index, GDAT_TYPE_IMAGE, STRIP_ENTRY))
        return s;
    s.image = draw_gdat_image(category, index, STRIP_ENTRY);
    s.palette = draw_gdat_image_palette(category, index, STRIP_ENTRY);
    s.count = s.image ? s.image->width / MAP_ICON_W : 0;
    return s;
}

/* The party, minion, and marker icons of the map item. */
static Strip map_item_icons(uint16_t thing)
{
    int category = item_gdat_category(thing), index = item_gdat_index(thing);
    return (Strip){draw_gdat_image(category, index, MAP_ITEM_ICONS),
                   draw_gdat_image_palette(category, index, MAP_ITEM_ICONS), 0};
}

/* True when the GDAT key has the detail value: it shows only with MAP_OPTION_DETAILS. */
static int detail_only(int category, int index)
{
    return gdat_entry_index(gdat_current(), category, index, GDAT_TYPE_VALUE, STRIP_DETAIL_KEY) != 0;
}

bool mmap_shows_floor_ornament(int ornament)
{
    return ornament != ORNAMENT_NONE && !detail_only(CAT_FLOOR_ORNAMENT, ornament);
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
    if (d > 0)
        rect[RECT_W] -= d;
    d = (int16_t)((rect[RECT_Y] + rect[RECT_H]) - (clip[RECT_Y] + clip[RECT_H]));
    if (d > 0)
        rect[RECT_H] -= d;
    return rect[RECT_W] > 0 && rect[RECT_H] > 0;
}

/* Draws icon of a strip at (x, y) of the screen inside the grid, mirrored by mirror (MIRROR_X, MIRROR_Y). */
static void draw_icon(const Bitmap *image, const uint8_t *palette, int icon, int x, int y, int mirror)
{
    if (!image)
        return;
    int16_t r[RECT_SIZE] = {(int16_t)x, (int16_t)y, MAP_ICON_W, MAP_ICON_H}, dx, dy;
    if (!rect_clip(r, minion_map.clip, &dx, &dy))
        return;
    if (MAP_ICON_W - r[RECT_W] && mirror & MIRROR_X)
        dx = dx ? 0 : (int16_t)(MAP_ICON_W - r[RECT_W]);
    if (MAP_ICON_H - r[RECT_H] && mirror & MIRROR_Y)
        dy = dy ? 0 : (int16_t)(MAP_ICON_H - r[RECT_H]);
    offscreen_blit(area, image, r, (int16_t)(icon * MAP_ICON_W + dx), dy, ICON_KEY, mirror, palette);
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
static int level_icon_set(void)
{
    return dungeon_level_digit(dungeon_map_level(party.cur_map));
}

/* The icons of a wall square: the wall and the ornaments of its 4 faces. */
static void draw_wall(const ViewSquare *s, const Strip *base, int x, int y, int flags, int fake_seen, int other_map)
{
    if (flags & MAP_OPTION_DETAILS && fake_seen && game.game_time & 2) {
        draw_strip(base, ICON_FAKE_WALL_BLINK, x, y, 0);
        return;
    }
    if (!other_map)
        draw_strip(base, ICON_WALL, x, y, 0);
    for (int side = 0; side < VIEW_SIDE_COUNT; side++) {
        int ornament = s->side[side] & ORNAMENT_INDEX_MASK;
        if (ornament == ORNAMENT_NONE)
            continue;
        if (!(flags & MAP_OPTION_DETAILS) && detail_only(CAT_WALL_ORNAMENT, ornament))
            continue;
        Strip o = map_icon_strip(CAT_WALL_ORNAMENT, ornament);
        if (!o.count)
            continue;
        int frame = aspect_face_frame(s->side[side]);
        int icon = o.count < frame * 2 + 2 ? side & 1 : frame * 2 + (side & 1);
        draw_strip(&o, icon, x, y, mirror_two_sides[side]);
    }
}

/* The icons of a floor or teleporter square: the floor, its ornament, the teleporter field. */
static void draw_floor(const ViewSquare *s, const Strip *base, int x, int y, int flags)
{
    draw_strip(base, ICON_FLOOR, x, y, 0);
    int ornament = s->side[SIDE_FLOOR_ORNAMENT] & ORNAMENT_INDEX_MASK;
    if (flags & MAP_OPTION_DETAILS ? ornament != ORNAMENT_NONE : mmap_shows_floor_ornament(ornament)) {
        Strip o = map_icon_strip(CAT_FLOOR_ORNAMENT, ornament);
        if (o.count) {
            int frame = aspect_floor_frame(s->side[SIDE_FLOOR_ORNAMENT]);
            draw_strip(&o, o.count < frame + 1 ? 0 : frame, x, y, 0);
        }
    }
    if (s->aspect != ASPECT_TELEPORTER)
        return;
    Strip t = map_icon_strip(CAT_TELEPORTER, 0);
    if (t.count)
        draw_strip(&t, (int)(game.game_time % (uint32_t)t.count), x, y, 0);
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
            if (!(cflags & CREATURE_STATIC) && c.count / 2)
                frame = (int)(game.game_time % (uint32_t)(c.count / 2)) * 2;
            int side = dir_relative(dir, group_dir(group));
            draw_strip(&c, frame + (side & 1), x, y, mirror_two_sides[side]);
        } else if (!(cflags & CREATURE_STATIC) && !(cflags & POSSESSIONS_FLOOR_CLICKABLE)) {
            Strip m = map_item_icons(map_thing);
            draw_strip(&m, game.game_time & 1, x, y, 0);
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
            draw_strip(&o, 0, x, y, mirror_one_side[dir_relative(thing_cell(t), dir)]);
    }
}

/* How the pictures of a flying object show its sides: from the images that exist in its GDAT entry. */
static int missile_image_kind(uint16_t thing)
{
    int category = item_gdat_category(thing), index = item_gdat_index(thing);
    const gdat_t *g = gdat_current();
    if (!gdat_exists(g, category, index, GDAT_TYPE_IMAGE, MISSILE_PICTURE_FRONT))
        return MISSILE_NO_PICTURE;
    if (!gdat_exists(g, category, index, GDAT_TYPE_IMAGE, MISSILE_PICTURE_SIDE))
        return MISSILE_NO_SIDE;
    if (gdat_exists(g, category, index, GDAT_TYPE_IMAGE, MISSILE_PICTURE_BACK))
        return MISSILE_FRONT_BACK;
    if (gdat_exists(g, category, index, GDAT_TYPE_IMAGE, MISSILE_PICTURE_FRONT_2))
        return MISSILE_MIRRORED;
    return MISSILE_FRONT;
}

/* The icons of the missiles of a thing list from first. */
static void draw_missiles(uint16_t first, int dir, int x, int y)
{
    for (uint16_t t = first; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_PROJECTILE)
            continue;
        Strip m = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
        if (!m.count)
            continue;
        int icon = 0, cell = dir_relative(thing_cell(t), dir);
        if (m.count > MISSILE_SIDES_ICON) {
            int side = dir_relative(projectile_dir(t), dir);
            icon = MISSILE_SIDES_ICON;
            switch (missile_image_kind(t)) {
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
            if (gdat_entry_index(gdat_current(), CAT_CREATURE, item_gdat_index(t), GDAT_TYPE_VALUE, STRIP_DETAIL_KEY) ||
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
                if (s->aspect == ASPECT_WALL && !fake_seen) {
                    /* an item in an alcove */
                    if (o.count < ALCOVE_ICON_COUNT)
                        continue;
                    draw_strip(&o, (cell & 1) + 1, x, y, mirror_two_sides[cell]);
                } else {
                    draw_strip(&o, 0, x, y, mirror_one_side[cell]);
                }
            }
        }
    }
    if (group != THING_ENDOFLIST)
        draw_group(group, map_thing, dir, x, y, flags);
    draw_missiles(missiles, dir, x, y);
    for (uint16_t t = explosions; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (thing_type(t) != THING_EXPLOSION)
            continue;
        Strip e = map_icon_strip(item_gdat_category(t), item_gdat_index(t));
        if (e.count)
            draw_strip(&e, (game.game_time & 1) + 1, x, y, rand_2bits());
    }
}

/* The red frame around the party square at (x, y) of the screen. */
static void draw_party_frame(int x, int y)
{
    int16_t r[RECT_SIZE] = {(int16_t)(x - GAP_X), (int16_t)(y - GAP_Y), GAP_X * 2 + MAP_ICON_W, GAP_Y * 2 + MAP_ICON_H},
            dx, dy;
    rect_clip(r, minion_map.clip, &dx, &dy);
    offscreen_fill_rect(area, r, ui_colors[UI_RED]);
}

/* The icon of each living champion at its cell of the party square at (x, y), for a map that looks to dir. */
static void draw_party_icons(const Strip *p, int x, int y, int dir)
{
    for (int i = 0; i < game.champion_count; i++) {
        const Champion *c = champion(i);
        if (!c->health)
            continue;
        int icon = game.magic_light[MAGIC_INVISIBILITY] ? ICON_PARTY_INVISIBLE : i + ICON_PARTY_FIRST;
        draw_strip(p, icon, x, y, mirror_one_side[dir_relative(c->cell, dir)]);
    }
}

/* The square icon of the view square s at (x, y) of the screen; returns false when the square shows the level digit
   and nothing else. */
static bool draw_square_base(const ViewSquare *s, const Strip *base, int sx, int sy, int dir, int x, int y, int flags,
                             int fake_seen, int other_map)
{
    switch (s->aspect) {
    case ASPECT_WALL:
    case ASPECT_HIDDEN:
        draw_wall(s, base, x, y, flags, fake_seen, other_map);
        break;
    case ASPECT_FLOOR:
        if (s->square & FLOOR_SHOWS_LEVEL) {
            Strip digits = map_icon_strip(CAT_WALLSET, level_icon_set());
            draw_strip(&digits, 0, x, y, 0);
            return false;
        }
        draw_floor(s, base, x, y, flags);
        break;
    case ASPECT_TELEPORTER:
        draw_floor(s, base, x, y, flags);
        break;
    case ASPECT_DOOR_SIDE:
    case ASPECT_DOOR_FRONT: {
        int icon = s->aspect == ASPECT_DOOR_SIDE ? ICON_DOOR_SIDE : ICON_DOOR_FRONT;
        if (!door_has_button(s->side[SIDE_DOOR_THING]))
            icon += ICON_DOOR_NO_BUTTON;
        if (s->side[SIDE_DOOR_STATE] == DOOR_STATE_OPEN || s->side[SIDE_DOOR_STATE] == DOOR_STATE_BROKEN)
            icon += ICON_DOOR_OPEN;
        draw_strip(base, icon, x, y, 0);
        break;
    }
    case ASPECT_STAIRS_FRONT:
    case ASPECT_STAIRS_SIDE: {
        int d = dir_relative(dun_get_stairs_exit_direction(sx, sy), dir);
        if (s->side[SIDE_STAIRS_UP])
            d = dir_opposite(d);
        draw_strip(base, d + ICON_STAIRS_FIRST, x, y, 0);
        break;
    }
    case ASPECT_PIT: {
        int icon = ICON_PIT;
        if (s->side[SIDE_PIT_UP])
            icon = flags & MAP_OPTION_DETAILS && game.game_time & 1 ? ICON_PIT_BLINK : ICON_PIT_VARIANT;
        draw_strip(base, icon, x, y, 0);
        break;
    }
    }
    return true;
}

/* The party icons and the marker icon of a square at (x, y) of the screen, by flags. */
static void draw_party_and_marker(uint16_t map_thing, int x, int y, int dir, int flags)
{
    if (flags & MAP_PARTY) {
        Strip p = map_item_icons(map_thing);
        draw_party_icons(&p, x, y, dir);
    }
    if (flags & MAP_MARKER) {
        int icon = minion_map.marker_state == MARKER_ABOVE   ? ICON_MARKER_ABOVE
                   : minion_map.marker_state == MARKER_BELOW ? ICON_MARKER_BELOW
                                                             : ICON_MARKER;
        Strip m = map_item_icons(map_thing);
        draw_strip(&m, icon, x, y, 0);
    }
}

/* An open pit in the level above the square (sx, sy) of the current map shows as a hole in the ceiling. */
static void draw_pit_above(const Strip *base, int sx, int sy, int x, int y)
{
    int ax = sx, ay = sy;
    int m = dungeon_location_on_map(party.cur_map, LEVEL_UP, &ax, &ay);
    if (m < 0 || !inside_map(m, ax, ay))
        return; /* fix: as above, no read outside the map */
    uint8_t square = dungeon_square(m, ax, ay);
    if (square_type(square) == SQUARE_PIT && square & SQUARE_PIT_OPEN)
        draw_strip(base, ICON_PIT_ABOVE, x, y, 0);
}

/* The square icon (and the things) of the square (sx, sy) of the current map at (x, y) of the screen. */
static void ui_map_draw_square(uint16_t map_thing, int sx, int sy, int dir, int x, int y, int icon_set, int flags)
{
    ViewSquare s;
    aspect_get(&s, party.cur_map, dir, sx, sy);
    Strip base = map_icon_strip(CAT_WALLSET, icon_set);
    if (flags & MAP_PARTY)
        draw_party_frame(x, y);
    int fake_seen = square_type(s.square) == SQUARE_FAKE_WALL && s.square & FAKE_WALL_SEEN;
    int other_map = 0, saved_map = 0, saved_x = 0, saved_y = 0;
    for (;;) {
        if (!draw_square_base(&s, &base, sx, sy, dir, x, y, flags, fake_seen, other_map))
            return;
        draw_party_and_marker(map_thing, x, y, dir, flags);
        if (!(flags & (MAP_STATIC_CREATURES | MAP_MINION | MAP_OPTION_ITEMS | MAP_OPTION_CREATURES)))
            return;
        draw_things(&s, map_thing, dir, x, y, flags, fake_seen);
        if (s.aspect != ASPECT_WALL || other_map)
            break;
        saved_x = sx;
        saved_y = sy;
        /* a wall square: another map of the level may show a wall there; like the original, the search leaves
           (sx, sy) on the map that it found */
        int next = 0, m = party.cur_map;
        while (m == party.cur_map)
            m = dungeon_location_on_map_from(party.cur_map, 0, &sx, &sy, &next);
        /* fix: the original reads the square byte also outside the map (out of its column table) */
        if (m < 0 || !inside_map(m, sx, sy) || square_type(dungeon_square(m, sx, sy)) != SQUARE_WALL)
            break;
        other_map = 1;
        saved_map = party.cur_map;
        dun_set_current_map(m);
        aspect_get(&s, party.cur_map, dir, sx, sy);
    }
    if (other_map) {
        sx = saved_x;
        sy = saved_y;
        dun_set_current_map(saved_map);
        aspect_get(&s, party.cur_map, dir, sx, sy);
    }
    draw_pit_above(&base, sx, sy, x, y);
}

void mmap_draw_square_icon(Offscreen *target, const int16_t *clip, int sx, int sy, int x, int y, int party_item)
{
    int16_t saved[RECT_SIZE];
    memcpy(saved, minion_map.clip, sizeof saved);
    memcpy(minion_map.clip, clip, sizeof saved);
    area = target;
    if (party_item >= 0)
        draw_party_frame(x, y);
    ui_map_draw_square(THING_NONE, sx, sy, DIR_NORTH, x, y, dungeon_map_wallset(party.cur_map), 0);
    if (party_item >= 0) {
        Strip p = {draw_gdat_image(CAT_CONTAINER, party_item, MAP_ITEM_ICONS),
                   draw_gdat_image_palette(CAT_CONTAINER, party_item, MAP_ITEM_ICONS), 0};
        draw_party_icons(&p, x, y, DIR_NORTH);
    }
    memcpy(minion_map.clip, saved, sizeof saved);
}

int mmap_party_icons_item(void)
{
    for (int index = 0; index < GDAT_INDEX_COUNT; index++)
        if (gdat_exists(gdat_current(), CAT_CONTAINER, index, GDAT_TYPE_IMAGE, MAP_ITEM_ICONS))
            return index;
    return -1;
}

/* Sets the marker square (x, y) of map and the squares that it leads to one level up and down. */
static void ui_map_set_marker(int x, int y, int map)
{
    MinionMap *m = &minion_map;
    m->marker_x = m->below_x = m->above_x = (int16_t)x;
    m->marker_y = m->below_y = m->above_y = (int16_t)y;
    m->marker_map = (int16_t)map;
    m->above_map = (int16_t)dun_get_level_transfer_destination(&m->above_x, &m->above_y, map, LEVEL_UP,
                                                               TRANSFER_STAIRS | TRANSFER_FALL_INTO);
    m->below_map = (int16_t)dun_get_level_transfer_destination(&m->below_x, &m->below_y, m->marker_map, LEVEL_DOWN,
                                                               TRANSFER_STAIRS | TRANSFER_PIT);
}

/* The squares that show the minion of a map item: its square and the far end of a teleporter there. */
typedef struct {
    int x, y, map, linked_x, linked_y, linked_map;
} MinionSquares;

/* The minion squares of the map item thing (MAP_MINION) and its marker (MAP_MARKER). */
static void grid_find_minion_and_marker(uint16_t thing, int flags, MinionSquares *ms)
{
    *ms = (MinionSquares){.linked_map = -1};
    if (flags & MAP_MINION) {
        uint16_t w =
            thing_word(minion_find_link_projectile(thing_word(thing, MINION_MAP_GROUP), thing), PROJECTILE_LOCATION);
        ms->x = location_x(w);
        ms->y = location_y(w);
        ms->map = location_map(w);
        dun_set_current_map(ms->map);
        PassageEnd end;
        if (dun_get_linked_teleporter_dest(&end, ms->x, ms->y)) {
            ms->linked_x = end.x;
            ms->linked_y = end.y;
            ms->linked_map = end.map;
        }
    }
    if (flags & MAP_MARKER) {
        uint16_t w = thing_word(thing, MINION_MAP_LOCATION);
        ui_map_set_marker(location_x(w), location_y(w), location_map(w));
    }
}

/* The square (*sx, *sy) of the grid at row and col from (x, y) of map looking to dir, and its icon set; returns its
   map and makes it the current map. */
static int grid_square(int x, int y, int map, int dir, int row, int col, int16_t *sx, int16_t *sy, int *icon_set)
{
    *sx = (int16_t)x;
    *sy = (int16_t)y;
    dun_move_relative(dir, row, col, sx, sy);
    int lx = *sx, ly = *sy;
    int square_map = dungeon_location_on_map(map, 0, &lx, &ly);
    *sx = (int16_t)lx;
    *sy = (int16_t)ly;
    if (square_map < 0) {
        dun_set_current_map(map);
        *icon_set = square_type(dun_get_square(*sx, *sy)) == SQUARE_UNKNOWN ? level_icon_set()
                                                                            : dungeon_map_wallset(party.cur_map);
        return map;
    }
    dun_set_current_map(square_map);
    *icon_set = dungeon_map_wallset(party.cur_map);
    return square_map;
}

/* The party, minion, and marker flags of the grid square (sx, sy) of map m16; show: the ones that the grid shows. */
static int grid_square_flags(const MinionSquares *ms, int16_t sx, int16_t sy, int16_t m16, int flags, int show)
{
    MinionMap *mm = &minion_map;
    flags &= ~(MAP_PARTY | MAP_MINION | MAP_MARKER);
    if (show & MAP_MINION && ((sx == ms->x && sy == ms->y && m16 == ms->map) ||
                              (m16 == ms->linked_map && sx == ms->linked_x && sy == ms->linked_y)))
        flags |= MAP_MINION;
    if (show & MAP_MARKER && !mm->marker_state) {
        if (sx == mm->marker_x && sy == mm->marker_y && m16 == mm->marker_map) {
            mm->marker_state = MARKER_SHOWN;
        } else if (sx == mm->above_x && sy == mm->above_y && m16 == mm->above_map) {
            mm->marker_state = MARKER_ABOVE;
            ui_map_set_marker(sx, sy, m16);
        } else if (sx == mm->below_x && sy == mm->below_y && m16 == mm->below_map) {
            mm->marker_state = MARKER_BELOW;
            ui_map_set_marker(sx, sy, m16);
        }
        if (mm->marker_state)
            flags |= MAP_MARKER;
    }
    if (show & MAP_PARTY && m16 == party.square_map && sx == party.square_x && sy == party.square_y)
        flags |= MAP_PARTY;
    return flags;
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
    mm->marker_state = MARKER_NONE;
    int saved_map = party.cur_map;
    area = area_begin_draw(0);
    offscreen_fill_rect(area, zone_get_rect(ZONE_MAP_GRID, mm->clip), ui_colors[UI_BLACK]);
    mm->clip[RECT_X] += GAP_X;
    mm->clip[RECT_Y] += GAP_Y;
    mm->clip[RECT_W] -= GAP_X * 2;
    mm->clip[RECT_H] -= GAP_Y * 2;
    int left = mm->clip[RECT_X] + (int16_t)x_offset;
    int16_t row_y = (int16_t)(mm->clip[RECT_Y] + y_offset);
    int show = flags & (MAP_PARTY | MAP_MINION | MAP_MARKER);
    MinionSquares ms;
    grid_find_minion_and_marker(thing, flags, &ms);
    dun_set_current_map(map);
    int first_row = y_offset < 0 ? GRID_RADIUS + 1 : GRID_RADIUS;
    int first_col = x_offset < 0 ? -GRID_RADIUS - 1 : -GRID_RADIUS;
    for (int row = first_row; row >= -GRID_RADIUS; row--) {
        int16_t col_x = (int16_t)left;
        for (int col = first_col; col <= GRID_RADIUS; col++) {
            int16_t sx, sy;
            int icon_set;
            int16_t m16 = (int16_t)grid_square(x, y, map, dir, row, col, &sx, &sy, &icon_set);
            flags = grid_square_flags(&ms, sx, sy, m16, flags, show);
            ui_map_draw_square(thing, sx, sy, dir, col_x, row_y, icon_set, flags);
            col_x = (int16_t)(col_x + MAP_ICON_W + GAP_X);
        }
        row_y = (int16_t)(row_y + MAP_ICON_H + GAP_Y);
    }
    dun_set_current_map(saved_map);
}

void mmap_draw_option_buttons(int pressed)
{
    area = area_begin_draw(0);
    if (action.champion <= 0 || !(action.flags & MAP_HAS_OPTIONS))
        return;
    int down = (pressed ^ action.flags) & MAP_OPTIONS;
    for (int i = 0; i < MAP_OPTION_COUNT; i++, down >>= 1) {
        offscreen_draw_gdat_image(area, CAT_CONTAINER, action.map_item,
                                  down & 1 ? OPTION_BUTTON_DOWN : OPTION_BUTTON_UP, ZONE_OPTION_BUTTON_FIRST + i,
                                  UI_OPAQUE);
        char label[2] = {(char)(OPTION_LABEL_FIRST + i), '\0'};
        offscreen_draw_text_in_zone(area, ZONE_OPTION_LABEL_FIRST + i, ui_colors[UI_BLACK],
                                    ui_colors[UI_GRAY] | TEXT_TRANSPARENT, label);
    }
}

void mmap_draw(uint16_t thing)
{
    action.map_item = item_gdat_index(thing);
    action.flags |= MAP_STATIC_CREATURES | MAP_PARTY;
    int kind = container_type(thing);
    uint16_t group = thing_word(thing, MINION_MAP_GROUP), link = 0;
    if (kind != MAP_KIND_NO_OPTIONS) {
        action.flags |= MAP_HAS_OPTIONS;
        if (kind != MAP_KIND_PLAIN) {
            link = minion_find_link_projectile(group, thing);
            if (link)
                action.flags |= MAP_MINION;
            else if (action.flags & MAP_MINION)
                action.flags &= ~(MAP_MINION | MAP_PANEL_DRAWN);
        }
    }
    area = area_begin_draw(0);
    if (!(action.flags & MAP_PANEL_DRAWN)) {
        offscreen_draw_gdat_image(area, CAT_CONTAINER, action.map_item, MAP_PANEL_PICTURE, ZONE_PANEL, UI_OPAQUE);
        for (int i = 0; i < champ_action_count; i++)
            area_draw_name(i, 0);
        mmap_draw_option_buttons(0);
        action.flags |= MAP_PANEL_DRAWN;
    }
    int x = game.party_x, y = game.party_y, map = game.party_map, x_offset = 0, y_offset = 0;
    if (kind != MAP_KIND_PLAIN && kind != MAP_KIND_NO_OPTIONS) {
        if (location_map(thing_word(thing, MINION_MAP_LOCATION)) != MAP_LOCATION_NONE)
            action.flags |= MAP_MARKER;
        if (kind == MAP_KIND_MINION) {
            if (!link) {
                action.flags &= ~MAP_MARKER;
            } else {
                /* the grid follows the minion; it shifts by where the minion stands in its square */
                uint16_t w = thing_word(link, PROJECTILE_LOCATION);
                x = location_x(w);
                y = location_y(w);
                map = location_map(w);
                int dir = game.party_dir;
                int cell = aspect_creature_cell(group, dir_turn(creature_side_dir[group_dir(group)], dir));
                int col = (int16_t)cell % VIEW_CELL_ROW, row = (int16_t)cell / VIEW_CELL_ROW;
                if (col > VIEW_CELL_ROW / 2) {
                    x += dir_dx[dir_turn(dir, 1)];
                    y += dir_dy[dir_turn(dir, 1)];
                }
                if (row > VIEW_CELL_ROW / 2) {
                    x += dir_dx[dir_opposite(dir)];
                    y += dir_dy[dir_opposite(dir)];
                }
                x_offset = (GAP_X * 2 + MAP_ICON_W + 1) / VIEW_CELL_ROW * cell_shift[col];
                y_offset = (GAP_Y * 2 + MAP_ICON_H + 1) / VIEW_CELL_ROW * cell_shift[row];
            }
        } else if (action.flags & MAP_MARKER) {
            uint16_t w = thing_word(thing, MINION_MAP_LOCATION);
            x = location_x(w);
            y = location_y(w);
            map = location_map(w);
        }
    }
    ui_map_draw_grid(thing, (int16_t)x, (int16_t)y, map, game.party_dir, (int16_t)x_offset, (int16_t)y_offset,
                     action.flags);
    if (action.flags & MAP_DIMMED || !(action.flags & MAP_POWERED)) {
        int16_t r[RECT_SIZE];
        offscreen_fill_rect_dithered(area, zone_get_rect(ZONE_MAP_GRID, r), UI_BLACK);
    }
}
