/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gamefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crc32.h"

/* The sizes of the game files. */
#define SIZE_INTRPLAY_PCX 14545
#define SIZE_FTL          495046
#define SIZE_INTRO        1747588
#define SIZE_END          5078746
#define SIZE_CREDITS      5454542
#define SIZE_DRUM_BNK     5404
#define SIZE_MELODIC_BNK  5404
#define SIZE_SONGLIST_DAT 63
#define SIZE_GRAPHICS_DAT 8639757
#define SIZE_DUNGEON_DAT  39437

/* The CRC-32 of the game files. */
#define CRC_INTRPLAY_PCX 0x765c31b6
#define CRC_FTL          0x7cace77a
#define CRC_INTRO        0x6a3668c9
#define CRC_END          0x8cd811cc
#define CRC_CREDITS      0x9ce23e0c
#define CRC_DRUM_BNK     0x89422dd5
#define CRC_MELODIC_BNK  0x4df263e3
#define CRC_SONGLIST_DAT 0xfa6cdaca
#define CRC_GRAPHICS_DAT 0xc6d130f6
#define CRC_DUNGEON_DAT  0x2530a08a

/* The longest path of a game file below the folder, with the separator and the end mark. */
#define GAMEFILE_PATH_MAX 512

static const GameFile table[GAMEFILE_COUNT] = {
    {"INTRPLAY.PCX",      SIZE_INTRPLAY_PCX, CRC_INTRPLAY_PCX},
    {"FTL",               SIZE_FTL,          CRC_FTL         },
    {"INTRO",             SIZE_INTRO,        CRC_INTRO       },
    {"END",               SIZE_END,          CRC_END         },
    {"CREDITS",           SIZE_CREDITS,      CRC_CREDITS     },
    {"DRUM.BNK",          SIZE_DRUM_BNK,     CRC_DRUM_BNK    },
    {"MELODIC.BNK",       SIZE_MELODIC_BNK,  CRC_MELODIC_BNK },
    {"DATA/SONGLIST.DAT", SIZE_SONGLIST_DAT, CRC_SONGLIST_DAT},
    {GAMEFILE_MARKER,     SIZE_GRAPHICS_DAT, CRC_GRAPHICS_DAT},
    {"DATA/DUNGEON.DAT",  SIZE_DUNGEON_DAT,  CRC_DUNGEON_DAT },
};

const GameFile *gamefile_table(void)
{
    return table;
}

bool gamefile_is_valid(const GameFile *file, const uint8_t *data, size_t size)
{
    return size == file->size && crc32_update(0, data, size) == file->crc;
}

bool gamefile_is_valid_in(const GameFile *file, const char *folder)
{
    char path[GAMEFILE_PATH_MAX];
    if (snprintf(path, sizeof path, "%s/%s", folder, file->name) >= (int)sizeof path)
        return false;
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    uint8_t *data = malloc(file->size + 1u);
    if (!data) {
        fclose(fp);
        return false;
    }
    size_t got = fread(data, 1, file->size + 1u, fp); /* one byte more: a longer file fails */
    fclose(fp);
    bool ok = gamefile_is_valid(file, data, got);
    free(data);
    return ok;
}

const GameFile *gamefile_find_bad(const GameFile *files, const char *folder)
{
    for (int i = 0; i < GAMEFILE_COUNT; i++)
        if (!gamefile_is_valid_in(&files[i], folder))
            return &files[i];
    return NULL;
}
