/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The walls, floors, doors, and sky of the dungeon view. */
#include <stdlib.h>
#include <string.h>
#include "game_text.h"
#include "item.h"
#include "money.h"
#include "sensor.h"
#include "ui.h"
#include "view_internal.h"
#include "weather.h"

/* The position on the other side of the view axis. */
static const int8_t view_mirror[VIEW_SQUARE_COUNT] = {
    0, 2, 1, 3, 5, 4, 6, 8, 7, 10, 9, 11, 13, 12, 15, 14, 16, 18, 17, 20, 19, 22, 21,
};
/* The center position of each depth. */
static const uint8_t depth_center[POS_DEPTH_MAX + 1] = {0, 3, 6, 11, 16};
/* Floor ornament picture entries by position, and the entries for ornaments without their own pictures. */
static const int8_t floor_ornament_entry[VIEW_SQUARE_COUNT] = {
    0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9,
};
static const uint8_t floor_ornament_scaled_entry[VIEW_SQUARE_COUNT] = {
    2, 3, 3, 2, 3, 3, 2, 3, 3, 3, 3, 2, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2,
};
/* Ceiling pits (holes of pits on the level above) and floor pits. */
static const int16_t ceiling_pit_zone[POS_CEILING_PIT_LAST + 1] = {871, 870, 872, 868, 867, 869, 865, 864, 866};
static const uint8_t ceiling_pit_entry[POS_CEILING_PIT_LAST + 1] = {153, 154, 154, 156, 157, 157, 159, 160, 160};
static const uint8_t ceiling_pit_flip[POS_CEILING_PIT_LAST + 1] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
static const int16_t floor_pit_zone[POS_FAR_ROW] = {
    862, 861, 863, 859, 858, 860, 856, 855, 857, -1, -1, 853, 852, 854, 850, 851,
};
static const uint8_t floor_pit_flip[POS_FAR_ROW] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1};
static const uint8_t floor_pit_entry_up[POS_FAR_ROW] = {
    130, 131, 131, 133, 134, 134, 136, 137, 137, ENTRY_NONE, ENTRY_NONE, 118, 119, 119, 121, 121,
};
static const uint8_t floor_pit_entry[POS_FAR_ROW] = {
    107, 108, 108, 110, 111, 111, 113, 114, 114, ENTRY_NONE, ENTRY_NONE, 118, 119, 119, 121, 121,
};
/* Doors. */
static const int8_t door_ornament_row[DOOR_POSITIONS] = {4, -1, -1, 3, -1, -1, 2, -1, -1, -1, -1, 1, -1, 0};
static const uint8_t door_side_frame[DOOR_POSITIONS][2] = {
    {211,        212       },
    {ENTRY_NONE, ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {7,          8         },
    {ENTRY_NONE, 213       },
    {214,        ENTRY_NONE},
    {9,          10        },
    {ENTRY_NONE, 215       },
    {216,        ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {11,         12        },
    {13,         14        },
    {15,         16        },
};
/* Door top frames by position. */
static const int8_t door_top_frame[DOOR_TOP_POSITIONS] = {-1, -1, -1, 18, 19, 20, 21, 22, 23};
static const int16_t door_top_zone[DOOR_TOP_POSITIONS] = {0, 0, 0, 754, 753, 755, 751, 750, 752};
static const int8_t door_depth_row[POS_DEPTH_MAX + 1] = {3, 2, 1, 0, -1};
static const int16_t door_zone[POS_FAR_ROW] = {
    3810, -1, -1, 3790, 3780, 3800, 3760, 3750, 3770, -1, -1, 3730, 3720, 3740, 3700, 3710,
};
/* The frame parts (DOOR_FRAME_*) of a door seen from the front, drawn before and after the door. */
static const uint8_t door_front_frames[POS_FAR_ROW][2] = {
    {DOOR_FRAME_SIDES, 0               },
    {0,                0               },
    {0,                0               },
    {DOOR_FRAME_ALL,   0               },
    {DOOR_FRAME_TOP,   DOOR_FRAME_RIGHT},
    {DOOR_FRAME_TOP,   DOOR_FRAME_LEFT },
    {DOOR_FRAME_ALL,   0               },
    {DOOR_FRAME_TOP,   DOOR_FRAME_RIGHT},
    {DOOR_FRAME_TOP,   DOOR_FRAME_LEFT },
    {0,                0               },
    {0,                0               },
    {DOOR_FRAME_SIDES, 0               },
    {DOOR_FRAME_LEFT,  DOOR_FRAME_RIGHT},
    {DOOR_FRAME_RIGHT, DOOR_FRAME_LEFT },
    {0,                0               },
    {0,                0               },
};
/* Stairs seen from the side and from the front, by position and up (1) or down (0). */
static const uint8_t stairs_side_entry[POS_FAR_ROW][2] = {
    {ENTRY_NONE, ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {79,         59        },
    {80,         60        },
    {81,         61        },
    {82,         62        },
    {83,         63        },
    {84,         64        },
    {ENTRY_NONE, ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {85,         65        },
    {86,         66        },
    {87,         67        },
    {88,         68        },
    {89,         69        },
};
static const uint8_t stairs_side_flipped_entry[POS_FAR_ROW][2] = {
    {ENTRY_NONE, ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {79,         59        },
    {80,         60        },
    {80,         60        },
    {82,         62        },
    {83,         63        },
    {83,         63        },
    {ENTRY_NONE, ENTRY_NONE},
    {ENTRY_NONE, ENTRY_NONE},
    {85,         65        },
    {86,         66        },
    {86,         66        },
    {88,         68        },
    {88,         68        },
};
static const int16_t stairs_side_zone[POS_FAR_ROW][2] = {
    {-1,  -1 },
    {-1,  -1 },
    {-1,  -1 },
    {822, 809},
    {821, 808},
    {823, 810},
    {819, 806},
    {818, 805},
    {820, 807},
    {-1,  -1 },
    {-1,  -1 },
    {816, 803},
    {815, 802},
    {817, 804},
    {800, 800},
    {801, 801},
};
static const uint8_t stairs_front_entry[POS_CEILING_PIT_LAST + 1][2] = {
    {ENTRY_NONE, ENTRY_NONE},
    {205,        199       },
    {206,        200       },
    {ENTRY_NONE, ENTRY_NONE},
    {207,        201       },
    {208,        202       },
    {ENTRY_NONE, ENTRY_NONE},
    {209,        203       },
    {210,        204       },
};
static const int16_t stairs_front_zone[POS_CEILING_PIT_LAST + 1][2] = {
    {-1,  -1 },
    {832, 832},
    {833, 833},
    {-1,  -1 },
    {830, 828},
    {831, 829},
    {-1,  -1 },
    {826, 826},
    {827, 827},
};
/* ---- Walls ---- */

/* The glyph of a wall text character in the wall text font: a letter, a period, or a space. */
static uint8_t wall_text_glyph(uint8_t c)
{
    if (c >= 'A' && c <= 'Z')
        return (uint8_t)(c - 'A');
    return c == '.' ? GLYPH_PERIOD : GLYPH_SPACE;
}

/* The target square of the sensor record. */
static Location record_target(uint16_t record)
{
    return (Location){sensor_target_x(record), sensor_target_y(record)};
}

/* True when the target square of the sensor record holds an item of the item index type: in the possessions of a
   creature, or loose when loose is set. */
static bool target_has_item(uint16_t record, int type, bool loose)
{
    Location at = record_target(record);
    for (uint16_t t = aspect_first_thing(g_view_state.map, at.x, at.y); t != THING_ENDOFLIST;
         t = dungeon_next_thing(t)) {
        int kind = thing_type(t);
        if (loose && view_is_item(t)) {
            if (item_get_index(t) == type)
                return true;
        } else if (kind == THING_GROUP) {
            for (uint16_t p = thing_word(t, GROUP_POSSESSIONS); p != THING_ENDOFLIST; p = dungeon_next_thing(p))
                if (item_get_index(p) == type)
                    return true;
        }
    }
    return false;
}

/* Draws text centered at x with its base line at y in the view, in the view shade. */
static void show_centered_label(int16_t x, int16_t y, const char *text)
{
    int16_t count = UI_COLOR_COUNT;
    ScreenSize size = text_measure(text);
    if (!size.w)
        return;
    uint8_t colors[UI_COLOR_COUNT];
    memcpy(colors, g_ui_colors, sizeof colors);
    pal_apply_dim_remap(colors, g_view_state.shade, NO_KEY, NO_KEY, &count);
    text_draw_with_shadow(&g_view_viewport, x - (size.w >> 1), y,
                          (TextColors){colors[UI_WHITE], colors[UI_BLACK] | TEXT_TRANSPARENT}, text);
}

/* The price table of a shop ornament and its word count in *count; NULL when the ornament has none or it holds no
   word. */
static const uint8_t *price_table(uint8_t ornament, int *count)
{
    int item = gdat_find(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_TABLE, GFX_ORN_SHOP_TABLE);
    uint32_t size = 0;
    const uint8_t *data = item < 0 ? NULL : gdat_item(gdat_current(), item, &size);
    *count = (int)(size / 2);
    return *count ? data : NULL; /* fix: the original reads before an empty table */
}

/* The place of item in the price table of count words; count when the table does not hold it. */
static int price_index(const uint8_t *table, int count, uint16_t item)
{
    int at = 0;
    while (at < count && (le16(table + 2 * at) & ITEM_INDEX_NONE) != item)
        at++;
    return at;
}

/* Chooses the item that the display shows: the next or the last item of the price list that the merchant sells. */
static void choose_shop_item(uint16_t shop, uint16_t display, uint8_t ornament, int creature)
{
    bool forward = sensor_flag(shop, SHOP_STEP_FORWARD);
    uint16_t item = sensor_data(display);
    uint8_t set[ITEM_CLASS_SET_SIZE];
    int list = sensor_delay(shop); /* the delay bits of a shop: its price list */
    if (money_parse_item_class_set(creature, list * SHOP_CLASS_TEXT_STEP + SHOP_CLASS_TEXT, set, false)) {
        int count;
        const uint8_t *table = price_table(ornament, &count);
        int at = table ? price_index(table, count, item) : item;
        /* fix: the original loops for ever when the merchant sells no item of the list */
        for (int tries = 0; tries <= ITEM_INDEX_NONE; tries++) {
            at += forward || g_view_hit_offset_y < 0 ? 1 : -1;
            if (table) {
                if (at < 0)
                    at = count - 1;
                else if (at >= count)
                    at = 0;
                item = le16(table + 2 * at) & ITEM_INDEX_NONE;
            } else {
                at &= ITEM_INDEX_NONE;
                item = (uint16_t)at;
            }
            if (!(set[item / BYTE_BIT_COUNT] & 1 << (item % BYTE_BIT_COUNT)))
                continue;
            if (gdat_entry_index(gdat_current(), item_index_gdat_category(item), item_index_gdat_index(item),
                                 GDAT_TYPE_VALUE, ATTR_FLAGS) &
                    ITEM_FLAG_UNIQUE &&
                !target_has_item(shop, item, true) && !target_has_item(display, item, false) &&
                !sensor_party_possesses_type(item))
                continue;
            break;
        }
        sensor_set_flag(shop, SHOP_STEP_FORWARD, false);
    }
    sensor_set_flag(shop, SHOP_SHOWN, true);
    sensor_set_flag(display, SHOP_SHOWN, true);
    sensor_set_data(display, item & ITEM_INDEX_NONE);
}

/* Draws amount as coins at the offset of key from zone. */
static void show_shop_price(int16_t amount, uint8_t ornament, const PicturePlace *p, int offset_key)
{
    uint16_t offset = gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, offset_key);
    ScreenSize size = {gdat_offset_x(offset), gdat_offset_y(offset)};
    uint8_t coins[SHOP_PRICE_COINS];
    if (!amount)
        return;
    int16_t count = money_split_amount(amount, SHOP_PRICE_COINS, coins).count;
    if (count <= 0)
        return;
    Bitmap *strip = draw_temp_bitmap(count * COIN_WIDTH, COIN_HEIGHT, IMAGE_4BPP);
    if (!strip)
        return;
    view_setup_picture(&(PicturePlace){.scale_x = SCALE_FULL,
                                       .scale_y = SCALE_FULL,
                                       .depth = p->depth,
                                       .zone = ZONE_NONE,
                                       .anchor = p->anchor,
                                       .key = NO_KEY,
                                       .key2 = NO_KEY},
                       CAT_WALL_ORNAMENT, ornament, GFX_ORN_SHOP_COINS);
    if (!g_view_state.ctx.image)
        return;
    int16_t rect[RECT_SIZE] = {0, 0, COIN_WIDTH, COIN_HEIGHT};
    for (int i = 0; i < count; i++, rect[RECT_X] += COIN_WIDTH)
        ui_blit_bitmap(
            &(BlitSource){.bitmap = g_view_state.ctx.image, .x = coins[i] * COIN_WIDTH, .transparent = NO_KEY}, strip,
            rect);
    if (zone_compute_anchored_rect(strip, rect, (AnchoredZone){p->zone | ZONE_RELATIVE, p->anchor}, &size))
        ui_blit_bitmap(&(BlitSource){.bitmap = strip, .transparent = p->key, .colormap = g_view_state.ctx.palette},
                       &g_view_viewport, rect);
}

/* Draws the name and the sell and buy prices of the shop item in front of the party. */
static void show_shop_label(uint16_t shop, uint16_t item, int creature, uint8_t ornament, const PicturePlace *p)
{
    int category = item_index_gdat_category(item);
    int index = item_index_gdat_index(item);
    ScreenPoint label = zone_get_position(p->zone);
    uint16_t place =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, ORN_KEY_SHOP_LABEL);
    char raw[TEXT_SIZE], name[TEXT_SIZE];
    show_centered_label(label.x + gdat_offset_x(place), label.y + gdat_offset_y(place),
                        gtext_format(gtext_gdat(category, index, GFX_ITEM_NAME, raw), name));
    int list = sensor_delay(shop);
    PriceTerms buying = {creature, list, true, true};
    PriceTerms selling = {creature, list, false, true};
    int buy = money_get_item_value(item, &buying, -1);
    int sell = money_get_item_value(item, &selling, -1);
    show_shop_price(sell, ornament, p, ORN_KEY_SHOP_SELL);
    show_shop_price(buy, ornament, p, ORN_KEY_SHOP_BUY);
}

/* Draws the shop display of the wall sensor shop with the shop ornament: the item, its name and prices in front. */
static void show_shop_display(uint16_t shop, uint8_t ornament, int pos, const PicturePlace *p)
{
    Location target = record_target(shop);
    uint16_t display = THING_ENDOFLIST;
    for (uint16_t t = aspect_first_thing(g_view_state.map, target.x, target.y);
         t != THING_ENDOFLIST && thing_type(t) <= THING_SENSOR; t = dungeon_next_thing(t))
        if (thing_type(t) == THING_SENSOR && sensor_type(t) == SENSOR_SHOP) {
            display = t;
            break;
        }
    if (display == THING_ENDOFLIST)
        return; /* fix: the original reads a NULL record */
    int creature = sensor_data(shop) & BYTE_MASK;
    /* a view between game steps shows the item of the last view */
    if (!view_between_steps() && (!sensor_flag(shop, SHOP_SHOWN) || sensor_flag(shop, SHOP_STEP_FORWARD)))
        choose_shop_item(shop, display, ornament, creature);
    uint16_t item = sensor_data(display);
    int category = item_index_gdat_category(item);
    int index = item_index_gdat_index(item);
    uint16_t offset =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, ORN_KEY_ALCOVE_ITEMS);
    PicturePlace item_place = *p;
    item_place.flip = 0;
    item_place.dx = gdat_offset_x(offset);
    item_place.dy = gdat_offset_y(offset);
    item_place.key2 = NO_KEY;
    view_setup_picture(&item_place, category, index, 0);
    view_flush_picture();
    if (!target_has_item(shop, item, true) && !target_has_item(display, item, false)) {
        int count;
        const uint8_t *table = price_table(ornament, &count);
        int at = table ? price_index(table, count, item) : 0;
        uint16_t made = !view_between_steps() && table && at < count && le16(table + 2 * at) & SHOP_TABLE_CREATE
                            ? item_create_by_index(item)
                            : THING_NONE;
        if (made != THING_NONE) {
            dungeon_link_thing_to_square(made, g_view_state.map, target.x, target.y);
        } else {
            view_setup_picture(&item_place, CAT_WALL_ORNAMENT, ornament, GFX_ORN_SHOP_EMPTY);
            view_flush_picture();
        }
    }
    PicturePlace overlay = item_place;
    overlay.dx = 0;
    overlay.dy = 0;
    view_setup_picture(&overlay, CAT_WALL_ORNAMENT, ornament, GFX_ORN_OVERLAY);
    view_flush_picture();
    if (pos != DISPLAY_POS)
        return;
    show_shop_label(shop, item, creature, ornament, p);
}

/* Blits the lines of a wall text with the map font glyph, each line centered in width, from the row of r. */
static void blit_wall_text(Blit *glyph, const char *text, int16_t *r, int width, int height)
{
    for (const char *p = text;;) {
        int n = 0;
        while (p[n] && p[n] != '\n')
            n++;
        if (n && r[RECT_Y] >= 0 && r[RECT_Y] + r[RECT_H] <= height) { /* fix: skip a line outside the plate */
            r[RECT_X] = (int16_t)((width >> 1) - ((int16_t)(GLYPH_SIZE * n) >> 1));
            for (int i = 0; i < n && r[RECT_X] >= 0; i++, r[RECT_X] += GLYPH_SIZE) {
                glyph->src_x = wall_text_glyph((uint8_t)p[i]) * GLYPH_SIZE;
                gfx_blit(glyph, r);
            }
        }
        p += n;
        r[RECT_Y] = (int16_t)(r[RECT_Y] + GLYPH_SIZE + WALL_TEXT_LINE_GAP);
        if (!*p++)
            break;
    }
}

/* Draws the text of a front wall plate: the text goes into a bitmap of the plate size, centered, then into the view. */
static void show_wall_text(int pos, int entry, const PicturePlace *place)
{
    DrawCtx plate;
    draw_ctx_init(&plate, CAT_WALLSET, g_view_state.gfx_set, entry);
    const Bitmap *plate_image = draw_gdat_image(CAT_WALLSET, g_view_state.gfx_set, entry);
    const Bitmap *font = draw_gdat_image(CAT_WALLSET, g_view_state.gfx_set, GFX_MAP_FONT);
    if (!plate_image || !font)
        return;
    char text[WALL_TEXT_SIZE];
    gtext_decode_raw(text, sizeof text, g_view_squares[pos].text, false, false);
    int lines = 1;
    for (const char *p = text; *p; p++)
        lines += *p == '\n';
    int line_height = GLYPH_SIZE + WALL_TEXT_LINE_GAP;
    Bitmap *bitmap = draw_temp_bitmap(plate_image->width, plate_image->height, IMAGE_4BPP);
    if (!bitmap)
        return;
    gfx_fill_bitmap(bitmap, place->key);
    const uint8_t *font_palette = draw_gdat_image_palette(CAT_WALLSET, g_view_state.gfx_set, GFX_MAP_FONT);
    int16_t r[RECT_SIZE] = {0, (int16_t)((plate_image->height >> 1) - (lines * line_height >> 1)), GLYPH_SIZE,
                            font->height};
    Blit glyph = {.src = font->pixels,
                  .dst = bitmap->pixels,
                  .src_width = font->width,
                  .dst_width = bitmap->width,
                  .src_bpp = font->bpp,
                  .dst_bpp = bitmap->bpp,
                  .transparent = place->key,
                  .mirror = MIRROR_NONE,
                  .colormap = font_palette};
    blit_wall_text(&glyph, text, r, plate_image->width, plate_image->height);
    DrawCtx c;
    draw_ctx_init(&c, CAT_NONE, 0, 0);
    c.item = -1;
    draw_ctx_set_image(&c, bitmap);
    c.scale_x = (int16_t)place->scale_x;
    c.scale_y = (int16_t)place->scale_y;
    if (font_palette)
        memcpy(c.palette, font_palette, IMAGE_COLORS);
    c.palette_count = IMAGE_COLORS;
    view_get_depth_shading(c.palette, place->depth, place->key, NO_KEY, &c.palette_count);
    c.dst = &g_view_viewport;
    c.zone = place->zone;
    c.anchor = (int16_t)place->anchor;
    c.src_x = plate.dx;
    c.src_y = plate.dy;
    c.transparent = (int16_t)place->key;
    draw_ctx_prepare(&c);
    draw_ctx_draw(&c);
}

/* Draws the champion portrait of a portrait ornament on the front wall at pos; a click on the front wall finds it. */
static void show_wall_portrait(int pos, uint8_t ornament, const PicturePlace *p, bool record_click)
{
    uint16_t offset =
        gdat_entry_index(gdat_current(), CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_VALUE2, PORTRAIT_OFFSET_ENTRY);
    view_setup_picture(&(PicturePlace){.scale_x = p->scale_y,
                                       .scale_y = p->scale_y,
                                       .dx = gdat_offset_x(offset),
                                       .dy = gdat_offset_y(offset),
                                       .depth = p->depth,
                                       .zone = p->zone,
                                       .anchor = p->anchor,
                                       .key = NO_KEY,
                                       .key2 = NO_KEY},
                       CAT_CHAMPION, (uint8_t)g_view_squares[pos].text, GFX_PORTRAIT);
    view_flush_picture_or_measure(record_click);
    if (record_click && p->depth == DEPTH_FRONT)
        view_add_click_zone(g_view_state.ctx.rect, CLICK_WALL_ORNAMENT, pos);
}

/* Draws a text plate on the wall face at side of the wall at pos, and the wall text on a front plate. */
static void show_wall_plate(int pos, int side, const PicturePlace *p, bool record_click)
{
    int entry = GFX_MAP_PLATE_FRONT;
    int flip = 0;
    if (side < 0)
        entry = GFX_MAP_PLATE_SIDE;
    else if (side > 0 && view_picture_exists(CAT_WALLSET, g_view_state.gfx_set, GFX_MAP_PLATE_RIGHT))
        entry = GFX_MAP_PLATE_RIGHT;
    else if (side > 0) {
        entry = GFX_MAP_PLATE_SIDE;
        flip = 1;
    }
    view_setup_picture(&(PicturePlace){.flip = flip,
                                       .scale_x = p->scale_x,
                                       .scale_y = p->scale_y,
                                       .depth = p->depth,
                                       .zone = p->zone,
                                       .anchor = p->anchor,
                                       .key = p->key,
                                       .key2 = NO_KEY},
                       CAT_WALLSET, g_view_state.gfx_set, entry);
    view_flush_picture_or_measure(record_click);
    if (side == 0 && g_view_squares[pos].text != THING_TEXT_NONE)
        show_wall_text(pos, entry, p);
}

/* Draws a wet wall ornament: its background picture, then the ornament with the rain over it. */
static void show_wet_wall_ornament(uint8_t ornament, int entry, const PicturePlace *p, bool record_click,
                                   int16_t click_rect[RECT_SIZE])
{
    view_setup_picture(&(PicturePlace){.flip = p->flip,
                                       .scale_x = p->scale_x,
                                       .scale_y = p->scale_y,
                                       .depth = p->depth,
                                       .zone = p->zone,
                                       .anchor = p->anchor,
                                       .key = NO_KEY,
                                       .key2 = NO_KEY,
                                       .mode = DRAW_NO_IMAGE},
                       CAT_WALL_ORNAMENT, ornament, entry);
    view_setup_picture(&(PicturePlace){.flip = p->flip,
                                       .scale_x = p->scale_x,
                                       .scale_y = p->scale_y,
                                       .dx = g_view_state.ctx.dx,
                                       .dy = g_view_state.ctx.dy,
                                       .depth = p->depth,
                                       .zone = p->zone,
                                       .anchor = p->anchor,
                                       .key = p->key,
                                       .key2 = NO_KEY},
                       CAT_WALL_ORNAMENT, ornament, GFX_ORN_WET_BACKGROUND);
    view_flush_picture_or_measure(record_click);
    DrawCtx c;
    Bitmap *offscreen = view_render_offscreen(
        &c,
        &(PicturePlace){.flip = p->flip,
                        .scale_x = p->scale_x,
                        .scale_y = p->scale_y,
                        .depth = p->depth,
                        .key = p->key,
                        .key2 = (int16_t)aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_OFFSCREEN_COLOR)},
        CAT_WALL_ORNAMENT, ornament, entry);
    if (offscreen) {
        view_draw_wet_image(&c, offscreen, p->key);
        draw_ctx_draw(&c);
        view_show_offscreen(&c, offscreen, p->zone, p->anchor, view_mapped_key(&c, p->key));
    }
    memcpy(click_rect, c.rect, sizeof c.rect); /* the wet picture gives the click zone */
}

/* Draws what a front wall ornament of kind holds: the shop display, the overlay picture, or the items in an alcove. */
static void show_ornament_contents(int pos, uint8_t ornament, int kind, const PicturePlace *p, bool record_click)
{
    g_view_state.alcove = g_view_state.ctx;
    if (view_picture_exists(CAT_WALL_ORNAMENT, ornament, GFX_ORN_OVERLAY)) {
        if (kind == KIND_SHOP) {
            show_shop_display(g_view_squares[pos].text, ornament, pos, p);
        } else {
            view_setup_picture(&(PicturePlace){.flip = p->flip,
                                               .scale_x = p->scale_x,
                                               .scale_y = p->scale_y,
                                               .depth = p->depth,
                                               .zone = p->zone,
                                               .anchor = p->anchor,
                                               .key = p->key,
                                               .key2 = NO_KEY},
                               CAT_WALL_ORNAMENT, ornament, GFX_ORN_OVERLAY);
            view_flush_picture_or_measure(record_click);
        }
    } else if (kind == KIND_ALCOVE) {
        view_show_alcove_contents(pos);
    }
}

/* The picture of the wall ornament on the face at side (0 front, < 0 left, > 0 right) in frame: a right face without
   a picture of its own shows the side picture mirrored. */
static ThingPicture wall_ornament_picture(uint8_t ornament, int side, int frame)
{
    ThingPicture pic = {.entry = ORN_PICTURE_SIDE, .flip = 0};
    if (side == 0) {
        pic.entry = ORN_PICTURE_FRONT; /* front ornaments are not mirrored: the depth flip of the original is off */
    } else if (side > 0) {
        if (view_picture_exists(CAT_WALL_ORNAMENT, ornament, (uint8_t)(frame + ORN_PICTURE_RIGHT)))
            pic.entry = ORN_PICTURE_RIGHT;
        else
            pic.flip = 1;
    }
    pic.entry = (uint8_t)(pic.entry + frame);
    return pic;
}

int view_show_wall_ornament(int pos, int side, bool record_click)
{
    /* the side words of the left, front, right faces */
    uint16_t w = g_view_squares[pos].side[side < 0 ? SIDE_FRONT - 1 : side > 0 ? SIDE_FRONT + 1 : SIDE_FRONT];
    int frame = view_face_frame_entry(w);
    int depth = view_depth_of(pos);
    uint8_t ornament = aspect_word_ornament(w);
    if (ornament == ORNAMENT_NONE)
        return NO_KEY;
    bool plate = ornament == ORNAMENT_PLATE;
    int kind = aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_KIND);
    int key = plate ? 0 : aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_COLOR);
    if (!key)
        key = g_view_state.wall_key;
    uint16_t place = plate ? 0 : aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_PLACE);
    int anchor = place ? VIEW_HIGH_BYTE(place) : 0;
    int cell = place ? VIEW_LOW_BYTE(place) - 1 : CELL_CENTER;
    PicturePlace p = {.scale_x = g_view_depth_scale[depth],
                      .scale_y = g_view_depth_scale[depth],
                      .depth = depth,
                      .zone = view_element_zone(pos, cell, side != 0),
                      .anchor = anchor,
                      .key = key,
                      .key2 = NO_KEY};
    if (kind == KIND_PORTRAIT && side == 0) {
        show_wall_portrait(pos, ornament, &p, record_click);
        if (!record_click)
            return key;
    }
    if (abs(side) >= LATERAL_FAR_SIDE) {
        if (depth == DEPTH_MIDDLE)
            p.scale_x = SCALE_WIDE_SIDE;
        else if (depth == DEPTH_FAR)
            p.scale_x = SCALE_FAR_SIDE;
    }
    if (plate) {
        show_wall_plate(pos, side, &p, record_click);
        return key;
    }
    int16_t click_rect[RECT_SIZE] = {0};
    ThingPicture pic = wall_ornament_picture(ornament, side, frame);
    int entry = pic.entry;
    p.flip = pic.flip;
    /* as in the original, the hit test (record_click false) of a wet ornament draws it and its contents */
    if (side == 0 && aspect_value_key(CAT_WALL_ORNAMENT, ornament, ORN_KEY_WET)) {
        show_wet_wall_ornament(ornament, entry, &p, record_click, click_rect);
    } else {
        view_setup_picture(&p, CAT_WALL_ORNAMENT, ornament, entry);
        view_flush_picture_or_measure(record_click);
        if (!record_click)
            return key;
        memcpy(click_rect, g_view_state.ctx.rect, sizeof click_rect);
    }
    if (record_click && kind != KIND_PORTRAIT && pos >= POS_NEAR_SIDE_1 && pos <= POS_FRONT)
        view_add_click_zone(click_rect, CLICK_WALL_ORNAMENT, pos);
    if (kind && side == 0)
        show_ornament_contents(pos, ornament, kind, &p, record_click);
    return key;
}

