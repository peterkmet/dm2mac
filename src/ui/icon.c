/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "icon.h"
#include <stdlib.h>
#include "gdat.h"
#include "ids.h"
#include "video.h"

/* The creature graphic of the icon (the mummy) and the see-through color of the creature graphics. */
#define ICON_CREATURE     0x26
#define ICON_CREATURE_KEY 0x2f

IconSize icon_build(const Gdat *g, uint32_t *pixels, int max)
{
    const IconSize none = {0, 0};
    ImageInfo info;
    int item = gdat_find(g, CAT_CREATURE, ICON_CREATURE, GDAT_TYPE_IMAGE, GFX_ICON);
    if (item < 0 || gdat_image_info(g, item, &info) != GDAT_OK || info.bpp != IMAGE_8BPP)
        return none;
    int width = info.width;
    int height = info.height;
    if (width * height > max)
        return none;
    /* The palette colors of the decoded icon graphic. */
    uint8_t *decoded = malloc(info.size ? info.size : 1);
    if (!decoded || gdat_decode_image(g, item, decoded, info.size) < 0) {
        free(decoded);
        return none;
    }
    for (int i = 0; i < width * height; i++) {
        uint8_t rgb[RGB_CHANNELS];
        gdat_palette_color(g, decoded[i], rgb);
        uint8_t red = video_vga_to_8bit(rgb[RGB_RED]);
        uint8_t green = video_vga_to_8bit(rgb[RGB_GREEN]);
        uint8_t blue = video_vga_to_8bit(rgb[RGB_BLUE]);
        pixels[i] = decoded[i] == ICON_CREATURE_KEY ? 0 : video_argb_pack(red, green, blue);
    }
    free(decoded);
    return (IconSize){width, height};
}
