/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The action area: the hands and the spell icons of the party, the open hand or spell panel, and the result of
   an action. */
#include "action_area.h"
#include <string.h>
#include "action.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "game_text.h"
#include "ids.h"
#include "input.h"
#include "item.h"
#include "minion.h"
#include "minion_map.h"
#include "money.h"
#include "num.h"
#include "random.h"
#include "text.h"
#include "ui.h"
#include "zone.h"

/* The damage star of an action result: the damage limits of its sizes, its scales, and its digits. */
#define RESULT_SMALL_MAX   15 /* damage up to this shows the small damage star */
#define RESULT_SCALED_MAX  40
#define RESULT_SMALL_SCALE 31
#define RESULT_LARGE_SCALE 47
#define RESULT_SCALE_Y     53
#define RESULT_DIGITS      5

/* Nothing drawn: no drawn cell or direction of a champion, no drawn wall set. */
#define DRAWN_NONE 0xff

/* drawn.action_mode: no input zone refresh yet for the current panels. */
#define ACTION_MODE_STALE (-1)

/* Game steps that the advanced mode keeps the damage of a hit after the original delay (not in the original game). */
#define DAMAGE_HOLD_EXTRA_TICKS 1

/* The index of a coin in the pile offsets wraps at 32. */
#define COIN_PILE_MASK 0x1f

/* The coin piles of an open money box. */
#define COIN_PILE_MAX  32 /* the most coins of a type that a pile shows */
#define PILE_SLOT_STEP 8  /* the coin pile of each slot starts 8 offsets further */

/* The transparent colors of the action area pictures. */
#define CONTAINER_LID_KEY 10
#define ICON_KEY_COLOR    UI_RED /* the background of a hand icon bitmap; the blit skips it */

/* The sizes and frames of the action area pictures. */
#define SPELL_RUNE_SETS 4 /* the rune sets 0 to 3; the panel of set 4 has no runes */
#define AURA_FRAMES     4 /* the aura picture holds 4 x 4 frames: direction by game time */
#define ICON_OFFSET     1 /* the icon of an item in a hand casts a shadow 1 pixel up and left */
#define ICON_BPP        8
#define FACE_BPP        4
#define JITTER_CENTER   2 /* haste_jitter: a roll of 1 to 3 moves by -1 to 1 */
#define PICTURE_PAIR    2 /* hand backs, map action pictures: plain, then flashed or highlighted */

