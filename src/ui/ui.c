/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "action.h"
#include "action_area.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "ids.h"
#include "input.h"
#include "item.h"
#include "money.h"
#include "play.h"
#include "save.h"
#include "text.h"
#include "ui_internal.h"
#include "view.h"
#include "zone.h"

/* Frames, shadows, and offsets of the interface pictures, in pixels. */
#define BAR_SHADOW       1 /* the status bars have a shadow 1 pixel down and right */
#define GAUGE_SHADOW     2
#define GAUGE_GLYPH_LEFT 9 /* the gauge glyph stands this far left of the gauge */
#define GAUGE_MARK_INSET 3 /* the left end mark stands this far right of the gauge start */
#define GAUGE_TEXT_RAISE 3 /* the base line of the gauge glyphs above the gauge bottom */

/* The face pictures and the damage star. */
#define FACE_INVISIBLE_FRAMES 4  /* the faces of an invisible party follow the 4 normal faces */
#define FACE_SKIP_COLOR       2  /* the original skips color 2 of the face picture */
#define DAMAGE_STAR_KEY       10 /* transparent color of the damage star */

/* The bars and the values of the status boxes and of the inventory. */
#define BAR_COUNT       3
#define SCALE_PARTS     10000
#define NUMBER_DIGITS   3 /* the values of the status boxes and of the inventory have 3 digits */
#define LOAD_HIGH_PARTS 5 /* a load above 5/8 of the maximum load shows yellow */
#define LOAD_HIGH_WHOLE 8

/* The number arguments of the inventory texts: the object weight and the champion load in kg and tenths. */
#define ARG_WEIGHT_KG     0
#define ARG_WEIGHT_TENTHS 1
#define ARG_LOAD_KG       2
#define ARG_LOAD_TENTHS   3
#define ARG_LOAD_MAX_KG   4

/* Inventory buttons: bits of g_ui.buttons. */
#define BUTTON_CLOSE 0x01
#define BUTTON_EXIT  0x02
#define BUTTON_SLEEP 0x04
#define BUTTON_SAVE  0x08
#define BUTTON_SOUND 0x10
#define BUTTON_ALL   0x1f

/* Object info gauge glyphs of the font. */
#define GLYPH_CHARGES     0x71
#define GLYPH_STRENGTH    0x72
#define GLYPH_THROW       0x73
#define GLYPH_POTION      0x75
#define GLYPH_GAUGE_LEFT  0x60
#define GLYPH_GAUGE_RIGHT 0x65

/* The text of one glyph: the glyph and the end of the text. */
#define GLYPH_TEXT_SIZE 2

/* The scales of the object info gauges. */
#define WEAPON_STRENGTH_MAX 80
#define WEAPON_THROW_MAX    100
#define ARMOUR_CLASS_MAX    200

/* The object info shows up to two gauges: the charges and a value. */
#define OBJECT_GAUGE_COUNT 2

/* Masks of the armour class and of the dither color. */
#define ARMOUR_CLASS_MASK 0xff
#define DITHER_COLOR_MASK 0x0f /* the dither pattern holds 4-bit pixels */

/* The UI colors of the gauges. */
#define GAUGE_CHARGE_COLOR UI_CYAN
#define GAUGE_VALUE_COLOR  UI_BROWN
#define GAUGE_REST_COLOR   UI_DARK_GRAY /* the part of an object gauge above its value */

/* The range of the food and water gauges, and the value under which they show red. */
#define FOOD_MIN       (-1024)
#define GAUGE_STARVING (-512)

/* Description lines: longer lines break at a space up to this column. */
#define DESCRIPTION_LINE  18
#define DESCRIPTION_BREAK 17

/* Each movement arrow has a normal and a pressed picture. */
#define ARROW_PICTURES 2

/* The split color of a gauge bar without a split fill (the original passes 0). */
#define GAUGE_NO_SPLIT 0

/* The highest skill level that the skills panel names. */
#define SKILL_LEVEL_MAX 16

/* Character codes of the scroll font. */
#define SCROLL_LETTER_FIRST 0x01 /* the scroll font has its letters at 1 to 26 */
#define SCROLL_HIGH_OFFSET  0xa0 /* characters above 'z' move by this */

/* A title that starts with one of these follows the name without a space. */
#define TITLE_NO_SPACE ",;-"

/* A coin name, a space, and an int16_t count of up to 6 characters. */
#define COIN_TEXT_SIZE (TEXT_SIZE + 7)

/* The slots of the status boxes (champion * 2 + hand), of the inventory (8 + slot), of the object info,
   and of the open container: zone, icon of the empty slot, and the drawn state. */
#define SLOT_ENTRY_COUNT          55
#define SLOT_STATUS_COUNT         8
#define SLOT_INVENTORY_FIRST      8
#define SLOT_INFO                 46
#define SLOT_CONTAINER_INFO_FIRST 47
#define SLOT_FRAMED_COUNT         14 /* slots that have a frame: the hands and the inventory hands to legs */
#define SLOT_BACKGROUND_COUNT     38 /* slots that redraw their background */
#define SLOT_WOUND_COUNT          6

/* The drawn state of a slot. */
#define SLOT_ACTIVE  0x01
#define SLOT_WOUNDED 0x02

/* The empty slot icon of a slot that shows none. */
#define NO_ICON 0xff

/* The values at the ends of a gauge bar. */
typedef struct {
    int16_t low, high;
} GaugeRange;

/* A value gauge of the object info: its glyph, value, scale, and the word that the text of the panel gives it. */
typedef struct {
    uint8_t glyph;
    int value, high;
    const char *label;
} ObjectGauge;

/* The redraw of one champion: what to draw, and what it drew. */
typedef struct {
    uint16_t flags; /* REDRAW_* of what to draw */
    bool inv;       /* the champion has the open inventory */
    bool force_slots, force_inventory;
    bool present;   /* the inventory changed: the view goes to the screen */
    bool box_drawn; /* the status box changed */
} ChampionRedraw;

/* The coins of a money box: the coin type of each used coin slot, the count of each coin type, and the total value. */
typedef struct {
    int type_count;
    int types[COIN_TYPE_MAX];
    int16_t counts[COIN_TYPE_MAX];
    int total;
} BoxCoins;

/* The zone and the empty slot icon of each slot entry. */
static const struct {
    uint16_t zone;
    uint8_t empty_icon;
} slot_layout[SLOT_ENTRY_COUNT] = {
    {0xd1,  0x38   },
    {0xd2,  0x34   },
    {0xd3,  0x38   },
    {0xd4,  0x34   },
    {0xd5,  0x38   },
    {0xd6,  0x34   },
    {0xd7,  0x38   },
    {0xd8,  0x34   },
    {0x1fb, 0x38   },
    {0x1fc, 0x34   },
    {0x1fd, 0x28   },
    {0x1fe, 0x30   },
    {0x1ff, 0x3c   },
    {0x200, 0x40   },
    {0x201, NO_ICON},
    {0x202, NO_ICON},
    {0x203, NO_ICON},
    {0x204, NO_ICON},
    {0x205, 0x2c   },
    {0x206, 0x44   },
    {0x207, 0x45   },
    {0x208, 0x46   },
    {0x209, NO_ICON},
    {0x20a, NO_ICON},
    {0x20b, NO_ICON},
    {0x20c, NO_ICON},
    {0x20d, NO_ICON},
    {0x20e, NO_ICON},
    {0x20f, NO_ICON},
    {0x210, NO_ICON},
    {0x211, NO_ICON},
    {0x212, NO_ICON},
    {0x213, NO_ICON},
    {0x214, NO_ICON},
    {0x215, NO_ICON},
    {0x216, NO_ICON},
    {0x217, NO_ICON},
    {0x218, NO_ICON},
    {0xe5,  NO_ICON},
    {0xe6,  NO_ICON},
    {0xe7,  NO_ICON},
    {0xe8,  NO_ICON},
    {0xe9,  NO_ICON},
    {0xea,  NO_ICON},
    {0xeb,  NO_ICON},
    {0xec,  NO_ICON},
    {0x1f9, NO_ICON},
    {0x219, NO_ICON},
    {0x21a, NO_ICON},
    {0x21b, NO_ICON},
    {0x21c, NO_ICON},
    {0x21d, NO_ICON},
    {0x21e, NO_ICON},
    {0x21f, NO_ICON},
    {0x220, NO_ICON}
};
/* The UI color of the status bars of each champion. */
static const uint8_t champion_colors[CHAMPION_MAX] = {UI_GREEN, UI_YELLOW, UI_RED, UI_BLUE};
/* The text entry (7, 0, entry) of each skill level 0 to 16. */
static const uint8_t skill_level_texts[SKILL_LEVEL_MAX + 1] = {0x01, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                                                               0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12};

