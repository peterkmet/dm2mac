#include <stdio.h>
#include <string.h>
#include "action.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "input.h"
#include "inventory.h"
#include "item.h"
#include "save.h"
#include "text.h"
#include "ui.h"
#include "video.h"
#include "view.h"
#include "zone.h"
#include "money.h"

#define ICON_FRAME 1                /* the slot frame around an icon */
#define FACE_INVISIBLE_FRAMES 4     /* the faces of an invisible party follow the 4 normal faces */
#define BAR_SHADOW 1                /* the status bars have a shadow 1 pixel down and right */
#define GAUGE_SHADOW 2
#define BAR_COUNT 3
#define SCALE_PARTS 10000
#define TENTHS 10
#define FACE_SKIP_COLOR 2           /* the original skips color 2 of the face picture */
#define DAMAGE_STAR_KEY 10          /* transparent color of the damage star */
#define NUMBER_DIGITS 3             /* the values of the status boxes and of the inventory have 3 digits */
#define LOAD_HIGH_PARTS 5           /* a load above 5/8 of the maximum load shows yellow */
#define LOAD_HIGH_WHOLE 8
#define GAUGE_GLYPH_LEFT 9          /* the gauge glyph stands this far left of the gauge */
#define GAUGE_MARK_INSET 3          /* the left end mark stands this far right of the gauge start */
#define GAUGE_TEXT_RAISE 3          /* the base line of the gauge glyphs above the gauge bottom */
/* The number arguments of the inventory texts: the object weight and the champion load in kg and tenths. */
enum { ARG_WEIGHT_KG, ARG_WEIGHT_TENTHS, ARG_LOAD_KG, ARG_LOAD_TENTHS, ARG_LOAD_MAX_KG };

/* Status box backgrounds (1, 2, entry). */
enum { BOX_ALIVE = 0, BOX_DEAD = 1, BOX_DAMAGE = 3, BOX_SLOT_FRAME = 4, BOX_SLOT_WOUNDED = 5, BOX_SLOT_ACTIVE = 6,
       BOX_INVENTORY = 9 };
/* Inventory panel pictures (7, 0, entry). */
enum {
    PANEL_BACK = 0, PANEL_PANEL = 1, PANEL_OBJECT_NAME = 5, PANEL_FOOD = 6, PANEL_WATER = 7,
    PANEL_POISON = 8, PANEL_BUTTON_EXIT = 0x0b, PANEL_BUTTON_SLEEP = 0x0d, PANEL_BUTTON_SAVE = 0x0f,
    PANEL_BUTTON_SOUND = 0x11, PANEL_BUTTON_CLOSE = 0x13, PANEL_MOUTH = 0x20, PANEL_EYE = 0x25,
    PANEL_ICON_GOOD = 4, PANEL_ICON_BAD = 5
};
/* Inventory texts (7, 0, 5, entry). */
enum { TEXT_WEIGHT = 0x1f, TEXT_STAT_FIRST = 0x20, TEXT_LOAD = 0x2a };
/* Inventory buttons: bits of ui.buttons. */
enum { BUTTON_CLOSE = 1, BUTTON_EXIT = 2, BUTTON_SLEEP = 4, BUTTON_SAVE = 8, BUTTON_SOUND = 0x10, BUTTON_ALL = 0x1f };
/* Inventory panel modes. */
enum { PANEL_MODE_NONE, PANEL_MODE_FOOD, PANEL_MODE_SKILLS, PANEL_MODE_OBJECT, PANEL_MODE_SCROLL = 5,
       PANEL_MODE_MOUTH = 7 };
/* Object info gauge glyphs and their scales. */
enum { GLYPH_CHARGES = 0x71, GLYPH_STRENGTH = 0x72, GLYPH_THROW = 0x73, GLYPH_POTION = 0x75, GLYPH_GAUGE_LEFT = 0x60,
       GLYPH_GAUGE_RIGHT = 0x65 };
#define WEAPON_STRENGTH_MAX 80
#define WEAPON_THROW_MAX 100
#define ARMOUR_CLASS_MAX 200
#define ARMOUR_CLASS_MASK 0xff
#define DITHER_COLOR_MASK 0x0f      /* the dither pattern holds 4-bit pixels */
#define GAUGE_CHARGE_COLOR 4
#define GAUGE_VALUE_COLOR 5
#define FOOD_MIN (-0x400)
#define GAUGE_STARVING (-0x200)
#define DESCRIPTION_LINE 18         /* longer description lines break at a space up to this column */
#define DESCRIPTION_BREAK 17
#define SKILL_LEVEL_MAX 16
#define SCROLL_TEXT_MODE 0x8002     /* decode a scroll text: no leading new line, also when the text is hidden */
#define SCROLL_LETTER_FIRST 1       /* the scroll font has its letters at 1 to 26 */
#define SCROLL_HIGH_SHIFT 0xa0      /* characters above 'z' move by this */
#define SCROLL_PICTURE_ENTRY 0x10
#define OBJECT_PICTURE_ENTRY 0x11   /* (category, index, 1, 0x11): the big picture of an item */
#define TITLE_NO_SPACE ",;-"        /* a title that starts with one of these follows the name without a space */

/* The slots of the status boxes (champion * 2 + hand), of the inventory (8 + slot), of the object info,
   and of the open container: zone, icon of the empty slot, and the drawn state. */
#define SLOT_ENTRY_COUNT 55
#define SLOT_STATUS_COUNT 8
#define SLOT_INVENTORY_FIRST 8
#define SLOT_INFO 0x2e
#define SLOT_CONTAINER_INFO_FIRST 0x2f
#define SLOT_FRAMED_COUNT 14        /* slots that have a frame: the hands and the inventory hands to legs */
#define SLOT_BACKGROUND_COUNT 38    /* slots that redraw their background */
#define SLOT_WOUND_COUNT 6
#define SLOT_ACTIVE 0x01
#define SLOT_WOUNDED 0x02
#define NO_ICON 0xff
static const struct { uint16_t zone; uint8_t empty_icon; } slot_layout[SLOT_ENTRY_COUNT] = {
    {0xd1, 0x38}, {0xd2, 0x34}, {0xd3, 0x38}, {0xd4, 0x34}, {0xd5, 0x38}, {0xd6, 0x34}, {0xd7, 0x38}, {0xd8, 0x34},
    {0x1fb, 0x38}, {0x1fc, 0x34}, {0x1fd, 0x28}, {0x1fe, 0x30}, {0x1ff, 0x3c}, {0x200, 0x40}, {0x201, 0xff},
    {0x202, 0xff}, {0x203, 0xff}, {0x204, 0xff}, {0x205, 0x2c}, {0x206, 0x44}, {0x207, 0x45}, {0x208, 0x46},
    {0x209, 0xff}, {0x20a, 0xff}, {0x20b, 0xff}, {0x20c, 0xff}, {0x20d, 0xff}, {0x20e, 0xff}, {0x20f, 0xff},
    {0x210, 0xff}, {0x211, 0xff}, {0x212, 0xff}, {0x213, 0xff}, {0x214, 0xff}, {0x215, 0xff}, {0x216, 0xff},
    {0x217, 0xff}, {0x218, 0xff}, {0xe5, 0xff}, {0xe6, 0xff}, {0xe7, 0xff}, {0xe8, 0xff}, {0xe9, 0xff}, {0xea, 0xff},
    {0xeb, 0xff}, {0xec, 0xff}, {0x1f9, 0xff}, {0x219, 0xff}, {0x21a, 0xff}, {0x21b, 0xff}, {0x21c, 0xff},
    {0x21d, 0xff}, {0x21e, 0xff}, {0x21f, 0xff}, {0x220, 0xff}
};
/* The UI color of the status bars of each champion. */
static const uint8_t champion_colors[CHAMPION_MAX] = {UI_GREEN, UI_YELLOW, UI_RED, UI_BLUE};
/* The text entry (7, 0, entry) of each skill level 0 to 16. */
static const uint8_t skill_level_texts[SKILL_LEVEL_MAX + 1] = {
    0x01, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12
};