/* The offsets of up to 32 coins in a pile, from SKULL.EXE. */
static const int8_t coin_pile[COIN_PILE_MAX][2] = {
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

static Offscreen area;
static struct {
    uint8_t cell[CHAMPION_MAX], dir[CHAMPION_MAX]; /* the drawn cell and direction of each champion */
    bool alive[CHAMPION_MAX];
    bool busy[CHAMPION_MAX][ACTION_DELAY_COUNT]; /* the drawn busy state of the hands and the spell */
    uint8_t gfx_set;
    bool invisible;
    uint16_t aura_mask;
    int16_t leader;
    int16_t action_mode; /* the action mode of the last input zone refresh, ACTION_MODE_STALE for none */
} drawn;

/* The ticks to wait for the redraw after the damage shows: one for the original delay, then the extra ones. */
static int16_t damage_hold_ticks;

/* ---- Setup ---- */

void area_reset(void)
{
    damage_hold_ticks = 0;
    memset(&drawn, 0, sizeof drawn);
    drawn.gfx_set = DRAWN_NONE;
    drawn.leader = -1;
    drawn.action_mode = ACTION_MODE_STALE;
    area.bitmap = NULL;
}

void area_reset_champion(int16_t champion_index)
{
    drawn.cell[champion_index] = DRAWN_NONE;
    drawn.dir[champion_index] = DRAWN_NONE;
    drawn.alive[champion_index] = false;
    memset(drawn.busy[champion_index], 0, sizeof drawn.busy[champion_index]);
}

/* ---- Drawing ---- */

/* Opens the action area bitmap; clear: it starts black and goes to the screen as a whole. */
static void begin_draw(bool clear)
{
    if (area.bitmap)
        return;
    offscreen_create(&area, ZONE_ACTION_AREA, clear);
    if (clear)
        offscreen_fill_rect(&area, area.rect, g_ui_colors[UI_BLACK]);
}

Offscreen *area_begin_draw(bool clear)
{
    begin_draw(clear);
    return &area;
}

/* The icon of the item (or the empty hand) in a hand, with its shadow, on the icon background color; NULL when
   the hand has no action. */
static Bitmap *make_hand_icon(int16_t champion_index, int16_t hand)
{
    uint16_t thing = champion(champion_index)->slots[hand];
    if (!champ_list_actions(champion_index, thing, hand))
        return NULL;
    Bitmap *icon = draw_temp_bitmap(ICON_SIZE + ICON_OFFSET, ICON_SIZE + ICON_OFFSET, ICON_BPP);
    if (!icon)
        return NULL;
    bool empty = thing == THING_NONE;
    DrawCtx c;
    if (empty)
        draw_ctx_init(&c, CAT_INTERFACE, UI_INDEX_STATUS, GFX_ACTION_EMPTY_HAND + (hand == SLOT_ACTION_HAND));
    else
        draw_ctx_init(&c, item_gdat_category(thing), item_gdat_index(thing), item_icon_index(thing, -1, false));
    draw_ctx_prepare(&c);
    gfx_fill_bitmap(icon, g_ui_colors[ICON_KEY_COLOR]);
    if (!c.image)
        return icon;
    if (!empty) {
        uint8_t shadow[IMAGE_COLORS];
        memset(shadow, g_ui_colors[UI_BLACK], sizeof shadow);
        shadow[UI_KEY] = g_ui_colors[ICON_KEY_COLOR];
        const int16_t r[RECT_SIZE] = {ICON_OFFSET, ICON_OFFSET, ICON_SIZE, ICON_SIZE};
        ui_blit_bitmap(&(BlitSource){.bitmap = c.image, .transparent = GFX_OPAQUE, .colormap = shadow}, icon, r);
        uint32_t size;
        const uint8_t *remap = gdat_item(
            gdat_current(), gdat_find(gdat_current(), CAT_INTERFACE, 0, GDAT_TYPE_REMAP, GFX_ICON_SHADOW_REMAP), &size);
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
    ui_blit_bitmap(
        &(BlitSource){.bitmap = c.image, .transparent = UI_KEY, .colormap = c.palette_count > 0 ? c.palette : NULL},
        icon, r);
    return icon;
}

void area_draw_hand_icon(int16_t champion_index, int16_t hand, bool flash)
{
    begin_draw(false);
    const Champion *c = champion(champion_index);
    int zone = (hand == SLOT_ACTION_HAND ? ZONE_ACTION_HAND_FIRST : ZONE_READY_HAND_FIRST) +
               dir_relative(c->cell, g_game.party_dir);
    int16_t r[RECT_SIZE];
    zone_get_rect(zone, r);
    if (!c->health) {
        offscreen_fill_rect(&area, r, g_ui_colors[UI_BLACK]);
        return;
    }
    offscreen_draw_gdat_image(
        &area,
        (GdatKey){CAT_INTERFACE, UI_INDEX_ACTION, (uint8_t)(hand * PICTURE_PAIR + GFX_ACTION_HAND_BACK_FIRST + flash)},
        zone, GFX_OPAQUE);
    Bitmap *icon = make_hand_icon(champion_index, hand);
    if (icon) {
        int16_t centered[RECT_SIZE] = {(int16_t)(((r[RECT_W] - icon->width + 1) >> 1) + r[RECT_X]),
                                       (int16_t)(r[RECT_Y] + ((r[RECT_H] - icon->height + 1) >> 1)), icon->width,
                                       icon->height};
        offscreen_blit(&area, &(BlitSource){.bitmap = icon, .transparent = g_ui_colors[ICON_KEY_COLOR]}, centered);
    }
    if (c->action_delay[hand] || g_party_sleeping)
        offscreen_fill_rect_dithered(&area, r, UI_BLACK);
}

/* Dims the picture that draw context d drew in the action area with a black dither. */
static void dim_drawn(const DrawCtx *d)
{
    int16_t r[RECT_SIZE] = {(int16_t)(d->rect[RECT_X] + area.rect[RECT_X]),
                            (int16_t)(d->rect[RECT_Y] + area.rect[RECT_Y]), d->rect[RECT_W], d->rect[RECT_H]};
    offscreen_fill_rect_dithered(&area, r, UI_BLACK);
}

void area_draw_spell_icon(int16_t champion_index, bool flash)
{
    begin_draw(false);
    const Champion *c = champion(champion_index);
    int rel = dir_relative(c->cell, g_game.party_dir);
    int16_t r[RECT_SIZE];
    offscreen_fill_rect(&area, zone_get_rect(ZONE_SPELL_ICON_FIRST + rel, r), g_ui_colors[UI_BLACK]);
    if (!c->health)
        return;
    bool mirror = rel == CELL_FRONT_RIGHT || rel == CELL_BACK_RIGHT;
    int rune = rel <= CELL_FRONT_RIGHT ? GFX_ACTION_SPELL_RUNE_LEFT : GFX_ACTION_SPELL_RUNE_RIGHT;
    int picture = rel <= CELL_FRONT_RIGHT ? GFX_ACTION_SPELL_HAND_LEFT : GFX_ACTION_SPELL_HAND_RIGHT;
    if (champion_index == g_game.leader)
        rune++;
    if (flash)
        picture++;
    DrawCtx d;
    draw_ctx_init(&d, CAT_INTERFACE, UI_INDEX_ACTION, picture);
    d.flip = mirror ? MIRROR_H : 0;
    offscreen_draw_ctx(&area, draw_ctx_prepare(&d), ZONE_SPELL_HAND_ICON_FIRST + rel, UI_CYAN);
    if (g_party_sleeping || c->action_delay[ACTION_SPELL])
        dim_drawn(&d);
    draw_ctx_init(&d, CAT_INTERFACE, UI_INDEX_ACTION, rune);
    d.flip = mirror ? MIRROR_H : 0;
    offscreen_draw_ctx(&area, draw_ctx_prepare(&d), ZONE_SPELL_RUNE_ICON_FIRST + rel, UI_CYAN);
    if (g_party_sleeping)
        dim_drawn(&d);
}

void area_draw_entered_runes(bool clear)
{
    begin_draw(false);
    if (clear)
        offscreen_draw_gdat_image(&area, (GdatKey){CAT_INTERFACE, UI_INDEX_SPELL, GFX_SPELL_RUNES_BACK},
                                  ZONE_RUNES_BACK, GFX_OPAQUE);
    const Champion *c = champion(g_action.champion - 1);
    /* as in the original: the end mark of four runes is in unknown_26[0] */
    size_t n = strnlen(c->runes, CHAMPION_RUNES_SIZE + 1);
    for (size_t i = 0; i < n; i++) {
        char s[RUNE_TEXT_SIZE] = {c->runes[i], '\0'};
        offscreen_draw_text_in_zone(&area, ZONE_RUNE_ENTERED_FIRST + (int)i,
                                    (TextColors){g_ui_colors[UI_BLACK], g_ui_colors[UI_LIGHT_GRAY] | TEXT_TRANSPARENT},
                                    s);
    }
}

void area_draw_name(int i, bool highlight)
{
    begin_draw(false);
    const ChampionAction *a = &g_champ_actions[i];
    if (g_action.flags) {
        offscreen_draw_gdat_image(&area,
                                  (GdatKey){CAT_CONTAINER, g_action.map_item,
                                            (uint8_t)((a->entry - GFX_ACTION_FIRST) * PICTURE_PAIR +
                                                      GFX_MAP_ACTION_PICTURE_BASE + highlight)},
                                  ZONE_MAP_ACTION_FIRST + i, GFX_OPAQUE);
        return;
    }
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_INTERFACE, UI_INDEX_ACTION, GFX_ACTION_BUTTON + highlight},
                              ZONE_ACTION_BUTTON_FIRST + i, GFX_OPAQUE);
    offscreen_draw_shadow_text(&area, ZONE_ACTION_NAME_FIRST + i,
                               (TextColors){g_ui_colors[UI_WHITE], g_ui_colors[UI_BLACK] | TEXT_TRANSPARENT},
                               gtext_gdat_label(a->category, a->index, a->entry));
}

