/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "icon.h"
#include "ids.h"
#include "video.h"

/* The creature graphic of the icon (the mummy) and the see-through color of the creature graphics. */
#define ICON_INDEX       0x26
#define ICON_TRANSPARENT 0x2f

/* Size of the decode buffer in pixels. */
#define ICON_MAX_BYTES 65536

/* The palette colors of the decoded icon graphic. */
static uint8_t decoded[ICON_MAX_BYTES];

IconSize icon_build(const Gdat *g, uint32_t *pixels, int max)
{
    const IconSize none = {0, 0};
    ImageInfo info;
    int item = gdat_find(g, CAT_CREATURE, ICON_INDEX, GDAT_TYPE_IMAGE, GFX_ICON);
    if (item < 0 || gdat_image_info(g, item, &info) != GDAT_OK || info.bpp != IMAGE_8BPP)
        return none;
    int width = info.width;
    int height = info.height;
    if (width * height > max || width * height > ICON_MAX_BYTES ||
        gdat_decode_image(g, item, decoded, sizeof decoded) < 0)
        return none;
    for (int i = 0; i < width * height; i++) {
        uint8_t rgb[RGB_CHANNELS];
        gdat_palette_color(g, decoded[i], rgb);
        uint32_t red = video_vga_to_8bit(rgb[RGB_RED]);
        uint32_t green = video_vga_to_8bit(rgb[RGB_GREEN]);
        uint32_t blue = video_vga_to_8bit(rgb[RGB_BLUE]);
        pixels[i] = decoded[i] == ICON_TRANSPARENT ? 0 : video_argb_pack(red, green, blue);
    }
    return (IconSize){width, height};
}
