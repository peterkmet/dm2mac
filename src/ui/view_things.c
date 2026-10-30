/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The things of the dungeon view: objects, creatures, missiles, explosions, and their click zones. */
#include <stdlib.h>
#include <string.h>
#include "ai.h"
#include "game.h"
#include "item.h"
#include "projectile.h"
#include "random.h"
#include "view_internal.h"

/* The order of the cells of a square in the things drawing, for squares left of, on, and right of the view axis. */
static const uint8_t thing_cell_order[3][CELL_GRID_COUNT] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24},
    {0, 4, 1, 3, 2, 5, 9, 6, 8, 7, 10, 14, 11, 13, 12, 15, 19, 16, 18, 17, 20, 24, 21, 23, 22},
    {4, 3, 2, 1, 0, 9, 8, 7, 6, 5, 14, 13, 12, 11, 10, 19, 18, 17, 16, 15, 24, 23, 22, 21, 20},
};
/* The thing grid cell of the far left cell of each square up to depth 3. */
const uint8_t g_view_square_grid_corner[POS_FAR_ROW][2] = {
    {8,  0 },
    {4,  0 },
    {12, 0 },
    {8,  4 },
    {4,  4 },
    {12, 4 },
    {8,  8 },
    {4,  8 },
    {12, 8 },
    {0,  8 },
    {16, 8 },
    {8,  12},
    {4,  12},
    {12, 12},
    {0,  12},
    {16, 12},
};
/* The grid cell of the eye of the party in its square, by the direction it looks to. */
const uint8_t g_view_eye_cell[DIR_COUNT] = {22, 10, 2, 14};
/* The grid cells of the 4 cells of a square (north west, north east, south east, south west). */
const uint8_t g_view_cell_view_position[DIR_COUNT] = {6, 8, 18, 16};
/* The scale of things by depth * 4 + row (row 0 is the near row), from index 1. Index 0 is the byte before the
   table in SKULL.EXE, the last depth scale: the explosions in the near cells of the party square use it. */
static const uint8_t thing_scale[THING_SCALE_COUNT] = {
    0x13, 0x60, 0x57, 0x4e, 0x47, 0x40, 0x3a, 0x34, 0x2f, 0x2b, 0x27, 0x23, 0x1f, 0x1c, 0x1a, 0x17, 0x15, 0x13, 0x11,
};
/* The scale of missiles by depth * 2 - (1 for the near cells). */
static const uint8_t missile_scale[MISSILE_SCALE_COUNT] = {0x40, 0x34, 0x2b, 0x23, 0x1c, 0x17, 0x13};
/* Small offsets that keep the objects of a cell apart: an x and a y index into jitter_offset by spot. */
static const uint8_t jitter[JITTER_COUNT][2] = {
    {2, 5},
    {0, 6},
    {5, 7},
    {3, 0},
    {7, 1},
    {1, 2},
    {6, 3},
    {3, 3},
    {5, 5},
    {2, 6},
    {7, 7},
    {1, 0},
    {3, 1},
    {6, 2},
    {1, 3},
    {5, 3},
};
/* The pixel offsets of the jitter spots. */
static const int8_t jitter_offset[JITTER_OFFSET_COUNT] = {0, 1, 2, 3, 0, -3, -2, -1};
/* Object pictures by the open state of a chest (closed, open, and the click states lid and inside): side view, front
   view. The original keeps rows 2 and 3 in the 4 bytes after its 2-row table. */
static const uint8_t object_picture[4][2] = {
    {GFX_OBJECT_PICTURE_CLOSED, GFX_OBJECT_PICTURE_CLOSED + 1},
    {GFX_OBJECT_PICTURE_OPEN,   GFX_OBJECT_PICTURE_OPEN + 1  },
    {GFX_CHEST_OPEN_PICTURE,    GFX_CHEST_OPEN_PICTURE + 1   },
    {GFX_CHEST_INSIDE_PICTURE,  GFX_CHEST_INSIDE_PICTURE + 1 }
};
/* The possessions of a static creature: lift above the floor, and their cell shift by the side they lie on,
   for creatures that carry them high (0) and low (1). */
static const uint8_t possession_lift[2] = {58, 53};
static const int8_t possession_column_shift[2][DIR_COUNT] = {
    {-1, 1, 1, -1},
    {0,  1, 0, -1}
};
static const int8_t possession_row_shift[2][DIR_COUNT] = {
    {1, 1, -1, -1},
    {2, 0, -2, 0 }
};
/* The cells that show the possessions of a creature, for squares left of or on the view axis, and right of it. */
static const uint8_t possession_cells[2][4] = {
    {6, 8, 16, 18},
    {8, 6, 18, 16}
};
/* The explosions in an alcove: scale of the still picture and size of the explosion picture by depth. */
static const uint8_t alcove_explosion_scale[POS_DEPTH_MAX] = {0, 0x40, 0x28, 0x1e};
static const uint8_t alcove_explosion_size[POS_DEPTH_MAX] = {0, 0x30, 0x20, 0x18};
/* The order of the explosion cells by the party direction. */
static const int8_t explosion_cell_order[DIR_COUNT][EXPLOSION_ORDER_COUNT] = {
    {0, 1, EXPLOSION_CELL_SQUARE, 3, 2},
    {1, 2, EXPLOSION_CELL_SQUARE, 0, 3},
    {2, 3, EXPLOSION_CELL_SQUARE, 1, 0},
    {3, 0, EXPLOSION_CELL_SQUARE, 2, 1},
};
/* A creature stands nearer to the party by its size: straight ahead or beside, and diagonally (lateral, forward
   by the octant). */