/* ---- Redraw ---- */

/* Draws the aura of champion_index over its face in zone: frame by direction and game time. */
static void show_aura(int16_t champion_index, int zone)
{
    const Champion *c = champion(champion_index);
    if (!c->aura_value)
        return;
    DrawCtx a;
    draw_ctx_init(&a, CAT_INTERFACE, UI_INDEX_AURA, c->aura);
    draw_ctx_prepare(&a);
    a.w = (int16_t)(a.w / AURA_FRAMES);
    a.x = (int16_t)(dir_relative(c->dir, g_game.party_dir) * a.w);
    a.h = (int16_t)(a.h / AURA_FRAMES);
    a.y = (int16_t)((g_game.game_time & (AURA_FRAMES - 1)) * a.h);
    offscreen_draw_ctx(&area, &a, zone, UI_KEY);
}

/* Moves a face picture by -1 to 1 pixels while the party hastes. */
static void haste_jitter(int16_t *r)
{
    if (!g_game.magic_light[MAGIC_HASTE])
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

/* Draws the face of champion_index in zone through the face bitmap, moved while the party hastes, with its aura. */
static void put_face(Bitmap *face, int16_t champion_index, int zone)
{
    int16_t r[RECT_SIZE];
    ScreenSize size = {0, 0};
    if (!zone_compute_rect(face, r, zone, &size))
        return;
    haste_jitter(r);
    uint8_t palette[IMAGE_COLORS];
    ui_draw_champion_face_icon(face, champion_index, palette);
    offscreen_blit(
        &area, &(BlitSource){.bitmap = face, .x = size.w, .y = size.h, .transparent = UI_KEY, .colormap = palette}, r);
    show_aura(champion_index, zone);
}

/* Draws the faces of the party in their cells, with the auras. */
static void show_party_icons(void)
{
    begin_draw(false);
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_WALLSET, drawn.gfx_set, GFX_WALLSET_PARTY_BACK}, ZONE_PARTY_ICONS,
                              GFX_OPAQUE);
    Bitmap *face = face_bitmap();
    for (int i = 0; face && i < g_game.champion_count; i++) {
        const Champion *c = champion(i);
        int cell = dir_relative(c->cell, g_game.party_dir);
        if (!c->health || g_input_drag_state == cell + 1)
            continue;
        put_face(face, i, ZONE_PARTY_FACE_FIRST + cell);
    }
    if (g_party_sleeping) {
        int16_t r[RECT_SIZE];
        offscreen_fill_rect_dithered(&area, zone_get_rect(ZONE_PARTY_ICONS, r), UI_BLACK);
    }
}