int inv_arrows_on_close = 1;

static struct {
    struct { uint8_t icon, gdat_index, flags; uint16_t thing; } slots[SLOT_ENTRY_COUNT];
    int16_t drawn_inventory;    /* the inventory that the panel shows */
    uint16_t drawn_buttons;
    uint16_t shown_object;      /* the object of the panel */
    int16_t shown_eye, shown_mouth;
    int16_t description_x, description_y;
    uint8_t shown_raised[SKILL_BASE_COUNT];   /* the raised skill counts that the skills panel showed */
} cache;
static Offscreen status_box;

/* ---- Drawing helpers ---- */

void ui_blit_bitmap(const Bitmap *src, Bitmap *dst, const int16_t *rect, int src_x, int src_y, int transparent,
                     const uint8_t *colormap)
{
    gfx_blit(src->pixels, dst->pixels, rect, src_x, src_y, src->width, dst->width, (int16_t)transparent, MIRROR_NONE,
             src->bpp, dst->bpp, colormap);
}

void ui_fill_rect_dithered(uint8_t *dst, const int16_t *rect, int ui_color, int dst_width)
{
    if (!rect || !rect[RECT_W] || !rect[RECT_H]) return;
    uint16_t start = (uint16_t)(rect[RECT_Y] * dst_width + rect[RECT_X]);
    int phase = ((start / dst_width) ^ start) & 1;
    uint8_t color = ui_colors[ui_color & DITHER_COLOR_MASK];
    for (int row = 0; row < (uint16_t)rect[RECT_H]; row++, phase ^= 1) {
        uint16_t at = (uint16_t)(start + row * dst_width);
        for (int col = phase; col < (uint16_t)rect[RECT_W]; col += 2) dst[(uint16_t)(at + col)] = color;
    }
}

void ui_draw_gdat_in_viewport_zone(int category, int index, int entry, int zone, int transparent)
{
    DrawCtx c;
    draw_ctx_init(&c, category, index, entry);
    c.transparent = (int16_t)transparent;
    c.dst = &view_viewport;
    c.dx = c.dy = 0;
    c.zone = (int16_t)zone;
    draw_ctx_draw(draw_ctx_prepare(&c));
}

void ui_fill_zone_screen(int zone, int color)
{
    int16_t r[RECT_SIZE];
    if (zone_get_rect(zone, r)) gfx_fill_rect(draw_screen.pixels, r, color, draw_screen.width, IMAGE_8BPP);
}

int16_t *zone_get_icon_rect(int zone, int16_t *rect)
{
    int16_t w = ICON_SIZE, h = ICON_SIZE;
    zone_compute_rect(NULL, rect, (int16_t)zone, &w, &h, ZONE_NO_ANCHOR);
    rect[RECT_X] -= ICON_FRAME;
    rect[RECT_Y] -= ICON_FRAME;
    rect[RECT_W] += 2 * ICON_FRAME;
    rect[RECT_H] += 2 * ICON_FRAME;
    return rect;
}

void zone_get_position(int zone, int16_t *x, int16_t *y)
{
    int16_t r[RECT_SIZE] = {0}, w = 1, h = 1;
    zone_compute_rect(NULL, r, (int16_t)zone, &w, &h, ZONE_NO_ANCHOR);
    *x = r[RECT_X];
    *y = r[RECT_Y];
}

void ui_draw_champion_face_icon(Bitmap *dst, int champ, uint8_t *palette)
{
    const Champion *c = champion(champ);
    int frame = dir_relative(c->dir, game.party_dir) + (game.magic_light[MAGIC_INVISIBILITY] ? FACE_INVISIBLE_FRAMES : 0);
    const uint8_t *colors = draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_FACES, champ);
    if (colors) memcpy(palette, colors, FACE_ICON_COLORS);
    const int16_t r[RECT_SIZE] = {0, 0, FACE_SIZE, FACE_SIZE};
    /* the original skips color 2 of the face picture */
    ui_blit_bitmap(draw_gdat_image(CAT_INTERFACE, UI_INDEX_FACES, champ), dst, r, FACE_SIZE * frame, 0, FACE_SKIP_COLOR, NULL);
}

/* ---- Offscreen bitmaps ---- */

void offscreen_create(Offscreen *o, int zone, int mark_dirty)
{
    if ((int16_t)zone != -1) zone_get_rect(zone, o->rect);
    o->bitmap = draw_temp_bitmap(o->rect[RECT_W], o->rect[RECT_H], IMAGE_8BPP);
    o->dirty_count = 0;
    if (mark_dirty) offscreen_add_dirty_rect(o, o->rect);
}

void offscreen_flush(Offscreen *o, int close)
{
    for (int i = 0; i < o->dirty_count; i++) {
        const int16_t *d = o->dirty[i];
        gfx_blit(o->bitmap->pixels, draw_screen.pixels, d, (int16_t)(d[RECT_X] - o->rect[RECT_X]),
                 (int16_t)(d[RECT_Y] - o->rect[RECT_Y]), o->rect[RECT_W], draw_screen.width, GFX_OPAQUE, MIRROR_NONE, IMAGE_8BPP,
                 IMAGE_8BPP, NULL);
    }
    o->dirty_count = 0;
    if (close) o->bitmap = NULL;  /* draw_free_temp frees it */
}

/* True when rectangle outer holds rectangle inner. */
static int contains(const int16_t *outer, const int16_t *inner)
{
    return outer[RECT_X] <= inner[RECT_X] && inner[RECT_X] + inner[RECT_W] - 1 <= outer[RECT_X] + outer[RECT_W] - 1
           && outer[RECT_Y] <= inner[RECT_Y] && inner[RECT_Y] + inner[RECT_H] - 1 <= outer[RECT_Y] + outer[RECT_H] - 1;
}

void offscreen_add_dirty_rect(Offscreen *o, const int16_t *rect)
{
    if (!rect) return;
    int i = 0;
    for (; i < o->dirty_count; i++) {
        if (contains(o->dirty[i], rect)) return;
        if (contains(rect, o->dirty[i])) break;  /* the new rect replaces a smaller one */
    }
    if (i == o->dirty_count) {
        if (i >= OFFSCREEN_DIRTY_MAX) offscreen_flush(o, 0);
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
        if (d[RECT_H] < 1) {
            o->dirty_count--;
            return;
        }
        d[RECT_Y] = (int16_t)(d[RECT_Y] - delta);
    }
    delta = (int16_t)((o->rect[RECT_X] + o->rect[RECT_W] - 1) - (d[RECT_X] + d[RECT_W] - 1));
    if (delta < 0) d[RECT_W] = (int16_t)(d[RECT_W] + delta);
    delta = (int16_t)((o->rect[RECT_H] + o->rect[RECT_Y] - 1) - (d[RECT_Y] + d[RECT_H] - 1));
    if (delta < 0) d[RECT_H] = (int16_t)(d[RECT_H] + delta);
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
    if (!rect) return;
    int16_t r[RECT_SIZE];
    local_rect(o, rect, r);
    gfx_fill_rect(o->bitmap->pixels, r, (uint16_t)color, o->rect[RECT_W], IMAGE_8BPP);
    offscreen_add_dirty_rect(o, rect);
}

