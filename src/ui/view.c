/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "num.h"
#include "projectile.h"
#include "random.h"
#include "sound_request.h"
#include "view_internal.h"
#include "weather.h"

/* The scale (1/64) of the pictures at each depth. */
const uint8_t g_view_depth_scale[POS_DEPTH_MAX + 1] = {96, 64, 43, 28, 19};
/* The wall faces that a wall square shows. */
const uint8_t g_view_wall_faces[VIEW_SQUARE_COUNT] = {
    0,
    WALL_SHOWS_SIDE,
    WALL_SHOWS_SIDE,
    WALL_SHOWS_FRONT,
    WALL_SHOWS_BOTH,
    WALL_SHOWS_BOTH,
    WALL_SHOWS_FRONT,
    WALL_SHOWS_BOTH,
    WALL_SHOWS_BOTH,
    WALL_SHOWS_SIDE,
    WALL_SHOWS_SIDE,
    WALL_SHOWS_FRONT,
    WALL_SHOWS_BOTH,
    WALL_SHOWS_BOTH,
    WALL_SHOWS_BOTH,
    WALL_SHOWS_BOTH,
    WALL_SHOWS_FRONT,
    WALL_SHOWS_FRONT,
    WALL_SHOWS_FRONT,
    WALL_SHOWS_FRONT,
    WALL_SHOWS_FRONT,
    0,
    0,
};
const uint8_t g_view_door_front_shown[POS_FAR_ROW] = {1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1};
/* The neighbour squares of each position: left, right, ahead, behind (LINK_NONE: none). A seamless passage shows them
   again, and the possessions of a creature can lie on them. */
const uint8_t g_view_linked_squares[VIEW_SQUARE_COUNT][NEIGHBOUR_COUNT] = {
    {1,         2,         3,         LINK_NONE},
    {LINK_NONE, 0,         4,         LINK_NONE},
    {0,         LINK_NONE, 5,         LINK_NONE},
    {4,         5,         6,         0        },
    {LINK_NONE, 3,         7,         1        },
    {3,         LINK_NONE, 8,         2        },
    {7,         8,         11,        3        },
    {9,         6,         12,        4        },
    {6,         10,        13,        5        },
    {LINK_NONE, 7,         14,        LINK_NONE},
    {8,         LINK_NONE, 15,        LINK_NONE},
    {12,        13,        16,        6        },
    {14,        11,        17,        7        },
    {11,        15,        18,        8        },
    {LINK_NONE, 12,        19,        9        },
    {13,        LINK_NONE, 20,        10       },
    {17,        18,        LINK_NONE, 11       },
    {19,        16,        LINK_NONE, 12       },
    {16,        20,        LINK_NONE, 13       },
    {21,        17,        LINK_NONE, 14       },
    {18,        22,        LINK_NONE, 15       },
    {LINK_NONE, 19,        LINK_NONE, LINK_NONE},
    {20,        LINK_NONE, LINK_NONE, LINK_NONE},
};
static const int8_t view_offset[VIEW_SQUARE_COUNT][2] = {
    {0,  0},
    {-1, 0},
    {1,  0},
    {0,  1},
    {-1, 1},
    {1,  1},
    {0,  2},
    {-1, 2},
    {1,  2},
    {-2, 2},
    {2,  2},
    {0,  3},
    {-1, 3},
    {1,  3},
    {-2, 3},
    {2,  3},
    {0,  4},
    {-1, 4},
    {1,  4},
    {-2, 4},
    {2,  4},
    {-3, 4},
    {3,  4},
};
/* The light (1/64 darker) of each depth, and while walking toward or away. */
static const int8_t depth_light[POS_DEPTH_MAX + 1] = {0, 0, 12, 28, 46};
static const int8_t walk_light[POS_DEPTH_MAX + 1] = {0, 0, 5, 19, 36};
static const int8_t walk_back_light[POS_DEPTH_MAX + 1] = {-1, 0, -7, -9, -10};
/* The far squares in drawing order. */
static const uint8_t far_order[POS_DRAWN] = {19, 20, 17, 18, 16, 14, 15, 12, 13, 11, 9, 10, 7, 8, 6, 4, 5, 3, 1, 2};
/* Hidden square masks by the wall pattern of the rows (bit n: view position n). */
static const uint32_t hide_row1[4] = {0, 0x200, 0x400, 0x600};
static const uint32_t hide_row2[ROW_PATTERNS] = {0,        0x10840,  0x204200, 0x2b5ac0,
                                                 0x408400, 0x55ad40, 0x60c600, 0x7fffc0};
static const uint32_t hide_row3[ROW_PATTERNS] = {0,        0x10800,  0x204000, 0x2b5800,
                                                 0x408000, 0x55a800, 0x60c000, 0x7ff800};
static const uint32_t hide_row4[ROW_PATTERNS] = {0, 0x10000, 0x20000, 0xb0000, 0x40000, 0x50000, 0x60000, 0x70000};
/* Zones of the side wall ornaments by position. */
static const int16_t side_ornament_zone[POS_FAR_ROW] = {
    -1, 4425, 4450, -1, 4500, 4525, -1, 4575, 4600, 4625, 4650, -1, 4700, 4725, 4750, 4775,
};
/* Positions whose things the drawing shows; 9 and 10 show none, the explosions show to depth 4. */
const uint8_t g_view_square_things_shown[POS_FAR_ROW] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1};
ViewState g_view_state = {.landmark_set = -1, .pending.first = THING_ENDOFLIST, .flight.map = -1};

ViewSquare g_view_squares[VIEW_SQUARE_COUNT];
Bitmap g_view_viewport = {g_view_state.viewport_pixels, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, IMAGE_8BPP};
ViewClickZone g_view_click_zones[VIEW_CLICK_ZONE_MAX];
int g_view_click_zone_count;
uint8_t g_view_creature_grid[VIEW_GRID_SIZE];
bool g_view_arrows_enabled;
uint8_t g_view_object_grid[VIEW_GRID_SIZE];
uint32_t g_view_missile_masks[VIEW_SQUARE_COUNT];
uint32_t g_view_object_masks[VIEW_SQUARE_COUNT];
int16_t g_view_hit_offset_y;

int view_lateral_of(int pos)
{
    return view_offset[pos][0];
}

int view_depth_of(int pos)
{
    return view_offset[pos][1];
}

int view_face_frame_entry(uint16_t w)
{
    return (int8_t)VIEW_HIGH_BYTE(w);
}

bool view_picture_exists(int category, int index, int entry)
{
    return gdat_exists(gdat_current(), category, index, GDAT_TYPE_IMAGE, entry);
}

uint8_t view_map_door_type(uint16_t door)
{
    return g_view_state.door_types[door_map_type(door)];
}

bool view_between_steps(void)
{
    return g_view_state.flight.step_ticks != 0;
}

Location view_step_relative(Location at, int16_t dir, int forward, int right)
{
    return (Location){
        (int16_t)(at.x + g_dir_dx[dir & DIR_MASK] * forward + g_dir_dx[dir_turn(dir, TURN_RIGHT)] * right),
        (int16_t)(at.y + g_dir_dy[dir & DIR_MASK] * forward + g_dir_dy[dir_turn(dir, TURN_RIGHT)] * right)};
}