/* Draws the header of an open hand or spell panel with the champion name. */
static void show_panel_header(void)
{
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_INTERFACE, UI_INDEX_ACTION, GFX_ACTION_MENU_HEADER},
                              ZONE_MENU_HEADER, GFX_OPAQUE);
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_INTERFACE, UI_INDEX_ACTION, GFX_ACTION_MENU_BACK}, ZONE_MENU_BACK,
                              GFX_OPAQUE);
    int16_t champion_index = g_action.champion - 1;
    offscreen_draw_shadow_text(&area, ZONE_MENU_NAME,
                               (TextColors){g_ui_colors[champion_index == g_game.leader ? UI_GOLD : UI_WHITE],
                                            g_ui_colors[UI_DARKEST_GRAY] | TEXT_TRANSPARENT},
                               champion(champion_index)->name);
}

/* Draws the face of the champion whose hand or spell panel is open. */
static void show_panel_face(void)
{
    begin_draw(false);
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_WALLSET, drawn.gfx_set, GFX_WALLSET_FACE_BACK}, ZONE_FACE_BACK,
                              GFX_OPAQUE);
    Bitmap *face = face_bitmap();
    if (face)
        put_face(face, g_action.champion - 1, ZONE_FACE);
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_INTERFACE, UI_INDEX_ACTION, GFX_ACTION_FACE_LEFT}, ZONE_FACE_LEFT,
                              GFX_OPAQUE);
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_INTERFACE, UI_INDEX_ACTION, GFX_ACTION_FACE_RIGHT}, ZONE_FACE_RIGHT,
                              GFX_OPAQUE);
}

/* Draws the spell panel: the runes of the next set, the entered runes, the face. */
static void show_spell_panel(void)
{
    int set = champion(g_action.champion - 1)->rune_count;
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_INTERFACE, UI_INDEX_SPELL, (uint8_t)(set + 1)}, ZONE_PANEL,
                              GFX_OPAQUE);
    if (set < SPELL_RUNE_SETS) {
        for (int i = 0; i < SPELL_RUNES_PER_SET; i++) {
            char s[RUNE_TEXT_SIZE] = {(char)(set * SPELL_RUNES_PER_SET + SPELL_RUNE_GLYPH_FIRST + i), '\0'};
            offscreen_draw_text_in_zone(
                &area, ZONE_RUNE_CHOICE_FIRST + i,
                (TextColors){g_ui_colors[UI_BLACK], g_ui_colors[UI_LIGHT_GRAY] | TEXT_TRANSPARENT}, s);
        }
    }
    area_draw_entered_runes(false);
    show_panel_face();
}

/* Draws the action menu of the open hand. */
static void show_action_menu(void)
{
    for (int i = 0; i < g_champ_action_count; i++)
        area_draw_name(i, false);
    show_panel_face();
}