void offscreen_fill_rect_dithered(Offscreen *o, const int16_t *rect, int ui_color)
{
    if (!rect) return;
    int16_t r[RECT_SIZE];
    local_rect(o, rect, r);
    ui_fill_rect_dithered(o->bitmap->pixels, r, ui_color, o->rect[RECT_W]);
    offscreen_add_dirty_rect(o, rect);
}

void offscreen_blit(Offscreen *o, const Bitmap *src, const int16_t *rect, int src_x, int src_y, int transparent,
                    int mirror, const uint8_t *colormap)
{
    if (!rect || !src) return;
    int16_t r[RECT_SIZE];
    local_rect(o, rect, r);
    gfx_blit(src->pixels, o->bitmap->pixels, r, (int16_t)src_x, (int16_t)src_y, src->width, o->rect[RECT_W],
             (int16_t)transparent, (uint16_t)mirror, src->bpp, IMAGE_8BPP, colormap);
    offscreen_add_dirty_rect(o, rect);
}

void offscreen_draw_gdat_image(Offscreen *o, int category, int index, int entry, int zone, int transparent)
{
    const Bitmap *image = draw_gdat_image(category, index, entry);
    int16_t r[RECT_SIZE], sx = 0, sy = 0;
    if (!image) return;
    int16_t *rect = zone_compute_rect(image, r, (int16_t)zone, &sx, &sy, ZONE_NO_ANCHOR);
    offscreen_blit(o, image, rect, sx, sy, transparent, MIRROR_NONE, draw_gdat_image_palette(category, index, entry));
}

void offscreen_draw_ctx(Offscreen *o, DrawCtx *ctx, int zone, int transparent)
{
    int16_t r[RECT_SIZE];
    ctx->dst = o->bitmap;
    if ((int16_t)zone == -1) {
        memcpy(r, ctx->rect, sizeof r);
    } else {
        int16_t w = ctx->w, h = ctx->h;
        if (!ctx->image || !zone_compute_rect(ctx->image, r, (int16_t)zone, &w, &h, ZONE_NO_ANCHOR)) return;
        /* like the original, the clipped part goes into the size of the picture part, not its position */
        ctx->w = (int16_t)(ctx->w + w);
        ctx->h = (int16_t)(ctx->h + h);
    }
    local_rect(o, r, ctx->rect);
    ctx->zone = (int16_t)ZONE_NONE;
    ctx->transparent = (int16_t)transparent;
    draw_ctx_draw(ctx);
    offscreen_add_dirty_rect(o, r);
}

void offscreen_draw_text_at(Offscreen *o, int x, int y, int color, int bg, const char *text)
{
    int16_t w, h;
    if (!text_measure(text, &w, &h)) return;
    text_draw(o->bitmap->pixels, o->rect[RECT_W], (int16_t)(x - o->rect[RECT_X]), (int16_t)(y - o->rect[RECT_Y]),
                  (uint16_t)color, (uint16_t)bg, text, IMAGE_8BPP);
    /* like the original, the changed rect starts at the base line */
    const int16_t r[RECT_SIZE] = {[RECT_X] = (int16_t)x, [RECT_Y] = (int16_t)y, [RECT_W] = w, [RECT_H] = h};
    offscreen_add_dirty_rect(o, r);
}

/* The base line start of text in zone (screen coordinates) and its rectangle; returns 0 when it is not visible. */
static int16_t *text_rect_in_zone(int zone, const char *text, int16_t *r)
{
    int16_t w, h;
    if (!text_measure(text, &w, &h)) return NULL;
    return zone_compute_rect(NULL, r, (int16_t)zone, &w, &h, ZONE_NO_ANCHOR);
}

void offscreen_draw_text_in_zone(Offscreen *o, int zone, int color, int bg, const char *text)
{
    int16_t r[RECT_SIZE];
    if (!text_rect_in_zone(zone, text, r)) return;
    text_draw(o->bitmap->pixels, o->rect[RECT_W], (int16_t)(r[RECT_X] - o->rect[RECT_X]),
                  (int16_t)(r[RECT_Y] + r[RECT_H] - 1 - o->rect[RECT_Y]), (uint16_t)color, (uint16_t)bg, text, IMAGE_8BPP);
    offscreen_add_dirty_rect(o, r);
}

void offscreen_draw_text_in_zone_with_shadow(Offscreen *o, int zone, int color, int bg, const char *text)
{
    int16_t r[RECT_SIZE];
    if (!text_rect_in_zone(zone, text, r)) return;
    text_draw_with_shadow(o->bitmap->pixels, o->rect[RECT_W], (int16_t)(r[RECT_X] - o->rect[RECT_X]),
                          (int16_t)(r[RECT_Y] + r[RECT_H] - 1 - o->rect[RECT_Y]), (uint16_t)color, (int16_t)bg, text);
    offscreen_add_dirty_rect(o, r);
}

/* ---- Screen ---- */

void ui_draw_movement_arrows(void)
{
    enum { ARROWS = 2, PUSH_ARROWS = 0x0e };
    Offscreen o;
    int entry = ui.push_mode ? PUSH_ARROWS : ARROWS;
    offscreen_create(&o, ZONE_MOVEMENT_ARROWS, 1);
    gfx_fill_bitmap(o.bitmap, ui_colors[UI_BLACK]);
    for (int zone = ZONE_ARROW_FIRST; zone < ZONE_ARROW_FIRST + ZONE_ARROW_COUNT; zone++, entry += 2)
        offscreen_draw_gdat_image(&o, CAT_INTERFACE, UI_INDEX_ARROWS, entry, zone, UI_OPAQUE);
    offscreen_flush(&o, 1);
    draw_free_temp();
    input_set_mode(ui.push_mode ? INPUT_MODE_PUSH : INPUT_MODE_MOVE);
}

void ui_reset(void)
{
    memset(&ui, 0, sizeof ui);
    memset(&cache, 0, sizeof cache);
    ui.screen_needs_refresh = 1;
    cache.shown_object = THING_NONE;
    status_box.bitmap = NULL;
    action_reset();
}

void ui_draw_game_screen(void)
{
    ui_fill_zone_screen(ZONE_SCREEN, 0);
    ui_draw_movement_arrows();
    champ_init_party_state();
    ui_draw_all_champion_states(1);
}

/* ---- Status boxes ---- */

static int in_inventory(int champ) { return champ + 1 == ui.inventory; }

/* Opens the status box bitmap of champ with its background; clear: the whole box goes to the screen. */
static void status_box_begin(int champ, int clear)
{
    if (status_box.bitmap) return;
    int background = !champion(champ)->health ? BOX_DEAD : in_inventory(champ) ? BOX_INVENTORY : BOX_ALIVE;
    offscreen_create(&status_box, ZONE_STATUS_BOX + champ, clear);
    offscreen_draw_gdat_image(&status_box, CAT_INTERFACE, UI_INDEX_STATUS, background, ZONE_STATUS_BOX + champ, UI_OPAQUE);
    if (!clear) status_box.dirty_count = 0;
}