RelativeOffset view_relative_offset(int16_t dir, int dx, int dy)
{
    switch (dir) {
    case DIR_NORTH:
        return (RelativeOffset){(int16_t)dx, (int16_t)-dy};
    case DIR_EAST:
        return (RelativeOffset){(int16_t)dy, (int16_t)dx};
    case DIR_SOUTH:
        return (RelativeOffset){(int16_t)-dx, (int16_t)dy};
    default:
        return (RelativeOffset){(int16_t)-dy, (int16_t)-dx};
    }
}

bool view_is_item(uint16_t thing)
{
    int type = thing_type(thing);
    return type >= THING_WEAPON && type <= THING_MISC;
}

int view_get_thing_position(uint16_t thing, int16_t dir)
{
    int cell = CELL_CENTER;
    switch (thing_type(thing)) {
    case THING_GROUP:
        return aspect_creature_cell(thing, dir_relative(dir, group_dir(thing)));
    case THING_EXPLOSION:
        if (explosion_is_centered(thing))
            break;
        __attribute__((fallthrough));
    case THING_WEAPON:
    case THING_ARMOUR:
    case THING_SCROLL:
    case THING_POTION:
    case THING_CONTAINER:
    case THING_MISC:
    case THING_PROJECTILE:
        cell = g_view_cell_view_position[thing_cell(thing)];
        break;
    }
    return aspect_rotate_cell(cell, dir);
}

int view_position_to_cell(int position)
{
    for (int cell = 0; cell < DIR_COUNT; cell++)
        if (g_view_cell_view_position[cell] == position)
            return cell;
    return position == CELL_CENTER ? DIR_COUNT : -1;
}

bool view_get_flip_parity(int kind, int16_t dir, Location at)
{
    bool parity = (at.y + at.x + dir + dungeon_map_offset_x(g_view_state.map) + dungeon_map_offset_y(g_view_state.map) +
                   dungeon_map_level(g_view_state.map)) &
                  1;
    bool party_parity = g_game.party_dir & 1;
    int time = g_game.game_time & GAME_TIME_FRAMES;
    uint16_t flags = g_view_state.flip_flags;
    if (kind == PARITY_FLOOR) {
        if (!(flags & FLIP_FLOOR_BY_SQUARE))
            return flags & FLIP_FLOOR_BY_PARTY ? party_parity : false;
        if (flags & FLIP_FLOOR_BY_TIME)
            return time >= HALF_TIME_FRAMES;
        return parity;
    }
    if (kind == PARITY_CEILING) {
        if (flags & FLIP_CEILING_BY_SQUARE)
            return flags & FLIP_CEILING_BY_TIME ? time < HALF_TIME_FRAMES : !parity;
        return flags & FLIP_CEILING_BY_PARTY ? party_parity : false;
    }
    return parity;
}

/* ---- Map graphics ---- */

void view_enter_map(int16_t map)
{
    const Gdat *g = gdat_current();
    sndreq_clear_requests();
    g_view_state.party_map = map;
    g_view_state.map = g_view_state.party_map;
    g_view_state.gfx_set = dungeon_map_wallset(map);
    g_view_state.door_types[0] = dungeon_map_door_type(map, 0);
    g_view_state.door_types[1] = dungeon_map_door_type(map, 1);
    g_view_state.wall_key = (int16_t)aspect_value_key(CAT_WALLSET, g_view_state.gfx_set, MAP_KEY_WALL_COLOR);
    g_view_state.flip_flags = aspect_value_key(CAT_WALLSET, g_view_state.gfx_set, MAP_KEY_FLIP_FLAGS);
    aspect_enter_map(map);
    g_view_state.rain_shown = weather_is_shown(aspect_value_key(CAT_WALLSET, g_view_state.gfx_set, MAP_KEY_SKY_STATE));
    /* floor and ceiling pictures that do not fill the viewport leave a band that is cleared */
    const Bitmap *floor = draw_gdat_image(CAT_WALLSET, g_view_state.gfx_set, GFX_MAP_FLOOR);
    const Bitmap *ceiling = draw_gdat_image(CAT_WALLSET, g_view_state.gfx_set, GFX_MAP_CEILING);
    g_view_state.clear = false;
    if (floor && ceiling) {
        uint16_t area = (uint16_t)(floor->width * floor->height);
        uint16_t rest = (uint16_t)(VIEWPORT_WIDTH * VIEWPORT_HEIGHT - ceiling->width * ceiling->height);
        if (area < rest) {
            g_view_state.clear = true;
            int16_t r[RECT_SIZE] = {0, ceiling->height, VIEWPORT_WIDTH,
                                    (int16_t)(VIEWPORT_HEIGHT - (ceiling->height + floor->height))};
            memcpy(g_view_state.clear_rect, r, sizeof r);
        }
    }
    uint32_t size;
    const uint8_t *colors = gdat_item(g, gdat_find(g, CAT_INTERFACE, 0, GDAT_TYPE_DATA, GFX_UI_COLORS), &size);
    g_view_state.clear_color = colors && size ? colors[0] : 0;
}

/* ---- Things of the view squares ---- */

bool view_has_creature_picture(int type, int entry)
{
    return view_picture_exists(CAT_CREATURE, type, (uint8_t)entry);
}

GridPoint view_offset_toward_party(GridPoint eye, GridPoint creature, int16_t creature_size)
{
    if (!creature_size)
        return creature;
    if (creature_size > CREATURE_SIZE_MAX)
        creature_size = CREATURE_SIZE_MAX;
    int16_t step = g_view_toward_party_step[creature_size - 1];
    int16_t dx = (int16_t)abs((int16_t)(eye.x - creature.x));
    if (!dx) {
        creature.y = (int16_t)(eye.y < creature.y ? creature.y - step : creature.y + step);
        return creature;
    }
    int16_t dy = (int16_t)abs((int16_t)(creature.y - eye.y));
    if (!dy) {
        creature.x = (int16_t)(eye.x < creature.x ? creature.x - step : creature.x + step);
        return creature;
    }
    bool below = eye.y < creature.y;
    bool right = eye.x < creature.x;
    bool same = below == right;
    bool steep = same == (dx < dy);
    int octant = steep | same << OCTANT_SAME_SHIFT | right << OCTANT_RIGHT_SHIFT;
    creature.x = (int16_t)(creature.x + g_view_toward_party_diagonal[creature_size - 1][octant][0]);
    creature.y = (int16_t)(creature.y + g_view_toward_party_diagonal[creature_size - 1][octant][1]);
    return creature;
}

/* Records the things of the square at for the party eye at view position pos: the grid cells of the square, the
   creatures in the object grid, and the cells with missiles and objects. */
