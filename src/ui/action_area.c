/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The action area: the hands and the spell icons of the party, the open hand or spell panel, and the result of
   an action. */
#include "action_area.h"
#include "ids.h"
#include "action.h"
#include "champ_action.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "item.h"
#include "minion.h"
#include "money.h"
#include "random.h"
#include "minion_map.h"
#include "text.h"
#include "ui.h"
#include "zone.h"
#include <string.h>

/* Action area pictures (1, 4, entry) and (1, 5, entry). */
enum {
    ACTION_HAND_BACK_FIRST = 2,
    ACTION_EMPTY_HAND = 7,
    ACTION_MENU_BACK = 0x0e,
    ACTION_FACE_LEFT = 0x10,
    ACTION_FACE_RIGHT = 0x12,
    ACTION_MENU_HEADER = 0x14,
    ACTION_BUTTON = 0x15,
    ACTION_SPELL_HAND_LEFT = 6,
    ACTION_SPELL_HAND_RIGHT = 8,
    ACTION_SPELL_RUNE_LEFT = 0x0a,
    ACTION_SPELL_RUNE_RIGHT = 0x0c,
    SPELL_RUNES_BACK = 9,
    ACTION_RESULT_MISS = 0x17,
    ACTION_RESULT_NOTHING = 0x18,
    ACTION_RESULT_CANT = 0x19,
    ACTION_RESULT_DAMAGE = 1,
    RESULT_SPELL_FIZZLE = 0x0e,
    RESULT_SPELL_NEEDS = 0x0c,
    RESULT_SPELL_OTHER = 0x0d
};
/* Wall set pictures (8, set, entry) behind the party and the action champion. */
enum { WALLSET_PARTY_BACK = 0xf5, WALLSET_FACE_BACK = 0xf6 };
#define RESULT_SMALL_MAX        15 /* damage up to this shows the small damage star */
#define RESULT_SCALED_MAX       40
#define RESULT_SMALL_SCALE      31
#define RESULT_LARGE_SCALE      47
#define RESULT_SCALE_Y          53
#define RESULT_DIGITS           5
#define AMMO_ATTR               5
#define AMMO_NOT_AMMO           0x8000
#define AMMO_HAND_DEFAULT       0x38
#define AMMO_HAND_OTHER         0x34
#define AMMO_ICON               0x18
#define CONTAINER_PICTURE       0x10
#define CONTAINER_LID           0x12
#define CONTAINER_LID_KEY       10
#define MONEY_PICTURE           0x10
#define COIN_PILE_MAX           32 /* the most coins of a type that a pile shows */
#define COIN_PILE_MASK          0x1f
#define COIN_ICON               0x18
#define MAP_ACTION_PICTURE_BASE 0x41
#define SPELL_RUNE_SETS         4 /* the rune sets 0 to 3; the panel of set 4 has no runes */
#define AURA_FRAMES             4 /* the aura picture holds 4 x 4 frames: direction by game time */
#define ICON_OFFSET             1 /* the icon of an item in a hand casts a shadow 1 pixel up and left */
#define ICON_SHADOW_REMAP       1 /* GDAT (1, 0, 7, 1): the colors of an item icon in a hand */
#define FLAGS_MINION_HEADER     0x0400
#define ICON_KEY_COLOR          UI_RED /* the background of a hand icon bitmap; the blit skips it */
#define UI_INDEX_AURA           7
#define NO_CELL                 0xff
#define ICON_COLORS             16 /* the colors of a 4 bpp picture */
#define ICON_BPP                8
#define FACE_BPP                4
#define JITTER_CENTER           2 /* haste_jitter: a roll of 1 to 3 moves by -1 to 1 */
#define PILE_SLOT_STEP          8 /* the coin pile of each slot starts 8 offsets further */

static Offscreen area;
static struct {
    uint8_t cell[CHAMPION_MAX], dir[CHAMPION_MAX]; /* the drawn cell and direction of each champion */
    int alive[CHAMPION_MAX];
    uint8_t busy[CHAMPION_MAX][3]; /* the drawn busy state of the hands and the spell */
    uint8_t gfx_set;
    int invisible;
    uint16_t aura_mask;
    int16_t leader;
} drawn;

void area_reset_champion(int champ)
{
    drawn.cell[champ] = drawn.dir[champ] = NO_CELL;
    drawn.alive[champ] = 0;
    memset(drawn.busy[champ], 0, sizeof drawn.busy[champ]);
}