const int8_t g_view_toward_party_step[CREATURE_SIZE_MAX] = {1, 1, 2};
const int8_t g_view_toward_party_diagonal[CREATURE_SIZE_MAX][OCTANT_COUNT][2] = {
    {{0, -1}, {1, 0},  {1, 0}, {0, 1}, {0, 1},  {-1, 0}, {-1, 0},  {0, -1} },
    {{1, -1}, {1, -1}, {1, 1}, {1, 1}, {-1, 1}, {-1, 1}, {-1, -1}, {-1, -1}},
    {{1, -2}, {2, -1}, {2, 1}, {1, 2}, {-1, 2}, {-2, 1}, {-2, 1},  {-1, -2}}, /* (-2, 1) twice, as in SKULL.EXE */
};
/* The cell and the scale of the creature that the party pushes, by push arrow. */
static const uint8_t arrow_cells[PUSH_ARROW_NONE + 1] = {2, 0x0e, 0x16, 0x16, 0x16, 0x0a, 0x0c};
static const uint8_t arrow_scales[PUSH_ARROW_NONE + 1] = {0x34, 0x40, 0x4e, 0x4e, 0x4e, 0x40, 0x40};

/* Sparkle fields: the tile start (in 16 pixels), the mask picture (bit 7 mirrored, 0xff none), width, height. */
static const uint8_t sparkle_field[POS_FAR_ROW][4] = {
    {0x3b, 0xff, 224, 136},
    {0x3f, 0x05, 33,  136},
    {0x3f, 0x85, 33,  136},
    {0x3d, 0xff, 160, 111},
    {0x3f, 0x04, 60,  111},
    {0x3f, 0x84, 60,  111},
    {0x3c, 0xff, 106, 74 },
    {0x3f, 0x03, 78,  74 },
    {0x3f, 0x83, 78,  74 },
    {0x3f, 0x02, 8,   52 },
    {0x3f, 0x82, 8,   52 },
    {0x3f, 0xff, 70,  49 },
    {0x3f, 0x01, 83,  49 },
    {0x3f, 0x81, 83,  49 },
    {0x3f, 0x00, 36,  49 },
    {0x3f, 0x80, 36,  49 },
};
/* ---- Things ---- */

void view_show_sparkle_field(int pos, int category, int index)
{
    if (pos >= POS_FAR_ROW)
        return;
    const uint8_t *field = sparkle_field[pos];
    int16_t rect[RECT_SIZE];
    ScreenSize size = {field[SPARKLE_WIDTH], field[SPARKLE_HEIGHT]};
    if (!zone_compute_rect(NULL, rect, ZONE_WALL + pos, &size))
        return;
    const uint8_t *mask = NULL;
    if (field[SPARKLE_MASK_PICTURE] != SPARKLE_MASK_NONE) {
        const Bitmap *picture = draw_gdat_image(category, index, field[SPARKLE_MASK_PICTURE] & SPARKLE_MASK_ENTRY);
        if (!picture)
            return;
        mask = picture->pixels;
        if (field[SPARKLE_MASK_PICTURE] & SPARKLE_MASK_MIRRORED) {
            Bitmap *mirrored = draw_temp_bitmap(picture->width, picture->height, IMAGE_4BPP);
            if (!mirrored)
                return;
            gfx_fill_bitmap(mirrored, 0); /* fix: the original leaves the last column of an odd width undefined */
            int16_t r[RECT_SIZE] = {0, 0, (int16_t)(picture->width - (picture->width & 1)), picture->height};
            Blit blit = {.src = picture->pixels,
                         .dst = mirrored->pixels,
                         .src_width = picture->width,
                         .dst_width = picture->width,
                         .src_bpp = IMAGE_4BPP,
                         .dst_bpp = IMAGE_4BPP,
                         .transparent = GFX_OPAQUE,
                         .mirror = MIRROR_H};
            gfx_blit(&blit, r);
            mask = mirrored->pixels;
        }
    }
    const Bitmap *sparkles = draw_gdat_image(category, index, GFX_SPARKLE_PICTURE);
    if (!sparkles)
        return;
    const uint8_t *palette = view_get_shaded_palette(category, index, GFX_SPARKLE_PICTURE, 0);
    int offset = rand_next() & RANDOM_BYTE;
    int width = (int16_t)((rand_bit() + field[SPARKLE_TILE]) * SPARKLE_TILE_STEP);
    TileFill fill = {
        .tile = sparkles->pixels, .tile_width = width, .tile_offset = offset, .mask = mask, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, g_view_viewport.pixels, VIEWPORT_WIDTH, rect);
}

/* Moves the cell at by columns and rows (rows toward the far side); the cell can go onto a neighbour square. False
   when it leaves the view. */
static bool shift_object_cell(SquareCell *at, int columns, int rows)
{
    int p = at->pos;
    int c = at->cell;
    if (rows) {
        c -= rows * CELL_GRID_SIDE;
        if (c < 0) {
            p = g_view_linked_squares[p][NEIGHBOUR_AHEAD];
            c += CELL_ROW_STEP;
        } else if (c >= CELL_GRID_COUNT) {
            p = g_view_linked_squares[p][NEIGHBOUR_BEHIND];
            c -= CELL_ROW_STEP;
        }
        if (p == LINK_NONE)
            return false;
    }
    if (columns) {
        int column = c % CELL_GRID_SIDE + columns;
        if (column > CELL_COLUMN_LAST) {
            c += columns - GRID_SQUARE_CELLS;
            p = g_view_linked_squares[p][NEIGHBOUR_RIGHT];
        } else if (column < 0) {
            c += columns + GRID_SQUARE_CELLS;
            p = g_view_linked_squares[p][NEIGHBOUR_LEFT];
        } else {
            c += columns;
        }
        if (p == LINK_NONE)
            return false;
    }
    *at = (SquareCell){p, c};
    return true;
}

/* The picture of an object in cell of the square at pos: the side or the front view, by the open state of a chest,
   mirrored by the side of the view axis. */