static void load_square_things(int pos, const ViewEye *eye, Location at)
{
    int16_t dir = eye->dir;
    ViewSquare *s = g_view_squares + pos;
    g_view_missile_masks[pos] = 0;
    g_view_object_masks[pos] = 0;
    if (pos < POS_FAR_ROW && s->aspect != ASPECT_WALL) {
        const uint8_t *corner = g_view_square_grid_corner[pos];
        for (int col = 0; col < CELL_GRID_SIDE; col++)
            for (int row = 0; row < GRID_SQUARE_CELLS; row++)
                g_view_creature_grid[(corner[0] + col) * GRID_STRIDE + corner[1] + row] = (uint8_t)pos;
    }
    uint16_t thing = s->first_thing;
    if (thing == THING_ENDOFLIST)
        return;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        int type = thing_type(thing);
        if (type == THING_PROJECTILE) {
            g_view_missile_masks[pos] |= 1u << view_get_thing_position(thing, dir);
        } else if (view_is_item(thing)) {
            /* a shop stand on a floor shows its objects itself */
            if (s->aspect != ASPECT_FLOOR || s->side[SIDE_SHOP_DISPLAY] != 0)
                g_view_object_masks[pos] |= 1u << view_get_thing_position(thing, dir);
        } else if (type == THING_GROUP) {
            /* this adds the cell of the push arrow for a creature in front of the party */
            GridPoint c = aspect_cell_to_xy(at, view_get_thing_position(thing, DIR_NORTH));
            if (pos != POS_PARTY)
                c = view_offset_toward_party(eye->point, c, creature_info(gdat_current(), group_creature(thing))->size);
            RelativeOffset offset = view_relative_offset(dir, c.x - eye->point.x, c.y - eye->point.y);
            int16_t forward = offset.forward;
            if (forward >= GRID_FORWARD_LIMIT || abs(offset.lateral) >= GRID_LATERAL_LIMIT)
                continue;
            uint8_t *column = g_view_object_grid + (offset.lateral + GRID_LATERAL_CENTER) * GRID_STRIDE;
            while (forward >= 0 && column[forward] != NO_POSITION)
                forward--;
            if (forward >= 0)
                column[forward] = (uint8_t)pos;
        }
    }
}

/* The four faces of a wall square show no ornament. */
static void clear_wall_ornaments(ViewSquare *s)
{
    for (int i = 0; i < VIEW_SIDE_COUNT; i++)
        s->side[i] = ORNAMENT_NONE;
}

/* While walking, the party square shows what the squares beside it will show. */
static void load_walking_party_square(ViewSquare *s)
{
    if (s->aspect == ASPECT_WALL) {
        if (g_view_squares[POS_FRONT].aspect == ASPECT_TELEPORTER) {
            s->aspect = ASPECT_TELEPORTER;
        } else {
            clear_wall_ornaments(s);
            s->text = 0; /* as in the original: 0 here, THING_TEXT_NONE on the side squares */
            s->first_thing = THING_ENDOFLIST;
        }
        for (int i = POS_NEAR_SIDE_1; i <= POS_NEAR_SIDE_2; i++) {
            ViewSquare *side = g_view_squares + i;
            ViewSquare *ahead = g_view_squares + g_view_linked_squares[i][NEIGHBOUR_AHEAD];
            side->first_thing = THING_ENDOFLIST;
            side->square = ahead->square & SQUARE_TYPE_BITS;
            side->aspect = ahead->aspect;
            if (side->aspect == ASPECT_WALL) {
                clear_wall_ornaments(side);
                side->text = THING_TEXT_NONE;
            } else if (side->aspect == ASPECT_PIT) {
                side->aspect = ASPECT_FLOOR;
                side->square = 0;
            } else if (side->aspect == ASPECT_FLOOR || side->aspect == ASPECT_TELEPORTER ||
                       side->aspect == ASPECT_DOOR_SIDE) {
                side->side[SIDE_FLOOR_ORNAMENT] = ORNAMENT_NONE;
            }
            g_view_missile_masks[i] = 0;
            g_view_object_masks[i] = 0;
        }
    } else if (s->aspect == ASPECT_DOOR_FRONT) {
        s->aspect = ASPECT_FLOOR;
        s->square = 0;
        s->side[SIDE_FLOOR_ORNAMENT] = ORNAMENT_NONE;
    }
}

void view_load_square(Location at, int pos, int16_t dir)
{
    Location square = view_step_relative(at, dir, view_depth_of(pos), view_lateral_of(pos));
    ViewSquare *s = g_view_squares + pos;
    s->x = (int8_t)square.x;
    s->y = (int8_t)square.y;
    aspect_get(s, g_view_state.map, dir, square.x, square.y);
    if (g_view_state.walking && pos == POS_PARTY)
        load_walking_party_square(s);
    ViewEye eye = {.dir = dir, .point = aspect_cell_to_xy(at, g_view_eye_cell[dir])};
    load_square_things(pos, &eye, square);
}

void view_unhide_linked_squares(void)
{
    TeleporterLink link;
    for (int pos = 0; pos < POS_LINKED; pos++) {
        ViewSquare *s = g_view_squares + pos;
        if (s->aspect != ASPECT_FLOOR || !dungeon_teleporter_link(g_view_state.map, s->x, s->y, &link))
            continue;
        for (int k = 0; k < NEIGHBOUR_COUNT; k++) {
            uint8_t other = g_view_linked_squares[pos][k];
            /* fix: the original reads far past the view squares for the missing neighbours */
            if (other != LINK_NONE && g_view_squares[other].aspect == ASPECT_HIDDEN)
                g_view_squares[other].aspect = ASPECT_WALL;
        }
    }
}

/* The walls of the center, left, and right squares of the row at center: bits 0 to 2. */
static uint32_t row_walls(uint32_t walls, int center)
{
    return walls >> center & ROW_WALLS;
}

/* The hide table index of the row at center: its walls, and with a center wall the side walls of the row before. */
static uint32_t row_pattern(uint32_t walls, int before, int center)
{
    return (walls & VIEW_POS_BIT(center) ? row_walls(walls, before) & ROW_SIDE_WALLS : 0) | row_walls(walls, center);
}

void view_mark_hidden_squares(void)
{
    uint32_t walls = 0;
    for (int pos = 0; pos < POS_FAR_ROW; pos++)
        if (g_view_squares[pos].aspect == ASPECT_WALL)
            walls |= VIEW_POS_BIT(pos);
    /* the rows: positions 1-2, 3-5, 6-8, 11-13; a center wall hides the squares behind it */
    uint32_t hidden = hide_row1[row_walls(walls, POS_PARTY) >> 1] |
                      hide_row2[row_pattern(walls, POS_PARTY, POS_FRONT)] |
                      hide_row3[row_pattern(walls, POS_FRONT, POS_DEPTH2_CENTER)] |
                      hide_row4[row_pattern(walls, POS_DEPTH2_CENTER, POS_DEPTH3_CENTER)];
    uint32_t covered = walls | hidden;
    if (covered & VIEW_POS_BIT(POS_DEPTH3_LEFT) &&
        covered & (VIEW_POS_BIT(POS_FAR_SIDE_1) | VIEW_POS_BIT(POS_DEPTH2_FAR_LEFT)))
        hidden |= HIDE_FAR_LEFT;
    if (covered & VIEW_POS_BIT(POS_DEPTH3_RIGHT) &&
        covered & (VIEW_POS_BIT(POS_FAR_SIDE_2) | VIEW_POS_BIT(POS_DEPTH2_FAR_RIGHT)))
        hidden |= HIDE_FAR_RIGHT;
    for (int pos = 0; pos < VIEW_SQUARE_COUNT; pos++)
        if (hidden & VIEW_POS_BIT(pos) && g_view_squares[pos].aspect == ASPECT_WALL)
            g_view_squares[pos].aspect = ASPECT_HIDDEN;
}