bool g_inv_arrows_on_close = true;

static struct {
    struct {
        uint8_t icon, gdat_index, flags;
        uint16_t thing;
    } slots[SLOT_ENTRY_COUNT];
    int16_t drawn_inventory; /* the inventory that the panel shows */
    uint16_t drawn_buttons;
    uint16_t shown_object; /* the object of the panel */
    bool shown_eye, shown_mouth;
    int16_t description_x, description_y;
    uint8_t shown_raised[SKILL_BASE_COUNT]; /* the raised skill counts that the skills panel showed */
} cache;
static Offscreen status_box;

/* The value gauge of an object of type: the strength or throw power of a weapon, the armour class of an armour, or
   the power of a potion; false when the object shows none. */
static bool object_value_gauge(uint16_t thing, int type, ObjectGauge *g)
{
    if (type == THING_WEAPON) {
        *g = (ObjectGauge){GLYPH_STRENGTH, item_attr(thing, ATTR_STRENGTH), WEAPON_STRENGTH_MAX, "strength"};
        if (!g->value)
            *g = (ObjectGauge){GLYPH_THROW, item_attr(thing, ATTR_THROW_POWER), WEAPON_THROW_MAX, "throw"};
    } else if (type == THING_ARMOUR) {
        *g = (ObjectGauge){GLYPH_STRENGTH, item_attr(thing, ATTR_ARMOUR_CLASS) & ARMOUR_CLASS_MASK, ARMOUR_CLASS_MAX,
                           "armour"};
    } else if (type == THING_POTION) {
        *g = (ObjectGauge){GLYPH_POTION, potion_power(thing), POTION_POWER_MAX, "power"};
    } else {
        return false;
    }
    return g->value != 0;
}

/* ---- Setup ---- */

void ui_reset(void)
{
    game_clear_ui_state();
    memset(&cache, 0, sizeof cache);
    game_set_screen_refresh(true);
    cache.shown_object = THING_NONE;
    status_box.bitmap = NULL;
    action_reset();
}

/* ---- Queries ---- */

/* Adds a formatted line to the object info lines; a full list takes no more lines. Returns the new count. */
static int info_add(char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE], int count, const char *fmt, ...)
{
    if (count >= OBJECT_INFO_LINES)
        return count;
    va_list args;
    va_start(args, fmt);
    vsnprintf(lines[count], OBJECT_INFO_SIZE, fmt, args);
    va_end(args);
    return count + 1;
}

/* Adds each non-empty line of text (lines end at a new line) to the object info lines. */
static int info_add_text(char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE], int count, const char *text)
{
    for (const char *p = text; *p;) {
        int n = (int)strcspn(p, "\n");
        if (n)
            count = info_add(lines, count, "%.*s", n, p);
        p += n;
        if (*p)
            p++;
    }
    return count;
}

/* Appends item to line after a comma; a line that has no room for it goes to the object info lines first. */
static int info_append(char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE], int count, char *line, const char *item)
{
    static const char separator[] = ", ";
    size_t used = strlen(line);
    if (used && used + strlen(separator) + strlen(item) >= OBJECT_INFO_SIZE) {
        count = info_add(lines, count, "%s", line);
        used = 0;
    }
    snprintf(line + used, OBJECT_INFO_SIZE - used, "%s%s", used ? separator : "", item);
    return count;
}

/* Adds the names of the items of a chest, as many to a line as fit. */
static int info_add_contents(char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE], int count, uint16_t container)
{
    char line[OBJECT_INFO_SIZE] = "";
    uint16_t t = thing_word(container, CONTAINER_CONTENTS);
    for (int i = 0; t != THING_ENDOFLIST && i < CONTAINER_SLOTS; i++, t = dungeon_next_thing(t))
        count = info_append(lines, count, line, item_name(t));
    return *line ? info_add(lines, count, "%s", line) : count;
}

/* Counts the coins of a money box: the coin type of each used coin slot in slot order, and the total value. */
static void box_coins(uint16_t box, BoxCoins *b)
{
    money_count_coins_in_box(box, b->counts);
    b->type_count = 0;
    b->total = 0;
    for (int slot = 0; slot < COIN_TYPE_MAX; slot++) {
        int type = money_coin_type_for_slot(box, slot);
        if (type < 0)
            continue;
        b->total += money_coin_value(type) * b->counts[type];
        b->types[b->type_count++] = type;
    }
}

/* Adds the coin count of each coin slot of a money box ("<coin name> <count>", as many to a line as fit) and the
   total value. */
static int info_add_coins(char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE], int count, uint16_t box)
{
    BoxCoins b;
    box_coins(box, &b);
    char raw[TEXT_SIZE], name[TEXT_SIZE], coin[COIN_TEXT_SIZE], line[OBJECT_INFO_SIZE] = "";
    for (int i = 0; i < b.type_count; i++) {
        int type = b.types[i];
        uint16_t item = money_coin_item(type);
        gtext_format(gtext_gdat(item_index_gdat_category(item), item_index_gdat_index(item), GFX_ITEM_NAME, raw), name);
        snprintf(coin, sizeof coin, "%s %d", name, b.counts[type]);
        count = info_append(lines, count, line, coin);
    }
    if (*line)
        count = info_add(lines, count, "%s", line);
    return info_add(lines, count, "total %d", b.total);
}

int ui_object_info_lines(uint16_t thing, char lines[OBJECT_INFO_LINES][OBJECT_INFO_SIZE])
{
    if (thing == THING_NONE)
        return 0;
    int type = thing_type(thing);
    int count = info_add(lines, 0, "%s", item_name(thing));
    int weight = item_weight(thing);
    gtext_format_set_number(ARG_WEIGHT_KG, weight / WEIGHT_TENTHS);
    gtext_format_set_number(ARG_WEIGHT_TENTHS, weight % WEIGHT_TENTHS);
    char text[TEXT_SIZE * 2];
    count = info_add_text(lines, count, gtext_gdat(CAT_PANEL, 0, TEXT_WEIGHT, text));
    if (type == THING_SCROLL)
        return info_add_text(lines, count, gtext_decode_dungeon(text, sizeof text, thing, false, true));
    if (type == THING_CONTAINER) {
        if (item_is_chest(thing))
            return info_add_contents(lines, count, thing);
        return item_is_moneybox(thing) ? info_add_coins(lines, count, thing) : count;
    }
    if (item_has_charged_action(thing) && item_charges(thing))
        count = info_add(lines, count, "charges %d/%d", item_charges(thing), item_max_charges(thing));
    ObjectGauge g;
    if (object_value_gauge(thing, type, &g))
        count = info_add(lines, count, "%s %d/%d", g.label, g.value, g.high);
    return count;
}

char *ui_skill_rank_text(int16_t champion_index, int skill, char *text)
{
    int level = champ_get_skill_level(champion_index, skill, true);
    if (level > SKILL_LEVEL_MAX)
        level = SKILL_LEVEL_MAX;
    gtext_format_set_entry(skill);
    return gtext_gdat(CAT_PANEL, 0, skill_level_texts[level], text);
}

/* ---- Offscreen bitmaps ---- */

/* True when rectangle outer holds rectangle inner. */
static bool contains(const int16_t *outer, const int16_t *inner)
{
    return outer[RECT_X] <= inner[RECT_X] && inner[RECT_X] + inner[RECT_W] - 1 <= outer[RECT_X] + outer[RECT_W] - 1 &&
           outer[RECT_Y] <= inner[RECT_Y] && inner[RECT_Y] + inner[RECT_H] - 1 <= outer[RECT_Y] + outer[RECT_H] - 1;
}

/* Marks rect as changed: it replaces the smaller changed rects, clipped to the offscreen rect; a full list goes to
   the screen first. */
static void add_dirty_rect(Offscreen *o, const int16_t *rect)
{
    if (!rect)
        return;
    int i = 0;
    for (; i < o->dirty_count; i++) {
        if (contains(o->dirty[i], rect))
            return;
        if (contains(rect, o->dirty[i]))
            break; /* the new rect replaces a smaller one */
    }
    if (i == o->dirty_count) {
        if (i >= OFFSCREEN_DIRTY_MAX)
            offscreen_flush(o, false);
        i = o->dirty_count++;
    }
    int16_t *d = o->dirty[i];
    memcpy(d, rect, sizeof o->dirty[i]);
    int16_t delta = (int16_t)(d[RECT_X] - o->rect[RECT_X]);
    if (delta < 0) {
        d[RECT_W] = (int16_t)(d[RECT_W] + delta);
        if (d[RECT_W] <= 0) {
            o->dirty_count--;
            return;
        }
        d[RECT_X] = (int16_t)(d[RECT_X] - delta);
    }
    delta = (int16_t)(d[RECT_Y] - o->rect[RECT_Y]);
    if (delta < 0) {
        d[RECT_H] = (int16_t)(d[RECT_H] + delta);
        if (d[RECT_H] <= 0) {
            o->dirty_count--;
            return;
        }
        d[RECT_Y] = (int16_t)(d[RECT_Y] - delta);
    }
    delta = (int16_t)((o->rect[RECT_X] + o->rect[RECT_W] - 1) - (d[RECT_X] + d[RECT_W] - 1));
    if (delta < 0)
        d[RECT_W] = (int16_t)(d[RECT_W] + delta);
    delta = (int16_t)((o->rect[RECT_Y] + o->rect[RECT_H] - 1) - (d[RECT_Y] + d[RECT_H] - 1));
    if (delta < 0)
        d[RECT_H] = (int16_t)(d[RECT_H] + delta);
}

