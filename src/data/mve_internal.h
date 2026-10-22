/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_MVE_INTERNAL_H
#define DM2_DATA_MVE_INTERNAL_H
/* Prefixes: mve_ */

/* Shared by mve.c and mve_video.c: the block size and the video decoder for the video data opcode. */

#include "mve.h"

/* The size of a video block. */
#define MVE_BLOCK_SIZE 8 /* pixels per side of a video block */

/* Decodes the blocks of one video data opcode into the current frame; false when the data is too short. */
bool mve_decode_video(MveStream *m, const uint8_t *video, int video_size);

#endif /* DM2_DATA_MVE_INTERNAL_H */