/* ---- Pictures ---- */

int view_element_zone(int element, int offset, bool use_table)
{
    return use_table ? side_ornament_zone[element] + offset : element * ZONE_CELL_GRID + ZONE_VIEW_ELEMENTS + offset;
}

int view_rounded_scale(int a, int b)
{
    return ((a << SCALE_ROUND_SHIFT) / b + 1) >> 1;
}

void view_get_depth_shading(uint8_t *palette, int depth, int key1, int key2, int16_t *count)
{
    int light, entry = depth;
    if (!g_view_state.walking) {
        light = depth_light[depth];
    } else if (depth < 0) {
        light = walk_back_light[-depth] > -g_view_state.shade ? walk_back_light[-depth] : -g_view_state.shade;
        entry = 1;
    } else {
        light = walk_light[depth];
        entry = depth + GFX_REMAP_WALK_BASE;
    }
    if (gdat_exists(gdat_current(), CAT_WALLSET, g_view_state.gfx_set, GDAT_TYPE_REMAP, entry)) {
        DimmedRemap remap = {CAT_WALLSET, g_view_state.gfx_set, entry, g_view_state.shade, key1, key2};
        pal_get_dimmed_remap(gdat_current(), palette, &remap, count);
    } else
        pal_apply_dim_remap(palette,
                            SHADE_FULL - ((SHADE_FULL - light) * (SHADE_FULL - g_view_state.shade) >> SHADE_SHIFT),
                            key1, key2, count);
}

void view_setup_picture(const PicturePlace *p, int category, int index, int entry)
{
    DrawCtx *c = draw_ctx_init(&g_view_state.ctx, category, index, entry);
    int scale_x = p->scale_x;
    int depth = p->depth;
    if (scale_x != p->scale_y && (depth == DEPTH_MIDDLE || depth == DEPTH_FAR)) {
        uint16_t v = aspect_value_key(category, index, depth == DEPTH_MIDDLE ? ORN_KEY_RATIO_2 : ORN_KEY_RATIO_3);
        if (VIEW_LOW_BYTE(v))
            scale_x = view_rounded_scale((int)VIEW_HIGH_BYTE(v), VIEW_LOW_BYTE(v)); /* fix: the original divides by 0 */
    }
    c->flip = (int16_t)p->flip;
    c->scale_x = (int16_t)scale_x;
    c->scale_y = (int16_t)p->scale_y;
    c->dx = (int16_t)(c->dx + p->dx);
    c->dy = (int16_t)(c->dy + p->dy);
    c->zone = p->zone;
    c->anchor = (int16_t)p->anchor;
    c->dst = &g_view_viewport;
    c->transparent = (int16_t)p->key;
    c->mode = p->mode;
    view_get_depth_shading(c->palette, depth, p->key, p->key2, &c->palette_count);
    draw_ctx_prepare(c);
}

void view_flush_picture(void)
{
    draw_ctx_draw(&g_view_state.ctx);
}

void view_flush_picture_or_measure(bool draw)
{
    if (!draw)
        g_view_state.ctx.mode = DRAW_RECT_ONLY;
    view_flush_picture();
}

void view_add_click_zone(const int16_t *rect, enum ClickKind kind, int pos)
{
    if (g_view_click_zone_count >= VIEW_CLICK_ZONE_MAX)
        return; /* fix: the original has no limit */
    ViewClickZone *z = &g_view_click_zones[g_view_click_zone_count++];
    memcpy(z->rect, rect, sizeof z->rect);
    z->kind = (uint8_t)kind;
    z->pos = (uint8_t)pos;
    z->thing = THING_NONE;
}

void view_set_click_zone(int index, uint16_t thing, enum ClickKind kind, int pos)
{
    ViewClickZone *z = &g_view_click_zones[index];
    const int16_t *r = g_view_state.ctx.rect;
    if (z->thing == THING_NONE) {
        memcpy(z->rect, r, sizeof z->rect);
        int16_t h = z->rect[RECT_H];
        if (h < CLICK_LOW_HEIGHT) {
            if (h < CLICK_MIN_HEIGHT)
                z->rect[RECT_H] = CLICK_MIN_HEIGHT;
            z->rect[RECT_Y] = (int16_t)(z->rect[RECT_Y] + (h >> 1) - CLICK_HALF_SIZE);
        }
        int16_t w = z->rect[RECT_W];
        if (w < CLICK_MIN_WIDTH) {
            z->rect[RECT_W] = CLICK_MIN_WIDTH;
            z->rect[RECT_X] = (int16_t)(z->rect[RECT_X] + (w >> 1) - CLICK_HALF_SIZE);
        }
    } else {
        int x2 = max_int((int16_t)(z->rect[RECT_X] + z->rect[RECT_W]), (int16_t)(r[RECT_X] + r[RECT_W]));
        int x1 = min_int(z->rect[RECT_X], r[RECT_X]);
        z->rect[RECT_X] = (int16_t)x1;
        z->rect[RECT_W] = (int16_t)(x2 - x1);
        int y2 = max_int((int16_t)(z->rect[RECT_Y] + z->rect[RECT_H]), (int16_t)(r[RECT_Y] + r[RECT_H]));
        int y1 = min_int(z->rect[RECT_Y], r[RECT_Y]);
        z->rect[RECT_Y] = (int16_t)y1;
        z->rect[RECT_H] = (int16_t)(y2 - y1);
    }
    z->kind = (uint8_t)kind;
    z->thing = thing;
    z->pos = (uint8_t)pos;
}

const uint8_t *view_get_shaded_palette(int category, int index, int entry, int key)
{
    const uint8_t *colors = draw_gdat_image_palette(category, index, entry);
    int16_t count = 0;
    if (colors) {
        memcpy(g_view_state.shaded_palette, colors, IMAGE_COLORS);
        count = IMAGE_COLORS;
    } else if (g_view_state.shade == VIEW_SHADE_MAX) {
        return NULL;
    }
    return pal_apply_dim_remap(g_view_state.shaded_palette, g_view_state.shade, key, NO_KEY, &count);
}

/* Copies a bitmap mirrored left to right. */
static void copy_mirrored(const Bitmap *src, Bitmap *dst)
{
    gfx_blit_image(src, dst, MIRROR_H);
}