/* Draws the wall block at pos with the clip margins that open squares beside it need. */
static void show_wall_block(int pos)
{
    int lateral = view_lateral_of(pos);
    int depth = view_depth_of(pos);
    uint16_t margins =
        aspect_value_key(CAT_WALLSET, g_view_state.gfx_set, MAP_KEY_WALL_MARGINS + (depth > DEPTH_FRONT));
    int left = 0;
    int right = 0;
    if (depth > DEPTH_FRONT) {
        if (g_view_squares[POS_FRONT_LEFT].aspect == ASPECT_WALL)
            left = VIEW_HIGH_BYTE(margins);
        if (g_view_squares[POS_FRONT_RIGHT].aspect == ASPECT_WALL)
            right = VIEW_LOW_BYTE(margins);
    } else if (depth > 0) {
        if (g_view_squares[POS_NEAR_SIDE_1].aspect == ASPECT_WALL)
            left = VIEW_HIGH_BYTE(margins);
        if (g_view_squares[POS_NEAR_SIDE_2].aspect == ASPECT_WALL)
            right = VIEW_LOW_BYTE(margins);
    }
    if (left || right)
        zone_set_clip_margins(left, 0, right, 0);
    int flip = lateral > 0, entry;
    if (pos >= POS_FAR_ROW) {
        if (abs(lateral) == LATERAL_FAR_SIDE)
            flip = 0;
        flip ^= g_view_state.flip;
        entry = GFX_MAP_FAR_WALL;
    } else if (g_view_state.flip) {
        entry = (uint8_t)(view_mirror[pos] + GFX_MAP_WALL_FLIPPED);
        if (!view_picture_exists(CAT_WALLSET, g_view_state.gfx_set, entry)) {
            if (lateral == 0)
                flip = 1;
            entry -= MAP_WALL_FLIPPED_MISSING;
        }
    } else {
        entry = pos + GFX_MAP_WALL;
    }
    view_setup_picture(&(PicturePlace){.flip = flip,
                                       .scale_x = SCALE_FULL,
                                       .scale_y = SCALE_FULL,
                                       .depth = g_view_state.walking ? -depth : 0,
                                       .zone = ZONE_WALL + pos,
                                       .anchor = ZONE_NO_ANCHOR,
                                       .key = g_view_state.wall_key,
                                       .key2 = NO_KEY},
                       CAT_WALLSET, g_view_state.gfx_set, entry);
    view_flush_picture();
    g_zone_clip_rect[RECT_W] = 0;
}