void area_reset(void)
{
    memset(&drawn, 0, sizeof drawn);
    drawn.gfx_set = NO_CELL;
    drawn.leader = -1;
    area.bitmap = NULL;
}

/* Opens the action area bitmap; clear: it starts black and goes to the screen as a whole. */
static void begin_draw(int clear)
{
    if (area.bitmap)
        return;
    offscreen_create(&area, ZONE_ACTION_AREA, clear);
    if (clear)
        offscreen_fill_rect(&area, area.rect, ui_colors[UI_BLACK]);
}

Offscreen *area_begin_draw(int clear)
{
    begin_draw(clear);
    return &area;
}

/* Draws the result of an action: the damage in a star, or the picture of a failure. */
static void draw_action_result(int16_t value)
{
    begin_draw(1);
    int index = UI_INDEX_ACTION, entry = ACTION_RESULT_DAMAGE;
    if (value < 0) {
        if (value == RESULT_NOTHING)
            entry = ACTION_RESULT_NOTHING;
        else if (value == RESULT_NO_AMMO)
            entry = ACTION_RESULT_MISS;
        else if (value == RESULT_CANT)
            entry = ACTION_RESULT_CANT;
        else {
            index = UI_INDEX_SPELL;
            entry = value == RESULT_UNKNOWN_SPELL   ? RESULT_SPELL_FIZZLE
                    : value == RESULT_WIZARD_FAILED ? RESULT_SPELL_NEEDS
                                                    : RESULT_SPELL_OTHER;
        }
    }
    DrawCtx c;
    draw_ctx_init(&c, CAT_INTERFACE, index, entry);
    if (value >= 0 && value <= RESULT_SCALED_MAX) {
        c.scale_x = value > RESULT_SMALL_MAX ? RESULT_LARGE_SCALE : RESULT_SMALL_SCALE;
        c.scale_y = RESULT_SCALE_Y;
    }
    offscreen_draw_ctx(&area, draw_ctx_prepare(&c), ZONE_RESULT, UI_OPAQUE);
    if (value >= 0) {
        char digits[RESULT_DIGITS] = {0};
        int at = RESULT_DIGITS - 1;
        do {
            digits[--at] = (char)('0' + value % DECIMAL_BASE);
            value = (int16_t)(value / DECIMAL_BASE);
        } while (value);
        offscreen_draw_text_in_zone(&area, ZONE_RESULT, ui_colors[UI_CYAN], ui_colors[UI_BLACK], digits + at);
    } else if (value == RESULT_NO_AMMO) {
        offscreen_draw_gdat_image(&area, CAT_PANEL, 0, action.result_flag ? AMMO_HAND_OTHER : AMMO_HAND_DEFAULT,
                                  ZONE_AMMO_HAND, UI_OPAQUE);
        for (int slot_index = 0; slot_index < GDAT_NO_INDEX; slot_index++) {
            uint16_t ammo = gdat_entry_index(gdat_current(), CAT_WEAPON, slot_index, GDAT_TYPE_VALUE, AMMO_ATTR);
            if (ammo && !(ammo & AMMO_NOT_AMMO) && ammo & action.result_ammo) {
                offscreen_draw_gdat_image(&area, CAT_WEAPON, slot_index, AMMO_ICON, ZONE_AMMO_ITEM, UI_OPAQUE);
                return;
            }
        }
    }
}

/* The icon of the item (or the empty hand) in a hand, with its shadow, on the icon background color; NULL when
   the hand has no action. */