void view_show_map_picture(int entry, int zone, int key, int flip)
{
    DrawCtx c;
    draw_ctx_init(&c, CAT_WALLSET, g_view_state.gfx_set, entry);
    pal_apply_dim_remap(c.palette, g_view_state.shade, key, NO_KEY, &c.palette_count);
    c.transparent = (int16_t)key;
    c.dst = &g_view_viewport;
    c.flip = (int16_t)flip;
    if (g_view_state.walking && (zone == ZONE_CEILING || zone == ZONE_FLOOR))
        c.src_y = (int16_t)(c.src_y + (zone == ZONE_CEILING ? WALK_CEILING_DY : WALK_FLOOR_DY));
    c.zone = zone;
    draw_ctx_prepare(&c);
    draw_ctx_draw(&c);
}

Bitmap *view_render_offscreen(DrawCtx *ctx, const PicturePlace *p, int category, int index, int entry)
{
    view_setup_picture(&(PicturePlace){.flip = p->flip,
                                       .scale_x = p->scale_x,
                                       .scale_y = p->scale_y,
                                       .depth = p->depth,
                                       .zone = ZONE_NONE,
                                       .anchor = ZONE_NO_ANCHOR,
                                       .key = p->key,
                                       .key2 = NO_KEY},
                       category, index, entry);
    *ctx = g_view_state.ctx;
    ctx->transparent = (int16_t)p->key2;
    int16_t r[RECT_SIZE] = {0, 0, ctx->w, ctx->h};
    memcpy(ctx->rect, r, sizeof r);
    ctx->dst = draw_temp_bitmap(ctx->w > 0 ? ctx->w : 1, ctx->h > 0 ? ctx->h : 1, IMAGE_8BPP);
    return ctx->dst;
}

int view_mapped_key(const DrawCtx *c, int key)
{
    return c->palette_count > 0 ? c->palette[(uint8_t)key] : key;
}

void view_show_offscreen(DrawCtx *ctx, Bitmap *offscreen, int zone, int anchor, int key)
{
    ctx->transparent = (int16_t)key;
    ctx->dst = &g_view_viewport;
    ctx->zone = zone;
    ctx->anchor = (int16_t)anchor;
    ctx->palette_count = 0;
    draw_ctx_set_image(ctx, offscreen);
    draw_ctx_draw(ctx);
}

/* ---- Rain ---- */

/* The wind direction relative to the party. */
static int rain_side(void)
{
    return dir_relative(g_game.wind_dir, g_view_state.party_dir);
}

/* The rain picture for the rain intensity and the wind from side (relative to the party). */
static int rain_picture(int side)
{
    int level = g_game.rain_intensity >= RAIN_STRONG   ? RAIN_LEVEL_STRONG
                : g_game.rain_intensity >= RAIN_MEDIUM ? RAIN_LEVEL_MEDIUM
                : g_game.rain_intensity >= RAIN_LIGHT  ? RAIN_LEVEL_LIGHT
                                                       : 0;
    return (side == DIR_NORTH || side == DIR_SOUTH ? RAIN_ALONG : RAIN_ACROSS) + level;
}

bool view_shows_rain(void)
{
    return g_game.rain_intensity && g_view_state.rain_shown;
}

/* Draws the rain over the view: a random part of the rain picture, tiled. */
static void show_rain(void)
{
    if (!view_shows_rain())
        return;
    int16_t rect[RECT_SIZE];
    ScreenSize size = {VIEWPORT_WIDTH, VIEWPORT_HEIGHT};
    if (!zone_compute_rect(NULL, rect, ZONE_WALL, &size))
        return;
    int side = rain_side();
    int entry = rain_picture(side);
    const Bitmap *rain = draw_gdat_image(CAT_SKY, g_view_state.gfx_set, entry);
    if (!rain)
        return;
    if (side == DIR_EAST) {
        Bitmap *mirrored = draw_temp_bitmap(rain->width, rain->height, rain->bpp);
        if (!mirrored)
            return;
        copy_mirrored(rain, mirrored);
        rain = mirrored;
    }
    const uint8_t *palette = view_get_shaded_palette(CAT_SKY, g_view_state.gfx_set, entry, 0);
    int offset = rand_next() & RANDOM_BYTE;
    int width = (int16_t)(rain->width * rain->height - TILE_TRIM - (rand_next() & RANDOM_TILE_SPREAD));
    if (width <= offset)
        return; /* fix: the tiled blit of a tile row not longer than its start never ends */
    TileFill fill = {.tile = rain->pixels, .tile_width = width, .tile_offset = offset, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, g_view_viewport.pixels, VIEWPORT_WIDTH, rect);
}

void view_draw_wet_image(const DrawCtx *ctx, Bitmap *offscreen, int color_key)
{
    int transparent = view_mapped_key(ctx, color_key);
    if (!g_game.rain_intensity) {
        gfx_fill_bitmap(offscreen, transparent);
        return;
    }
    int side = rain_side();
    int entry = rain_picture(side);
    const Bitmap *rain = draw_gdat_image(CAT_SKY, g_view_state.gfx_set, entry);
    Bitmap *drops = rain ? draw_temp_bitmap(ctx->rect[RECT_W], rain->height, IMAGE_4BPP) : NULL;
    const uint8_t *shaded = view_get_shaded_palette(CAT_SKY, g_view_state.gfx_set, entry, 0);
    if (!drops || !shaded)
        return;
    int16_t r[RECT_SIZE] = {0, 0, (int16_t)((ctx->rect[RECT_W] + 1) & ~1), rain->height};
    Blit blit = {.src = rain->pixels,
                 .dst = drops->pixels,
                 .src_width = rain->width,
                 .dst_width = r[RECT_W],
                 .src_bpp = rain->bpp,
                 .dst_bpp = drops->bpp,
                 .transparent = GFX_OPAQUE,
                 .mirror = side == DIR_EAST ? MIRROR_H : MIRROR_NONE};
    gfx_blit(&blit, r);
    uint8_t palette[IMAGE_COLORS];
    memcpy(palette, shaded, IMAGE_COLORS);
    palette[0] = (uint8_t)transparent;
    int16_t area = (int16_t)(drops->width * drops->height);
    int offset = rand_below((uint16_t)(area - WET_TILE_TRIM));
    int width = (int16_t)(area - (rand_next() & RANDOM_TILE_SPREAD) - WET_TILE_EXTRA);
    if (width <= offset) { /* fix: the original tiles from past the end of the rain bitmap */
        gfx_fill_bitmap(offscreen, transparent);
        return;
    }
    TileFill fill = {.tile = drops->pixels,
                     .tile_width = width,
                     .tile_offset = offset,
                     .transparent = GFX_OPAQUE,
                     .colormap = palette};
    gfx_blit_tiled_4to8(&fill, offscreen->pixels, ctx->w, ctx->rect);
}

/* ---- The view ---- */