static ThingPicture pick_object_picture(uint16_t thing, int pos, int16_t cell, enum ChestState open_state,
                                        bool in_alcove)
{
    uint8_t category = item_gdat_category(thing);
    uint8_t index = item_gdat_index(thing);
    bool chest = category == CAT_CONTAINER && index < CHEST_KINDS;
    if (chest && open_state == CHEST_CLOSED && container_is_open(thing))
        open_state = CHEST_OPEN;
    ThingPicture pic = {.entry = object_picture[open_state][0], .flip = 0};
    int lateral = view_lateral_of(pos);
    if (lateral != 0) {
        pic.flip = chest && lateral == 1;
    } else {
        int column = cell % CELL_GRID_SIDE;
        if ((column == CELL_GRID_SIDE / 2 || in_alcove) &&
            view_picture_exists(category, index, object_picture[open_state][1]))
            pic.entry = object_picture[open_state][1];
        else
            pic.flip = chest && !in_alcove && column > CELL_GRID_SIDE / 2;
    }
    return pic;
}

int view_next_spot(int spot, bool in_alcove)
{
    if (in_alcove)
        return spot + 1 >= ALCOVE_SPOT_END ? ALCOVE_FIRST_SPOT : spot + 1;
    return (spot + 1) % JITTER_COUNT;
}

/* The track of the projectile thing on view square pos in a list of tracks, NULL for none; the cell bits of a
   projectile change in flight. A track on a view square that is not pos or next to it belongs to a freed projectile
   record that a new projectile took. */
static const FlightTrack *find_track(const FlightTrack *list, int count, uint16_t thing, int pos)
{
    for (int i = 0; i < count; i++)
        if (list[i].thing == (thing & THING_ID_MASK) && abs(view_depth_of(list[i].pos) - view_depth_of(pos)) <= 1 &&
            abs(view_lateral_of(list[i].pos) - view_lateral_of(pos)) <= 1)
            return &list[i];
    return NULL;
}

/* The top left corner of the view picture without the clip of its zone; false when it does not show. */
static bool picture_corner(ViewPoint *corner)
{
    const DrawCtx *c = &g_view_state.ctx;
    int16_t rect[RECT_SIZE];
    ScreenSize size = {(int16_t)(c->src_x + c->dx), (int16_t)(c->src_y + c->dy)};
    if (c->w <= 0 || c->h <= 0 || !c->image ||
        !zone_compute_anchored_rect(c->image, rect, (AnchoredZone){c->zone | ZONE_RELATIVE, c->anchor}, &size))
        return false;
    corner->x = (int16_t)(rect[RECT_X] - size.w);
    corner->y = (int16_t)(rect[RECT_Y] - size.h);
    return true;
}

/* Draws the view picture of the projectile thing (THING_NONE: a picture of no projectile) on view square pos. A view
   of a game step keeps its corner; a view between game steps draws it part of the way back to its corner of the game
   step before. */
static void flush_projectile(uint16_t thing, int pos)
{
    ViewPoint corner;
    if (thing != THING_NONE && picture_corner(&corner)) {
        if (g_view_state.flight.step_ticks) {
            const FlightTrack *t = find_track(g_view_state.flight.tracks, g_view_state.flight.count, thing, pos);
            ViewPoint shift =
                t ? view_flight_shift(t->from, t->to, g_view_state.flight.tick, g_view_state.flight.step_ticks)
                  : (ViewPoint){0, 0};
            g_view_state.ctx.src_x = (int16_t)(g_view_state.ctx.src_x + shift.x);
            g_view_state.ctx.src_y = (int16_t)(g_view_state.ctx.src_y + shift.y);
        } else if (g_view_state.flight.count < FLIGHT_TRACK_MAX) {
            const FlightTrack *last = find_track(g_view_state.flight.last, g_view_state.flight.last_count, thing, pos);
            g_view_state.flight.tracks[g_view_state.flight.count++] =
                (FlightTrack){thing & THING_ID_MASK, pos, last ? last->to : corner, corner};
        }
    }
    view_flush_picture();
}

void view_show_object(uint16_t thing, int pos, const ObjectLook *look)
{
    const Gdat *g = gdat_current();
    SquareCell at = {pos, CELL_CENTER};
    int row, zone, dx = 0;
    int dy = look->dy;
    if (look->group != THING_NONE) {
        int type = group_creature(look->group);
        bool low = (creature_info(g, type)->possessions & POSSESSIONS_KIND_MASK) <= POSSESSIONS_LOW_KIND;
        int16_t lift = (int16_t)gdat_entry_index(g, CAT_CREATURE, type, GDAT_TYPE_VALUE, CREATURE_KEY_POSSESSION_LIFT);
        dy -= lift ? lift : possession_lift[low];
        int side = dir_relative(thing_cell(thing), g_view_state.party_dir);
        /* this adds the cell of the push arrow for a creature in front of the party */
        if (!shift_object_cell(&at, possession_column_shift[low][side], possession_row_shift[low][side]))
            return;
    } else if (!look->in_alcove) {
        at.cell = view_get_thing_position(thing, g_view_state.party_dir);
    }
    int depth = view_depth_of(at.pos);
    if (look->in_alcove) {
        row = 0;
        uint16_t place = gdat_entry_index(g, g_view_state.alcove.category, g_view_state.alcove.index, GDAT_TYPE_VALUE2,
                                          ORN_KEY_ALCOVE_ITEMS);
        dx = g_view_state.alcove.dx + gdat_offset_x(place) + jitter_offset[jitter[look->spot][0]];
        dy = gdat_offset_y(place) + g_view_state.alcove.dy + jitter_offset[jitter[look->spot][1]];
        zone = g_view_state.alcove.zone;
    } else {
        row = CELL_COLUMN_LAST - at.cell / CELL_GRID_SIDE;
        if (at.pos == POS_PARTY && row < PARTY_ROWS_BELOW_VIEW)
            return;
        zone = zone_id_for_view_cell(at.pos, at.cell, 0) | ZONE_RELATIVE;
    }
    uint8_t category = item_gdat_category(thing);
    uint8_t index = item_gdat_index(thing);
    ThingPicture pic = pick_object_picture(thing, at.pos, at.cell, look->open_state, look->in_alcove);
    if (look->spot != -1) { /* as in the original, the alcove items get the jitter a second time */
        dx += jitter_offset[jitter[look->spot][0]];
        if (look->group == THING_NONE)
            dy += jitter_offset[jitter[look->spot][1]];
    }
    uint16_t offset = gdat_entry_index(g, category, CAT_ALCOVE_KEY, GDAT_TYPE_VALUE2, pic.entry);
    if (offset) {
        dx += gdat_offset_x(offset);
        dy += gdat_offset_y(offset);
    }
    int scale = thing_scale[depth * GRID_SQUARE_CELLS + row + OBJECT_SCALE_START];
    view_setup_picture(&(PicturePlace){.flip = pic.flip,
                                       .scale_x = scale,
                                       .scale_y = scale,
                                       .dx = (int16_t)dx,
                                       .dy = (int16_t)dy,
                                       .depth = depth,
                                       .zone = zone,
                                       .key = THING_KEY,
                                       .key2 = NO_KEY},
                       category, index, pic.entry);
    if (!look->record_click)
        g_view_state.ctx.mode = DRAW_RECT_ONLY;
    flush_projectile(look->projectile, at.pos);
}

