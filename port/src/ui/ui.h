#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "draw.h"
#include "gfx.h"

/* Prefixes: ui_ offscreen_ */

/* Layout zones of the screen and the panels. */
enum ui_zone {
    ZONE_SCREEN = 2, ZONE_VIEWPORT = 7, ZONE_MOVEMENT_ARROWS = 9, ZONE_ACTION_AREA = 11,
    ZONE_ARROW_FIRST = 0x28, ZONE_ARROW_COUNT = 6,
    ZONE_STATUS_BOX = 0xa1,         /* + champion */
    ZONE_STATUS_NAME = 0xa5,        /* + champion */
    ZONE_STATUS_PORTRAIT = 0xad,    /* + champion */
    ZONE_STATUS_DAMAGE = 0xb1,      /* + champion */
    ZONE_STATUS_BARS = 0xb9,        /* + champion */
    ZONE_STATUS_BAR_FIRST = 0xc1,   /* + champion + 4 * bar */
    ZONE_INVENTORY_BACK = 4, ZONE_INVENTORY_PANEL = 0x1ee, ZONE_FOOD_GAUGE = 0x1f0, ZONE_WATER_GAUGE = 0x1f1,
    ZONE_POISON_GAUGE = 0x1f3, ZONE_FOOD_LABEL = 0x1f4, ZONE_WATER_LABEL = 0x1f5, ZONE_POISON_LABEL = 0x1f6,
    ZONE_OBJECT_NAME_BACK = 0x1f8, ZONE_OBJECT_NAME = 0x1fa, ZONE_EYE = 0x221, ZONE_MOUTH = 0x222,
    ZONE_VALUES_BACK = 0x225, ZONE_HEALTH_VALUE = 0x226, ZONE_STAMINA_VALUE = 0x227, ZONE_MANA_VALUE = 0x228,
    ZONE_INVENTORY_TITLE = 0x229, ZONE_LOAD_BACK = 0x22a, ZONE_LOAD = 0x22b, ZONE_DESCRIPTION = 0x22c,
    ZONE_SKILLS = 0x22d, ZONE_STATS = 0x22f, ZONE_SCROLL_TEXT = 0x230, ZONE_BUTTON_SAVE = 0x232,
    ZONE_BUTTON_SLEEP = 0x234, ZONE_BUTTON_EXIT = 0x236, ZONE_BUTTON_SOUND = 0x238, ZONE_BUTTON_CLOSE = 0x267,
    ZONE_COIN_TOTAL = 0x239, ZONE_COIN_COUNT_FIRST = 0x26a, ZONE_OBJECT_GAUGE_FIRST = 0x270
};
/* Zones of the action area. */
enum {
    ZONE_PARTY_ICONS = 0x2f, ZONE_PARTY_FACE_FIRST = 0x35, ZONE_RESULT = 0x39, ZONE_MENU_BACK = 0x3b,
    ZONE_MENU_HEADER = 0x3c, ZONE_MENU_NAME = 0x3d, ZONE_ACTION_BUTTON_FIRST = 0x3f, ZONE_ACTION_NAME_FIRST = 0x42,
    ZONE_ACTION_HAND_FIRST = 0x46, ZONE_READY_HAND_FIRST = 0x4a, ZONE_SPELL_ICON_FIRST = 0x4f,
    ZONE_SPELL_RUNE_ICON_FIRST = 0x53, ZONE_SPELL_HAND_ICON_FIRST = 0x57, ZONE_PANEL = 0x5c, ZONE_FACE_BACK = 0x5d,
    ZONE_FACE = 0x5e, ZONE_FACE_LEFT = 0x60, ZONE_FACE_RIGHT = 0x61, ZONE_MAP_ACTION_FIRST = 0x6e,
    ZONE_AMMO_ITEM = 0x7d, ZONE_AMMO_HAND = 0x7e, ZONE_COIN_PILE_FIRST = 0xdd, ZONE_CONTAINER_LID = 0xe3,
    ZONE_CONTAINER_SLOT_FIRST = 0xe5, ZONE_RUNES_BACK = 0xfc, ZONE_RUNE_CHOICE_FIRST = 0xff,
    ZONE_RUNE_ENTERED_FIRST = 0x105
};

/* GDAT images of the interface: (category 1, index, 1, entry), the inventory set (7, 0, 1, entry), the wall sets. */
enum { UI_INDEX_STATUS = 2, UI_INDEX_ARROWS = 3, UI_INDEX_ACTION = 4, UI_INDEX_SPELL = 5, UI_INDEX_FACES = 6 };
#define UI_KEY 12           /* transparent color of the interface pictures */
#define UI_OPAQUE (-1)
#define ICON_SIZE 16                /* item icons are 16 x 16 */
#define FACE_SIZE 17                /* champion face icons are 17 x 17, one per direction */