static Bitmap *action_hand_icon(int champ, int hand)
{
    uint16_t thing = champion(champ)->slots[hand];
    if (!champ_list_actions(champ, thing, hand))
        return NULL;
    Bitmap *icon = draw_temp_bitmap(ICON_SIZE + ICON_OFFSET, ICON_SIZE + ICON_OFFSET, ICON_BPP);
    if (!icon)
        return NULL;
    int empty = thing == THING_NONE;
    DrawCtx c;
    if (empty)
        draw_ctx_init(&c, CAT_INTERFACE, UI_INDEX_STATUS, ACTION_EMPTY_HAND + (hand == SLOT_ACTION_HAND));
    else
        draw_ctx_init(&c, item_gdat_category(thing), item_gdat_index(thing), item_icon_index(thing, -1, 0));
    draw_ctx_prepare(&c);
    gfx_fill_bitmap(icon, ui_colors[ICON_KEY_COLOR]);
    if (!c.image)
        return icon;
    if (!empty) {
        uint8_t shadow[ICON_COLORS];
        memset(shadow, ui_colors[UI_BLACK], sizeof shadow);
        shadow[UI_KEY] = ui_colors[ICON_KEY_COLOR];
        const int16_t r[RECT_SIZE] = {ICON_OFFSET, ICON_OFFSET, ICON_SIZE, ICON_SIZE};
        ui_blit_bitmap(c.image, icon, r, 0, 0, UI_OPAQUE, shadow);
        uint32_t size;
        const uint8_t *remap = gdat_item(
            gdat_current(), gdat_find(gdat_current(), CAT_INTERFACE, 0, GDAT_TYPE_REMAP, ICON_SHADOW_REMAP), &size);
        if (remap) {
            if (c.palette_count < 1) {
                memcpy(c.palette, remap, REMAP_COLORS);
                c.palette_count = REMAP_COLORS;
            } else {
                for (int i = c.palette_count - 1; i >= 0; i--)
                    c.palette[i] = remap[c.palette[i]];
            }
        }
    }
    const int16_t r[RECT_SIZE] = {0, 0, empty ? c.w : ICON_SIZE, empty ? c.h : ICON_SIZE};
    ui_blit_bitmap(c.image, icon, r, 0, 0, UI_KEY, c.palette_count > 0 ? c.palette : NULL);
    return icon;
}

/* Draws a hand of a champion in the action area: its back, its item icon, dimmed while it is busy. */
void area_draw_hand_icon(int champ, int hand, int flash)
{
    begin_draw(0);
    const Champion *c = champion(champ);
    int zone = (hand == SLOT_ACTION_HAND ? ZONE_ACTION_HAND_FIRST : ZONE_READY_HAND_FIRST) +
               dir_relative(c->cell, game.party_dir);
    int16_t r[RECT_SIZE];
    zone_get_rect(zone, r);
    if (!c->health) {
        offscreen_fill_rect(&area, r, ui_colors[UI_BLACK]);
        return;
    }
    offscreen_draw_gdat_image(&area, CAT_INTERFACE, UI_INDEX_ACTION,
                              (uint8_t)(hand * 2 + ACTION_HAND_BACK_FIRST + flash), zone, UI_OPAQUE);
    Bitmap *icon = action_hand_icon(champ, hand);
    if (icon) {
        int16_t centered[RECT_SIZE] = {(int16_t)(((r[RECT_W] - icon->width + 1) >> 1) + r[RECT_X]),
                                       (int16_t)(r[RECT_Y] + ((r[RECT_H] - icon->height + 1) >> 1)), icon->width,
                                       icon->height};
        offscreen_blit(&area, icon, centered, 0, 0, ui_colors[ICON_KEY_COLOR], MIRROR_NONE, NULL);
    }
    if (c->action_delay[hand] || party_sleeping)
        offscreen_fill_rect_dithered(&area, r, UI_BLACK);
}

/* Draws the aura of champ over its face in zone: frame by direction and game time. */
static void draw_aura(int champ, int zone)
{
    const Champion *c = champion(champ);
    if (!c->aura_value)
        return;
    DrawCtx a;
    draw_ctx_init(&a, CAT_INTERFACE, UI_INDEX_AURA, c->aura);
    draw_ctx_prepare(&a);
    a.w = (int16_t)(a.w / AURA_FRAMES);
    a.x = (int16_t)(dir_relative(c->dir, game.party_dir) * a.w);
    a.h = (int16_t)(a.h / AURA_FRAMES);
    a.y = (int16_t)((game.game_time & (AURA_FRAMES - 1)) * a.h);
    offscreen_draw_ctx(&area, &a, zone, UI_KEY);
}

/* Moves a face picture by -1 to 1 pixels while the party hastes. */
static void haste_jitter(int16_t *r)
{
    if (!game.magic_light[MAGIC_HASTE])
        return;
    int d = rand_2bits();
    if (!d)
        return;
    r[RECT_Y] = (int16_t)(r[RECT_Y] + d - JITTER_CENTER);
    d = rand_2bits();
    if (d)
        r[RECT_X] = (int16_t)(r[RECT_X] + d - JITTER_CENTER);
}

/* A cleared bitmap for a champion face icon. */
static Bitmap *face_bitmap(void)
{
    Bitmap *face = draw_temp_bitmap(FACE_SIZE, FACE_SIZE, FACE_BPP);
    if (face)
        memset(face->pixels, 0, (FACE_SIZE + 1) / 2 * FACE_SIZE);
    return face;
}