/* Draws the objects of a thing list in cell (0 to 3, relative to the party) of the square at pos; with group,
   they are the possessions of that static creature. */
static void show_cell_objects(uint16_t thing, int pos, int16_t cell, uint16_t group)
{
    if (thing == THING_ENDOFLIST || view_depth_of(pos) > DEPTH_FAR)
        return;
    const CreatureInfo *info = group != THING_NONE ? creature_info(gdat_current(), group_creature(group)) : NULL;
    int spot = 0;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (!view_is_item(thing) || dir_relative(thing_cell(thing), g_view_state.party_dir) != cell)
            continue;
        if (info && !(info->possessions & STATIC_POSSESSIONS_SHOWN) && !(info->possessions & POSSESSIONS_CELLS_MASK)) {
            /* a creature that hides what it holds: one click zone for all of it */
            if (g_view_click_zone_count < VIEW_CLICK_ZONE_MAX) {
                ViewClickZone *z = &g_view_click_zones[g_view_click_zone_count++];
                if (!zone_get_rect(info->possession_zone, z->rect))
                    memset(z->rect, 0, sizeof z->rect);
                z->kind = CLICK_POSSESSIONS;
                z->thing = thing;
                z->pos = (uint8_t)pos;
            }
            return;
        }
        view_show_object(thing, pos,
                         &(ObjectLook){.spot = spot, .group = group, .record_click = true, .projectile = THING_NONE});
        spot = view_next_spot(spot, false);
        if (info) {
            if (creature_cell_mask(info->possessions, DIR_NORTH) >> cell & 1)
                view_set_click_zone(CLICK_ZONE_POSSESSION_FIRST + cell, thing, CLICK_POSSESSIONS, pos);
        } else if (pos == POS_PARTY || (pos == POS_FRONT && cell > CELL_FRONT_RIGHT)) {
            Location at = {g_view_squares[pos].x, g_view_squares[pos].y};
            uint16_t g = creature_find_group_near_point(at, thing_cell(thing)).group;
            if (g == THING_NONE || creature_is_flying(g) ||
                creature_group_info(g)->possessions & POSSESSIONS_FLOOR_CLICKABLE)
                view_set_click_zone(cell, thing, CLICK_OBJECT, pos);
        }
    }
}

/* The picture of the animation frame of a creature of type seen from side, its mirror, and whether the picture of
   the other side shows. */
static SidePicture pick_side_picture(int type, const uint8_t *frame, int side, int anim)
{
    SidePicture pic;
    int entry = frame[FRAME_ENTRY + side];
    pic.flip = 0;
    pic.mirrored = false;
    if (!view_has_creature_picture(type, entry)) {
        int back = dir_opposite(side);
        pic.mirrored = back & 1;
        pic.flip = pic.mirrored;
        entry = frame[FRAME_ENTRY + back];
        if (!view_has_creature_picture(type, entry)) {
            pic.flip = 0;
            entry = frame[FRAME_ENTRY + DIR_SOUTH];
        }
    } else {
        int bits = frame[FRAME_MIRROR] >> ((DIR_WEST - side) * FRAME_MIRROR_SHIFT) & FRAME_MIRROR_MASK;
        if (bits & FRAME_MIRROR_ALWAYS || (anim & ANIM_MIRROR && bits & FRAME_MIRROR_BY_ANIM))
            pic.flip = 1;
    }
    if (!view_has_creature_picture(type, entry)) {
        int saved = pic.flip;
        pic.flip = 0;
        entry = (uint8_t)(GFX_CREATURE_SIDE + side);
        if (!view_has_creature_picture(type, entry)) {
            int other = (uint8_t)(GFX_CREATURE_SIDE + dir_opposite(side));
            if (side & 1 && view_has_creature_picture(type, other)) {
                pic.flip = 1;
                pic.mirrored = true;
                entry = other;
            } else {
                entry = GFX_CREATURE_ANY_SIDE;
                pic.flip = saved;
            }
        }
        if (pic.flip != saved)
            pic.mirrored = true;
    }
    pic.entry = entry;
    return pic;
}