void offscreen_create(Offscreen *o, int zone, bool mark_dirty)
{
    if (zone != ZONE_NONE)
        zone_get_rect(zone, o->rect);
    o->bitmap = draw_temp_bitmap(o->rect[RECT_W], o->rect[RECT_H], IMAGE_8BPP);
    o->dirty_count = 0;
    if (mark_dirty)
        add_dirty_rect(o, o->rect);
}

void offscreen_flush(Offscreen *o, bool close)
{
    for (int i = 0; i < o->dirty_count; i++) {
        const int16_t *d = o->dirty[i];
        Blit blit = {.src = o->bitmap->pixels,
                     .dst = g_draw_screen.pixels,
                     .src_x = (int16_t)(d[RECT_X] - o->rect[RECT_X]),
                     .src_y = (int16_t)(d[RECT_Y] - o->rect[RECT_Y]),
                     .src_width = o->rect[RECT_W],
                     .dst_width = g_draw_screen.width,
                     .src_bpp = IMAGE_8BPP,
                     .dst_bpp = IMAGE_8BPP,
                     .transparent = GFX_OPAQUE,
                     .mirror = MIRROR_NONE};
        gfx_blit(&blit, d);
    }
    o->dirty_count = 0;
    if (close)
        o->bitmap = NULL; /* draw_free_temp frees it */
}

/* The screen rectangle rect in the coordinates of the offscreen bitmap. */
static void local_rect(const Offscreen *o, const int16_t *rect, int16_t *out)
{
    out[RECT_X] = (int16_t)(rect[RECT_X] - o->rect[RECT_X]);
    out[RECT_Y] = (int16_t)(rect[RECT_Y] - o->rect[RECT_Y]);
    out[RECT_W] = rect[RECT_W];
    out[RECT_H] = rect[RECT_H];
}

void offscreen_fill_rect(Offscreen *o, const int16_t *rect, int color)
{
    if (!rect)
        return;
    int16_t r[RECT_SIZE];
    local_rect(o, rect, r);
    gfx_fill_rect(o->bitmap->pixels, r, (uint16_t)color, o->rect[RECT_W], IMAGE_8BPP);
    add_dirty_rect(o, rect);
}

/* Fills every second pixel of rect in dst with UI color ui_color. */
static void fill_rect_dithered(uint8_t *dst, const int16_t *rect, int ui_color, int dst_width)
{
    if (!rect || !rect[RECT_W] || !rect[RECT_H])
        return;
    int start = rect[RECT_Y] * dst_width + rect[RECT_X];
    int phase = ((start / dst_width) ^ start) & 1;
    uint8_t color = g_ui_colors[ui_color & DITHER_COLOR_MASK];
    for (int row = 0; row < rect[RECT_H]; row++, phase ^= 1) {
        uint8_t *line = dst + start + row * dst_width;
        for (int col = phase; col < rect[RECT_W]; col += 2)
            line[col] = color;
    }
}

void offscreen_fill_rect_dithered(Offscreen *o, const int16_t *rect, int ui_color)
{
    if (!rect)
        return;
    int16_t r[RECT_SIZE];
    local_rect(o, rect, r);
    fill_rect_dithered(o->bitmap->pixels, r, ui_color, o->rect[RECT_W]);
    add_dirty_rect(o, rect);
}

void offscreen_blit(Offscreen *o, const BlitSource *source, const int16_t *rect)
{
    const Bitmap *src = source->bitmap;
    if (!rect || !src)
        return;
    int16_t r[RECT_SIZE];
    local_rect(o, rect, r);
    Blit blit = {.src = src->pixels,
                 .dst = o->bitmap->pixels,
                 .src_x = source->x,
                 .src_y = source->y,
                 .src_width = src->width,
                 .dst_width = o->rect[RECT_W],
                 .src_bpp = src->bpp,
                 .dst_bpp = IMAGE_8BPP,
                 .transparent = source->transparent,
                 .mirror = source->mirror,
                 .colormap = source->colormap};
    gfx_blit(&blit, r);
    add_dirty_rect(o, rect);
}

void offscreen_draw_gdat_image(Offscreen *o, GdatKey picture, int zone, int transparent)
{
    const Bitmap *image = draw_gdat_image(picture.category, picture.index, picture.entry);
    int16_t r[RECT_SIZE];
    int16_t sx = 0;
    int16_t sy = 0;
    if (!image)
        return;
    int16_t *rect = zone_compute_rect(image, r, zone, &sx, &sy);
    offscreen_blit(o,
                   &(BlitSource){.bitmap = image,
                                 .x = sx,
                                 .y = sy,
                                 .transparent = transparent,
                                 .colormap = draw_gdat_image_palette(picture.category, picture.index, picture.entry)},
                   rect);
}

void offscreen_draw_ctx(Offscreen *o, DrawCtx *ctx, int zone, int transparent)
{
    int16_t r[RECT_SIZE];
    ctx->dst = o->bitmap;
    if (zone == ZONE_NONE) {
        memcpy(r, ctx->rect, sizeof r);
    } else {
        int16_t w = ctx->w;
        int16_t h = ctx->h;
        if (!ctx->image || !zone_compute_rect(ctx->image, r, zone, &w, &h))
            return;
        /* as in the original, the clipped part goes into the size of the picture part, not its position */
        ctx->w = (int16_t)(ctx->w + w);
        ctx->h = (int16_t)(ctx->h + h);
    }
    local_rect(o, r, ctx->rect);
    ctx->zone = (int16_t)ZONE_NONE;
    ctx->transparent = (int16_t)transparent;
    draw_ctx_draw(ctx);
    add_dirty_rect(o, r);
}

void offscreen_draw_text_at(Offscreen *o, int16_t x, int16_t y, TextColors colors, const char *text)
{
    TextSize size = text_measure(text);
    if (!size.w)
        return;
    Bitmap dst = {o->bitmap->pixels, o->rect[RECT_W], o->rect[RECT_H], IMAGE_8BPP};
    text_draw(&dst, (int16_t)(x - o->rect[RECT_X]), (int16_t)(y - o->rect[RECT_Y]), colors, text);
    /* as in the original, the changed rect starts at the base line */
    const int16_t r[RECT_SIZE] = {[RECT_X] = x, [RECT_Y] = y, [RECT_W] = size.w, [RECT_H] = size.h};
    add_dirty_rect(o, r);
}

/* The base line start of text in zone (screen coordinates) and its rectangle; NULL when it is not visible. */
static int16_t *zoned_text_rect(int zone, const char *text, int16_t *r)
{
    TextSize size = text_measure(text);
    if (!size.w)
        return NULL;
    return zone_compute_rect(NULL, r, zone, &size.w, &size.h);
}

void offscreen_draw_text_in_zone(Offscreen *o, int zone, int color, int bg, const char *text)
{
    int16_t r[RECT_SIZE];
    if (!zoned_text_rect(zone, text, r))
        return;
    Bitmap dst = {o->bitmap->pixels, o->rect[RECT_W], o->rect[RECT_H], IMAGE_8BPP};
    text_draw(&dst, (int16_t)(r[RECT_X] - o->rect[RECT_X]), (int16_t)(r[RECT_Y] + r[RECT_H] - 1 - o->rect[RECT_Y]),
              (TextColors){color, bg}, text);
    add_dirty_rect(o, r);
}

void offscreen_draw_shadow_text(Offscreen *o, int zone, int color, int bg, const char *text)
{
    int16_t r[RECT_SIZE];
    if (!zoned_text_rect(zone, text, r))
        return;
    Bitmap dst = {o->bitmap->pixels, o->rect[RECT_W], o->rect[RECT_H], IMAGE_8BPP};
    text_draw_with_shadow(&dst, (int16_t)(r[RECT_X] - o->rect[RECT_X]),
                          (int16_t)(r[RECT_Y] + r[RECT_H] - 1 - o->rect[RECT_Y]), (TextColors){color, bg}, text);
    add_dirty_rect(o, r);
}

