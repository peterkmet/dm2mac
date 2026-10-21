/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "icon.h"
#include "video.h"

#define ICON_INDEX 0x26      /* the mummy */
#define ICON_ENTRY 9         /* the front view */
#define ICON_TRANSPARENT 47  /* the see-through color of the creature graphics */
#define ICON_MAX_BYTES (256 * 256)

int icon_build(const gdat_t *g, uint32_t *argb, int max, int *w, int *h)
{
    static uint8_t px[ICON_MAX_BYTES];
    int bpp, item = gdat_find(g, CAT_CREATURE, ICON_INDEX, GDAT_TYPE_IMAGE, ICON_ENTRY);
    if (item < 0 || gdat_image_size(g, item, w, h, &bpp) < 0 || bpp != IMAGE_8BPP) return 0;
    if (*w * *h > max || *w * *h > ICON_MAX_BYTES || gdat_decode_image(g, item, px, sizeof px) < 0) return 0;
    for (int i = 0; i < *w * *h; i++) {
        uint8_t rgb[RGB_CHANNELS];
        gdat_palette_color(g, px[i], rgb);
        uint32_t red = video_vga_to_8bit(rgb[0]), green = video_vga_to_8bit(rgb[1]), blue = video_vga_to_8bit(rgb[2]);
        argb[i] = px[i] == ICON_TRANSPARENT ? 0 : video_argb_pack(red, green, blue);
    }
    return 1;
}