/* Draws the health, stamina, and mana bars of a status box. */
static void draw_champion_bars(int champ)
{
    const Champion *c = champion(champ);
    int16_t r[RECT_SIZE];
    offscreen_add_dirty_rect(&status_box, zone_get_rect(ZONE_STATUS_BARS + champ, r));
    int16_t values[BAR_COUNT][2] = {
        {c->health, c->max_health}, {c->stamina, c->max_stamina}, {c->mana, c->max_mana > c->mana ? c->max_mana : c->mana}
    };
    for (int i = 0; i < BAR_COUNT; i++) {
        if (!values[i][1]) continue;
        uint16_t scale = (uint16_t)(values[i][0] * SCALE_PARTS / values[i][1]);
        if (!zone_get_rect_clipped(ZONE_STATUS_BAR_FIRST + champ + CHAMPION_MAX * i, r, SCALE_PARTS, scale)) continue;
        int16_t shadow[RECT_SIZE] = {(int16_t)(r[RECT_X] + BAR_SHADOW), (int16_t)(r[RECT_Y] + BAR_SHADOW), r[RECT_W],
                                     r[RECT_H]};
        offscreen_fill_rect(&status_box, shadow, ui_colors[UI_BLACK]);
        offscreen_fill_rect(&status_box, r, ui_colors[champion_colors[champ]]);
    }
}

/* Draws the damage star with the last damage of a champion in its status box. */
static void draw_damage_number(int champ)
{
    offscreen_draw_gdat_image(&status_box, CAT_INTERFACE, UI_INDEX_STATUS, BOX_DAMAGE, ZONE_STATUS_DAMAGE + champ,
                              DAMAGE_STAR_KEY);
    offscreen_draw_text_in_zone(&status_box, ZONE_STATUS_DAMAGE + champ, ui_colors[UI_WHITE], ui_colors[UI_RED],
                                text_format_number(champion(champ)->damage, 0, NUMBER_DIGITS));
}

/* Draws a value and its maximum ("83/ 83") in zone of the inventory. */
static void draw_value_pair(int zone, unsigned value, unsigned maximum)
{
    char s[TEXT_SIZE];
    strcpy(s, text_format_number(value, 1, NUMBER_DIGITS));
    strcat(s, "/");
    strcat(s, text_format_number(maximum, 1, NUMBER_DIGITS));
    text_draw_in_zone_with_shadow(zone, ui_colors[UI_LIGHT_GRAY], ui_colors[UI_DARK_GRAY] | TEXT_TRANSPARENT, s);
}

/* Draws the health, stamina, and mana values of the inventory. */
static void draw_champion_values(const Champion *c)
{
    draw_value_pair(ZONE_HEALTH_VALUE, (uint16_t)c->health, (uint16_t)c->max_health);
    draw_value_pair(ZONE_STAMINA_VALUE, (uint16_t)(c->stamina / TENTHS), (uint16_t)(c->max_stamina / TENTHS));
    draw_value_pair(ZONE_MANA_VALUE, (uint16_t)c->mana, (uint16_t)c->max_mana);
}

/* Draws the good or bad state icon over the eye or the mouth of the inventory. */
static void draw_panel_icon(int icon, int zone)
{
    int16_t r[RECT_SIZE];
    zone_get_icon_rect(zone, r);
    ui_blit_bitmap(draw_gdat_image(CAT_INTERFACE, UI_INDEX_STATUS, icon), &view_viewport, r, 0, 0, UI_KEY,
                    draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_STATUS, icon));
}

/* Draws the portrait of the champion with the open inventory, dimmed while the party is invisible. */
static void draw_champion_portrait(int champ)
{
    offscreen_draw_gdat_image(&status_box, CAT_CHAMPION, champion(champ)->portrait, 0,
                              ZONE_STATUS_PORTRAIT + champ, UI_OPAQUE);
    if (game.magic_light[MAGIC_INVISIBILITY]) {
        int16_t r[RECT_SIZE];
        offscreen_fill_rect_dithered(&status_box, zone_get_rect(ZONE_STATUS_PORTRAIT + champ, r), UI_DARKEST_GRAY);
    }
}

/* Draws an inventory button (save, sleep, sound, exit, close), pressed when its bit is set. */
static void draw_inventory_button(int icon, int zone, uint16_t mask)
{
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, ui.buttons & mask ? icon + 1 : icon, zone, UI_OPAQUE);
}

/* Copies the part of the inventory background under zone into the view picture. */
static void restore_panel_back(int zone)
{
    int16_t r[RECT_SIZE];
    zone_get_rect(zone, r);
    ui_blit_bitmap(draw_gdat_image(CAT_PANEL, 0, PANEL_BACK), &view_viewport, r, r[RECT_X], r[RECT_Y], UI_OPAQUE,
                    draw_gdat_image_palette(CAT_PANEL, 0, PANEL_BACK));
}

/* Draws one slot: background, frame (active hand, wound), and the item icon. */
static void draw_slot(uint16_t thing, int slot, int wounded, int active, int draw_frame)
{
    int category, index, icon;
    if (thing == THING_NONE) {
        category = CAT_PANEL;
        index = 0;
        icon = (uint8_t)(slot_layout[slot].empty_icon + wounded);
    } else {
        category = item_gdat_category(thing);
        index = item_gdat_index(thing);
        icon = item_icon_index(thing, slot < SLOT_STATUS_COUNT ? slot % SLOT_HAND_COUNT : slot - SLOT_INVENTORY_FIRST, 1);
    }
    int zone = slot_layout[slot].zone;
    int16_t r[RECT_SIZE];
    if (slot < SLOT_BACKGROUND_COUNT) {
        zone_get_rect(zone, r);
        if (slot < SLOT_STATUS_COUNT)
            offscreen_blit(&status_box, draw_gdat_image(CAT_INTERFACE, UI_INDEX_STATUS, BOX_ALIVE), r,
                           (int16_t)(r[RECT_X] - status_box.rect[RECT_X]), (int16_t)(r[RECT_Y] - status_box.rect[RECT_Y]),
                           UI_OPAQUE, MIRROR_NONE, draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_STATUS, BOX_ALIVE));
        else
            restore_panel_back(zone);
    }
    if (draw_frame && slot < SLOT_FRAMED_COUNT) {
        zone_get_icon_rect(zone, r);
        int frame = active ? BOX_SLOT_ACTIVE : wounded ? BOX_SLOT_WOUNDED : BOX_SLOT_FRAME;
        const Bitmap *image = draw_gdat_image(CAT_INTERFACE, UI_INDEX_STATUS, frame);
        const uint8_t *colors = draw_gdat_image_palette(CAT_INTERFACE, UI_INDEX_STATUS, frame);
        if (slot < SLOT_STATUS_COUNT) offscreen_blit(&status_box, image, r, 0, 0, UI_KEY, MIRROR_NONE, colors);
        else ui_blit_bitmap(image, &view_viewport, r, 0, 0, UI_KEY, colors);
    }
    if (icon == NO_ICON) return;
    if (slot < SLOT_STATUS_COUNT) offscreen_draw_gdat_image(&status_box, category, index, icon, zone, UI_KEY);
    else ui_draw_gdat_in_viewport_zone(category, index, icon, zone, UI_KEY);
}