void view_show_creature(int pos, int depth, uint16_t group)
{
    const Gdat *g = gdat_current();
    int type = group_creature(group);
    uint8_t frame[CREATURE_FRAME_SIZE];
    creature_anim_frame(g, group, frame);
    /* a vanishing creature keeps the cell, size, and shift of the frame where it stopped */
    uint8_t still[CREATURE_FRAME_SIZE];
    memcpy(still, frame, sizeof still);
    ActiveGroup *active = creature_active(group);
    if (active && active->action == ACTION_VANISH)
        creature_anim_frame_of(g, type, (uint16_t)active->counters[0], (uint16_t)active->counters[1], still);
    int anim = active ? active->anim_bits : 0;
    int side = creature_info(g, type)->flags & CREATURE_FACES_PARTY
                   ? DIR_SOUTH
                   : dir_relative(g_view_state.party_dir, group_dir(group));
    SidePicture pic = pick_side_picture(type, frame, side, anim);
    int cell = still[FRAME_CELL], scale;
    if (pos == POS_FRONT && g_view_arrows_enabled) {
        /* by push arrow: the cell and scale of the creature that the party pushes */
        cell = arrow_cells[g_party.push_arrow];
        scale = arrow_scales[g_party.push_arrow];
        side = 0;
    } else {
        uint32_t size;
        const uint8_t *sizes =
            gdat_item(g, gdat_find(g, CAT_CREATURE, type, GDAT_TYPE_CREATURE_DATA, GFX_CREATURE_SIZES), &size);
        unsigned at = still[FRAME_SIZE] * DIR_COUNT + side;
        scale = gfx_scale_64(sizes && at < size ? sizes[at] : 0,
                             g_view_depth_scale[depth]); /* fix: no read past the table */
    }
    int zone = zone_id_for_view_cell(pos, cell, side) | ZONE_RELATIVE;
    int shift = (int8_t)still[FRAME_SHIFT];
    int dx = 0;
    int dy = 0;
    if (shift) {
        int sx = 0;
        int sy = 0;
        if (side == DIR_NORTH)
            sy = -CREATURE_SHIFT_Y;
        else if (side == DIR_SOUTH)
            sy = CREATURE_SHIFT_Y;
        else
            sx = pic.mirrored ? CREATURE_SHIFT_X : -CREATURE_SHIFT_X;
        dx = gfx_scale_64((int16_t)sx, (int16_t)shift);
        dy = gfx_scale_64((int16_t)sy, (int16_t)shift);
    }
    view_setup_picture(
        &(PicturePlace){.flip = pic.flip,
                        .scale_x = scale,
                        .scale_y = scale,
                        .dx = jitter_offset[anim & ANIM_JITTER] + dx,
                        .dy = jitter_offset[anim >> ANIM_JITTER_Y_SHIFT & ANIM_JITTER] + dy,
                        .depth = depth,
                        .zone = zone,
                        .anchor = ZONE_NO_ANCHOR,
                        .key = (int16_t)gdat_entry_index(g, CAT_CREATURE, type, GDAT_TYPE_VALUE, CREATURE_KEY_COLOR),
                        .key2 = NO_KEY},
        CAT_CREATURE, type, pic.entry);
}

/* Draws the possessions of a static creature in the cells of cell_mask; the other cells wait for
   view_show_pending_possessions. */
static void show_possessions(uint16_t first, int pos, uint32_t cell_mask, uint16_t group)
{
    if (first == THING_ENDOFLIST || !(MISSILE_POSITIONS & VIEW_POS_BIT(pos)))
        return;
    g_view_state.pending.first = THING_ENDOFLIST;
    if (view_depth_of(pos) == 0)
        return;
    const uint8_t *cells = possession_cells[view_lateral_of(pos) > 0];
    uint32_t missing = 0;
    for (int k = 0; k < DIR_COUNT; k++) {
        uint32_t bit = 1u << cells[k];
        if (cell_mask & bit)
            show_cell_objects(first, pos, view_position_to_cell(cells[k]), group);
        else
            missing |= bit;
    }
    if (missing) {
        g_view_state.pending.first = first;
        g_view_state.pending.group = group;
        g_view_state.pending.cells = missing;
    }
}

bool view_show_creatures(uint16_t thing, int pos, uint32_t cell_mask)
{
    bool drawn = false;
    int depth = view_depth_of(pos);
    if (g_view_state.walking && pos == POS_PARTY)
        return false;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (thing_type(thing) != THING_GROUP)
            continue;
        const CreatureInfo *info = creature_info(gdat_current(), group_creature(thing));
        if (cell_mask & THINGS_STATIC_ONLY && !(info->flags & CREATURE_STATIC))
            continue;
        drawn = true;
        view_show_creature(pos, depth, thing);
        view_flush_picture();
        if (!(info->flags & CREATURE_STATIC)) {
            if (!view_between_steps())
                group_set_mood_flag(thing, GROUP_SEEN, true);
        } else if (info->possessions & POSSESSIONS_KIND_MASK && depth <= DEPTH_FAR) {
            if (g_view_squares[pos].aspect == ASPECT_DOOR_FRONT)
                cell_mask = THINGS_NEAR; /* as in the original */
            show_possessions(thing_word(thing, GROUP_POSSESSIONS), pos, cell_mask, thing);
        }
    }
    return drawn;
}

void view_show_pending_possessions(int pos)
{
    if (g_view_state.pending.first != THING_ENDOFLIST)
        show_possessions(g_view_state.pending.first, pos, g_view_state.pending.cells, g_view_state.pending.group);
    g_view_state.pending.first = THING_ENDOFLIST;
}

