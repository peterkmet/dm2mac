/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "icon.h"
#include "ids.h"
#include "video.h"

/* The creature graphic of the icon (the mummy) and the see-through color of the creature graphics. */
#define ICON_INDEX       0x26
#define ICON_TRANSPARENT 0x2f

/* Size of the decode buffer in pixels. */
#define ICON_MAX_BYTES 65536

bool icon_build(const gdat_t *g, uint32_t *argb, int max, int *out_width, int *out_height)
{
    static uint8_t px[ICON_MAX_BYTES];
    int bpp, item = gdat_find(g, CAT_CREATURE, ICON_INDEX, GDAT_TYPE_IMAGE, GFX_ICON);
    if (item < 0 || gdat_image_size(g, item, out_width, out_height, &bpp) < 0 || bpp != IMAGE_8BPP)
        return false;
    if (*out_width * *out_height > max || *out_width * *out_height > ICON_MAX_BYTES ||
        gdat_decode_image(g, item, px, sizeof px) < 0)
        return false;
    for (int i = 0; i < *out_width * *out_height; i++) {
        uint8_t rgb[RGB_CHANNELS];
        gdat_palette_color(g, px[i], rgb);
        uint32_t red = video_vga_to_8bit(rgb[RGB_RED]);
        uint32_t green = video_vga_to_8bit(rgb[RGB_GREEN]);
        uint32_t blue = video_vga_to_8bit(rgb[RGB_BLUE]);
        argb[i] = px[i] == ICON_TRANSPARENT ? 0 : video_argb_pack(red, green, blue);
    }
    return true;
}