void view_show_wall_square(int pos)
{
    show_wall_block(pos);
    switch (g_view_wall_faces[pos]) {
    case WALL_SHOWS_FRONT:
        view_show_wall_ornament(pos, 0, true);
        break;
    case WALL_SHOWS_BOTH:
        view_show_wall_ornament(pos, 0, true);
        __attribute__((fallthrough));
    case WALL_SHOWS_SIDE:
        view_show_wall_ornament(pos, view_lateral_of(pos), true);
        break;
    }
}

/* ---- Floors, pits, stairs ---- */

/* Draws a floor ornament through an offscreen bitmap: wet with the rain, or over a passage (cleared). */
static void show_floor_ornament_offscreen(uint8_t ornament, int entry, const PicturePlace *p, bool wet)
{
    DrawCtx c;
    Bitmap *offscreen = view_render_offscreen(
        &c,
        &(PicturePlace){.flip = p->flip,
                        .scale_x = p->scale_x,
                        .scale_y = p->scale_y,
                        .depth = p->depth,
                        .key = p->key,
                        .key2 = (int16_t)aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_OFFSCREEN_COLOR)},
        CAT_FLOOR_ORNAMENT, ornament, entry);
    if (!offscreen)
        return;
    int transparent = view_mapped_key(&c, p->key);
    if (wet)
        view_draw_wet_image(&c, offscreen, p->key);
    else
        gfx_fill_bitmap(offscreen, transparent);
    draw_ctx_draw(&c);
    view_show_offscreen(&c, offscreen, p->zone, p->anchor, transparent);
}

