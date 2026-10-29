# CD images

The reader of an ISO 9660 CD image with the game files (ECMA-119), in `src/data/iso.c`. It opens the image with `"rb"` and writes nothing. The use of the reader is in [game-files.md](game-files.md).

## Sector layouts

| Layout | Sector | User data |
|---|---|---|
| Cooked image | 2048 bytes | the whole sector |
| Raw MODE1 image | 2352 bytes | 2048 bytes at offset 16, after the sync and the header |

The reader picks the layout by the sync pattern at the start of the image: a zero byte, ten bytes `0xff`, a zero byte (12 bytes). Without the pattern, the sectors are 2048 bytes. The sector count is the image size divided by the sector size.

In a raw image, the mode byte (offset 15) of sector 16 must be `0x01` (MODE1). A raw image with mode `0x02` is a MODE2 image (an XA image) and fails with its own message. Any other mode value is a damaged image.

## Volume descriptor

Sector 16 is the primary volume descriptor. The reader requires type `0x01` and the identifier `CD001` at offset 1. It takes the root directory record at offset 156 of the descriptor: the extent (first sector, offset 2) and the data length (offset 10). The root extent must lie inside the image.

## Directory records

The reader reads these fields of a record:

| Field | Offset |
|---|---|
| Length | 0 |
| Extent (first sector) | 2 |
| Data length | 10 |
| Flags (bit `0x02`: directory) | 25 |
| Name length | 32 |
| Name | 33 |

It follows each directory once. A path has at most two parts (`DATA/GRAPHICS.DAT`): a file in the root directory, or a subdirectory of the root directory and a file in it. A file or a folder at the wrong place in the path does not match. The sectors of an extent must all lie inside the image. A record that is too short for its name, or that ends outside the directory, fails the search.

## Names

A name matches without case. The reader cuts the version suffix `;1`, then a final `.`, so `FTL.;1` matches `FTL` and `GRAPHICS.DAT;1` matches `GRAPHICS.DAT`.

## What fails

| Case | Result |
|---|---|
| File too short, no sector 16, no `CD001` | damaged image |
| MODE2 (XA) raw image | fails with a message that names MODE2 |
| Root extent outside the image | damaged image |
| File extent outside the image | the file is not found |
| Joliet and Rock Ridge names | not read; the reader uses the ISO 9660 names of the primary descriptor |
| A `.cue` file | ignored; the port reads `.iso`, `.img`, and `.bin` files only |

## Deviations from the original

None, because only the port reads CD images.