/* ---- Interface pictures ---- */

void ui_blit_bitmap(const BlitSource *source, Bitmap *dst, const int16_t *rect)
{
    const Bitmap *src = source->bitmap;
    if (!src)
        return; /* fix: a picture missing from GRAPHICS.DAT draws nothing */
    Blit blit = {.src = src->pixels,
                 .dst = dst->pixels,
                 .src_x = source->x,
                 .src_y = source->y,
                 .src_width = src->width,
                 .dst_width = dst->width,
                 .src_bpp = src->bpp,
                 .dst_bpp = dst->bpp,
                 .transparent = source->transparent,
                 .mirror = source->mirror,
                 .colormap = source->colormap};
    gfx_blit(&blit, rect);
}

void ui_draw_viewport_image(int category, int index, int entry, int zone, int transparent)
{
    DrawCtx c;
    draw_ctx_init(&c, category, index, entry);
    c.transparent = (int16_t)transparent;
    c.dst = &g_view_viewport;
    c.dx = 0;
    c.dy = 0;
    c.zone = (int16_t)zone;
    draw_ctx_draw(draw_ctx_prepare(&c));
}

void ui_fill_zone_screen(int zone, int color)
{
    int16_t r[RECT_SIZE];
    if (zone_get_rect(zone, r))
        gfx_fill_rect(g_draw_screen.pixels, r, color, g_draw_screen.width, IMAGE_8BPP);
}

void ui_draw_champion_face_icon(Bitmap *dst, int16_t champion_index, uint8_t *face_colors)
{
    const Champion *c = champion(champion_index);
    int frame =
        dir_relative(c->dir, g_game.party_dir) + (g_game.magic_light[MAGIC_INVISIBILITY] ? FACE_INVISIBLE_FRAMES : 0);
    const uint8_t *colors = draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_FACES, champion_index);
    if (colors)
        memcpy(face_colors, colors, FACE_ICON_COLORS);
    const int16_t r[RECT_SIZE] = {0, 0, FACE_SIZE, FACE_SIZE};
    /* the original skips color 2 of the face picture */
    ui_blit_bitmap(&(BlitSource){.bitmap = draw_gdat_image(CAT_INTERFACE, UI_INDEX_FACES, champion_index),
                                 .x = FACE_SIZE * frame,
                                 .transparent = FACE_SKIP_COLOR},
                   dst, r);
}

void ui_render_hand_icon(void)
{
    uint8_t identity[HAND_ICON_COLORS]; /* the icon keeps its color numbers */
    for (int i = 0; i < HAND_ICON_COLORS; i++)
        identity[i] = (uint8_t)i;
    uint16_t t = g_game.leader_hand;
    int category = item_gdat_category(t);
    int index = item_gdat_index(t);
    int entry = item_icon_index(t, -1, true);
    const Bitmap *image = draw_gdat_image(category, index, entry);
    const uint8_t *colors = draw_gdat_image_palette(category, index, entry);
    Bitmap cursor = {g_leader_hand.cursor, HAND_ICON_SIZE, HAND_ICON_SIZE, IMAGE_8BPP};
    memset(g_leader_hand.cursor, HAND_ICON_KEY, sizeof g_leader_hand.cursor);
    const int16_t r[RECT_SIZE] = {0, 0, HAND_ICON_SIZE, HAND_ICON_SIZE};
    if (image && colors) {
        ui_blit_bitmap(&(BlitSource){.bitmap = image, .transparent = GFX_OPAQUE, .colormap = identity}, &cursor, r);
        memcpy(g_leader_hand.palette, colors, sizeof g_leader_hand.palette);
    }
    draw_free_temp();
}

void ui_draw_interface_image(int category, int index, int entry, int zone)
{
    DrawCtx d;
    draw_ctx_init(&d, category, index, entry);
    d.transparent = GFX_OPAQUE;
    d.zone = (int16_t)zone;
    draw_ctx_draw(draw_ctx_prepare(&d));
}

/* ---- Screen ---- */

void ui_draw_game_screen(void)
{
    ui_fill_zone_screen(ZONE_SCREEN, 0);
    ui_draw_movement_arrows();
    champ_init_party_state();
    ui_draw_all_champion_states(true);
}

void ui_draw_movement_arrows(void)
{
    Offscreen o;
    int entry = g_ui.push_mode ? GFX_PUSH_ARROWS : GFX_ARROWS;
    offscreen_create(&o, ZONE_MOVEMENT_ARROWS, true);
    gfx_fill_bitmap(o.bitmap, g_ui_colors[UI_BLACK]);
    for (int zone = ZONE_ARROW_FIRST; zone < ZONE_ARROW_FIRST + ZONE_ARROW_COUNT; zone++, entry += ARROW_PICTURES)
        offscreen_draw_gdat_image(&o, (GdatKey){CAT_INTERFACE, UI_INDEX_ARROWS, entry}, zone, GFX_OPAQUE);
    offscreen_flush(&o, true);
    draw_free_temp();
    input_set_mode(g_ui.push_mode ? INPUT_MODE_PUSH : INPUT_MODE_MOVE);
}

/* ---- Status boxes and inventory ---- */

/* The champion index of the open inventory, -1 for none (g_ui.inventory is an ordinal). */
static int16_t inventory_champion(void)
{
    return (int16_t)(g_ui.inventory - 1);
}

static bool in_inventory(int champ)
{
    return champ == inventory_champion();
}

/* Opens the status box bitmap of champ with its background; clear: the whole box goes to the screen. */
static void status_box_begin(int champ, bool clear)
{
    if (status_box.bitmap)
        return;
    int background = !champion(champ)->health ? GFX_BOX_DEAD : in_inventory(champ) ? GFX_BOX_INVENTORY : GFX_BOX_ALIVE;
    offscreen_create(&status_box, ZONE_STATUS_BOX + champ, clear);
    offscreen_draw_gdat_image(&status_box, (GdatKey){CAT_INTERFACE, UI_INDEX_STATUS, background},
                              ZONE_STATUS_BOX + champ, GFX_OPAQUE);
    if (!clear)
        status_box.dirty_count = 0;
}

/* Draws the health, stamina, and mana bars of a status box. */
static void show_status_bars(int champ)
{
    const Champion *c = champion(champ);
    int16_t r[RECT_SIZE];
    add_dirty_rect(&status_box, zone_get_rect(ZONE_STATUS_BARS + champ, r));
    const int16_t values[BAR_COUNT] = {c->health, c->stamina, c->mana};
    const int16_t maxima[BAR_COUNT] = {c->max_health, c->max_stamina, c->max_mana > c->mana ? c->max_mana : c->mana};
    for (int i = 0; i < BAR_COUNT; i++) {
        if (!maxima[i])
            continue;
        uint16_t scale = (uint16_t)(values[i] * SCALE_PARTS / maxima[i]);
        if (!zone_get_rect_clipped(ZONE_STATUS_BAR_FIRST + champ + CHAMPION_MAX * i, r, SCALE_PARTS, scale))
            continue;
        int16_t shadow[RECT_SIZE] = {(int16_t)(r[RECT_X] + BAR_SHADOW), (int16_t)(r[RECT_Y] + BAR_SHADOW), r[RECT_W],
                                     r[RECT_H]};
        offscreen_fill_rect(&status_box, shadow, g_ui_colors[UI_BLACK]);
        offscreen_fill_rect(&status_box, r, g_ui_colors[champion_colors[champ]]);
    }
}

/* Draws the damage star with the last damage of a champion in its status box. */
static void show_damage_number(int champ)
{
    offscreen_draw_gdat_image(&status_box, (GdatKey){CAT_INTERFACE, UI_INDEX_STATUS, GFX_BOX_DAMAGE},
                              ZONE_STATUS_DAMAGE + champ, DAMAGE_STAR_KEY);
    offscreen_draw_text_in_zone(&status_box, ZONE_STATUS_DAMAGE + champ, g_ui_colors[UI_WHITE], g_ui_colors[UI_RED],
                                gtext_format_number(champion(champ)->damage, false, NUMBER_DIGITS));
}

/* Draws a value and its maximum ("83/ 83") in zone of the inventory. */
static void show_value_pair(int zone, int16_t value, int16_t maximum)
{
    char s[TEXT_SIZE];
    strcpy(s, gtext_format_number(value, true, NUMBER_DIGITS));
    strcat(s, "/");
    strcat(s, gtext_format_number(maximum, true, NUMBER_DIGITS));
    text_draw_in_zone_shadowed(zone, g_ui_colors[UI_LIGHT_GRAY], g_ui_colors[UI_DARK_GRAY] | TEXT_TRANSPARENT, s);
}