enum MissileKind view_missile_kind(uint16_t thing)
{
    int category = item_gdat_category(thing);
    int index = item_gdat_index(thing);
    if (!view_picture_exists(category, index, GFX_MISSILE_PICTURE_FRONT))
        return MISSILE_NO_PICTURE;
    if (!view_picture_exists(category, index, GFX_MISSILE_PICTURE_SIDE))
        return MISSILE_NO_SIDE;
    if (view_picture_exists(category, index, GFX_MISSILE_PICTURE_BACK))
        return MISSILE_FRONT_BACK;
    if (view_picture_exists(category, index, GFX_MISSILE_PICTURE_FRONT_2))
        return MISSILE_MIRRORED;
    return MISSILE_FRONT;
}

/* The picture of a missile of picture kind in cell (0 to 3, relative to the party) of the square at pos, and its
   mirror: by its flight direction and the parity of its square. */
static ThingPicture missile_picture(uint16_t thing, enum MissileKind kind, int16_t cell, int pos, bool spell)
{
    int dir = projectile_dir(thing);
    int lateral = view_lateral_of(pos);
    int square_parity = (g_view_squares[pos].x + g_view_squares[pos].y) & 1;
    ThingPicture pic = {.flip = 0};
    if ((dir & 1) != (g_view_state.party_dir & 1)) { /* the missile flies across the view */
        pic.entry = GFX_MISSILE_PICTURE_SIDE;
        if (kind == MISSILE_MIRRORED) {
            if (cell == CELL_FRONT_LEFT || cell == CELL_BACK_LEFT)
                pic.flip |= MIRROR_H;
            if (square_parity)
                pic.flip |= MIRROR_V;
            else
                pic.flip ^= MIRROR_H;
        } else if (dir_turn(g_view_state.party_dir, TURN_RIGHT) == dir) {
            pic.flip = MIRROR_H;
        }
        return pic;
    }
    if (kind == MISSILE_MIRRORED) {
        if (square_parity)
            pic.flip = MIRROR_V;
        pic.entry =
            (cell < CELL_BACK_RIGHT) == (square_parity != 0) ? GFX_MISSILE_PICTURE_FRONT : GFX_MISSILE_PICTURE_FRONT_2;
    } else if (kind == MISSILE_FRONT || (kind == MISSILE_FRONT_BACK && dir != g_view_state.party_dir)) {
        pic.entry = GFX_MISSILE_PICTURE_FRONT;
    } else {
        pic.entry = GFX_MISSILE_PICTURE_BACK;
    }
    if (lateral < 0 || (lateral == 0 && cell != CELL_FRONT_RIGHT && cell != CELL_BACK_RIGHT))
        pic.flip |= MIRROR_H;
    if (cell & 1 && spell)
        pic.flip |= MIRROR_V;
    return pic;
}

/* Draws the missiles of a thing list at grid cell position of the square at pos. */
static void show_missiles(uint16_t thing, int pos, int position)
{
    int cell = view_position_to_cell(position);
    if (thing == THING_ENDOFLIST || cell < 0)
        return;
    int depth = view_depth_of(pos);
    if ((depth == 0 && cell >= CELL_BACK_RIGHT) || !(MISSILE_POSITIONS & VIEW_POS_BIT(pos)))
        return;
    for (; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing)) {
        if (view_get_thing_position(thing, g_view_state.party_dir) != position || thing_type(thing) != THING_PROJECTILE)
            continue;
        int zone = zone_id_for_view_cell(pos, position, 0);
        enum MissileKind kind = view_missile_kind(thing);
        uint16_t payload = thing_word(thing, PROJECTILE_PAYLOAD);
        if (kind == MISSILE_NO_PICTURE) {
            uint16_t object = thing_with_cell(payload, thing_cell(thing));
            view_show_object(
                object, pos,
                &(ObjectLook){
                    .dy = MISSILE_DY, .spot = -1, .group = THING_NONE, .record_click = true, .projectile = thing});
            continue;
        }
        int category = item_gdat_category(thing);
        int index = item_gdat_index(thing);
        bool spell = category == CAT_EXPLOSION;
        int power = projectile_energy(thing);
        int scale = SCALE_FULL;
        int offset_scale = SCALE_FULL;
        if ((spell && power != POWER_MAX) || depth != 0) {
            int i = depth * 2 - (cell >> 1);
            if (i < 0)
                continue;
            offset_scale = missile_scale[i];
            scale = offset_scale;
            if (spell) {
                if (payload == MISSILE_HALF_POWER_SPELL)
                    power = (power >> 1) + MISSILE_HALF_POWER_BASE;
                scale = gfx_scale_64((int16_t)view_rounded_scale(power, POWER_MAX), (int16_t)offset_scale);
                if (scale < MISSILE_SCALE_MIN)
                    scale = MISSILE_SCALE_MIN;
            }
        }
        ThingPicture pic = {.entry = GFX_MISSILE_PICTURE_FRONT, .flip = 0};
        if (kind != MISSILE_NO_SIDE)
            pic = missile_picture(thing, kind, cell, pos, spell);
        int flips =
            spell ? gdat_entry_index(gdat_current(), CAT_EXPLOSION, index, GDAT_TYPE_VALUE, EXPLOSION_KEY_MISSILE_FLIP)
                  : MIRROR_HV;
        view_setup_picture(&(PicturePlace){.flip = flips & pic.flip,
                                           .scale_x = scale,
                                           .scale_y = scale,
                                           .depth = depth,
                                           .zone = zone | ZONE_RELATIVE,
                                           .key = THING_KEY,
                                           .key2 = NO_KEY},
                           category, index, pic.entry);
        g_view_state.ctx.src_y = (int16_t)(g_view_state.ctx.src_y + gfx_scale_64(MISSILE_DY, (int16_t)offset_scale));
        flush_projectile(thing, pos);
    }
}