/* Draws the faces of the party in their cells, with the auras. */
static void draw_party_icons(void)
{
    begin_draw(0);
    offscreen_draw_gdat_image(&area, CAT_WALLSET, drawn.gfx_set, WALLSET_PARTY_BACK, ZONE_PARTY_ICONS, UI_OPAQUE);
    Bitmap *face = face_bitmap();
    for (int i = 0; face && i < game.champion_count; i++) {
        const Champion *c = champion(i);
        int cell = dir_relative(c->cell, game.party_dir);
        if (!c->health || input_drag_state == cell + 1)
            continue;
        int16_t r[RECT_SIZE], sx = 0, sy = 0;
        int zone = ZONE_PARTY_FACE_FIRST + cell;
        if (!zone_compute_rect(face, r, zone, &sx, &sy, ZONE_NO_ANCHOR))
            continue;
        haste_jitter(r);
        uint8_t palette[ICON_COLORS];
        ui_draw_champion_face_icon(face, i, palette);
        offscreen_blit(&area, face, r, sx, sy, UI_KEY, MIRROR_NONE, palette);
        draw_aura(i, zone);
    }
    if (party_sleeping) {
        int16_t r[RECT_SIZE];
        offscreen_fill_rect_dithered(&area, zone_get_rect(ZONE_PARTY_ICONS, r), UI_BLACK);
    }
}

/* Draws the spell hand and the rune icon of a champion in the action area. */
void area_draw_spell_icon(int champ, int flash)
{
    begin_draw(0);
    const Champion *c = champion(champ);
    int rel = dir_relative(c->cell, game.party_dir);
    int16_t r[RECT_SIZE];
    offscreen_fill_rect(&area, zone_get_rect(ZONE_SPELL_ICON_FIRST + rel, r), ui_colors[UI_BLACK]);
    if (!c->health)
        return;
    int mirror = rel == 1 || rel == 2;
    int rune = rel <= 1 ? ACTION_SPELL_RUNE_LEFT : ACTION_SPELL_RUNE_RIGHT;
    int hand = rel <= 1 ? ACTION_SPELL_HAND_LEFT : ACTION_SPELL_HAND_RIGHT;
    if (champ == game.leader)
        rune++;
    if (flash)
        hand++;
    DrawCtx d;
    draw_ctx_init(&d, CAT_INTERFACE, UI_INDEX_ACTION, hand);
    d.flip = (int16_t)mirror;
    offscreen_draw_ctx(&area, draw_ctx_prepare(&d), ZONE_SPELL_HAND_ICON_FIRST + rel, UI_CYAN);
    if (party_sleeping || c->action_delay[ACTION_SPELL]) {
        int16_t s[RECT_SIZE] = {(int16_t)(d.rect[RECT_X] + area.rect[RECT_X]),
                                (int16_t)(d.rect[RECT_Y] + area.rect[RECT_Y]), d.rect[RECT_W], d.rect[RECT_H]};
        offscreen_fill_rect_dithered(&area, s, UI_BLACK);
    }
    draw_ctx_init(&d, CAT_INTERFACE, UI_INDEX_ACTION, rune);
    d.flip = (int16_t)mirror;
    offscreen_draw_ctx(&area, draw_ctx_prepare(&d), ZONE_SPELL_RUNE_ICON_FIRST + rel, UI_CYAN);
    if (party_sleeping) {
        int16_t s[RECT_SIZE] = {(int16_t)(d.rect[RECT_X] + area.rect[RECT_X]),
                                (int16_t)(d.rect[RECT_Y] + area.rect[RECT_Y]), d.rect[RECT_W], d.rect[RECT_H]};
        offscreen_fill_rect_dithered(&area, s, UI_BLACK);
    }
}

/* Draws the header of an open hand or spell panel with the champion name. */
static void draw_menu_header(void)
{
    offscreen_draw_gdat_image(&area, CAT_INTERFACE, UI_INDEX_ACTION, ACTION_MENU_HEADER, ZONE_MENU_HEADER, UI_OPAQUE);
    offscreen_draw_gdat_image(&area, CAT_INTERFACE, UI_INDEX_ACTION, ACTION_MENU_BACK, ZONE_MENU_BACK, UI_OPAQUE);
    int champ = action.champion - 1;
    offscreen_draw_text_in_zone_with_shadow(&area, ZONE_MENU_NAME, ui_colors[champ == game.leader ? UI_GOLD : UI_WHITE],
                                            ui_colors[UI_DARKEST_GRAY] | TEXT_TRANSPARENT, champion(champ)->name);
}