/* Draws a champion slot when it changed; returns 1 when it drew. */
static int draw_champion_slot(int champ, int slot, int force)
{
    int entry;
    if (in_inventory(champ)) {
        entry = slot + SLOT_INVENTORY_FIRST;
    } else {
        if (slot >= SLOT_HAND_COUNT || ui.candidate == champ + 1) return 0;
        entry = champ * SLOT_HAND_COUNT + slot;
    }
    const Champion *c = champion(champ);
    uint16_t thing = c->slots[slot];
    int redraw_frame = force, active = 0, wounded = 0, changed = force;
    if (slot < SLOT_HAND_COUNT) {
        active = champ + 1 == action.champion && slot == action.hand;
        if (!(cache.slots[entry].flags & SLOT_ACTIVE) != !active) {
            redraw_frame = 1;
            cache.slots[entry].flags ^= SLOT_ACTIVE;
        }
    }
    if (slot < SLOT_WOUND_COUNT) {
        wounded = (c->wounds & 1 << slot) != 0;
        if (!(cache.slots[entry].flags & SLOT_WOUNDED) != !wounded) {
            redraw_frame = 1;
            cache.slots[entry].flags ^= SLOT_WOUNDED;
        }
    }
    if (thing != THING_NONE) {
        uint8_t icon = (uint8_t)item_icon_index(thing, slot, (item_attr(thing, ATTR_FLAGS) & ITEM_FLAG_ICON_IN_HAND) != 0);
        if (icon != cache.slots[entry].icon) {
            changed = 1;
            cache.slots[entry].icon = icon;
        }
        uint8_t index = item_gdat_index(thing);
        if (index != cache.slots[entry].gdat_index) {
            changed = 1;
            cache.slots[entry].gdat_index = index;
        }
    }
    if (!changed && !redraw_frame && cache.slots[entry].thing == thing) return 0;
    if (thing == THING_NONE || entry < SLOT_STATUS_COUNT) redraw_frame = 1;
    if (entry < SLOT_STATUS_COUNT) status_box_begin(champ, 0);
    cache.slots[entry].thing = thing;
    draw_slot(thing, entry, wounded, active, redraw_frame);
    return 1;
}

/* ---- Inventory panel ---- */

/* Draws description lines (17 characters) under each other; "\f" at the start goes back to the first line. */
static void draw_description_text(const char *text)
{
    if (*text == '\f') {
        text++;
        zone_get_position(ZONE_DESCRIPTION, &cache.description_x, &cache.description_y);
    }
    char buf[TEXT_SIZE];
    snprintf(buf, sizeof buf, "%s", text);
    for (char *line = buf; *line;) {
        char *brk = NULL;
        if (strlen(line) > DESCRIPTION_LINE) {
            for (brk = line + DESCRIPTION_BREAK; *brk != ' '; brk--) {}
            *brk = '\0';
        }
        text_draw_in_viewport(cache.description_x, cache.description_y, ui_colors[UI_LIGHT_GRAY], line);
        cache.description_y = (int16_t)(cache.description_y + TEXT_LINE_HEIGHT);
        if (brk) line = brk + 1;
        else *line = '\0';
    }
}

/* Draws a centered line of a scroll in the scroll font. */
static void draw_scroll_line(int center_x, int y, const char *line)
{
    int16_t w, h;
    if (!text_measure(line, &w, &h)) return;
    char mapped[TEXT_SIZE];
    size_t n = 0;
    for (; line[n] && n + 1 < sizeof mapped; n++) {
        uint8_t c = (uint8_t)line[n];
        if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + SCROLL_LETTER_FIRST);
        else if (c > 'z') c = (uint8_t)(c + SCROLL_HIGH_SHIFT);
        mapped[n] = (char)c;
    }
    mapped[n] = '\0';
    text_draw_in_viewport((int16_t)(center_x - (w >> 1)), y, ui_colors[UI_BLACK], mapped);
}

/* Draws an open scroll with its text in the inventory panel. */
static void draw_scroll(uint16_t scroll)
{
    char text[TEXT_SIZE * 2], line[TEXT_SIZE * 2];
    text_decode_dungeon(text, sizeof text, scroll, SCROLL_TEXT_MODE);
    ui.panel_mode = PANEL_MODE_SCROLL;
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_PANEL, ZONE_INVENTORY_PANEL, UI_OPAQUE);
    ui_draw_gdat_in_viewport_zone(CAT_SCROLL, 0, SCROLL_PICTURE_ENTRY, ZONE_INVENTORY_PANEL, UI_KEY);
    int newlines = 0;
    size_t n = strlen(text);
    for (size_t i = 0; i < n; i++) newlines += text[i] == '\n';
    int lines = newlines + 1;
    if (n && text[n - 1] == '\n') lines = n > 1 && text[n - 2] == '\n' ? newlines - 1 : newlines;
    int16_t x, y, r[RECT_SIZE];
    zone_get_position(ZONE_SCROLL_TEXT, &x, &y);
    zone_get_rect(ZONE_INVENTORY_PANEL, r);
    y = (int16_t)(y - ((((int16_t)lines * TEXT_LINE_HEIGHT - FONT_BASELINE - 1) >> 1) - FONT_GLYPH_HEIGHT));
    for (int16_t pos = 0; text[pos];) {
        text_wrap_line(text, line, &pos, r[RECT_W]);
        draw_scroll_line(x, y, line);
        y = (int16_t)(y + TEXT_LINE_HEIGHT);
        if (text[pos] == '\n') pos++;
    }
}

/* A gauge bar in zone: value from low to high; split fills the rest of the zone with UI color split. */
static void draw_gauge_bar(int value, int zone, int color, int low, int high, int split)
{
    value = (int16_t)value;
    if (value < GAUGE_STARVING) color = UI_RED;
    else if (value < 0) color = UI_YELLOW;
    int16_t bar[RECT_SIZE], full[RECT_SIZE], shadow[RECT_SIZE];
    uint16_t scale = (uint16_t)((int16_t)(value - low) * SCALE_PARTS / (high - (int16_t)low));
    if (!zone_get_rect_clipped(zone, bar, scale, SCALE_PARTS)) return;
    if (split) {
        zone_get_rect(zone, full);
        memcpy(shadow, full, sizeof shadow);
        if (bar[RECT_X] + bar[RECT_W] - 1 == full[RECT_X] + full[RECT_W] - 1) {
            split = 0;
        } else {
            full[RECT_W] = (int16_t)((full[RECT_X] + full[RECT_W] - 1) - (bar[RECT_X] + bar[RECT_W] - 1));
            full[RECT_X] = (int16_t)(bar[RECT_X] + bar[RECT_W]);
        }
    } else {
        memcpy(shadow, bar, sizeof shadow);
    }
    shadow[RECT_X] = (int16_t)(shadow[RECT_X] + GAUGE_SHADOW);
    shadow[RECT_Y] = (int16_t)(shadow[RECT_Y] + GAUGE_SHADOW);
    gfx_fill_rect(view_viewport.pixels, shadow, ui_colors[UI_BLACK], view_viewport.width, IMAGE_8BPP);
    gfx_fill_rect(view_viewport.pixels, bar, ui_colors[color], view_viewport.width, IMAGE_8BPP);
    if (split) gfx_fill_rect(view_viewport.pixels, full, ui_colors[split], view_viewport.width, IMAGE_8BPP);
}