/* Starts the picture of an explosion of power size into zone, mirrored by flip. */
static void setup_explosion_picture(int type, int size, int flip, int zone)
{
    int scale = (int16_t)(size & ~1);
    scale = scale < EXPLOSION_SCALE_MIN   ? EXPLOSION_SCALE_MIN
            : scale > EXPLOSION_SCALE_MAX ? EXPLOSION_SCALE_MAX
                                          : scale;
    DrawCtx *c = draw_ctx_init(&g_view_state.ctx, CAT_EXPLOSION, type, GFX_EXPLOSION_PICTURE);
    c->src_x = (int16_t)(c->src_x + c->dx);
    c->src_y = (int16_t)(c->src_y + c->dy);
    c->dx = 0;
    c->dy = 0;
    c->zone = zone;
    c->anchor = 0;
    c->transparent = THING_KEY;
    c->dst = &g_view_viewport;
    c->flip = (int16_t)flip;
    c->scale_y = (int16_t)scale;
    c->scale_x = c->scale_y;
    pal_apply_dim_remap(c->palette, g_view_state.shade, THING_KEY, NO_KEY, &c->palette_count);
    draw_ctx_prepare(c);
}

/* Adds 1 to a global byte variable of the game, up to GLOBAL_BYTE_MAX. */
static void global_byte_add(int var)
{
    if (!view_between_steps())
        game_global_byte_add(var);
}

/* Tints the whole view with an explosion on the party square: a random part of its tint picture, tiled. */
static void show_explosion_tint(int type, int power)
{
    int level = power >> EXPLOSION_TINT_LEVEL_SHIFT;
    int entry =
        GFX_EXPLOSION_TINT + (level ? (level > EXPLOSION_TINT_LEVELS ? TINT_ENTRY_STRONG : TINT_ENTRY_WEAK) : 0);
    const Bitmap *tint = draw_gdat_image(CAT_EXPLOSION, type, entry);
    if (!tint)
        return;
    int area = tint->width * tint->height;
    const uint8_t *palette = view_get_shaded_palette(CAT_EXPLOSION, type, entry, 0);
    int16_t rect[RECT_SIZE];
    int offset = (int16_t)rand_below((uint16_t)(area - TILE_TRIM));
    int width = (int16_t)(area - TILE_TRIM + (rand_next() & RANDOM_TILE_SPREAD));
    if (width <= offset)
        return; /* fix: the tiled blit of a tile row not longer than its start never ends */
    TileFill fill = {.tile = tint->pixels, .tile_width = width, .tile_offset = offset, .colormap = palette};
    gfx_blit_tiled_4to8(&fill, g_view_viewport.pixels, VIEWPORT_WIDTH, zone_get_rect(ZONE_DIALOG_BACK, rect));
}

/* Moves the picture of a strong explosion up, and sideways when it is not centered: it grows and rises. order is
   the place of its cell in the explosion order. */
static void raise_explosion(int power, bool near, int depth, bool centered, int order)
{
    int16_t grow = (int16_t)(power - EXPLOSION_POWER_BIG);
    if (grow > 0) {
        grow = (int16_t)(EXPLOSION_GROW_BASE - grow);
        if (near && view_depth_of(POS_PARTY) == depth)
            grow = EXPLOSION_GROW_NEAR;
        g_view_state.ctx.src_y = (int16_t)(g_view_state.ctx.src_y + VIEWPORT_HEIGHT * EXPLOSION_RISE_FACTOR / grow);
        if (!centered) {
            int16_t spread = (int16_t)(VIEWPORT_WIDTH * EXPLOSION_RISE_FACTOR / (grow - EXPLOSION_SPREAD_MIN));
            bool right = order == EXPLOSION_ORDER_FAR_RIGHT || order == EXPLOSION_ORDER_NEAR_RIGHT;
            g_view_state.ctx.src_x = (int16_t)(g_view_state.ctx.src_x + (right ? -spread : spread));
        }
    }
}

/* Draws the explosion thing at the square at pos, when its cell is the cell at order in the explosion order and in
   cell_mask; a sparkle explosion draws its sparkle field at the last order. */
static void show_explosion(uint16_t thing, int pos, int order, uint32_t cell_mask)
{
    int order_cell = explosion_cell_order[g_view_state.party_dir][order];
    int depth = view_depth_of(pos);
    int type = explosion_type(thing);
    bool centered = explosion_is_centered(thing);
    if (view_picture_exists(CAT_EXPLOSION, type, GFX_SPARKLE_PICTURE)) {
        if (order == EXPLOSION_ORDER_LAST)
            view_show_sparkle_field(pos, CAT_EXPLOSION, type);
        return;
    }
    if (order_cell != (centered ? EXPLOSION_CELL_SQUARE : thing_cell(thing)))
        return;
    int flip =
        rand_2bits() & gdat_entry_index(gdat_current(), CAT_EXPLOSION, type, GDAT_TYPE_VALUE, EXPLOSION_KEY_FLIP);
    int power = gdat_entry_index(gdat_current(), CAT_EXPLOSION, type, GDAT_TYPE_VALUE, EXPLOSION_KEY_FIXED_POWER)
                    ? POWER_MAX
                    : explosion_power(thing);
    bool near = pos == POS_PARTY;
    if (near) {
        if (type != EXPLOSION_GLOBAL_COUNTER) {
            show_explosion_tint(type, power);
            return;
        }
        global_byte_add(GLOBAL_EXPLOSIONS_NEAR);
    } else {
        if (type == EXPLOSION_GLOBAL_COUNTER)
            global_byte_add(GLOBAL_EXPLOSIONS_AWAY);
        if (depth == 0 && order >= EXPLOSION_ORDER_NEAR_LEFT)
            return; /* the near cells of the party square are below the view */
    }
    int position = centered ? CELL_CENTER : g_view_cell_view_position[thing_cell(thing)];
    int turned = aspect_rotate_cell(position, g_view_state.party_dir);
    if (!(cell_mask & 1u << turned))
        return;
    int scale = thing_scale[depth * GRID_SQUARE_CELLS + EXPLOSION_SCALE_START - turned / CELL_GRID_SIDE];
    /* as in the original, the party direction, not g_view_state.party_dir, which show_linked_squares turns */
    int zone = zone_id_for_view_cell(pos, position, g_game.party_dir) | ZONE_RELATIVE;
    int strength = power + 1 > EXPLOSION_POWER_SIZE ? power + 1 : EXPLOSION_POWER_SIZE;
    setup_explosion_picture(type, (int16_t)(scale * strength >> EXPLOSION_SIZE_SHIFT), flip, zone);
    raise_explosion(power, near, depth, centered, order);
    g_view_state.ctx.src_x = gfx_scale_64(g_view_state.ctx.src_x, (int16_t)scale);
    g_view_state.ctx.src_y = gfx_scale_64(g_view_state.ctx.src_y, (int16_t)scale);
    view_flush_picture();
    if (near)
        show_explosion_tint(type, power);
}