/* Draws the health, stamina, and mana values of the inventory. */
static void show_champion_values(const Champion *c)
{
    show_value_pair(ZONE_HEALTH_VALUE, c->health, c->max_health);
    show_value_pair(ZONE_STAMINA_VALUE, c->stamina / STAMINA_DISPLAY_UNIT, c->max_stamina / STAMINA_DISPLAY_UNIT);
    show_value_pair(ZONE_MANA_VALUE, c->mana, c->max_mana);
}

/* Draws the good or bad state icon over the eye or the mouth of the inventory. */
static void show_state_icon(int icon, int zone)
{
    int16_t r[RECT_SIZE];
    zone_get_icon_rect(zone, r);
    ui_blit_bitmap(&(BlitSource){.bitmap = draw_gdat_image(CAT_INTERFACE, UI_INDEX_STATUS, icon),
                                 .transparent = UI_KEY,
                                 .colormap = draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_STATUS, icon)},
                   &g_view_viewport, r);
}

/* Draws the portrait of the champion with the open inventory, dimmed while the party is invisible. */
static void show_portrait(int champ)
{
    offscreen_draw_gdat_image(&status_box, (GdatKey){CAT_CHAMPION, champion(champ)->portrait, 0},
                              ZONE_STATUS_PORTRAIT + champ, GFX_OPAQUE);
    if (g_game.magic_light[MAGIC_INVISIBILITY]) {
        int16_t r[RECT_SIZE];
        offscreen_fill_rect_dithered(&status_box, zone_get_rect(ZONE_STATUS_PORTRAIT + champ, r), UI_DARKEST_GRAY);
    }
}

/* Draws an inventory button (save, sleep, sound, exit, close), pressed when its bit is set. */
static void show_inventory_button(int icon, int zone, uint16_t mask)
{
    ui_draw_viewport_image(CAT_PANEL, 0, g_ui.buttons & mask ? icon + 1 : icon, zone, GFX_OPAQUE);
}

/* Copies the part of the inventory background under zone into the view picture. */
static void restore_panel_back(int zone)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(zone, r);
    ui_blit_bitmap(&(BlitSource){.bitmap = draw_gdat_image(CAT_PANEL, 0, GFX_PANEL_BACK),
                                 .x = r[RECT_X],
                                 .y = r[RECT_Y],
                                 .transparent = GFX_OPAQUE,
                                 .colormap = draw_gdat_image_palette(CAT_PANEL, 0, GFX_PANEL_BACK)},
                   &g_view_viewport, r);
}

/* Draws one slot: background, frame (active hand, wound), and the item icon. */
static void show_slot(uint16_t thing, int16_t slot, bool wounded, bool active, bool draw_frame)
{
    int category, index, icon;
    if (thing == THING_NONE) {
        category = CAT_PANEL;
        index = 0;
        icon = (uint8_t)(slot_layout[slot].empty_icon + wounded);
    } else {
        category = item_gdat_category(thing);
        index = item_gdat_index(thing);
        icon = item_icon_index(thing, slot < SLOT_STATUS_COUNT ? slot % SLOT_HAND_COUNT : slot - SLOT_INVENTORY_FIRST,
                               true);
    }
    int zone = slot_layout[slot].zone;
    int16_t r[RECT_SIZE];
    if (slot < SLOT_BACKGROUND_COUNT) {
        zone_get_rect(zone, r);
        if (slot < SLOT_STATUS_COUNT)
            offscreen_blit(
                &status_box,
                &(BlitSource){.bitmap = draw_gdat_image(CAT_INTERFACE, UI_INDEX_STATUS, GFX_BOX_ALIVE),
                              .x = (int16_t)(r[RECT_X] - status_box.rect[RECT_X]),
                              .y = (int16_t)(r[RECT_Y] - status_box.rect[RECT_Y]),
                              .transparent = GFX_OPAQUE,
                              .colormap = draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_STATUS, GFX_BOX_ALIVE)},
                r);
        else
            restore_panel_back(zone);
    }
    if (draw_frame && slot < SLOT_FRAMED_COUNT) {
        zone_get_icon_rect(zone, r);
        int frame = active ? GFX_BOX_SLOT_ACTIVE : wounded ? GFX_BOX_SLOT_WOUNDED : GFX_BOX_SLOT_FRAME;
        const Bitmap *image = draw_gdat_image(CAT_INTERFACE, UI_INDEX_STATUS, frame);
        const uint8_t *colors = draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_STATUS, frame);
        if (slot < SLOT_STATUS_COUNT)
            offscreen_blit(&status_box, &(BlitSource){.bitmap = image, .transparent = UI_KEY, .colormap = colors}, r);
        else
            ui_blit_bitmap(&(BlitSource){.bitmap = image, .transparent = UI_KEY, .colormap = colors}, &g_view_viewport,
                           r);
    }
    if (icon == NO_ICON)
        return;
    if (slot < SLOT_STATUS_COUNT)
        offscreen_draw_gdat_image(&status_box, (GdatKey){category, index, icon}, zone, UI_KEY);
    else
        ui_draw_viewport_image(category, index, icon, zone, UI_KEY);
}

/* Draws a champion slot when it changed; true when it drew. */
static bool show_changed_slot(int champ, int16_t slot, bool force)
{
    int entry;
    if (in_inventory(champ)) {
        entry = slot + SLOT_INVENTORY_FIRST;
    } else {
        if (slot >= SLOT_HAND_COUNT || g_ui.candidate == champ + 1)
            return false;
        entry = champ * SLOT_HAND_COUNT + slot;
    }
    const Champion *c = champion(champ);
    uint16_t thing = c->slots[slot];
    bool redraw_frame = force;
    bool active = false;
    bool wounded = false;
    bool changed = force;
    if (slot < SLOT_HAND_COUNT) {
        active = champ + 1 == g_action.champion && slot == g_action.hand;
        if (!(cache.slots[entry].flags & SLOT_ACTIVE) != !active) {
            redraw_frame = true;
            cache.slots[entry].flags ^= SLOT_ACTIVE;
        }
    }
    if (slot < SLOT_WOUND_COUNT) {
        wounded = (c->wounds & 1 << slot) != 0;
        if (!(cache.slots[entry].flags & SLOT_WOUNDED) != !wounded) {
            redraw_frame = true;
            cache.slots[entry].flags ^= SLOT_WOUNDED;
        }
    }
    if (thing != THING_NONE) {
        uint8_t icon =
            (uint8_t)item_icon_index(thing, slot, (item_attr(thing, ATTR_FLAGS) & ITEM_FLAG_ICON_IN_HAND) != 0);
        if (icon != cache.slots[entry].icon) {
            changed = true;
            cache.slots[entry].icon = icon;
        }
        uint8_t index = item_gdat_index(thing);
        if (index != cache.slots[entry].gdat_index) {
            changed = true;
            cache.slots[entry].gdat_index = index;
        }
    }
    if (!changed && !redraw_frame && cache.slots[entry].thing == thing)
        return false;
    if (thing == THING_NONE || entry < SLOT_STATUS_COUNT)
        redraw_frame = true;
    if (entry < SLOT_STATUS_COUNT)
        status_box_begin(champ, false);
    cache.slots[entry].thing = thing;
    show_slot(thing, entry, wounded, active, redraw_frame);
    return true;
}

/* Draws description lines (17 characters) under each other; "\f" at the start goes back to the first line. */
static void show_description(const char *text)
{
    if (*text == '\f') {
        text++;
        ScreenPoint at = zone_get_position(ZONE_DESCRIPTION);
        cache.description_x = at.x;
        cache.description_y = at.y;
    }
    char buf[TEXT_SIZE];
    snprintf(buf, sizeof buf, "%s", text);
    for (char *line = buf; *line;) {
        char *next = line + strlen(line);
        char kept = '\0'; /* the character that a break inside a word cuts off, put back after the draw */
        if (strlen(line) > DESCRIPTION_LINE) {
            char *brk = line + DESCRIPTION_BREAK;
            while (brk > line && *brk != ' ')
                brk--;
            if (*brk == ' ') {
                *brk = '\0';
                next = brk + 1;
            } else {
                /* fix: a word without a space breaks at the line width; the original scans before the buffer */
                next = line + DESCRIPTION_LINE;
                kept = *next;
                *next = '\0';
            }
        }
        text_draw_in_viewport(cache.description_x, cache.description_y, g_ui_colors[UI_LIGHT_GRAY], line);
        cache.description_y = (int16_t)(cache.description_y + TEXT_LINE_HEIGHT);
        if (kept)
            *next = kept;
        line = next;
    }
}