/* Draws the things of the squares behind the seamless passage link at pos and depth, on the linked map. */
static void show_linked_squares(const TeleporterLink *link, int pos, int depth)
{
    /* the squares behind the passage, on the linked map */
    int16_t saved_dir = g_view_state.party_dir;
    int16_t saved_map = g_view_state.map;
    g_view_state.party_dir = link->target_side;
    g_view_state.map = link->map;
    Location at = view_step_relative((Location){link->x, link->y}, g_view_state.party_dir, -depth, 0);
    for (int d = POS_DEPTH_MAX; d >= depth; d--)
        view_load_square(at, depth_center[d], g_view_state.party_dir);
    for (int d = POS_DEPTH_MAX; d > depth; d--)
        view_show_square_things(depth_center[d], THINGS_ALL, true);
    g_view_state.map = saved_map;
    g_view_state.party_dir = saved_dir;
    view_show_square_things(pos, THINGS_NEAR, true);
}

/* Where and how the floor ornament shows on the square at pos; scaled: it shows the scaled picture of an ornament
   without a picture of its own there. */
static PicturePlace floor_ornament_place(int pos, uint8_t ornament, bool scaled)
{
    int depth = view_depth_of(pos);
    int flip = 0;
    if (view_lateral_of(pos) == 0) {
        if (!aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NO_FLIP))
            flip = g_view_state.flip ^ !(depth & 1);
    } else if (view_lateral_of(pos) >= 1) {
        flip = 1;
    }
    int key = aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_COLOR);
    if (!key)
        key = g_view_state.wall_key;
    int cell = CELL_CENTER;
    int anchor = 0;
    uint16_t place = aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_PLACE);
    if (place) {
        anchor = VIEW_HIGH_BYTE(place);
        cell = VIEW_LOW_BYTE(place) - 1;
    }
    int zone = zone_id_for_view_cell(pos, cell, 0);
    int scale = scaled ? g_view_depth_scale[depth] : SCALE_FULL;
    if ((pos == POS_NEAR_SIDE_1 || pos == POS_NEAR_SIDE_2) &&
        aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_BIG_NEAR))
        scale = SCALE_BIG_NEAR;
    PicturePlace p = {.flip = flip,
                      .scale_x = scale,
                      .scale_y = scale,
                      .depth = depth,
                      .zone = zone,
                      .anchor = anchor,
                      .key = key,
                      .key2 = NO_KEY};
    return p;
}