/* Draws the face of the champion whose hand or spell panel is open. */
static void draw_action_champion_face(void)
{
    begin_draw(0);
    offscreen_draw_gdat_image(&area, CAT_WALLSET, drawn.gfx_set, WALLSET_FACE_BACK, ZONE_FACE_BACK, UI_OPAQUE);
    Bitmap *face = face_bitmap();
    int16_t r[RECT_SIZE], sx = 0, sy = 0;
    if (face && zone_compute_rect(face, r, ZONE_FACE, &sx, &sy, ZONE_NO_ANCHOR)) {
        haste_jitter(r);
        int champ = action.champion - 1;
        uint8_t palette[ICON_COLORS];
        ui_draw_champion_face_icon(face, champ, palette);
        offscreen_blit(&area, face, r, sx, sy, UI_KEY, MIRROR_NONE, palette);
        draw_aura(champ, ZONE_FACE);
    }
    offscreen_draw_gdat_image(&area, CAT_INTERFACE, UI_INDEX_ACTION, ACTION_FACE_LEFT, ZONE_FACE_LEFT, UI_OPAQUE);
    offscreen_draw_gdat_image(&area, CAT_INTERFACE, UI_INDEX_ACTION, ACTION_FACE_RIGHT, ZONE_FACE_RIGHT, UI_OPAQUE);
}

/* Draws the runes that the champion entered in the spell panel. */
void area_draw_entered_runes(int clear)
{
    begin_draw(0);
    if (clear)
        offscreen_draw_gdat_image(&area, CAT_INTERFACE, UI_INDEX_SPELL, SPELL_RUNES_BACK, ZONE_RUNES_BACK, UI_OPAQUE);
    const Champion *c = champion(action.champion - 1);
    size_t n = strnlen(c->runes, CHAMPION_SIZE - offsetof(Champion, runes));
    for (size_t i = 0; i < n; i++) {
        char s[2] = {c->runes[i], '\0'};
        offscreen_draw_text_in_zone(&area, ZONE_RUNE_ENTERED_FIRST + (int)i, ui_colors[UI_BLACK],
                                    ui_colors[UI_LIGHT_GRAY] | TEXT_TRANSPARENT, s);
    }
}

/* Draws the spell panel: the runes of the next set, the entered runes, the face. */
static void draw_spell_panel(void)
{
    int set = champion(action.champion - 1)->rune_count;
    offscreen_draw_gdat_image(&area, CAT_INTERFACE, UI_INDEX_SPELL, (uint8_t)(set + 1), ZONE_PANEL, UI_OPAQUE);
    if (set < SPELL_RUNE_SETS) {
        for (int i = 0; i < SPELL_RUNES_PER_SET; i++) {
            char s[2] = {(char)(set * SPELL_RUNES_PER_SET + SPELL_RUNE_GLYPH_FIRST + i), '\0'};
            offscreen_draw_text_in_zone(&area, ZONE_RUNE_CHOICE_FIRST + i, ui_colors[UI_BLACK],
                                        ui_colors[UI_LIGHT_GRAY] | TEXT_TRANSPARENT, s);
        }
    }
    area_draw_entered_runes(0);
    draw_action_champion_face();
}

/* Draws the button and the name of action i of the action menu. */
void area_draw_name(int i, int highlight)
{
    begin_draw(0);
    const ChampionAction *a = &champ_actions[i];
    if (action.flags) {
        offscreen_draw_gdat_image(&area, CAT_CONTAINER, action.map_item,
                                  (uint8_t)((a->entry - ACTION_ENTRY_FIRST) * 2 + MAP_ACTION_PICTURE_BASE + highlight),
                                  ZONE_MAP_ACTION_FIRST + i, UI_OPAQUE);
        return;
    }
    offscreen_draw_gdat_image(&area, CAT_INTERFACE, UI_INDEX_ACTION, ACTION_BUTTON + highlight,
                              ZONE_ACTION_BUTTON_FIRST + i, UI_OPAQUE);
    offscreen_draw_text_in_zone_with_shadow(&area, ZONE_ACTION_NAME_FIRST + i, ui_colors[UI_WHITE],
                                            ui_colors[UI_BLACK] | TEXT_TRANSPARENT,
                                            text_gdat_label(a->category, a->index, a->entry));
}

/* Draws the action menu of the open hand. */
static void draw_action_menu(void)
{
    for (int i = 0; i < champ_action_count; i++)
        area_draw_name(i, 0);
    draw_action_champion_face();
}