/* Draws a centered line of a scroll in the scroll font. */
static void show_scroll_line(int center_x, int16_t y, const char *line)
{
    TextSize size = text_measure(line);
    if (!size.w)
        return;
    char mapped[TEXT_SIZE];
    size_t n = 0;
    for (; line[n] && n + 1 < sizeof mapped; n++) {
        uint8_t c = (uint8_t)line[n];
        if (c >= 'A' && c <= 'Z')
            c = (uint8_t)(c - 'A' + SCROLL_LETTER_FIRST);
        else if (c > 'z')
            c = (uint8_t)(c + SCROLL_HIGH_OFFSET);
        mapped[n] = (char)c;
    }
    mapped[n] = '\0';
    text_draw_in_viewport((int16_t)(center_x - (size.w >> 1)), y, g_ui_colors[UI_BLACK], mapped);
}

/* Draws an open scroll with its text in the inventory panel. */
static void show_scroll(uint16_t scroll)
{
    char text[TEXT_SIZE * 2], line[TEXT_SIZE * 2];
    gtext_decode_dungeon(text, sizeof text, scroll, false, true);
    game_set_panel_mode(PANEL_MODE_SCROLL);
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_PANEL, ZONE_INVENTORY_PANEL, GFX_OPAQUE);
    ui_draw_viewport_image(CAT_SCROLL, 0, GFX_SCROLL_PICTURE, ZONE_INVENTORY_PANEL, UI_KEY);
    int newlines = 0;
    size_t n = strlen(text);
    for (size_t i = 0; i < n; i++)
        newlines += text[i] == '\n';
    int lines = newlines + 1;
    /* a trailing new line adds no line; two trailing new lines drop one */
    if (n && text[n - 1] == '\n')
        lines = n > 1 && text[n - 2] == '\n' ? newlines - 1 : newlines;
    int16_t r[RECT_SIZE];
    ScreenPoint at = zone_get_position(ZONE_SCROLL_TEXT);
    int16_t x = at.x;
    int16_t y = at.y;
    zone_get_rect(ZONE_INVENTORY_PANEL, r);
    /* centers the text block on the scroll */
    y = (int16_t)(y - ((((int16_t)lines * TEXT_LINE_HEIGHT - FONT_BASELINE - 1) >> 1) - FONT_GLYPH_HEIGHT));
    for (int16_t pos = 0; text[pos];) {
        text_wrap_line(text, line, &pos, r[RECT_W]);
        show_scroll_line(x, y, line);
        y = (int16_t)(y + TEXT_LINE_HEIGHT);
        if (text[pos] == '\n')
            pos++;
    }
}

/* A gauge bar in zone: value from range.low to range.high; split fills the rest of the zone with UI color split
   (GAUGE_NO_SPLIT: no fill). */
static void show_gauge_bar(int16_t value, int zone, int color, GaugeRange range, int split)
{
    if (value < GAUGE_STARVING)
        color = UI_RED;
    else if (value < 0)
        color = UI_YELLOW;
    int16_t bar[RECT_SIZE], full[RECT_SIZE], shadow[RECT_SIZE];
    /* 16-bit as in the original */
    uint16_t scale = (uint16_t)((int16_t)(value - range.low) * SCALE_PARTS / (range.high - range.low));
    if (!zone_get_rect_clipped(zone, bar, scale, SCALE_PARTS))
        return;
    if (split != GAUGE_NO_SPLIT) {
        zone_get_rect(zone, full);
        memcpy(shadow, full, sizeof shadow);
        if (bar[RECT_X] + bar[RECT_W] - 1 == full[RECT_X] + full[RECT_W] - 1) {
            split = GAUGE_NO_SPLIT;
        } else {
            full[RECT_W] = (int16_t)((full[RECT_X] + full[RECT_W] - 1) - (bar[RECT_X] + bar[RECT_W] - 1));
            full[RECT_X] = (int16_t)(bar[RECT_X] + bar[RECT_W]);
        }
    } else {
        memcpy(shadow, bar, sizeof shadow);
    }
    shadow[RECT_X] = (int16_t)(shadow[RECT_X] + GAUGE_SHADOW);
    shadow[RECT_Y] = (int16_t)(shadow[RECT_Y] + GAUGE_SHADOW);
    gfx_fill_rect(g_view_viewport.pixels, shadow, g_ui_colors[UI_BLACK], g_view_viewport.width, IMAGE_8BPP);
    gfx_fill_rect(g_view_viewport.pixels, bar, g_ui_colors[color], g_view_viewport.width, IMAGE_8BPP);
    if (split != GAUGE_NO_SPLIT)
        gfx_fill_rect(g_view_viewport.pixels, full, g_ui_colors[split], g_view_viewport.width, IMAGE_8BPP);
}

/* An object gauge: the bar with its glyph left of it and the end marks. */
static void show_object_gauge(int zone, int value, int high, uint8_t glyph, int color)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(zone, r))
        return;
    show_gauge_bar(value, zone, color, (GaugeRange){0, high}, GAUGE_REST_COLOR);
    int y = (int16_t)(r[RECT_Y] + r[RECT_H] - GAUGE_TEXT_RAISE);
    int bg = g_ui_colors[UI_BLACK] | TEXT_TRANSPARENT;
    char s[GLYPH_TEXT_SIZE] = {(char)glyph, '\0'};
    text_draw_with_shadow(&g_view_viewport, (int16_t)(r[RECT_X] - GAUGE_GLYPH_LEFT), y,
                          (TextColors){g_ui_colors[color], bg}, s);
    s[0] = GLYPH_GAUGE_LEFT;
    text_draw_with_shadow(&g_view_viewport, (int16_t)(r[RECT_X] + GAUGE_MARK_INSET), y,
                          (TextColors){g_ui_colors[UI_WHITE], bg}, s);
    s[0] = GLYPH_GAUGE_RIGHT;
    /* the original places the right end mark by the font glyph height */
    text_draw_with_shadow(&g_view_viewport, (int16_t)(r[RECT_X] + r[RECT_W] - 1 - FONT_GLYPH_HEIGHT - 1), y,
                          (TextColors){g_ui_colors[UI_WHITE], bg}, s);
}

/* Draws the coin count of each coin slot of a money box and the total value. */
static void show_coin_counts(uint16_t box)
{
    BoxCoins b;
    box_coins(box, &b);
    char s[TEXT_SIZE];
    for (int i = 0; i < b.type_count; i++) {
        snprintf(s, sizeof s, "%d", b.counts[b.types[i]]);
        text_draw_in_zone(ZONE_COINS_FIRST + i, g_ui_colors[UI_LIGHT_GRAY], s);
    }
    snprintf(s, sizeof s, "%d", b.total);
    text_draw_in_zone(ZONE_COIN_TOTAL, g_ui_colors[UI_LIGHT_GRAY], s);
}

/* Draws the items of a chest in the object info. */
static void show_chest_contents(uint16_t container)
{
    uint16_t t = thing_word(container, CONTAINER_CONTENTS);
    for (int i = 0; t != THING_ENDOFLIST && i < CONTAINER_SLOTS; i++, t = dungeon_next_thing(t))
        show_slot(t, SLOT_CONTAINER_INFO_FIRST + i, false, false, false);
}

/* Draws the gauges of an object of type in the object info: its charges, and the strength or throw power of a
   weapon, the armour class of an armour, or the power of a potion. */
static void show_object_gauges(uint16_t thing, int type)
{
    static const int16_t gauge_zones[OBJECT_GAUGE_COUNT] = {ZONE_OBJECT_GAUGE_FIRST, ZONE_OBJECT_GAUGE_FIRST + 1};
    int gauge = 0;
    if (item_has_charged_action(thing)) {
        int charges = item_charges(thing);
        if (charges) {
            show_object_gauge(gauge_zones[0], charges, item_max_charges(thing), GLYPH_CHARGES, GAUGE_CHARGE_COLOR);
            gauge = 1;
        }
    }
    ObjectGauge g;
    if (object_value_gauge(thing, type, &g))
        show_object_gauge(gauge_zones[gauge], g.value, g.high, g.glyph, GAUGE_VALUE_COLOR);
}

/* Draws the info panel of an object (full: its picture, name, weight, and gauges; else only a scroll);
   false when the panel shows nothing. */