bool view_show_floor_ornament(int pos)
{
    int entry = floor_ornament_entry[pos];
    if (entry < 0)
        return false;
    ViewSquare *s = g_view_squares + pos;
    int depth = view_depth_of(pos);
    TeleporterLink link;
    bool linked = dungeon_teleporter_link(g_view_state.map, s->x, s->y, &link);
    if (depth > DEPTH_FAR && !linked)
        return false;
    /* as in the original, the party direction, not g_view_state.party_dir, which show_linked_squares turns */
    if (linked && pos != POS_NEAR_SIDE_1 && pos != POS_NEAR_SIDE_2 && link.side != g_game.party_dir &&
        link.side != dir_opposite(g_game.party_dir))
        return false;
    uint16_t w = s->side[SIDE_FLOOR_ORNAMENT];
    int frame = VIEW_HIGH_BYTE(w);
    entry = (uint8_t)(entry + frame);
    uint8_t ornament = aspect_word_ornament(w);
    if (ornament == ORNAMENT_NONE || ornament == 0)
        return false;
    if ((pos == POS_FAR_SIDE_1 || pos == POS_FAR_SIDE_2) &&
        aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_NOT_FAR_SIDE))
        return false;
    bool scaled = !view_picture_exists(CAT_FLOOR_ORNAMENT, ornament, entry);
    if (scaled)
        entry = (uint8_t)(floor_ornament_scaled_entry[pos] + frame);
    PicturePlace p = floor_ornament_place(pos, ornament, scaled);
    bool wet = depth && aspect_value_key(CAT_FLOOR_ORNAMENT, ornament, ORN_KEY_WET);
    if (!depth || (!linked && !wet)) {
        view_setup_picture(&p, CAT_FLOOR_ORNAMENT, ornament, entry);
        view_flush_picture();
        return false;
    }
    PicturePlace no_image = p;
    no_image.key = NO_KEY;
    no_image.key2 = NO_KEY;
    no_image.mode = DRAW_NO_IMAGE;
    view_setup_picture(&no_image, CAT_FLOOR_ORNAMENT, ornament, entry);
    if (view_lateral_of(pos) == 0) {
        PicturePlace background = p;
        background.dx = g_view_state.ctx.dx;
        background.dy = g_view_state.ctx.dy;
        view_setup_picture(&background, CAT_FLOOR_ORNAMENT, ornament, (uint8_t)(entry + GFX_ORN_WET_BACKGROUND));
        view_flush_picture();
        show_linked_squares(&link, pos, depth);
    }
    show_floor_ornament_offscreen(ornament, entry, &p, wet);
    return linked;
}