/* Draws the coin of pile offset at over the pile rectangle r, clipped to the action area. */
static void put_coin(const Bitmap *image, const uint8_t *colors, const int16_t *r, int at)
{
    int16_t coin[RECT_SIZE] = {(int16_t)(r[RECT_X] + coin_pile[at][0]), (int16_t)(r[RECT_Y] + coin_pile[at][1]),
                               r[RECT_W], r[RECT_H]};
    int16_t cx = 0;
    int16_t cy = 0;
    int16_t d = (int16_t)(area.rect[RECT_X] - coin[RECT_X]);
    if (d > 0) {
        cx = d;
        coin[RECT_X] += d;
        coin[RECT_W] -= d;
    }
    d = (int16_t)(area.rect[RECT_Y] - coin[RECT_Y]);
    if (d > 0) {
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
    offscreen_blit(&area, &(BlitSource){.bitmap = image, .x = cx, .y = cy, .transparent = UI_KEY, .colormap = colors},
                   coin);
}

/* Draws an open money box with a pile of coins for each coin slot. */
static void show_money_box(uint16_t box)
{
    offscreen_draw_gdat_image(&area, (GdatKey){CAT_CONTAINER, item_gdat_index(box), GFX_CONTAINER_PICTURE}, ZONE_PANEL,
                              GFX_OPAQUE);
    int16_t counts[COIN_TYPE_MAX];
    money_count_coins_in_box(box, counts);
    for (int slot = 0; slot < COIN_TYPE_MAX; slot++) {
        int type = money_coin_type_for_slot(box, slot);
        if (type < 0 || counts[type] <= 0)
            continue;
        uint16_t item = money_coin_item(type);
        int index = item_index_gdat_index(item);
        int category = item_index_gdat_category(item);
        const Bitmap *image = draw_gdat_image(category, index, GFX_ITEM_ICON);
        const uint8_t *colors = draw_gdat_image_palette(category, index, GFX_ITEM_ICON);
        int16_t r[RECT_SIZE];
        ScreenSize size = {0, 0};
        /* fix: skip a coin without an image or a pile zone; no read of an unset rectangle */
        if (!image || !zone_compute_rect(image, r, ZONE_COIN_PILE_FIRST + slot, &size))
            continue;
        /* the pile shows the last COIN_PILE_MAX coins */
        int at = slot * PILE_SLOT_STEP + max_int(counts[type] - COIN_PILE_MAX, 0);
        int n = min_int(counts[type], COIN_PILE_MAX);
        for (int k = 0; k < n; k++) {
            at = (at + 1) & COIN_PILE_MASK;
            put_coin(image, colors, r, at);
        }
    }
}

/* Draws an open chest with its items; full also draws the chest and its lid. */
static void show_chest(uint16_t container, bool full)
{
    int index = item_gdat_index(container);
    int16_t back[RECT_SIZE] = {0};
    if (full) {
        offscreen_draw_gdat_image(&area, (GdatKey){CAT_CONTAINER, index, GFX_CONTAINER_PICTURE}, ZONE_PANEL,
                                  GFX_OPAQUE);
        offscreen_draw_gdat_image(&area, (GdatKey){CAT_CONTAINER, index, GFX_CONTAINER_LID}, ZONE_CONTAINER_LID,
                                  CONTAINER_LID_KEY);
    } else {
        zone_get_rect(ZONE_PANEL, back);
    }
    for (int i = 0; i < CONTAINER_SLOTS; i++) {
        uint16_t t = g_action.open_items[i];
        if (t == THING_NONE)
            continue;
        int zone = ZONE_CONTAINER_SLOT_FIRST + i;
        if (!full) {
            int16_t r[RECT_SIZE];
            zone_get_rect(zone, r);
            offscreen_blit(
                &area,
                &(BlitSource){.bitmap = draw_gdat_image(CAT_CONTAINER, index, GFX_CONTAINER_PICTURE),
                              .x = (int16_t)(r[RECT_X] - back[RECT_X]),
                              .y = (int16_t)(r[RECT_Y] - back[RECT_Y]),
                              .transparent = GFX_OPAQUE,
                              .colormap = draw_gdat_image_palette(CAT_CONTAINER, index, GFX_CONTAINER_PICTURE)},
                r);
        }
        int icon = item_icon_index(t, SLOT_CONTAINER_FIRST + i, true);
        offscreen_draw_gdat_image(&area, (GdatKey){item_gdat_category(t), item_gdat_index(t), icon}, zone, UI_KEY);
    }
}

/* True when a living champion stands in the cell of the dead champion c. */
static bool is_cell_taken(const Champion *c)
{
    for (int k = 0; k < g_game.champion_count; k++)
        if (champion(k)->cell == c->cell && champion(k)->health)
            return true;
    return false;
}

/* Draws the hand icons of champion champion_index that changed, flashed or went busy; full or redraw draws both. */
static void show_hand_icons(int16_t champion_index, bool full, bool redraw, bool flash_this)
{
    const Champion *c = champion(champion_index);
    for (int16_t hand = 0; hand < SLOT_HAND_COUNT; hand++) {
        bool flash = false;
        bool update = false;
        if (flash_this && g_action.flash_hand == hand) {
            flash = g_action.flash > 0;
            update = true;
        }
        bool busy = c->action_delay[hand] != 0;
        if (busy != drawn.busy[champion_index][hand]) {
            drawn.busy[champion_index][hand] = busy;
            update = true;
        }
        if (g_action.hand_items[champion_index][hand] != c->slots[hand] || full || redraw) {
            g_action.hand_items[champion_index][hand] = c->slots[hand];
            update = true;
        }
        if (full || redraw || update)
            area_draw_hand_icon(champion_index, hand, flash);
    }
}

/* Draws the hands and the spell icons of the party; full redraws all of them. */
static void show_party_hands(bool full, bool changed)
{
    bool party_changed = changed;
    g_action.mode = ACTION_MODE_HANDS;
    if (full || changed)
        drawn.action_mode = ACTION_MODE_STALE;
    for (int i = 0; i < g_game.champion_count; i++) {
        const Champion *c = champion(i);
        if (!c->health && is_cell_taken(c))
            continue;
        bool redraw = false;
        bool alive = c->health > 0;
        if (alive != drawn.alive[i]) {
            drawn.alive[i] = alive;
            redraw = true;
            drawn.action_mode = ACTION_MODE_STALE;
        }
        bool flash_this = (g_action.flash < 0 ? -g_action.flash : g_action.flash) - 1 == i;
        show_hand_icons(i, full, redraw, flash_this);
        bool flash = false;
        if (flash_this && g_action.flash_hand == ACTION_HAND_SPELL) {
            flash = g_action.flash > 0;
            redraw = true;
        }
        bool busy = c->action_delay[ACTION_SPELL] != 0;
        if (busy != drawn.busy[i][ACTION_SPELL]) {
            drawn.busy[i][ACTION_SPELL] = busy;
            redraw = true;
        }
        int16_t leader = champ_leader();
        if (full || redraw || (drawn.leader != leader && (drawn.leader == i || leader == i)))
            area_draw_spell_icon(i, flash);
        if (c->aura_value || drawn.aura_mask)
            party_changed = true;
        if (c->aura_value)
            drawn.aura_mask |= 1 << i;
        else
            drawn.aura_mask &= ~(1 << i);
    }
    if (g_action.flash < 0)
        g_action.flash = 0;
    g_action.flash = (int16_t)-g_action.flash;
    if (full || party_changed)
        show_party_icons();
}

/* Tracks the drawn cells and directions of the champions, the wall set, haste, and invisibility; returns true when
   the party icons must redraw. */
static bool party_icons_changed(void)
{
    bool changed = false;
    for (int i = 0; i < g_game.champion_count; i++) {
        const Champion *c = champion(i);
        uint8_t dir = (uint8_t)dir_relative(c->dir, g_game.party_dir);
        uint8_t cell = (uint8_t)dir_relative(c->cell, g_game.party_dir);
        if (!c->health || g_input_drag_state == cell + 1)
            cell = DRAWN_NONE;
        if (cell != drawn.cell[i] || dir != drawn.dir[i]) {
            changed = true;
            if (cell != DRAWN_NONE)
                g_action.dirty = 1;
            drawn.cell[i] = cell;
            drawn.dir[i] = dir;
        }
    }
    bool invisible = g_game.magic_light[MAGIC_INVISIBILITY] != 0;
    if (drawn.gfx_set != dungeon_map_wallset(g_game.party_map) || g_game.magic_light[MAGIC_HASTE] ||
        drawn.invisible != invisible) {
        changed = true;
        drawn.gfx_set = dungeon_map_wallset(g_game.party_map);
        drawn.invisible = invisible;
    }
    return changed;
}

/* Marks the input zones again when the action mode changed since the last time: the zones of the panels, for
   example the cursor zone of a money box, depend on the action mode. */
static void refresh_input_zones(void)
{
    if (drawn.action_mode == g_action.mode)
        return;
    input_refresh_mode();
    drawn.action_mode = (int16_t)g_action.mode;
}

/* Draws the panel of the open hand: a minion map, a money box, a chest, or the action menu; with nothing to show,
   the hand closes. */
static void show_hand_panel(void)
{
    int16_t champion_index = g_action.champion - 1;
    uint16_t thing = champion(champion_index)->slots[g_action.hand];
    if (item_is_minion_map(thing) && !g_action.flags) {
        minion_map_open(champion_index);
    } else if (item_is_moneybox(thing)) {
        g_action.mode = ACTION_MODE_MONEY_BOX;
    } else if (item_is_chest(thing) && g_action.mode != ACTION_MODE_CONTAINER) {
        action_open_container(thing);
        g_action.mode = ACTION_MODE_CONTAINER;
    }
    if (g_action.mode == ACTION_MODE_MONEY_BOX) {
        show_money_box(thing);
    } else if (g_action.mode == ACTION_MODE_CONTAINER) {
        show_chest(thing, true);
    } else if (champ_list_actions(champion_index, thing, g_action.hand) && g_action.mode != ACTION_MODE_MINION_MAP) {
        g_action.mode = ACTION_MODE_MENU;
        refresh_input_zones(); /* the menu draws with the input zones of its actions */
        show_action_menu();
    } else if (g_action.mode == ACTION_MODE_MINION_MAP) {
        if (!(g_action.flags & MAP_PANEL_DRAWN))
            drawn.action_mode = ACTION_MODE_STALE;
        mmap_draw(thing);
    } else {
        g_action.champion = 0;
        g_action.mode = ACTION_MODE_HANDS;
    }
}

/* Draws the open hand or spell panel when it changed; returns false while the panel waits for its redraw delay. */
static bool show_open_panel(bool full, bool party_changed, bool tick)
{
    if (g_action.redraw_delay || full || party_changed || (g_action.mode == ACTION_MODE_MINION_MAP && tick)) {
        if (g_action.redraw_delay > ACTION_REDRAW_NEXT) {
            g_action.redraw_delay--;
            return false;
        }
        if (g_action.flags && !g_action.redraw_delay && !full) {
            begin_draw(false);
        } else {
            begin_draw(true);
            show_panel_header();
            g_action.flags &= ~MAP_PANEL_DRAWN;
        }
        if (g_action.hand == ACTION_HAND_SPELL) {
            g_action.mode = ACTION_MODE_SPELL;
            show_spell_panel();
        } else {
            show_hand_panel();
        }
        g_action.redraw_delay = 0;
    } else if (g_action.mode == ACTION_MODE_CONTAINER && tick) {
        begin_draw(false);
        show_chest(champion(g_action.champion - 1)->slots[g_action.hand], false);
    }
    return true;
}

/* The face of the champion of the open panel redraws while it has an aura, and once after the aura ends. */
static void show_panel_face_aura(void)
{
    if (g_action.champion <= 0 || (g_action.mode != ACTION_MODE_MENU && g_action.mode != ACTION_MODE_SPELL))
        return;
    const Champion *c = champion(g_action.champion - 1);
    if (c->aura_value || drawn.aura_mask) {
        show_panel_face();
        if (c->aura_value)
            drawn.aura_mask |= 1 << (g_action.champion - 1);
        else
            drawn.aura_mask &= ~(1 << (g_action.champion - 1));
    }
}

/* Draws the result of an action: the damage in a star, or the picture of a failure. */
static void show_action_result(int16_t value)
{
    begin_draw(true);
    int index = UI_INDEX_ACTION;
    int entry = GFX_ACTION_RESULT_DAMAGE;
    if (value < 0) {
        if (value == RESULT_NOTHING)
            entry = GFX_ACTION_RESULT_NOTHING;
        else if (value == RESULT_NO_AMMO)
            entry = GFX_ACTION_RESULT_MISS;
        else if (value == RESULT_CANT)
            entry = GFX_ACTION_RESULT_CANT;
        else {
            index = UI_INDEX_SPELL;
            entry = value == RESULT_UNKNOWN_SPELL   ? GFX_RESULT_SPELL_FIZZLE
                    : value == RESULT_WIZARD_FAILED ? GFX_RESULT_SPELL_NEEDS
                                                    : GFX_RESULT_SPELL_OTHER;
        }
    }
    DrawCtx c;
    draw_ctx_init(&c, CAT_INTERFACE, index, entry);
    if (value >= 0 && value <= RESULT_SCALED_MAX) {
        c.scale_x = value > RESULT_SMALL_MAX ? RESULT_LARGE_SCALE : RESULT_SMALL_SCALE;
        c.scale_y = RESULT_SCALE_Y;
    }
    offscreen_draw_ctx(&area, draw_ctx_prepare(&c), ZONE_RESULT, GFX_OPAQUE);
    if (value >= 0) {
        char digits[RESULT_DIGITS] = {0};
        int at = RESULT_DIGITS - 1;
        do {
            digits[--at] = (char)('0' + value % DECIMAL_BASE);
            value = (int16_t)(value / DECIMAL_BASE);
        } while (value);
        offscreen_draw_text_in_zone(&area, ZONE_RESULT, (TextColors){g_ui_colors[UI_CYAN], g_ui_colors[UI_BLACK]},
                                    digits + at);
    } else if (value == RESULT_NO_AMMO) {
        offscreen_draw_gdat_image(
            &area, (GdatKey){CAT_PANEL, 0, g_action.result_flag ? GFX_AMMO_HAND_OTHER : GFX_AMMO_HAND_DEFAULT},
            ZONE_AMMO_HAND, GFX_OPAQUE);
        for (int weapon_index = 0; weapon_index < GDAT_NO_INDEX; weapon_index++) {
            uint16_t ammo = gdat_entry_index(gdat_current(), CAT_WEAPON, weapon_index, GDAT_TYPE_VALUE, ATTR_AMMO);
            if (ammo && !(ammo & AMMO_LAUNCHER) && ammo & g_action.result_ammo) {
                offscreen_draw_gdat_image(&area, (GdatKey){CAT_WEAPON, weapon_index, GFX_ITEM_ICON}, ZONE_AMMO_ITEM,
                                          GFX_OPAQUE);
                return;
            }
        }
    }
}

/* The end of each action area update: the drawing goes to the screen, and the input zones follow the action mode. */
static void finish_update(void)
{
    if (area.bitmap)
        offscreen_flush(&area, true);
    draw_free_temp();
    refresh_input_zones();
}

/* Draws what changed in the action area since the last update: the result of an action, the hands, the open panel. */
static void redraw_changes(bool tick)
{
    bool full = false;
    bool party_changed = false;
    if (!g_game.champion_count) {
        if (g_action.dirty || g_action.redraw_delay) {
            g_action.redraw_delay = 0;
            g_action.dirty = 0;
            begin_draw(true);
        }
        g_action.mode = ACTION_MODE_NONE;
        return;
    }
    if (tick)
        action_tick_hands();
    if (tick && g_action.mode == ACTION_MODE_MINION_MAP)
        minion_map_drain_mana();
    if (g_action.result) {
        show_action_result(g_action.result);
        damage_hold_ticks = g_game_mode == GAME_MODE_ADVANCED && g_action.result > 0 ? DAMAGE_HOLD_EXTRA_TICKS + 1 : 0;
        g_action.dirty = 1;
        g_action.result = 0;
        g_action.mode = ACTION_MODE_RESULT;
        return;
    }
    if (g_action.champion > 0 && g_action.hand >= 0 && g_action.hand != ACTION_HAND_SPELL &&
        g_action.hand_items[g_action.champion - 1][g_action.hand] !=
            champion(g_action.champion - 1)->slots[g_action.hand])
        action_area_close();
    if (g_action.champion >= 0)
        party_changed = party_icons_changed();
    if (g_action.dirty) {
        if (g_action.redraw_delay > ACTION_REDRAW_NEXT) {
            g_action.redraw_delay--;
            return;
        }
        if (g_action.mode != ACTION_MODE_RESULT)
            damage_hold_ticks = 0;
        if (damage_hold_ticks > DAMAGE_HOLD_EXTRA_TICKS) {
            if (tick)
                damage_hold_ticks--;
            return;
        }
        damage_hold_ticks = 0;
        begin_draw(true);
        g_action.dirty = 0;
        full = true;
    }
    if (!g_action.champion)
        show_party_hands(full, party_changed);
    drawn.leader = champ_leader();
    if (g_action.champion > 0 && !show_open_panel(full, party_changed, tick))
        return;
    show_panel_face_aura();
}

void area_update(bool tick)
{
    if (g_ui.candidate)
        return;
    redraw_changes(tick);
    finish_update();
}

bool area_end_damage_hold(int16_t x, int16_t y)
{
    int16_t rect[RECT_SIZE];
    zone_get_rect(ZONE_ACTION_AREA, rect);
    if (damage_hold_ticks <= 0 || damage_hold_ticks > DAMAGE_HOLD_EXTRA_TICKS || x < rect[RECT_X] || y < rect[RECT_Y] ||
        x >= rect[RECT_X] + rect[RECT_W] || y >= rect[RECT_Y] + rect[RECT_H])
        return false;
    damage_hold_ticks = 0;
    area_update(false);
    return true;
}