/* An object gauge: the bar with its glyph left of it and the end marks. */
static void draw_object_gauge(int zone, int value, int high, uint8_t glyph, int color)
{
    int16_t r[RECT_SIZE];
    if (!zone_get_rect(zone, r)) return;
    draw_gauge_bar(value, zone, color, 0, high, 1);
    int y = (int16_t)(r[RECT_Y] + r[RECT_H] - GAUGE_TEXT_RAISE), bg = ui_colors[UI_BLACK] | TEXT_TRANSPARENT;
    char s[2] = {(char)glyph, '\0'};
    text_draw_with_shadow(view_viewport.pixels, view_viewport.width, (int16_t)(r[RECT_X] - GAUGE_GLYPH_LEFT), y,
                          ui_colors[color], bg, s);
    s[0] = GLYPH_GAUGE_LEFT;
    text_draw_with_shadow(view_viewport.pixels, view_viewport.width, (int16_t)(r[RECT_X] + GAUGE_MARK_INSET), y,
                          ui_colors[UI_WHITE], bg, s);
    s[0] = GLYPH_GAUGE_RIGHT;
    text_draw_with_shadow(view_viewport.pixels, view_viewport.width,
                          (int16_t)(r[RECT_X] + r[RECT_W] - 1 - FONT_GLYPH_HEIGHT - 1), y, ui_colors[UI_WHITE], bg, s);
}

/* Draws the coin count of each coin slot of a money box and the total value. */
static void draw_coin_counts(uint16_t box)
{
    int16_t counts[COIN_TYPE_MAX];
    money_count_coins_in_box(box, counts);
    int total = 0, zone = ZONE_COIN_COUNT_FIRST;
    char s[TEXT_SIZE];
    for (int slot = 0; slot < COIN_TYPE_MAX; slot++) {
        int type = money_coin_type_for_slot(box, slot);
        if (type < 0) continue;
        total += money_coin_value(type) * counts[type];
        snprintf(s, sizeof s, "%d", counts[type]);
        text_draw_in_zone(zone++, ui_colors[UI_LIGHT_GRAY], s);
    }
    snprintf(s, sizeof s, "%d", total);
    text_draw_in_zone(ZONE_COIN_TOTAL, ui_colors[UI_LIGHT_GRAY], s);
}

/* Draws the items of a chest in the object info. */
static void draw_container_contents(uint16_t container)
{
    uint16_t t = thing_word(container, CONTAINER_CONTENTS);
    for (int i = 0; t != THING_ENDOFLIST && i < CONTAINER_SLOTS; i++, t = dungeon_next_thing(t))
        draw_slot(t, SLOT_CONTAINER_INFO_FIRST + i, 0, 0, 0);
}

/* Draws the gauges of an object of type in the object info: its charges, and the strength or throw power of a
   weapon, the armour class of an armour, or the power of a potion. */
static void draw_object_gauges(uint16_t thing, int type)
{
    static const int16_t gauge_zones[2] = {ZONE_OBJECT_GAUGE_FIRST, ZONE_OBJECT_GAUGE_FIRST + 1};
    int gauge = 0;
    if (item_has_charged_action(thing)) {
        int charges = (int16_t)item_charges(thing);
        if (charges) {
            draw_object_gauge(gauge_zones[0], charges, (int16_t)item_max_charges(thing), GLYPH_CHARGES, GAUGE_CHARGE_COLOR);
            gauge = 1;
        }
    }
    uint8_t glyph;
    int value, high;
    if (type == THING_WEAPON) {
        glyph = GLYPH_STRENGTH;
        high = WEAPON_STRENGTH_MAX;
        value = item_attr(thing, ATTR_STRENGTH);
        if (!value) {
            value = item_attr(thing, ATTR_THROW_POWER);
            glyph = GLYPH_THROW;
            high = WEAPON_THROW_MAX;
        }
    } else if (type == THING_ARMOUR) {
        glyph = GLYPH_STRENGTH;
        value = item_attr(thing, ATTR_ARMOUR_CLASS) & ARMOUR_CLASS_MASK;
        high = ARMOUR_CLASS_MAX;
    } else if (type == THING_POTION) {
        glyph = GLYPH_POTION;
        value = potion_power(thing);
        high = POTION_POWER_MAX;
    } else {
        return;
    }
    if ((uint16_t)value) draw_object_gauge(gauge_zones[gauge], (int16_t)value, high, glyph, GAUGE_VALUE_COLOR);
}

/* Draws the info panel of an object (full: its picture, name, weight, and gauges; else only a scroll);
   returns 0 when the panel shows nothing. */
static int draw_object_info(uint16_t thing, int full)
{
    if (thing == THING_NONE) return 0;
    draw_description_text("\f");
    int type = thing_type(thing);
    if (type == THING_SCROLL) {
        draw_scroll(thing);
        return 1;
    }
    ui.panel_mode = PANEL_MODE_OBJECT;
    if (!full) return 0;
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_PANEL, ZONE_INVENTORY_PANEL, UI_OPAQUE);
    int index = item_gdat_index(thing), category = item_gdat_category(thing);
    if (gdat_exists(gdat_current(), category, index, GDAT_TYPE_IMAGE, OBJECT_PICTURE_ENTRY))
        ui_draw_gdat_in_viewport_zone(category, index, OBJECT_PICTURE_ENTRY, ZONE_INVENTORY_PANEL, UI_KEY);
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_OBJECT_NAME, ZONE_OBJECT_NAME_BACK, UI_KEY);
    text_draw_in_zone(ZONE_OBJECT_NAME, ui_colors[UI_LIGHT_GRAY], item_name(thing));
    draw_slot(thing, SLOT_INFO, 0, 0, 0);
    uint16_t weight = (uint16_t)item_weight(thing);
    text_args.number[ARG_WEIGHT_KG] = (int16_t)(weight / TENTHS);
    text_args.number[ARG_WEIGHT_TENTHS] = (int16_t)(weight % TENTHS);
    char text[TEXT_SIZE];
    draw_description_text(text_gdat(CAT_PANEL, 0, TEXT_WEIGHT, text));
    if (type == THING_CONTAINER) {
        if (item_is_chest(thing)) draw_container_contents(thing);
        else if (item_is_moneybox(thing)) draw_coin_counts(thing);
        return 1;
    }
    draw_object_gauges(thing, type);
    return 1;
}

/* Draws the food and water gauges (and the poison gauge) of the inventory panel. */
static void draw_food_water_panel(void)
{
    const Champion *c = champion(ui.inventory - 1);
    ui.panel_mode = PANEL_MODE_FOOD;
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_PANEL, ZONE_INVENTORY_PANEL, UI_OPAQUE);
    draw_gauge_bar(c->food, ZONE_FOOD_GAUGE, UI_BROWN, FOOD_MIN, FOOD_MAX, 0);
    draw_gauge_bar(c->water, ZONE_WATER_GAUGE, UI_BLUE, FOOD_MIN, FOOD_MAX, 0);
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_FOOD, ZONE_FOOD_LABEL, UI_KEY);
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_WATER, ZONE_WATER_LABEL, UI_KEY);
    if (c->poisoned) {
        draw_gauge_bar(c->poison, ZONE_POISON_GAUGE, UI_RED, 0, POISON_MAX, 0);
        ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_POISON, ZONE_POISON_LABEL, UI_KEY);
    }
}