/* Draws an open money box with a pile of coins for each coin slot. */
static void draw_money_box(uint16_t box)
{
    /* the offsets of up to 32 coins in a pile, from SKULL.EXE */
    static const int8_t pile[COIN_PILE_MAX][2] = {
        {-2, 10 },
        {-8, 3  },
        {7,  -3 },
        {-6, 10 },
        {8,  -7 },
        {-8, 11 },
        {2,  -2 },
        {8,  -11},
        {0,  0  },
        {4,  8  },
        {-8, -10},
        {6,  7  },
        {2,  2  },
        {-6, 9  },
        {-3, -11},
        {2,  11 },
        {0,  -11},
        {-5, -7 },
        {-3, 3  },
        {8,  11 },
        {0,  -5 },
        {6,  -9 },
        {-7, 3  },
        {5,  1  },
        {4,  -7 },
        {-8, -3 },
        {8,  3  },
        {-3, -2 },
        {0,  7  },
        {-3, -4 },
        {-4, 0  },
        {2,  -11}
    };
    offscreen_draw_gdat_image(&area, CAT_CONTAINER, item_gdat_index(box), MONEY_PICTURE, ZONE_PANEL, UI_OPAQUE);
    int16_t counts[COIN_TYPE_MAX];
    money_count_coins_in_box(box, counts);
    for (int slot = 0; slot < COIN_TYPE_MAX; slot++) {
        int type = money_coin_type_for_slot(box, slot);
        if (type < 0 || counts[type] <= 0)
            continue;
        uint16_t item = money_coin_item(type);
        int index = item_index_gdat_index(item), category = item_index_gdat_category(item);
        const Bitmap *image = draw_gdat_image(category, index, COIN_ICON);
        const uint8_t *colors = draw_gdat_image_palette(category, index, COIN_ICON);
        int16_t r[RECT_SIZE], sx = 0, sy = 0;
        zone_compute_rect(image, r, ZONE_COIN_PILE_FIRST + slot, &sx, &sy, ZONE_NO_ANCHOR);
        int n = counts[type], at = slot * PILE_SLOT_STEP + (n > COIN_PILE_MAX ? n - COIN_PILE_MAX : 0);
        n = n < COIN_PILE_MAX ? n : COIN_PILE_MAX;
        do {
            at = (at + 1) & COIN_PILE_MASK;
            int16_t coin[RECT_SIZE] = {(int16_t)(r[RECT_X] + pile[at][0]), (int16_t)(r[RECT_Y] + pile[at][1]),
                                       r[RECT_W], r[RECT_H]};
            int16_t cx = 0, cy = 0, d;
            /* clip the coin to the action area */
            if ((d = (int16_t)(area.rect[RECT_X] - coin[RECT_X])) > 0) {
                cx = d;
                coin[RECT_X] += d;
                coin[RECT_W] -= d;
            }
            if ((d = (int16_t)(area.rect[RECT_Y] - coin[RECT_Y])) > 0) {
                cy = d;
                coin[RECT_Y] += d;
                coin[RECT_H] -= d;
            }
            d = (int16_t)((coin[RECT_W] + coin[RECT_X] - 1) - (area.rect[RECT_X] + area.rect[RECT_W] - 1));
            if (d > 0)
                coin[RECT_W] -= d;
            d = (int16_t)((coin[RECT_H] + coin[RECT_Y] - 1) - (area.rect[RECT_Y] + area.rect[RECT_H] - 1));
            if (d > 0)
                coin[RECT_H] -= d;
            offscreen_blit(&area, image, coin, cx, cy, UI_KEY, MIRROR_NONE, colors);
        } while (--n > 0);
    }
}

/* Draws an open chest with its items; full also draws the chest and its lid. */
static void draw_container(uint16_t container, int full)
{
    int index = item_gdat_index(container);
    int16_t back[RECT_SIZE] = {0};
    if (full) {
        offscreen_draw_gdat_image(&area, CAT_CONTAINER, index, CONTAINER_PICTURE, ZONE_PANEL, UI_OPAQUE);
        offscreen_draw_gdat_image(&area, CAT_CONTAINER, index, CONTAINER_LID, ZONE_CONTAINER_LID, CONTAINER_LID_KEY);
    } else {
        zone_get_rect(ZONE_PANEL, back);
    }
    for (int i = 0; i < CONTAINER_SLOTS; i++) {
        uint16_t t = action.open_items[i];
        if (t == THING_NONE)
            continue;
        int zone = ZONE_CONTAINER_SLOT_FIRST + i;
        if (!full) {
            int16_t r[RECT_SIZE];
            zone_get_rect(zone, r);
            offscreen_blit(&area, draw_gdat_image(CAT_CONTAINER, index, CONTAINER_PICTURE), r,
                           (int16_t)(r[RECT_X] - back[RECT_X]), (int16_t)(r[RECT_Y] - back[RECT_Y]), UI_OPAQUE,
                           MIRROR_NONE, draw_gdat_image_palette(CAT_CONTAINER, index, CONTAINER_PICTURE));
        }
        int icon = item_icon_index(t, SLOT_CONTAINER_FIRST + i, 1);
        offscreen_draw_gdat_image(&area, item_gdat_category(t), item_gdat_index(t), icon, zone, UI_KEY);
    }
}