void view_show_ceiling_pit(int pos)
{
    if (pos > POS_CEILING_PIT_LAST || !(g_view_state.flip_flags & FLIP_CEILING_PITS))
        return;
    Position above =
        dungeon_location_on_map((Position){g_view_state.map, g_view_squares[pos].x, g_view_squares[pos].y}, LEVEL_UP);
    if (above.map < 0)
        return;
    /* fix: the original reads the square without a bounds check; outside the map it is a wall */
    uint8_t square = dungeon_square_edge(above.map, above.x, above.y);
    if (square_type(square) != SQUARE_PIT || !square_flag(square, SQUARE_PIT_OPEN))
        return;
    int flip = pos == POS_PARTY ? view_get_flip_parity(PARITY_CEILING, g_view_state.party_dir,
                                                       (Location){g_view_state.party_x, g_view_state.party_y})
                                : ceiling_pit_flip[pos];
    view_show_map_picture(ceiling_pit_entry[pos], ceiling_pit_zone[pos], g_view_state.wall_key, flip);
}

void view_show_floor_pit(int pos)
{
    int16_t zone = floor_pit_zone[pos];
    if (zone < 0)
        return;
    bool up = g_view_squares[pos].side[SIDE_PIT_UP] != 0;
    if (pos >= POS_FLOOR_PIT_BLOCKED && up)
        return;
    int flip = pos == POS_PARTY ? view_get_flip_parity(PARITY_FLOOR, g_view_state.party_dir,
                                                       (Location){g_view_state.party_x, g_view_state.party_y})
                                : floor_pit_flip[pos];
    view_show_map_picture(up ? floor_pit_entry_up[pos] : floor_pit_entry[pos], zone, g_view_state.wall_key, flip);
}

void view_show_stairs_side(int pos)
{
    bool up = g_view_squares[pos].side[SIDE_STAIRS_UP] != 0;
    int16_t zone = stairs_side_zone[pos][up];
    if (zone < 0)
        return;
    uint8_t entry = stairs_side_entry[pos][up];
    if (view_picture_exists(CAT_WALLSET, g_view_state.gfx_set, entry)) {
        view_show_map_picture(entry, zone, g_view_state.wall_key, 0);
        return;
    }
    view_setup_picture(&(PicturePlace){.flip = 1,
                                       .scale_x = SCALE_FULL,
                                       .scale_y = SCALE_FULL,
                                       .zone = zone,
                                       .anchor = ZONE_NO_ANCHOR,
                                       .key = g_view_state.wall_key,
                                       .key2 = NO_KEY},
                       CAT_WALLSET, g_view_state.gfx_set, stairs_side_flipped_entry[pos][up]);
    view_flush_picture();
}

void view_show_stairs_front(int pos)
{
    if (pos > POS_CEILING_PIT_LAST)
        return;
    bool up = g_view_squares[pos].side[SIDE_STAIRS_UP] != 0;
    uint8_t entry = stairs_front_entry[pos][up];
    if (entry != ENTRY_NONE)
        view_show_map_picture(entry, stairs_front_zone[pos][up], g_view_state.wall_key, 0);
}

/* ---- Doors ---- */

/* The scale (1/64) that makes scaled pixels at least size pixels, rounded. */
static int inverse_scale(int16_t size, int16_t scaled)
{
    if (scaled <= 0)
        return SCALE_FULL; /* fix: the original divides by the 0 width of a damaged picture */
    int scale = (size * 2 * SCALE_FULL + SCALE_FULL) / scaled >> 1;
    return gfx_scale_64(scaled, (int16_t)scale) < size ? scale + 1 : scale;
}

/* Draws the ornament or the button of the door at pos; the doors near the party take clicks on it. */
static void show_door_ornament(int category, int ornament, int entry, int pos)
{
    int row = door_ornament_row[pos];
    if (row < 0 || ornament == ORNAMENT_NONE)
        return;
    int depth = view_depth_of(pos);
    int scale = g_view_depth_scale[depth];
    int base = 0;
    int key;
    if (category == CAT_DOOR_BUTTON) {
        base = aspect_value_key(CAT_DOOR_BUTTON, ornament, ORN_KEY_ZONE_ROW) * DOOR_BUTTON_FRAMES;
        key = g_view_state.wall_key;
    } else {
        key = (int16_t)aspect_value_key(category, ornament, ORN_KEY_COLOR);
        if (!key)
            key = g_view_state.wall_key;
    }
    view_setup_picture(&(PicturePlace){.scale_x = scale,
                                       .scale_y = scale,
                                       .depth = depth,
                                       .zone = base + ZONE_DOOR_ORNAMENT + row,
                                       .anchor = ZONE_NO_ANCHOR,
                                       .key = key,
                                       .key2 = NO_KEY},
                       category, ornament, entry);
    view_flush_picture();
    if (row >= DOOR_ROW_NEAR)
        view_add_click_zone(g_view_state.ctx.rect, CLICK_DOOR_BUTTON, pos);
}