static bool show_object_info(uint16_t thing, bool full)
{
    if (thing == THING_NONE)
        return false;
    show_description("\f");
    int type = thing_type(thing);
    if (type == THING_SCROLL) {
        show_scroll(thing);
        return true;
    }
    game_set_panel_mode(PANEL_MODE_OBJECT);
    if (!full)
        return false;
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_PANEL, ZONE_INVENTORY_PANEL, GFX_OPAQUE);
    int index = item_gdat_index(thing);
    int category = item_gdat_category(thing);
    if (gdat_exists(gdat_current(), category, index, GDAT_TYPE_IMAGE, GFX_OBJECT_PICTURE))
        ui_draw_viewport_image(category, index, GFX_OBJECT_PICTURE, ZONE_INVENTORY_PANEL, UI_KEY);
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_OBJECT_NAME, ZONE_OBJECT_NAME_BACK, UI_KEY);
    text_draw_in_zone(ZONE_OBJECT_NAME, g_ui_colors[UI_LIGHT_GRAY], item_name(thing));
    show_slot(thing, SLOT_INFO, false, false, false);
    int weight = item_weight(thing);
    gtext_format_set_number(ARG_WEIGHT_KG, weight / WEIGHT_TENTHS);
    gtext_format_set_number(ARG_WEIGHT_TENTHS, weight % WEIGHT_TENTHS);
    char text[TEXT_SIZE];
    show_description(gtext_gdat(CAT_PANEL, 0, TEXT_WEIGHT, text));
    if (type == THING_CONTAINER) {
        if (item_is_chest(thing))
            show_chest_contents(thing);
        else if (item_is_moneybox(thing))
            show_coin_counts(thing);
        return true;
    }
    show_object_gauges(thing, type);
    return true;
}

/* Draws the food and water gauges (and the poison gauge) of the inventory panel. */
static void show_food_water(void)
{
    const Champion *c = champion(inventory_champion());
    game_set_panel_mode(PANEL_MODE_FOOD);
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_PANEL, ZONE_INVENTORY_PANEL, GFX_OPAQUE);
    show_gauge_bar(c->food, ZONE_FOOD_GAUGE, UI_BROWN, (GaugeRange){FOOD_MIN, FOOD_MAX}, GAUGE_NO_SPLIT);
    show_gauge_bar(c->water, ZONE_WATER_GAUGE, UI_BLUE, (GaugeRange){FOOD_MIN, FOOD_MAX}, GAUGE_NO_SPLIT);
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_FOOD, ZONE_FOOD_LABEL, UI_KEY);
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_WATER, ZONE_WATER_LABEL, UI_KEY);
    if (c->poisoned) {
        show_gauge_bar(c->poison, ZONE_POISON_GAUGE, UI_RED, (GaugeRange){0, POISON_MAX}, GAUGE_NO_SPLIT);
        ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_POISON, ZONE_POISON_LABEL, UI_KEY);
    }
}

/* Draws the closed mouth of the inventory panel while a champion is recruited. */
static void show_closed_mouth(void)
{
    ui_draw_viewport_image(CAT_WALL_ORNAMENT, MOUTH_ORNAMENT, GFX_MOUTH_CLOSED, ZONE_INVENTORY_PANEL, GFX_OPAQUE);
    game_set_panel_mode(PANEL_MODE_MOUTH);
}

/* Draws the skill levels and the statistics of the champion in the inventory panel. */
static void show_skills_and_stats(void)
{
    int16_t champ = inventory_champion();
    Champion *c = champion(champ);
    char text[TEXT_SIZE];
    game_set_panel_mode(PANEL_MODE_SKILLS);
    ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_PANEL, ZONE_INVENTORY_PANEL, GFX_OPAQUE);
    ScreenPoint skills_at = zone_get_position(ZONE_SKILLS);
    int16_t x = skills_at.x;
    int16_t y = skills_at.y;
    for (int skill = 0; skill < SKILL_BASE_COUNT; skill++) {
        if (champ_get_skill_level(champ, skill, true) == SKILL_LEVEL_UNTRAINED)
            continue;
        ui_skill_rank_text(champ, skill, text);
        text_draw_in_viewport(x, y, g_ui_colors[g_champ_skill_raised[champ][skill] ? UI_GREEN : UI_LIGHT_GRAY], text);
        cache.shown_raised[skill] = g_champ_skill_raised[champ][skill];
        y = (int16_t)(y + TEXT_LINE_HEIGHT);
    }
    ScreenPoint stats_at = zone_get_position(ZONE_STATS);
    int16_t stat_x = stats_at.x;
    y = stats_at.y;
    for (int stat = STAT_STRENGTH; stat < STAT_COUNT; stat++) { /* luck has no line */
        text_draw_in_viewport(x, y, g_ui_colors[UI_LIGHT_GRAY], gtext_gdat(CAT_PANEL, 0, TEXT_STAT_FIRST + stat, text));
        int current = champ_get_stat(c, stat, STAT_CURRENT);
        int maximum = champ_get_stat(c, stat, STAT_MAXIMUM);
        int color = current < maximum ? UI_RED : current > maximum ? UI_GREEN : UI_LIGHT_GRAY;
        text_draw_in_viewport(stat_x, y, g_ui_colors[color], gtext_format_number(current, true, NUMBER_DIGITS));
        strcpy(text, "/");
        strcat(text, gtext_format_number(maximum, true, NUMBER_DIGITS));
        text_draw_in_viewport((int16_t)(FONT_ADVANCE * NUMBER_DIGITS + stat_x), y, g_ui_colors[UI_LIGHT_GRAY], text);
        y = (int16_t)(y + TEXT_LINE_HEIGHT);
    }
}

/* The object that the inventory panel shows: the leader hand object with the eye, none with the mouth, else the
   action hand object. */
static uint16_t panel_object(const Champion *c)
{
    if (g_ui.eye)
        return g_game.leader_hand;
    if (g_ui.mouth)
        return THING_NONE;
    return c->slots[SLOT_ACTION_HAND];
}

/* Draws the load and the maximum load of the champion in the inventory. */
static void show_load(int champ)
{
    restore_panel_back(ZONE_LOAD_BACK);
    int load = champ_get_load(champ);
    int max_load = champ_get_max_load(champion(champ));
    int color = load > max_load                                       ? UI_RED
                : load * LOAD_HIGH_WHOLE > max_load * LOAD_HIGH_PARTS ? UI_YELLOW
                                                                      : UI_LIGHT_GRAY;
    gtext_format_set_number(ARG_LOAD_KG, load / WEIGHT_TENTHS);
    gtext_format_set_number(ARG_LOAD_TENTHS, load % WEIGHT_TENTHS);
    gtext_format_set_number(ARG_LOAD_MAX_KG, max_load / WEIGHT_TENTHS);
    char text[TEXT_SIZE];
    text_draw_in_zone_shadowed(ZONE_LOAD, g_ui_colors[color], g_ui_colors[UI_DARK_GRAY] | TEXT_TRANSPARENT,
                               gtext_gdat(CAT_PANEL, 0, TEXT_LOAD, text));
}

/* Draws the inventory buttons whose pressed state changed; returns the redraw flags that they add. */
static uint16_t show_changed_buttons(uint16_t changed)
{
    uint16_t flags = 0;
    if (changed & BUTTON_SOUND)
        show_inventory_button(GFX_PANEL_BUTTON_SOUND, ZONE_BUTTON_SOUND, BUTTON_SOUND);
    if (!g_ui.candidate) {
        if (changed & BUTTON_CLOSE) {
            show_inventory_button(GFX_PANEL_BUTTON_CLOSE, ZONE_NAME_BUTTON, BUTTON_CLOSE);
            flags |= REDRAW_NAME;
        }
        if (changed & BUTTON_SAVE)
            show_inventory_button(GFX_PANEL_BUTTON_SAVE, ZONE_BUTTON_SAVE, BUTTON_SAVE);
        if (changed & BUTTON_SLEEP)
            show_inventory_button(GFX_PANEL_BUTTON_SLEEP, ZONE_BUTTON_SLEEP, BUTTON_SLEEP);
    }
    if (changed & BUTTON_EXIT)
        show_inventory_button(GFX_PANEL_BUTTON_EXIT, ZONE_BUTTON_EXIT, BUTTON_EXIT);
    cache.drawn_buttons = g_ui.buttons;
    return flags;
}

/* Draws the inventory background and the changed buttons of champ; sets the redraw flags of a new inventory. */
static void show_inventory_frame(ChampionRedraw *d)
{
    uint16_t buttons_changed = 0;
    if (g_ui.inventory == cache.drawn_inventory && !(d->flags & REDRAW_ALL_INVENTORY)) {
        if (d->inv)
            buttons_changed = g_ui.buttons ^ cache.drawn_buttons;
    } else if (!d->inv) {
        cache.drawn_inventory = 0;
    } else {
        ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_BACK, ZONE_DIALOG_BACK, GFX_OPAQUE);
        d->force_inventory = true;
        d->present = true;
        cache.drawn_inventory = g_ui.inventory;
        buttons_changed = BUTTON_ALL;
        d->flags |= REDRAW_STATUS_BOX | REDRAW_TITLE_ALL | REDRAW_LOAD | REDRAW_BARS | REDRAW_NAME;
    }
    if (buttons_changed) {
        d->flags |= show_changed_buttons(buttons_changed);
        d->present = true;
    }
}

