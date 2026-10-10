/* The automap of the advanced mode: the map picture. */
#include "advanced.h"
#include "automap.h"
#include "dungeon.h"
#include "minion.h"
#include "minion_map.h"
#include "move.h"
#include "text.h"
#include "view.h"

#define ICON_GAP 1                  /* the gap left of and above each square icon */
static int scroll_x, scroll_y;      /* squares between the party and the middle of the drawn map */

static int clamp(int lo, int v, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The middle square of the drawn map. */
static void draw_middle(int *x, int *y)
{
    *x = game.party_x + scroll_x;
    *y = game.party_y + scroll_y;
}

void automap_scroll(int dx, int dy)
{
    int x, y, map = game.party_map;
    if (map < 0 || map >= dungeon_map_count()) return;
    draw_middle(&x, &y);
    scroll_x += clamp(0, x + dx, dungeon_map_width(map) - 1) - x;
    scroll_y += clamp(0, y + dy, dungeon_map_height(map) - 1) - y;
}

void automap_scroll_reset(void)
{
    scroll_x = scroll_y = 0;
}

void automap_draw(Offscreen *o, const int16_t *clip)
{
    offscreen_fill_rect(o, clip, ui_colors[UI_BLACK]);
    int map = game.party_map, middle_x, middle_y;
    if (map < 0 || map >= dungeon_map_count()) return;
    draw_middle(&middle_x, &middle_y);
    int left = clip[RECT_X] + (clip[RECT_W] - MAP_CELL_SIZE) / 2 + ICON_GAP - middle_x * MAP_CELL_SIZE;
    int top = clip[RECT_Y] + (clip[RECT_H] - MAP_CELL_SIZE) / 2 + ICON_GAP - middle_y * MAP_CELL_SIZE;
    int saved = party.cur_map, party_item = mmap_party_icons_item();
    dun_set_current_map(map);
    for (int x = 0; x < dungeon_map_width(map); x++)
        for (int y = 0; y < dungeon_map_height(map); y++)
            if (seen_is_seen(map, x, y))
                mmap_draw_square_icon(o, clip, x, y, left + x * MAP_CELL_SIZE, top + y * MAP_CELL_SIZE,
                                            x == game.party_x && y == game.party_y ? party_item : -1);
    dun_set_current_map(saved);
}