/* A bitmap of a screen rectangle that collects drawing and copies the changed parts to the screen. */
#define OFFSCREEN_DIRTY_MAX 5
typedef struct {
    Bitmap *bitmap;             /* NULL while closed */
    int16_t rect[RECT_SIZE];    /* where it goes on the screen */
    int16_t dirty_count;
    int16_t dirty[OFFSCREEN_DIRTY_MAX][RECT_SIZE];
} Offscreen;

/* Opens an offscreen bitmap of zone (-1: of o->rect); mark_dirty marks it all as changed. */
void offscreen_create(Offscreen *o, int zone, int mark_dirty);
/* Copies the changed parts to the screen; close frees the bitmap. */
void offscreen_flush(Offscreen *o, int close);
void offscreen_add_dirty_rect(Offscreen *o, const int16_t *rect);
/* Drawing in screen coordinates. */
void offscreen_fill_rect(Offscreen *o, const int16_t *rect, int color);
/* Fills every second pixel (a checkerboard) with UI color ui_color. */
void offscreen_fill_rect_dithered(Offscreen *o, const int16_t *rect, int ui_color);
void offscreen_blit(Offscreen *o, const Bitmap *src, const int16_t *rect, int src_x, int src_y, int transparent,
                    int mirror, const uint8_t *colormap);
/* Draws a GDAT image placed in zone. */
void offscreen_draw_gdat_image(Offscreen *o, int category, int index, int entry, int zone, int transparent);
/* Draws a prepared picture placed in zone (-1: at its rect). */
void offscreen_draw_ctx(Offscreen *o, DrawCtx *ctx, int zone, int transparent);
/* Draws text with its base line at (x, y) in screen coordinates. */
void offscreen_draw_text_at(Offscreen *o, int x, int y, int color, int bg, const char *text);
/* Draws text placed in zone, plain or with a shadow. */
void offscreen_draw_text_in_zone(Offscreen *o, int zone, int color, int bg, const char *text);
void offscreen_draw_text_in_zone_with_shadow(Offscreen *o, int zone, int color, int bg, const char *text);

/* Fills every second pixel of rect in dst with UI color ui_color. */
void ui_fill_rect_dithered(uint8_t *dst, const int16_t *rect, int ui_color, int dst_width);
/* Copies src into rect of dst from (src_x, src_y). */
void ui_blit_bitmap(const Bitmap *src, Bitmap *dst, const int16_t *rect, int src_x, int src_y, int transparent,
                     const uint8_t *colormap);
/* Draws the GDAT image (category, index, entry) in zone of the view picture, without its GDAT offset. */
void ui_draw_gdat_in_viewport_zone(int category, int index, int entry, int zone, int transparent);
/* Fills the screen rectangle of zone with palette color color. */
void ui_fill_zone_screen(int zone, int color);
/* A face icon (17 x 17, 4 bpp) of a champion, turned to the party direction, into dst; its 16 colors into palette. */
#define FACE_ICON_COLORS 16
void ui_draw_champion_face_icon(Bitmap *dst, int champ, uint8_t *palette);

/* Interface state that the drawing reads. Later steps change it (Step 14: input, Step 15: champions). */
typedef struct {
    int16_t inventory;          /* champion ordinal (champion + 1) of the open inventory, 0 none */
    uint16_t candidate;         /* champion ordinal of a champion that is being recruited, 0 none */
    int16_t screen_needs_refresh;
    uint16_t buttons;           /* inventory buttons that show pressed */
    int16_t eye, mouth;         /* the eye or the mouth of the inventory is held */
    int16_t push_mode;          /* the movement arrows show the push arrows */
    int16_t panel_mode;         /* what the inventory panel shows */
} UiState;

extern UiState ui;

/* Clears the interface caches for a new screen. */
void ui_reset(void);
/* Draws the whole screen of a loaded game: the background, the movement arrows, the champions, the action area. */
void ui_draw_game_screen(void);
void ui_draw_movement_arrows(void);
/* Draws what changed of the status box (and of the inventory) of champ. */
void ui_draw_champion_state(int champ);
/* Draws all status boxes, the open inventory, and the action area (tick: one game tick passed). */
void ui_draw_all_champion_states(int tick);
/* Sets redraw flags on all champions. */
void ui_set_champion_redraw_flags(uint16_t flags);
/* Opens the inventory of champ, or closes it when it is open (champ 4: close). */
void ui_toggle_inventory(int champ);
/* Closing the inventory redraws the movement arrows (0 while the party falls asleep). */
extern int inv_arrows_on_close;

#endif