/* Draws the hands and the spell icons of the party; full redraws all of them. */
static void draw_party_hands(int full, int changed)
{
    int party_changed = changed;
    action.mode = ACTION_MODE_HANDS;
    for (int i = 0; i < game.champion_count; i++) {
        const Champion *c = champion(i);
        if (!c->health) {
            int k = 0;
            for (; k < game.champion_count; k++)
                if (champion(k)->cell == c->cell && champion(k)->health)
                    break;
            if (k < game.champion_count)
                continue; /* a living champion stands in the cell of this dead one */
        }
        int redraw = 0;
        int alive = c->health > 0;
        if (alive != drawn.alive[i]) {
            drawn.alive[i] = alive;
            redraw = 1;
        }
        int flash_this = (action.flash < 0 ? -action.flash : action.flash) - 1 == i;
        for (int hand = 0; hand < SLOT_HAND_COUNT; hand++) {
            int flash = 0, update = 0;
            if (flash_this && action.flash_hand == hand) {
                flash = action.flash > 0;
                update = 1;
            }
            uint8_t busy = c->action_delay[hand] != 0;
            if (busy != drawn.busy[i][hand]) {
                drawn.busy[i][hand] = busy;
                update = 1;
            }
            if (action.hand_items[i][hand] != c->slots[hand] || full || redraw) {
                action.hand_items[i][hand] = c->slots[hand];
                update = 1;
            }
            if (full || redraw || update)
                area_draw_hand_icon(i, hand, flash);
        }
        int flash = 0;
        if (flash_this && action.flash_hand == ACTION_HAND_SPELL) {
            flash = action.flash > 0;
            redraw = 1;
        }
        uint8_t busy = c->action_delay[ACTION_SPELL] != 0;
        if (busy != drawn.busy[i][ACTION_SPELL]) {
            drawn.busy[i][ACTION_SPELL] = busy;
            redraw = 1;
        }
        int16_t leader = (int16_t)game.leader;
        if (full || redraw || (drawn.leader != leader && (drawn.leader == i || leader == i)))
            area_draw_spell_icon(i, flash);
        if (c->aura_value || drawn.aura_mask)
            party_changed = 1;
        if (c->aura_value)
            drawn.aura_mask |= 1 << i;
        else
            drawn.aura_mask &= ~(1 << i);
    }
    if (action.flash < 0)
        action.flash = 0;
    action.flash = (int16_t)-action.flash;
    if (full || party_changed)
        draw_party_icons();
}

/* Tracks the drawn cells and directions of the champions, the wall set, haste, and invisibility; returns 1 when the
   party icons must redraw. */
static int party_icons_changed(void)
{
    int changed = 0;
    for (int i = 0; i < game.champion_count; i++) {
        const Champion *c = champion(i);
        uint8_t dir = (uint8_t)dir_relative(c->dir, game.party_dir),
                cell = (uint8_t)dir_relative(c->cell, game.party_dir);
        if (!c->health || input_drag_state == cell + 1)
            cell = NO_CELL;
        if (cell != drawn.cell[i] || dir != drawn.dir[i]) {
            changed = 1;
            if (cell != NO_CELL)
                action.dirty = 1;
            drawn.cell[i] = cell;
            drawn.dir[i] = dir;
        }
    }
    int invisible = game.magic_light[MAGIC_INVISIBILITY] != 0;
    if (drawn.gfx_set != dungeon_map_wallset(game.party_map) || game.magic_light[MAGIC_HASTE] ||
        !drawn.invisible != !invisible) {
        changed = 1;
        drawn.gfx_set = dungeon_map_wallset(game.party_map);
        drawn.invisible = invisible;
    }
    return changed;
}