/* Draws the parts (DOOR_FRAME_*) of the frame of the door at pos, with the button or the ornament on its right side. */
static void show_door_frame(int pos, int parts)
{
    uint16_t door = g_view_squares[pos].side[SIDE_DOOR_THING];
    if (aspect_value_key(CAT_DOOR, view_map_door_type(door), DOOR_KEY_NO_FRAME))
        return;
    if (parts & DOOR_FRAME_TOP && pos < DOOR_TOP_POSITIONS && door_top_frame[pos] >= 0)
        view_show_map_picture(door_top_frame[pos], door_top_zone[pos], g_view_state.wall_key, 0);
    int mpos = g_view_state.flip ? view_mirror[pos] : pos;
    int left = g_view_state.flip ? 1 : 0;
    uint8_t entry = door_side_frame[mpos][left];
    if (parts & DOOR_FRAME_LEFT && entry != ENTRY_NONE) {
        view_setup_picture(&(PicturePlace){.scale_x = SCALE_FULL,
                                           .scale_y = SCALE_FULL,
                                           .zone = zone_id_for_view_cell(pos, CELL_LEFT_MIDDLE, 0),
                                           .anchor = ANCHOR_DOOR_RIGHT,
                                           .key = g_view_state.wall_key,
                                           .key2 = NO_KEY},
                           CAT_WALLSET, g_view_state.gfx_set, entry);
        view_flush_picture();
    }
    entry = door_side_frame[mpos][!left];
    if (!(parts & DOOR_FRAME_RIGHT) || entry == ENTRY_NONE)
        return;
    view_setup_picture(&(PicturePlace){.flip = 1,
                                       .scale_x = SCALE_FULL,
                                       .scale_y = SCALE_FULL,
                                       .zone = zone_id_for_view_cell(pos, CELL_RIGHT_MIDDLE, 0),
                                       .anchor = ANCHOR_DOOR_LEFT,
                                       .key = g_view_state.wall_key,
                                       .key2 = NO_KEY},
                       CAT_WALLSET, g_view_state.gfx_set, entry);
    view_flush_picture();
    if (door_flag(door, DOOR_BUTTON)) {
        show_door_ornament(CAT_DOOR_BUTTON, 0, door_flag(door, DOOR_BUTTON_PRESSED) * DOOR_BUTTON_FRAMES, pos);
    } else if (g_view_squares[pos].side[SIDE_FLOOR_ORNAMENT] != ORNAMENT_NONE) {
        uint16_t w = g_view_squares[pos].side[SIDE_FLOOR_ORNAMENT];
        show_door_ornament(CAT_WALL_ORNAMENT, aspect_word_ornament(w), (uint8_t)(view_face_frame_entry(w) + 1), pos);
    }
}

/* Puts the door picture d together offscreen with its ornament and its damage, and makes it the view picture. */
static void compose_door(const DoorPicture *d)
{
    /* the door with its ornament or damage is put together offscreen */
    DrawCtx c;
    Bitmap *offscreen = view_render_offscreen(
        &c,
        &(PicturePlace){
            .scale_x = d->scale, .scale_y = d->scale, .depth = d->shade_depth, .key = d->key, .key2 = NO_KEY},
        CAT_DOOR, d->type, d->entry);
    if (!offscreen)
        return;
    draw_ctx_draw(&c);
    int scale_x = d->scale;
    int scale_y = d->scale;
    const Bitmap *near = d->scale == SCALE_FULL ? draw_gdat_image(CAT_DOOR, d->type, 0) : NULL;
    if (near) {
        scale_x = inverse_scale(g_view_state.ctx.w, near->width);
        scale_y = inverse_scale(g_view_state.ctx.h, near->height);
    }
    if (d->ornament) {
        uint8_t o = dungeon_map_door_ornaments(g_view_state.party_map)[d->ornament - 1];
        int okey = (int16_t)aspect_value_key(CAT_DOOR_ORNAMENT, o, ORN_KEY_COLOR);
        if (!okey)
            okey = ORNAMENT_KEY_DEFAULT;
        int ozone =
            aspect_value_key(CAT_DOOR_ORNAMENT, o, ORN_KEY_ZONE_ROW) * ZONE_ROW_SIZE + ZONE_ORNAMENT_ROWS + d->row;
        view_setup_picture(&(PicturePlace){.scale_x = scale_x,
                                           .scale_y = scale_y,
                                           .depth = d->depth,
                                           .zone = ozone,
                                           .anchor = ZONE_NO_ANCHOR,
                                           .key = okey,
                                           .key2 = NO_KEY},
                           CAT_DOOR_ORNAMENT, o, 0);
        g_view_state.ctx.dst = offscreen;
        view_flush_picture();
    }
    if (d->destroyed) {
        int dkey = (int16_t)aspect_value_key(CAT_DOOR, d->type, ORN_KEY_DAMAGE_COLOR);
        if (!dkey)
            dkey = ORNAMENT_KEY_DEFAULT;
        int dzone = aspect_value_key(CAT_DOOR, d->type, ORN_KEY_KIND) * ZONE_ROW_SIZE + ZONE_ORNAMENT_ROWS + d->row;
        view_setup_picture(&(PicturePlace){.scale_x = scale_x,
                                           .scale_y = scale_y,
                                           .depth = d->depth,
                                           .zone = dzone,
                                           .anchor = ZONE_NO_ANCHOR,
                                           .key = dkey,
                                           .key2 = d->key},
                           CAT_DOOR, d->type, GFX_DOOR_DAMAGE);
        g_view_state.ctx.dst = offscreen;
        view_flush_picture();
    }
    c.transparent = (int16_t)view_mapped_key(&c, d->key);
    c.palette_count = 0;
    draw_ctx_set_image(&c, offscreen);
    g_view_state.ctx = c;
    g_view_state.ctx.dst = &g_view_viewport;
}

/* Starts the picture of the closed or moving door at depth (zone row row) in state: put together with its ornament
   or its damage when it has one. */
static void setup_door_picture(uint16_t door, int state, int depth, int row)
{
    uint8_t type = view_map_door_type(door);
    int key = (int16_t)aspect_value_key(CAT_DOOR, type, ORN_KEY_COLOR);
    if (!key)
        key = DOOR_KEY_DEFAULT;
    int entry = depth - 1;
    int scale = SCALE_FULL;
    int shade_depth = 0;
    if (depth == 0 || !view_picture_exists(CAT_DOOR, type, entry)) {
        if (depth) {
            shade_depth = depth;
            scale = g_view_depth_scale[depth];
        } else {
            scale = DOOR_NEAR_SCALE;
        }
        entry = 0;
    }
    int ornament = door_ornament(door);
    if (!ornament && state != DOOR_STATE_BROKEN) {
        view_setup_picture(&(PicturePlace){.scale_x = scale,
                                           .scale_y = scale,
                                           .depth = shade_depth,
                                           .zone = ZONE_NONE,
                                           .anchor = ZONE_NO_ANCHOR,
                                           .key = key,
                                           .key2 = NO_KEY},
                           CAT_DOOR, type, entry);
    } else {
        compose_door(&(DoorPicture){.type = type,
                                    .ornament = ornament,
                                    .destroyed = state == DOOR_STATE_BROKEN,
                                    .entry = entry,
                                    .scale = scale,
                                    .shade_depth = shade_depth,
                                    .depth = depth,
                                    .row = row,
                                    .key = key});
    }
}

/* Draws the door at pos between its frame parts, with the things of the cells of cell_mask behind it. */
static void show_door(int pos, int frame_before, int frame_after, uint32_t cell_mask)
{
    if (frame_before)
        show_door_frame(pos, frame_before);
    if (cell_mask)
        view_show_square_things(pos, cell_mask, false);
    ViewSquare *s = g_view_squares + pos;
    int state = s->side[SIDE_DOOR_STATE];
    int depth = view_depth_of(pos);
    int row = depth <= DEPTH_FAR ? door_depth_row[depth] : -1;
    int zone = door_zone[pos];
    if (state != DOOR_STATE_OPEN && depth <= DEPTH_FAR && row >= 0 && zone >= 0) {
        uint16_t door = s->side[SIDE_DOOR_THING];
        setup_door_picture(door, state, depth, row);
        if (state < DOOR_STATE_CLOSED) {
            zone += state;
            if (!door_flag(door, DOOR_VERTICAL)) {
                /* the two halves slide apart: the right half first */
                int16_t half = (int16_t)(g_view_state.ctx.w >> 1);
                g_view_state.ctx.w = half;
                g_view_state.ctx.x = (int16_t)(g_view_state.ctx.x + half);
                g_view_state.ctx.zone = zone + DOOR_HALF_OFFSET;
                view_flush_picture();
                g_view_state.ctx.x = (int16_t)(g_view_state.ctx.x - half);
                zone += DOOR_LEFT_OFFSET;
            }
        }
        g_view_state.ctx.zone = zone;
        view_flush_picture();
    }
    if (frame_after)
        show_door_frame(pos, frame_after);
}