/* Draws the side of the door frame in front of the party, unless its door type has no frame. */
static void show_front_door_side(void)
{
    if (aspect_value_key(CAT_DOOR, view_map_door_type(g_view_squares[POS_FRONT].side[SIDE_DOOR_THING]),
                         DOOR_KEY_NO_FRAME))
        return;
    view_setup_picture(&(PicturePlace){.flip = g_view_state.flip,
                                       .scale_x = DOOR_SIDE_SCALE,
                                       .scale_y = DOOR_SIDE_SCALE,
                                       .depth = DOOR_SIDE_DEPTH,
                                       .zone = zone_id_for_view_cell(POS_FRONT, CELL_FAR_MIDDLE, 0),
                                       .anchor = ZONE_NO_ANCHOR,
                                       .key = g_view_state.wall_key,
                                       .key2 = NO_KEY},
                       CAT_WALLSET, g_view_state.gfx_set, GFX_MAP_DOOR_SIDE);
    view_flush_picture();
}

/* Draws the squares in sight except the party square, from the far ones to the near ones. */
static void show_far_squares(void)
{
    uint32_t cell_mask = THINGS_ALL; /* kept from square to square, as in the original */
    for (int i = 0; i < POS_DRAWN; i++) {
        int pos = far_order[i];
        int aspect = g_view_squares[pos].aspect;
        uint16_t first = g_view_squares[pos].first_thing;
        if (aspect == ASPECT_WALL) { /* a creature can stand in a wall that is not solid */
            if (first != THING_ENDOFLIST)
                view_show_creatures(first, pos, THINGS_ALL);
            view_show_wall_square(pos);
            continue;
        }
        if (pos >= POS_FAR_ROW) {
            if (aspect == ASPECT_FLOOR || aspect == ASPECT_PIT || aspect == ASPECT_TELEPORTER)
                view_show_floor_ornament(pos);
            else if (aspect != ASPECT_DOOR_FRONT)
                continue;
            if (first != THING_ENDOFLIST)
                view_show_creatures(first, pos, THINGS_ALL);
            continue;
        }
        switch (aspect) {
        case ASPECT_DOOR_FRONT:
            view_show_door_front(pos);
            continue;
        case ASPECT_STAIRS_FRONT:
            view_show_stairs_front(pos);
            break;
        case ASPECT_STAIRS_SIDE:
            view_show_stairs_side(pos);
            break;
        case ASPECT_PIT:
            view_show_floor_pit(pos);
            __attribute__((fallthrough));
        case ASPECT_FLOOR:
        case ASPECT_TELEPORTER:
        case ASPECT_DOOR_SIDE:
            if (aspect == ASPECT_DOOR_SIDE && pos == POS_FRONT)
                show_front_door_side();
            cell_mask = view_show_floor_ornament(pos) ? THINGS_FAR : THINGS_ALL;
            view_show_ceiling_pit(pos);
            break;
        default:
            continue;
        }
        view_show_square_things(pos, cell_mask, true);
        if (aspect == ASPECT_TELEPORTER)
            view_show_sparkle_field(pos, CAT_TELEPORTER, 0);
    }
}

/* Draws the party square with its things, then the rain over the view and the explosions on the party square. */
static void show_party_square(void)
{
    int aspect = g_view_squares[POS_PARTY].aspect;
    bool linked = false;
    TeleporterLink link;
    if (aspect == ASPECT_DOOR_FRONT) {
        view_show_door_front(POS_PARTY);
    } else {
        if (aspect == ASPECT_PIT) {
            view_show_floor_pit(POS_PARTY);
        } else if (aspect == ASPECT_DOOR_SIDE) {
            if (!aspect_value_key(CAT_DOOR, view_map_door_type(g_view_squares[POS_PARTY].side[SIDE_DOOR_THING]),
                                  DOOR_KEY_NO_FRAME))
                view_show_map_picture(GFX_MAP_DOOR_SIDE, zone_id_for_view_cell(POS_PARTY, CELL_FAR_MIDDLE, 0),
                                      g_view_state.wall_key, g_view_state.flip);
        } else if (aspect == ASPECT_STAIRS_SIDE) {
            bool up = g_view_squares[POS_PARTY].side[SIDE_STAIRS_UP] != 0;
            view_show_map_picture(up ? GFX_MAP_STAIRS_SIDE_UP : GFX_MAP_STAIRS_SIDE_DOWN,
                                  up ? ZONE_PARTY_STAIRS_UP : ZONE_PARTY_STAIRS_DOWN, g_view_state.wall_key, 0);
            view_show_map_picture((up ? GFX_MAP_STAIRS_SIDE_UP : GFX_MAP_STAIRS_SIDE_DOWN) + 1,
                                  (up ? ZONE_PARTY_STAIRS_UP : ZONE_PARTY_STAIRS_DOWN) + 1, g_view_state.wall_key, 0);
        }
        view_show_ceiling_pit(POS_PARTY);
        linked =
            dungeon_teleporter_link(g_view_state.map, g_view_squares[POS_PARTY].x, g_view_squares[POS_PARTY].y, &link);
        if (!linked)
            view_show_floor_ornament(POS_PARTY);
        view_show_square_things(POS_PARTY, THINGS_ALL, false);
        if (aspect == ASPECT_TELEPORTER)
            view_show_sparkle_field(POS_PARTY, CAT_TELEPORTER, 0);
    }
    show_rain();
    if (linked)
        view_show_floor_ornament(POS_PARTY);
    view_show_explosions(g_view_squares[POS_PARTY].first_thing, POS_PARTY, THINGS_ALL);
}

void view_clear_viewport(void)
{
    if (g_view_state.clear)
        gfx_fill_rect(g_view_viewport.pixels, g_view_state.clear_rect, g_view_state.clear_color, VIEWPORT_WIDTH,
                      IMAGE_8BPP);
}

int view_position(int depth, int side)
{
    for (int pos = 0; pos < VIEW_SQUARE_COUNT; pos++)
        if (view_depth_of(pos) == depth && view_lateral_of(pos) == side)
            return pos;
    return -1;
}

unsigned view_draws(int pos, int aspect)
{
    if (pos < 0 || pos >= VIEW_SQUARE_COUNT)
        return 0;
    unsigned draws = g_view_wall_faces[pos];
    if (aspect == ASPECT_WALL)
        return draws;
    bool creatures = !(g_party.move_steps && pos == POS_PARTY); /* the walking view hides the party square creatures */
    if (pos >= POS_FAR_ROW) {
        bool floor = aspect == ASPECT_FLOOR || aspect == ASPECT_PIT || aspect == ASPECT_TELEPORTER;
        return pos < POS_DRAWN && (floor || aspect == ASPECT_DOOR_FRONT) ? draws | VIEW_DRAWS_CREATURES : draws;
    }
    if (aspect == ASPECT_DOOR_FRONT && !g_view_door_front_shown[pos])
        return draws;
    draws |= VIEW_DRAWS_FLOOR_ORNAMENT | VIEW_DRAWS_DOOR;
    if (!g_view_square_things_shown[pos])
        return draws;
    draws |= VIEW_DRAWS_OBJECTS | (creatures ? VIEW_DRAWS_CREATURES : 0);
    if (MISSILE_POSITIONS & VIEW_POS_BIT(pos))
        draws |= VIEW_DRAWS_MISSILES;
    if (EXPLOSION_POSITIONS & VIEW_POS_BIT(pos))
        draws |= VIEW_DRAWS_EXPLOSIONS;
    return draws;
}