/* Draws the panel of the open hand: a minion map, a money box, a chest, or the action menu; with nothing to show,
   the hand closes. */
static void draw_hand_panel(void)
{
    int champ = action.champion - 1;
    uint16_t thing = champion(champ)->slots[action.hand];
    if (item_is_minion_map(thing) && !action.flags) {
        minion_map_open(champ);
    } else if (item_is_moneybox(thing)) {
        action.mode = ACTION_MODE_MONEY_BOX;
    } else if (item_is_chest(thing) && action.mode != ACTION_MODE_CONTAINER) {
        action_open_container(thing);
        action.mode = ACTION_MODE_CONTAINER;
    }
    if (action.mode == ACTION_MODE_MONEY_BOX) {
        draw_money_box(thing);
    } else if (action.mode == ACTION_MODE_CONTAINER) {
        draw_container(thing, 1);
    } else if (champ_list_actions(champ, thing, action.hand) && action.mode != ACTION_MODE_MINION_MAP) {
        action.mode = ACTION_MODE_MENU;
        draw_action_menu();
    } else if (action.mode == ACTION_MODE_MINION_MAP) {
        mmap_draw(thing);
    } else {
        action.champion = 0;
        action.mode = ACTION_MODE_HANDS;
    }
}

/* Draws the open hand or spell panel when it changed; returns 0 while the panel waits for its redraw delay. */
static int draw_open_panel(int full, int party_changed, int tick)
{
    if (action.redraw_delay || full || party_changed || (action.mode == ACTION_MODE_MINION_MAP && tick)) {
        if (action.redraw_delay > 1) {
            action.redraw_delay--;
            return 0;
        }
        if (action.flags && !action.redraw_delay && !full) {
            begin_draw(0);
        } else {
            begin_draw(1);
            draw_menu_header();
            action.flags &= ~FLAGS_MINION_HEADER;
        }
        if (action.hand == ACTION_HAND_SPELL) {
            action.mode = ACTION_MODE_SPELL;
            draw_spell_panel();
        } else {
            draw_hand_panel();
        }
        action.redraw_delay = 0;
    } else if (action.mode == ACTION_MODE_CONTAINER && tick) {
        begin_draw(0);
        draw_container(champion(action.champion - 1)->slots[action.hand], 0);
    }
    return 1;
}

/* The face of the champion of the open panel redraws while it has an aura, and once after the aura ends. */
static void draw_open_face_aura(void)
{
    static const uint8_t mode_shows_face[] = {0, 1, 1, 0, 0, 0, 0};
    if (action.champion <= 0 || action.mode < 0 || !mode_shows_face[action.mode])
        return;
    const Champion *c = champion(action.champion - 1);
    if (c->aura_value || drawn.aura_mask) {
        draw_action_champion_face();
        if (c->aura_value)
            drawn.aura_mask |= 1 << (action.champion - 1);
        else
            drawn.aura_mask &= ~(1 << (action.champion - 1));
    }
}

void area_update(int tick)
{
    if (ui.candidate)
        return;
    int full = 0, party_changed = 0;
    if (!game.champion_count) {
        if (action.dirty || action.redraw_delay) {
            action.redraw_delay = 0;
            action.dirty = 0;
            begin_draw(1);
        }
        action.mode = ACTION_MODE_NONE;
        goto flush;
    }
    if (tick)
        action_tick_hands();
    if (tick && action.mode == ACTION_MODE_MINION_MAP)
        minion_map_drain_mana();
    if (action.result) {
        draw_action_result(action.result);
        action.dirty = 1;
        action.result = 0;
        action.mode = ACTION_MODE_RESULT;
        goto flush;
    }
    if (action.champion > 0 && action.hand >= 0 && action.hand != ACTION_HAND_SPELL &&
        action.hand_items[action.champion - 1][action.hand] != champion(action.champion - 1)->slots[action.hand])
        action_area_close();
    if (action.champion >= 0)
        party_changed = party_icons_changed();
    if (action.dirty) {
        if (action.redraw_delay > 1) {
            action.redraw_delay--;
            goto flush;
        }
        begin_draw(1);
        action.dirty = 0;
        full = 1;
    }
    if (!action.champion)
        draw_party_hands(full, party_changed);
    drawn.leader = (int16_t)game.leader;
    if (action.champion > 0 && !draw_open_panel(full, party_changed, tick))
        goto flush;
    draw_open_face_aura();
flush:
    if (area.bitmap)
        offscreen_flush(&area, 1);
    draw_free_temp();
}