/* Draws the closed mouth of the inventory panel while a champion is recruited. */
static void draw_mouth_picture(void)
{
    enum { MOUTH_CLOSED = 0xfa };
    ui_draw_gdat_in_viewport_zone(MOUTH_CATEGORY, MOUTH_INDEX, MOUTH_CLOSED, ZONE_INVENTORY_PANEL, UI_OPAQUE);
    ui.panel_mode = PANEL_MODE_MOUTH;
}

/* Draws the skill levels and the statistics of the champion in the inventory panel. */
static void draw_skills_and_statistics(void)
{
    int champ = ui.inventory - 1;
    Champion *c = champion(champ);
    char text[TEXT_SIZE];
    int16_t x, y, stat_x, unused;
    ui.panel_mode = PANEL_MODE_SKILLS;
    ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_PANEL, ZONE_INVENTORY_PANEL, UI_OPAQUE);
    zone_get_position(ZONE_SKILLS, &x, &y);
    for (int skill = 0; skill < SKILL_BASE_COUNT; skill++) {
        int level = champ_get_skill_level(champ, skill, 1);
        if (level > SKILL_LEVEL_MAX) level = SKILL_LEVEL_MAX;
        if (level == 1) continue;
        text_args.entry = (uint8_t)skill;
        text_gdat(CAT_PANEL, 0, skill_level_texts[level], text);
        text_draw_in_viewport(x, y, ui_colors[champ_skill_raised[champ][skill] ? UI_GREEN : UI_LIGHT_GRAY], text);
        cache.shown_raised[skill] = champ_skill_raised[champ][skill];
        y = (int16_t)(y + TEXT_LINE_HEIGHT);
    }
    zone_get_position(ZONE_STATS, &stat_x, &unused);
    y = unused;
    for (int stat = 1; stat < STAT_COUNT; stat++) {
        text_draw_in_viewport(x, y, ui_colors[UI_LIGHT_GRAY], text_gdat(CAT_PANEL, 0, TEXT_STAT_FIRST + stat, text));
        int16_t current = (int16_t)champ_get_stat(c, stat, 0);
        int maximum = (uint16_t)champ_get_stat(c, stat, 1);
        int color = current < maximum ? UI_RED : current > maximum ? UI_GREEN : UI_LIGHT_GRAY;
        text_draw_in_viewport(stat_x, y, ui_colors[color], text_format_number((uint16_t)current, 1, NUMBER_DIGITS));
        strcpy(text, "/");
        strcat(text, text_format_number((uint16_t)maximum, 1, NUMBER_DIGITS));
        text_draw_in_viewport((int16_t)(FONT_ADVANCE * NUMBER_DIGITS + stat_x), y, ui_colors[UI_LIGHT_GRAY], text);
        y = (int16_t)(y + TEXT_LINE_HEIGHT);
    }
}

/* The object that the inventory panel shows: the leader hand object with the eye, none with the mouth, else the
   action hand object. */
static uint16_t panel_object(const Champion *c)
{
    if (ui.eye) return game.leader_hand;
    if (ui.mouth) return THING_NONE;
    return c->slots[SLOT_ACTION_HAND];
}

/* Draws the load and the maximum load of the champion in the inventory. */
static void draw_load(int champ)
{
    restore_panel_back(ZONE_LOAD_BACK);
    uint16_t load = (uint16_t)champ_get_load(champ);
    int16_t max_load = (int16_t)champ_get_max_load(champion(champ));
    int color = load > max_load ? UI_RED : load * LOAD_HIGH_WHOLE > max_load * LOAD_HIGH_PARTS ? UI_YELLOW : UI_LIGHT_GRAY;
    text_args.number[ARG_LOAD_KG] = (int16_t)(load / TENTHS);
    text_args.number[ARG_LOAD_TENTHS] = (int16_t)(load % TENTHS);
    text_args.number[ARG_LOAD_MAX_KG] = (int16_t)((uint16_t)champ_get_max_load(champion(champ)) / TENTHS);
    char text[TEXT_SIZE];
    text_draw_in_zone_with_shadow(ZONE_LOAD, ui_colors[color], ui_colors[UI_DARK_GRAY] | TEXT_TRANSPARENT,
                                  text_gdat(CAT_PANEL, 0, TEXT_LOAD, text));
}

/* The redraw of one champion: what to draw, and what it drew. */
typedef struct {
    uint16_t flags;             /* REDRAW_* of what to draw */
    int inv;                    /* the champion has the open inventory */
    int force_slots, force_inventory;
    int present;                /* the inventory changed: the view goes to the screen */
    int box_drawn;              /* the status box changed */
} ChampionRedraw;

/* Draws the inventory buttons whose pressed state changed; returns the redraw flags that they add. */
static uint16_t draw_changed_buttons(uint16_t changed)
{
    uint16_t flags = 0;
    if (changed & BUTTON_SOUND) draw_inventory_button(PANEL_BUTTON_SOUND, ZONE_BUTTON_SOUND, BUTTON_SOUND);
    if (!ui.candidate) {
        if (changed & BUTTON_CLOSE) {
            draw_inventory_button(PANEL_BUTTON_CLOSE, ZONE_BUTTON_CLOSE, BUTTON_CLOSE);
            flags |= REDRAW_NAME;
        }
        if (changed & BUTTON_SAVE) draw_inventory_button(PANEL_BUTTON_SAVE, ZONE_BUTTON_SAVE, BUTTON_SAVE);
        if (changed & BUTTON_SLEEP) draw_inventory_button(PANEL_BUTTON_SLEEP, ZONE_BUTTON_SLEEP, BUTTON_SLEEP);
    }
    if (changed & BUTTON_EXIT) draw_inventory_button(PANEL_BUTTON_EXIT, ZONE_BUTTON_EXIT, BUTTON_EXIT);
    cache.drawn_buttons = ui.buttons;
    return flags;
}

/* Draws the inventory background and the changed buttons of champ; sets the redraw flags of a new inventory. */
static void draw_inventory_frame(ChampionRedraw *d)
{
    uint16_t buttons_changed = 0;
    if (ui.inventory == cache.drawn_inventory && !(d->flags & REDRAW_ALL_INVENTORY)) {
        if (d->inv) buttons_changed = ui.buttons ^ cache.drawn_buttons;
    } else if (!d->inv) {
        cache.drawn_inventory = 0;
    } else {
        ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_BACK, ZONE_INVENTORY_BACK, UI_OPAQUE);
        d->force_inventory = d->present = 1;
        cache.drawn_inventory = ui.inventory;
        buttons_changed = BUTTON_ALL;
        d->flags |= REDRAW_STATUS_BOX | REDRAW_TITLE_ALL | REDRAW_LOAD | REDRAW_BARS | REDRAW_NAME;
    }
    if (buttons_changed) {
        d->flags |= draw_changed_buttons(buttons_changed);
        d->present = 1;
    }
}

/* Draws the health, stamina, and mana values and the eye and mouth state icons of the inventory. */
static void draw_inventory_values(const Champion *c)
{
    restore_panel_back(ZONE_VALUES_BACK);
    draw_champion_values(c);
    int bad = c->food < 0 || c->water < 0 || c->poisoned;
    draw_panel_icon(bad ? PANEL_ICON_BAD : PANEL_ICON_GOOD, ZONE_EYE);
    int lowered = 0;
    for (int stat = 1; stat < STAT_COUNT && !lowered; stat++) lowered = c->stats[stat][0] < c->stats[stat][1];
    draw_panel_icon(lowered ? PANEL_ICON_BAD : PANEL_ICON_GOOD, ZONE_MOUTH);
}