bool view_shows_floor_ornament(int pos, int ornament)
{
    if (!(view_draws(pos, ASPECT_FLOOR) & VIEW_DRAWS_FLOOR_ORNAMENT) || ornament == ORNAMENT_NONE || !ornament)
        return false;
    return !((pos == POS_FAR_SIDE_1 || pos == POS_FAR_SIDE_2) &&
             aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NOT_FAR_SIDE));
}

bool view_shows_party(void)
{
    return g_view_state.party_x == g_game.party_x && g_view_state.party_y == g_game.party_y &&
           g_view_state.party_map == g_game.party_map && g_view_state.party_dir == g_game.party_dir;
}

/* A view of a game step starts new projectile tracks; the tracks of the view of the game step just before stay when the
   party view is the same. Another view in the same game step keeps them. */
static void start_tracks(int16_t dir, int16_t x, int16_t y)
{
    if (g_view_state.flight.step_ticks)
        return;
    bool same = g_view_state.flight.map == g_view_state.map && g_view_state.flight.x == x &&
                g_view_state.flight.y == y && g_view_state.flight.dir == dir &&
                g_view_state.flight.walking == g_view_state.walking;
    if (same && g_view_state.flight.time + 1 == g_game.game_time) {
        memcpy(g_view_state.flight.last, g_view_state.flight.tracks,
               sizeof g_view_state.flight.tracks[0] * (size_t)g_view_state.flight.count);
        g_view_state.flight.last_count = g_view_state.flight.count;
    } else if (!same || g_view_state.flight.time != g_game.game_time) {
        g_view_state.flight.last_count = 0;
    }
    g_view_state.flight.count = 0;
    g_view_state.flight.map = g_view_state.map;
    g_view_state.flight.x = x;
    g_view_state.flight.y = y;
    g_view_state.flight.dir = dir;
    g_view_state.flight.walking = g_view_state.walking;
    g_view_state.flight.time = g_game.game_time;
}

void view_draw_dungeon(Location at, int16_t dir)
{
    g_view_state.party_map = g_game.party_map;
    g_view_state.map = g_view_state.party_map;
    g_view_state.party_dir = dir;
    g_view_state.party_x = at.x;
    g_view_state.party_y = at.y;
    g_view_state.shade = game_has_cheat(CHEAT_LIGHT)
                             ? 0
                             : g_view_light_level * LIGHT_STEP; /* the creature rules keep g_view_light_level */
    g_view_state.walking = g_party.move_steps != 0;
    start_tracks(dir, at.x, at.y);
    g_view_click_zone_count = VIEW_CLICK_ZONE_FIXED;
    for (int i = 0; i < VIEW_CLICK_ZONE_FIXED; i++) {
        g_view_click_zones[i].thing = THING_NONE;
        g_view_click_zones[i].kind = CLICK_NONE;
    }
    memset(g_view_creature_grid, 0, sizeof g_view_creature_grid);
    memset(g_view_object_grid, NO_POSITION, sizeof g_view_object_grid);
    view_clear_viewport();
    g_view_arrows_enabled = g_party.push_mode || g_party.push_pending_group != THING_NONE;
    input_update_viewport_arrow();
    for (int pos = VIEW_SQUARE_COUNT - 1; pos >= 0; pos--)
        view_load_square(at, pos, dir);
    int margins = NO_KEY;
    if (g_view_squares[POS_DEPTH2_LEFT].aspect == ASPECT_WALL &&
        g_view_squares[POS_DEPTH2_CENTER].aspect == ASPECT_WALL &&
        g_view_squares[POS_DEPTH2_RIGHT].aspect == ASPECT_WALL)
        margins = MAP_KEY_FAR_MARGINS;
    if (g_view_squares[POS_FRONT_LEFT].aspect == ASPECT_WALL && g_view_squares[POS_FRONT].aspect == ASPECT_WALL &&
        g_view_squares[POS_FRONT_RIGHT].aspect == ASPECT_WALL)
        margins = MAP_KEY_NEAR_MARGINS;
    uint16_t m = margins == NO_KEY ? 0 : aspect_value_key(CAT_WALLSET, g_view_state.gfx_set, margins);
    zone_set_clip_margins(0, 0, 0, VIEW_LOW_BYTE(m));
    view_show_map_picture(GFX_MAP_CEILING, ZONE_CEILING, NO_KEY, view_get_flip_parity(PARITY_CEILING, dir, at));
    g_zone_clip_rect[RECT_W] = 0;
    zone_set_clip_margins(0, VIEW_HIGH_BYTE(m), 0, 0);
    view_show_map_picture(GFX_MAP_FLOOR, ZONE_FLOOR, NO_KEY, view_get_flip_parity(PARITY_FLOOR, dir, at));
    g_zone_clip_rect[RECT_W] = 0;
    g_view_state.flip = view_get_flip_parity(PARITY_SQUARE, dir, at);
    view_outdoor_draw_sky(dir, at.x, at.y);
    view_unhide_linked_squares();
    view_mark_hidden_squares();
    show_far_squares();
    show_party_square();
    draw_free_temp();
}

/* ---- Projectile flight ---- */

ViewPoint view_flight_shift(ViewPoint old_corner, ViewPoint new_corner, int tick, int step_ticks)
{
    if (step_ticks <= 0 || tick >= step_ticks)
        return (ViewPoint){0, 0};
    int rest = step_ticks - tick;
    return (ViewPoint){(int16_t)((old_corner.x - new_corner.x) * rest / step_ticks),
                       (int16_t)((old_corner.y - new_corner.y) * rest / step_ticks)};
}

void view_set_flight_tick(int tick, int step_ticks)
{
    g_view_state.flight.tick = tick;
    g_view_state.flight.step_ticks = step_ticks;
}

bool view_has_moving_projectiles(void)
{
    for (int i = 0; i < g_view_state.flight.count; i++)
        if (g_view_state.flight.tracks[i].from.x != g_view_state.flight.tracks[i].to.x ||
            g_view_state.flight.tracks[i].from.y != g_view_state.flight.tracks[i].to.y)
            return true;
    return false;
}

/* ---- Clicks ---- */

/* True when the last picture has a pixel other than key at (x, y) of the viewport. */
static bool hit_test_picture(int16_t x, int16_t y, int key)
{
    const DrawCtx *c = &g_view_state.ctx;
    const int16_t *r = c->rect;
    if (x < r[RECT_X] || x > r[RECT_X] + r[RECT_W] - 1 || y < r[RECT_Y] || y > r[RECT_Y] + r[RECT_H] - 1)
        return false;
    int sx = c->flip & MIRROR_H ? r[RECT_X] + r[RECT_W] - 1 - x + c->src_x : x - r[RECT_X] + c->src_x;
    int sy = y - r[RECT_Y] + c->src_y;
    const Bitmap *image = c->image;
    if (image && image->height >= 2)
        g_view_hit_offset_y = (int16_t)((sy - (image->height >> 1)) * PERCENT / (image->height >> 1));
    if (!image || sx < 0 || sy < 0 || sx >= image->width || sy >= image->height)
        return false;
    int pixel;
    if (image->bpp == IMAGE_8BPP) {
        pixel = image->pixels[sy * image->width + sx];
    } else {
        int i = sy * ((image->width + 1) & ~1) + sx;
        pixel = i & 1 ? image->pixels[i >> 1] & NIBBLE_MASK : image->pixels[i >> 1] >> NIBBLE_BIT_COUNT;
    }
    return pixel != key;
}