/* Draws the health, stamina, and mana values and the eye and mouth state icons of the inventory. */
static void show_inventory_values(const Champion *c)
{
    restore_panel_back(ZONE_VALUES_BACK);
    show_champion_values(c);
    bool bad = c->food < 0 || c->water < 0 || c->poisoned;
    show_state_icon(bad ? GFX_BOX_SLOT_WOUNDED : GFX_BOX_SLOT_FRAME, ZONE_EYE);
    bool lowered = false;
    for (int stat = STAT_STRENGTH; stat < STAT_COUNT && !lowered; stat++) /* luck does not count */
        lowered = c->stats[stat][STAT_CURRENT] < c->stats[stat][STAT_MAXIMUM];
    show_state_icon(lowered ? GFX_BOX_SLOT_WOUNDED : GFX_BOX_SLOT_FRAME, ZONE_MOUTH);
}

/* Draws the eye, the mouth, and the inventory panel when what the panel shows changed; true when it drew. */
static bool show_inventory_panel(const Champion *c, uint16_t flags)
{
    uint16_t object = panel_object(c);
    if (cache.shown_object != object || g_ui.mouth != cache.shown_mouth || g_ui.eye != cache.shown_eye ||
        flags & REDRAW_TITLE_ALL) {
        ui_draw_viewport_image(CAT_PANEL, 0, GFX_PANEL_EYE, ZONE_EYE, GFX_OPAQUE);
        /* the mouth picture of the held eye follows the mouth picture */
        ui_draw_viewport_image(CAT_PANEL, 0, g_ui.eye ? GFX_PANEL_MOUTH + 1 : GFX_PANEL_MOUTH, ZONE_MOUTH, GFX_OPAQUE);
        if (g_ui.eye) {
            if (object == THING_NONE)
                show_skills_and_stats();
            else
                show_object_info(object, true);
        } else if (g_ui.candidate) {
            if (!g_ui.mouth)
                show_closed_mouth();
            else
                show_food_water();
        } else if (!show_object_info(object, false)) {
            show_food_water();
        }
        cache.shown_object = object;
        cache.shown_eye = g_ui.eye;
        cache.shown_mouth = g_ui.mouth;
        return true;
    }
    if (cache.shown_object != THING_NONE && item_attr(cache.shown_object, ATTR_FLAGS) & ITEM_FLAG_SCROLL_INFO) {
        show_object_info(cache.shown_object, g_ui.eye);
        return true;
    }
    return false;
}

/* Draws the name of champ in its status box, or its name and title in the inventory. */
static void show_champion_name(int champ, ChampionRedraw *d)
{
    const Champion *c = champion(champ);
    int color = g_ui_colors[champ == g_game.leader && !g_ui.candidate ? UI_GOLD : UI_WHITE];
    if (!d->inv) {
        status_box_begin(champ, false);
        d->box_drawn = true;
        offscreen_draw_shadow_text(&status_box, ZONE_STATUS_NAME + champ, color,
                                   g_ui_colors[UI_DARK_GRAY] | TEXT_TRANSPARENT, c->name);
        return;
    }
    char title[TEXT_SIZE];
    snprintf(title, sizeof title, "%.*s", CHAMPION_NAME_SIZE, c->name);
    if (!c->title[0] || !strchr(TITLE_NO_SPACE, c->title[0]))
        strcat(title, " ");
    strncat(title, c->title, CHAMPION_TITLE_SIZE);
    text_draw_in_zone_shadowed(ZONE_NAME, color, g_ui.candidate ? g_ui_colors[UI_DARK_GRAY] : TEXT_TRANSPARENT, title);
    d->present = true;
}

/* Draws the bars, the slots, the inventory panel, the name, and the load of a living champ. */
static void show_living_champion(int champ, ChampionRedraw *d)
{
    Champion *c = champion(champ);
    if (d->flags & REDRAW_BARS) {
        status_box_begin(champ, false);
        d->box_drawn = true;
        show_status_bars(champ);
        if (c->damage_event != THING_NONE)
            show_damage_number(champ);
        if (d->inv) {
            show_inventory_values(c);
            d->present = true;
        }
    }
    if (!d->inv) {
        for (int slot = 0; slot < SLOT_HAND_COUNT; slot++)
            d->box_drawn |= show_changed_slot(champ, slot, d->force_slots);
    } else {
        d->present |= show_inventory_panel(c, d->flags);
        for (int slot = 0; slot < SLOT_COUNT; slot++)
            d->present |= show_changed_slot(champ, slot, d->force_inventory);
    }
    if (d->flags & REDRAW_NAME)
        show_champion_name(champ, d);
    if (d->inv && d->flags & REDRAW_LOAD) {
        show_load(champ);
        d->present = true;
    }
}

void ui_draw_champion_state(int16_t champion_index)
{
    Champion *c = champion(champion_index);
    ChampionRedraw d = {.flags = c->redraw, .inv = in_inventory(champion_index)};
    if (d.inv) {
        if (g_ui.screen_needs_refresh)
            d.flags |= REDRAW_ALL_INVENTORY;
        if (!g_game.sound_volume && !g_game.music_volume)
            game_set_inventory_buttons(g_ui.buttons | BUTTON_SOUND);
        else
            game_set_inventory_buttons(g_ui.buttons & ~BUTTON_SOUND);
    }
    show_inventory_frame(&d);
    if (d.flags & REDRAW_STATUS_BOX) {
        status_box_begin(champion_index, true);
        d.box_drawn = true;
        if (c->health) {
            if (d.inv)
                show_portrait(champion_index);
            d.flags |= d.inv ? REDRAW_BARS : REDRAW_BARS | REDRAW_NAME;
            d.force_slots = true;
        } else {
            offscreen_draw_shadow_text(&status_box, ZONE_STATUS_NAME + champion_index, g_ui_colors[UI_WHITE],
                                       g_ui_colors[UI_DARK_GRAY] | TEXT_TRANSPARENT, c->name);
        }
    }
    if (c->health)
        show_living_champion(champion_index, &d);
    if (d.box_drawn)
        offscreen_flush(&status_box, true);
    if (d.present) {
        game_set_screen_refresh(false);
        play_present_viewport();
    }
    c->redraw &= REDRAW_KEEP;
    draw_free_temp();
}

void ui_draw_all_champion_states(bool tick)
{
    for (int i = 0; i < g_game.champion_count; i++)
        if (i != inventory_champion())
            ui_draw_champion_state(i);
    int16_t open = inventory_champion();
    if (open >= 0) {
        Champion *c = champion(open);
        if (c->aura_value && c->aura >= AURA_STAT_FIRST && c->aura <= AURA_STAT_LAST) {
            bool title_too = g_ui.eye && g_game.leader_hand == THING_NONE; /* the eye shows the stats */
            c->redraw |= title_too ? REDRAW_LOAD | REDRAW_TITLE_ALL : REDRAW_LOAD;
        }
        ui_draw_champion_state(open);
    }
    area_update(tick);
}

void ui_toggle_inventory(int16_t champion_index)
{
    game_set_panel_mode(PANEL_MODE_NONE);
    cache.shown_object = THING_NONE;
    if ((champion_index < CHAMPION_MAX && !champion(champion_index)->health) || g_ui.push_mode || g_ui.mouth ||
        g_ui.eye)
        return;
    game_set_command_stop(true);
    int16_t open = inventory_champion();
    if (champion_index == open)
        champion_index = CHAMPION_MAX;
    if (open >= 0) {
        game_set_open_inventory(0);
        Champion *c = champion(open);
        if (c->health && !g_ui.candidate) {
            c->redraw |= REDRAW_STATUS_BOX;
            ui_draw_champion_state(open);
        }
        /* the raised skills that the player saw no longer show green */
        champ_forget_shown_raises(open, cache.shown_raised);
        if (g_party_sleeping || champion_index == CHAMPION_MAX) {
            if (!g_party_sleeping) {
                if (g_inv_arrows_on_close)
                    ui_draw_movement_arrows();
                view_clear_viewport();
            }
            input_refresh_mode();
            return;
        }
    }
    game_set_open_inventory(champion_index + 1);
    memset(cache.shown_raised, 0, sizeof cache.shown_raised);
    champion(champion_index)->redraw |= REDRAW_ALL;
    game_set_inventory_buttons(0);
    ui_draw_champion_state(champion_index);
    input_set_mode(INPUT_MODE_INVENTORY);
}