void view_show_door_front(int pos)
{
    if (!g_view_door_front_shown[pos])
        return;
    uint32_t mask = g_view_squares[pos].side[SIDE_DOOR_STATE] == DOOR_STATE_BROKEN ? 0 : THINGS_BEHIND_DOOR;
    view_show_square_things(pos, THINGS_NEAR, pos != POS_PARTY);
    if (pos == POS_PARTY) {
        if (mask)
            view_show_square_things(pos, THINGS_DOOR_CELL, false);
        show_door(pos, door_front_frames[pos][0], door_front_frames[pos][1], 0);
        return;
    }
    if (pos != POS_NEAR_SIDE_1 && pos != POS_NEAR_SIDE_2 && pos != POS_DEPTH2_FAR_LEFT && pos != POS_DEPTH2_FAR_RIGHT)
        show_door(pos, door_front_frames[pos][0], door_front_frames[pos][1], mask);
    view_show_square_things(pos, THINGS_FAR, true);
    view_show_pending_possessions(pos);
}

/* ---- The sky of outdoor maps ---- */

/* The distance from the party to a point at offset, rounded down; 0 for a point not in front. */
static int outdoor_relative_distance(RelativeOffset offset)
{
    if (offset.forward < 1)
        return 0;
    uint16_t n = (uint16_t)(offset.lateral * offset.lateral + offset.forward * offset.forward);
    if (n <= 2)
        return 1;
    uint16_t root = n >> 1, next;
    for (;;) { /* integer square root by Newton steps */
        next = (uint16_t)((n / root + root) >> 1);
        if (next >= root)
            return root;
        root = next;
    }
}

/* Places a landmark of the sky text definition for the party; false when it is not visible. */
static bool outdoor_place_sky_object(SkyObject *o, const char *definition, int16_t dir, int16_t x, int16_t y)
{
    o->dx = 0;
    o->dy = 0;
    int move = (int16_t)gdat_text_int_field(definition, "mv");
    if (move == SKY_FIXED) {
        o->scale_x = SCALE_FULL;
        o->scale_y = SCALE_FULL;
        return true;
    }
    if (move != SKY_LANDMARK)
        return false;
    int16_t px = (int16_t)(x + dungeon_map_offset_x(g_view_state.map));
    int16_t py = (int16_t)(y + dungeon_map_offset_y(g_view_state.map));
    int lx = (int16_t)gdat_text_int_field(definition, "xl");
    int ly = (int16_t)gdat_text_int_field(definition, "yl");
    RelativeOffset offset = view_relative_offset(dir, lx - px, ly - py);
    if (offset.forward < 1)
        return false;
    int distance = outdoor_relative_distance(offset);
    int16_t near = (int16_t)(SHADE_FULL - (distance - gdat_text_int_field(definition, "fd")));
    int scale = near > 1 ? near : 1;
    o->dx = (int16_t)(offset.lateral * SKY_LANDMARK_DISTANCE / distance);
    o->scale_y = (uint8_t)view_rounded_scale(scale, SHADE_FULL);
    o->scale_x = o->scale_y;
    return true;
}

/* Draws a sky object for the party at (x, y) looking to dir, smaller while the party walks. */
static void outdoor_draw_sky_object(const SkyObject *o, int16_t dir, int16_t x, int16_t y)
{
    if (!o->zone)
        return;
    Location at = {x, y};
    bool flip = (o->flip == SKY_FLIP_FLOOR && view_get_flip_parity(PARITY_FLOOR, dir, at)) ||
                (o->flip == SKY_FLIP_CEILING && view_get_flip_parity(PARITY_CEILING, dir, at)) ||
                (o->flip == SKY_FLIP_FLOOR_2 && view_get_flip_parity(PARITY_FLOOR, dir, at)) ||
                (o->flip == SKY_FLIP_CEILING_2 && view_get_flip_parity(PARITY_CEILING, dir, at));
    int scale_x = o->scale_x;
    int scale_y = o->scale_y;
    int16_t dx = o->dx;
    int16_t dy = o->dy;
    if (g_view_state.walking) {
        dx = gfx_scale_64(dx, WALK_SCALE);
        dy = gfx_scale_64(dy, WALK_SCALE);
        scale_x = gfx_scale_64((int16_t)scale_x, WALK_SCALE);
        scale_y = gfx_scale_64((int16_t)scale_y, WALK_SCALE);
        if (o->scale_x == SCALE_FULL) {
            dx = (int16_t)(dx + g_zone_walk_view_rect[RECT_X]);
            dy = (int16_t)(dy + (o->zone == ZONE_SKY_HIGH ? WALK_SKY_HIGH_DY : g_zone_walk_view_rect[RECT_Y]));
        } else {
            dy = (int16_t)(dy + WALK_SKY_DY);
        }
    }
    view_setup_picture(&(PicturePlace){.flip = flip,
                                       .scale_x = scale_x,
                                       .scale_y = scale_y,
                                       .zone = o->zone | ZONE_RELATIVE,
                                       .anchor = ZONE_NO_ANCHOR,
                                       .key = g_view_state.wall_key,
                                       .key2 = NO_KEY},
                       CAT_SKY, g_view_state.gfx_set, o->entry);
    g_view_state.ctx.src_x = (int16_t)(g_view_state.ctx.src_x + dx);
    g_view_state.ctx.src_y = (int16_t)(g_view_state.ctx.src_y + dy);
    view_flush_picture();
}

void view_outdoor_draw_sky(int16_t dir, int16_t x, int16_t y)
{
    if (g_game.weather_visible)
        for (const SkyObject *o = weather_sky_objects(); o->entry != SKY_OBJECT_END; o++)
            outdoor_draw_sky_object(o, dir, x, y);
    char text[WALL_TEXT_SIZE];
    if (g_view_state.landmark_set != g_view_state.gfx_set) {
        g_view_state.landmark_set = g_view_state.gfx_set;
        g_view_state.no_landmarks = true;
        memset(g_view_state.landmarks, 0, sizeof g_view_state.landmarks);
        for (int e = 0; e < SKY_LANDMARK_MAX; e++)
            if (view_picture_exists(CAT_SKY, g_view_state.gfx_set, e) &&
                *gdat_text(gdat_current(), (GdatKey){CAT_SKY, g_view_state.gfx_set, e}, text, sizeof text)) {
                g_view_state.landmarks[e / BYTE_BIT_COUNT] |= (uint8_t)(1 << (e % BYTE_BIT_COUNT));
                g_view_state.no_landmarks = false;
            }
    }
    if (g_view_state.no_landmarks)
        return;
    /* as in the original, the landmarks use the first sky object as their record */
    SkyObject *o = weather_sky_objects();
    for (int e = 0; e < SKY_LANDMARK_MAX; e++) {
        if (!(g_view_state.landmarks[e / BYTE_BIT_COUNT] & 1 << (e % BYTE_BIT_COUNT)))
            continue;
        gdat_text(gdat_current(), (GdatKey){CAT_SKY, g_view_state.gfx_set, e}, text, sizeof text);
        if (!outdoor_place_sky_object(o, text, dir, x, y))
            continue;
        o->entry = (uint8_t)e;
        o->zone = (int16_t)gdat_text_int_field(text, "cd");
        o->flip = (uint8_t)gdat_text_int_field(text, "fw");
        outdoor_draw_sky_object(o, dir, x, y);
    }
}