void view_show_explosions(uint16_t first, int pos, uint32_t cell_mask)
{
    if (first == THING_ENDOFLIST || !(EXPLOSION_POSITIONS & VIEW_POS_BIT(pos)))
        return;
    for (int k = 0; k < EXPLOSION_ORDER_COUNT; k++)
        for (uint16_t thing = first; thing != THING_ENDOFLIST; thing = dungeon_next_thing(thing))
            if (thing_type(thing) == THING_EXPLOSION)
                show_explosion(thing, pos, k, cell_mask);
}

void view_show_square_things(int pos, uint32_t cell_mask, bool with_explosions)
{
    if (pos >= POS_FAR_ROW || !g_view_square_things_shown[pos])
        return;
    const ViewSquare *s = g_view_squares + pos;
    uint16_t first = s->first_thing;
    const uint8_t *order = thing_cell_order[view_lateral_of(pos) < 0    ? ORDER_LEFT
                                            : view_lateral_of(pos) == 0 ? ORDER_AXIS
                                                                        : ORDER_RIGHT];
    const uint8_t *corner = g_view_square_grid_corner[pos];
    int last = pos == POS_PARTY ? PARTY_CELL_LAST : CELL_GRID_COUNT - 1;
    for (int i = 0; i <= last; i++) {
        int cell = order[i];
        uint32_t bit = cell_mask & 1u << cell;
        if (!bit)
            continue;
        if (bit & g_view_object_masks[pos])
            show_cell_objects(first, pos, view_position_to_cell(cell), THING_NONE);
        int grid =
            (corner[0] + cell % CELL_GRID_SIDE) * GRID_STRIDE + corner[1] + GRID_SQUARE_CELLS - cell / CELL_GRID_SIDE;
        uint8_t owner = g_view_object_grid[grid];
        if ((s->aspect == ASPECT_WALL || g_view_creature_grid[grid] == pos) && owner != NO_POSITION) {
            uint32_t mask = cell == CELL_CENTER ? cell_mask : cell_mask & ~THINGS_STATIC_ONLY;
            if (view_show_creatures(g_view_squares[owner].first_thing, owner, mask)) {
                g_view_object_grid[grid] = NO_POSITION;
                /* the missiles of the cells before, which the creature covered */
                if (g_view_missile_masks[pos] && i >= CREATURE_COVERED_CELLS)
                    for (int j = i - CREATURE_COVERED_CELLS; j < i; j++)
                        if (g_view_missile_masks[pos] & 1u << order[j])
                            show_missiles(first, pos, order[j]);
            }
        }
        if (bit & g_view_missile_masks[pos])
            show_missiles(first, pos, cell);
    }
    if (with_explosions)
        view_show_explosions(first, pos, cell_mask);
}

void view_show_alcove_contents(int pos)
{
    int spot = ALCOVE_FIRST_SPOT;
    int depth = view_depth_of(pos);
    for (uint16_t thing = g_view_squares[pos].first_thing; thing != THING_ENDOFLIST;
         thing = dungeon_next_thing(thing)) {
        /* as in the original, the party direction, not g_view_state.party_dir, which show_linked_squares turns */
        if (thing_cell(thing) != dir_opposite(g_game.party_dir))
            continue;
        if (view_is_item(thing)) {
            view_show_object(thing, pos,
                             &(ObjectLook){.spot = spot,
                                           .group = THING_NONE,
                                           .record_click = true,
                                           .in_alcove = true,
                                           .projectile = THING_NONE});
            spot = view_next_spot(spot, true);
            if (pos == POS_FRONT)
                view_set_click_zone(VIEW_CLICK_ZONE_ALCOVE, thing, CLICK_ALCOVE, POS_FRONT);
            continue;
        }
        if (thing_type(thing) != THING_EXPLOSION || !(ALCOVE_EXPLOSION_POSITIONS & VIEW_POS_BIT(pos)))
            continue;
        int type = explosion_type(thing);
        int flip = rand_2bits();
        int zone = view_element_zone(pos, CELL_CENTER, false);
        if (type == EXPLOSION_ALCOVE_STILL) {
            int scale = alcove_explosion_scale[depth];
            view_setup_picture(&(PicturePlace){.flip = flip,
                                               .scale_x = scale,
                                               .scale_y = scale,
                                               .zone = zone,
                                               .anchor = ZONE_NO_ANCHOR,
                                               .key = THING_KEY,
                                               .key2 = NO_KEY},
                               CAT_EXPLOSION, type, GFX_ALCOVE_STILL_PICTURE);
        } else if (type == EXPLOSION_ALCOVE_SCALED) {
            setup_explosion_picture(type, alcove_explosion_size[depth], flip, zone);
        } else {
            continue;
        }
        view_flush_picture();
    }
}