bool view_hit_test_creature(int16_t x, int16_t y, Location at, int *out_side)
{
    uint16_t group = creature_get_group_at(at);
    if (group == THING_NONE)
        return false;
    view_show_creature(POS_FRONT, HIT_DEPTH, group);
    int key = g_view_state.ctx.transparent;
    view_flush_picture_or_measure(false);
    if (!hit_test_picture(x, y, key)) {
        draw_free_temp();
        return false;
    }
    const DrawCtx *c = &g_view_state.ctx;
    const int16_t *r = c->rect;
    int sx = c->flip & MIRROR_H ? r[RECT_X] + r[RECT_W] - 1 - x + c->src_x : x - r[RECT_X] + c->src_x;
    int half = c->image ? c->image->width >> 1 : 0;
    int16_t offset = half ? (int16_t)((sx - c->x - half) * PERCENT / half) : 0;
    *out_side = offset < -HIT_SIDE_LIMIT ? TURN_LEFT : offset <= HIT_SIDE_LIMIT ? 0 : TURN_RIGHT;
    draw_free_temp();
    return true;
}

bool view_hit_test_wall_ornament(int pos, int16_t x, int16_t y)
{
    if (pos > POS_FRONT)
        return false;
    ViewSquare saved = g_view_squares[pos];
    Location at = view_step_relative((Location){g_view_state.party_x, g_view_state.party_y}, g_view_state.party_dir,
                                     view_depth_of(pos), view_lateral_of(pos));
    g_view_squares[pos].x = (int8_t)at.x;
    g_view_squares[pos].y = (int8_t)at.y;
    aspect_get(&g_view_squares[pos], g_view_state.map, g_view_state.party_dir, at.x, at.y);
    bool hit = false;
    int key = view_show_wall_ornament(pos, view_lateral_of(pos), false); /* the face that the party sees of the wall */
    if (key != NO_KEY)
        hit = hit_test_picture(x, y, key);
    if (pos == POS_FRONT || g_view_state.ctx.w < HIT_SMALL_SIZE || g_view_state.ctx.h < HIT_SMALL_SIZE)
        hit = true;
    g_view_squares[pos] = saved;
    draw_free_temp();
    return hit;
}

/* A click on a chest in the view that hit_state tells: it opens or closes, takes thing (placing), or gives its last
   item to the leader hand. hit: the object that the click hits. */
static ObjectClickResult chest_click(uint16_t chest, uint16_t hit, enum ChestState hit_state, uint16_t thing,
                                     bool placing)
{
    const ObjectClickResult drop = {OBJECT_CLICK_DROP, THING_NONE};
    switch (hit_state) {
    case CHEST_CLOSED:
    case CHEST_OPEN: /* not a state of a hit test */
        if (placing)
            return drop;
        if (!container_is_open(chest))
            return (ObjectClickResult){OBJECT_CLICK_HIT, hit};
        container_set_open(chest, false);
        break;
    case CHEST_HIT_OPEN:
        container_set_open(chest, true);
        break;
    case CHEST_HIT_INSIDE: {
        if (placing && item_attr(thing, ATTR_SLOTS) & ATTR_SLOTS_NO_CONTAINER)
            return drop;
        uint16_t *contents = container_contents(chest);
        uint16_t last = *contents;
        int count = 0;
        for (uint16_t c = *contents; c != THING_ENDOFLIST; c = dungeon_next_thing(c), count++)
            last = c;
        if (placing) {
            if (count >= CHEST_CAPACITY)
                return drop;
            dungeon_append_thing(thing, contents);
        } else if (count > 0) {
            dungeon_unlink_thing_from_list(last, contents);
            champ_put_in_leader_hand(last);
        }
        break;
    }
    }
    return (ObjectClickResult){OBJECT_CLICK_DONE, THING_NONE};
}

ObjectClickResult view_hit_test_object(const ViewObjectClick *click, uint16_t thing, bool placing)
{
    uint16_t group = THING_NONE, t;
    if (click->kind == CLICK_POSSESSIONS) {
        group = creature_group_at((Position){g_current_map.map, click->at.x, click->at.y});
        if (group == THING_NONE) /* fix: the creature left the square; no read of the record of THING_NONE */
            return (ObjectClickResult){OBJECT_CLICK_DONE, THING_NONE};
        t = thing_word(group, GROUP_POSSESSIONS);
    } else {
        t = dungeon_square_first_thing(g_current_map.map, click->at.x, click->at.y);
    }
    if (t == THING_ENDOFLIST)
        return placing ? (ObjectClickResult){OBJECT_CLICK_HIT, thing}
                       : (ObjectClickResult){OBJECT_CLICK_DONE, THING_NONE};
    int spot = click->kind == CLICK_ALCOVE ? ALCOVE_FIRST_SPOT : 0;
    uint16_t hit = THING_NONE;
    uint16_t chest = THING_NONE;
    enum ChestState hit_state = CHEST_CLOSED;
    for (; t != THING_ENDOFLIST; t = dungeon_next_thing(t)) {
        if (!view_is_item(t) || thing_cell(t) != thing_cell(thing))
            continue;
        enum ChestState state = CHEST_CLOSED;
        uint16_t this_chest = THING_NONE;
        if (item_is_chest(t) &&
            gdat_exists(gdat_current(), CAT_CONTAINER, item_gdat_index(t), GDAT_TYPE_IMAGE, GFX_CHEST_OPEN_PICTURE)) {
            this_chest = t;
            if (container_is_open(t))
                state = CHEST_HIT_INSIDE;
            else if (!placing)
                state = CHEST_HIT_OPEN;
        }
        for (;;) {
            view_show_object(t, click->pos,
                             &(ObjectLook){.spot = spot,
                                           .group = group,
                                           .open_state = state,
                                           .in_alcove = click->kind == CLICK_ALCOVE,
                                           .projectile = THING_NONE});
            if (hit_test_picture(click->x, click->y, THING_KEY)) {
                chest = this_chest;
                hit = t;
                hit_state = state;
                break;
            }
            if (state == CHEST_CLOSED)
                break;
            state = CHEST_CLOSED;
        }
        spot = view_next_spot(spot, click->kind == CLICK_ALCOVE); /* the spots of the drawing */
    }
    draw_free_temp();
    if (chest == THING_NONE)
        return (ObjectClickResult){OBJECT_CLICK_HIT, hit != THING_NONE ? hit : thing};
    return chest_click(chest, hit, hit_state, thing, placing);
}