/* Draws the eye, the mouth, and the inventory panel when what the panel shows changed; returns 1 when it drew. */
static int draw_inventory_panel(const Champion *c, uint16_t flags)
{
    uint16_t object = panel_object(c);
    if (cache.shown_object != object || ui.mouth != cache.shown_mouth || ui.eye != cache.shown_eye
        || flags & REDRAW_TITLE_ALL) {
        ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_EYE, ZONE_EYE, UI_OPAQUE);
        ui_draw_gdat_in_viewport_zone(CAT_PANEL, 0, PANEL_MOUTH + (ui.eye != 0), ZONE_MOUTH, UI_OPAQUE);
        if (ui.eye) {
            if (object == THING_NONE) draw_skills_and_statistics();
            else draw_object_info(object, 1);
        } else if (ui.candidate) {
            if (!ui.mouth) draw_mouth_picture();
            else draw_food_water_panel();
        } else if (!draw_object_info(object, 0)) {
            draw_food_water_panel();
        }
        cache.shown_object = object;
        cache.shown_eye = ui.eye;
        cache.shown_mouth = ui.mouth;
        return 1;
    }
    if (cache.shown_object != THING_NONE && item_attr(cache.shown_object, ATTR_FLAGS) & ITEM_FLAG_SCROLL_INFO) {
        draw_object_info(cache.shown_object, ui.eye);
        return 1;
    }
    return 0;
}

/* Draws the name of champ in its status box, or its name and title in the inventory. */
static void draw_champion_name(int champ, ChampionRedraw *d)
{
    const Champion *c = champion(champ);
    int color = ui_colors[champ == game.leader && !ui.candidate ? UI_GOLD : UI_WHITE];
    if (!d->inv) {
        status_box_begin(champ, 0);
        d->box_drawn = 1;
        offscreen_draw_text_in_zone_with_shadow(&status_box, ZONE_STATUS_NAME + champ, color,
                                                ui_colors[UI_DARK_GRAY] | TEXT_TRANSPARENT, c->name);
        return;
    }
    char title[TEXT_SIZE];
    snprintf(title, sizeof title, "%.*s", CHAMPION_NAME_SIZE, c->name);
    if (!c->title[0] || !strchr(TITLE_NO_SPACE, c->title[0])) strcat(title, " ");
    strncat(title, c->title, CHAMPION_TITLE_SIZE);
    text_draw_in_zone_with_shadow(ZONE_INVENTORY_TITLE, color, ui.candidate ? ui_colors[UI_DARK_GRAY] : TEXT_TRANSPARENT,
                                  title);
    d->present = 1;
}

/* Draws the bars, the slots, the inventory panel, the name, and the load of a living champ. */
static void draw_living_champion(int champ, ChampionRedraw *d)
{
    Champion *c = champion(champ);
    if (d->flags & REDRAW_BARS) {
        status_box_begin(champ, 0);
        d->box_drawn = 1;
        draw_champion_bars(champ);
        if (c->damage_event != THING_NONE) draw_damage_number(champ);
        if (d->inv) {
            draw_inventory_values(c);
            d->present = 1;
        }
    }
    if (!d->inv) {
        for (int slot = 0; slot < SLOT_HAND_COUNT; slot++) d->box_drawn |= draw_champion_slot(champ, slot, d->force_slots);
    } else {
        d->present |= draw_inventory_panel(c, d->flags);
        for (int slot = 0; slot < SLOT_COUNT; slot++) d->present |= draw_champion_slot(champ, slot, d->force_inventory);
    }
    if (d->flags & REDRAW_NAME) draw_champion_name(champ, d);
    if (d->inv && d->flags & REDRAW_LOAD) {
        draw_load(champ);
        d->present = 1;
    }
}

void ui_draw_champion_state(int champ)
{
    Champion *c = champion(champ);
    ChampionRedraw d = {.flags = c->redraw, .inv = in_inventory(champ)};
    if (d.inv) {
        if (ui.screen_needs_refresh) d.flags |= REDRAW_ALL_INVENTORY;
        if (!game.sound_volume && !game.music_volume) ui.buttons |= BUTTON_SOUND;
        else ui.buttons &= ~BUTTON_SOUND;
    }
    draw_inventory_frame(&d);
    if (d.flags & REDRAW_STATUS_BOX) {
        status_box_begin(champ, 1);
        d.box_drawn = 1;
        if (c->health) {
            if (d.inv) draw_champion_portrait(champ);
            d.flags |= d.inv ? REDRAW_BARS : REDRAW_BARS | REDRAW_NAME;
            d.force_slots = 1;
        } else {
            offscreen_draw_text_in_zone_with_shadow(&status_box, ZONE_STATUS_NAME + champ, ui_colors[UI_WHITE],
                                                    ui_colors[UI_DARK_GRAY] | TEXT_TRANSPARENT, c->name);
        }
    }
    if (c->health) draw_living_champion(champ, &d);
    if (d.box_drawn) offscreen_flush(&status_box, 1);
    if (d.present) {
        ui.screen_needs_refresh = 0;
        game_present_viewport();
    }
    c->redraw &= REDRAW_KEEP;
    draw_free_temp();
}

void ui_set_champion_redraw_flags(uint16_t flags)
{
    for (int i = 0; i < game.champion_count; i++) champion(i)->redraw |= flags;
}

void ui_draw_all_champion_states(int tick)
{
    for (int i = 0; i < game.champion_count; i++)
        if (i + 1 != ui.inventory) ui_draw_champion_state(i);
    if (ui.inventory) {
        Champion *c = champion(ui.inventory - 1);
        if (c->aura_value && c->aura >= AURA_STAT_FIRST && c->aura <= AURA_STAT_LAST)
            c->redraw |= !ui.eye || game.leader_hand != THING_NONE ? REDRAW_LOAD : REDRAW_LOAD | REDRAW_TITLE_ALL;
        ui_draw_champion_state(ui.inventory - 1);
    }
    action_update_area(tick);
}

void ui_toggle_inventory(int champ)
{
    ui.panel_mode = PANEL_MODE_NONE;
    cache.shown_object = THING_NONE;
    if ((champ < CHAMPION_MAX && !champion(champ)->health) || ui.push_mode || ui.mouth || ui.eye) return;
    input_command_stop = 1;
    int open = ui.inventory;
    if (champ + 1 == open) champ = CHAMPION_MAX;
    if (open) {
        ui.inventory = 0;
        Champion *c = champion(open - 1);
        if (c->health && !ui.candidate) {
            c->redraw |= REDRAW_STATUS_BOX;
            ui_draw_champion_state(open - 1);
        }
        /* the raised skills that the player saw no longer show green */
        for (int i = 0; i < SKILL_BASE_COUNT; i++)
            champ_skill_raised[open - 1][i] = (uint8_t)(champ_skill_raised[open - 1][i] - cache.shown_raised[i]);
        if (party_sleeping || champ == CHAMPION_MAX) {
            if (!party_sleeping) {
                if (inv_arrows_on_close) ui_draw_movement_arrows();
                view_clear_viewport();
            }
            input_refresh_mode();
            return;
        }
    }
    ui.inventory = (int16_t)(champ + 1);
    memset(cache.shown_raised, 0, sizeof cache.shown_raised);
    champion(champ)->redraw |= REDRAW_ALL;
    ui.buttons = 0;
    ui_draw_champion_state(champ);
    input_set_mode(INPUT_MODE_INVENTORY);
}
