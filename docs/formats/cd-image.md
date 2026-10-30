# CD images

`src/data/iso.c` reads an ISO 9660 CD image (ECMA-119) with the game files. It opens the image with `"rb"` and writes nothing. The install uses the reader as a source ([install.md](install.md#sources)).

## Sector layouts

| Layout | Sector | User data |
|---|---|---|
| Cooked image | 2048 bytes | the whole sector |
| Raw MODE1 image | 2352 bytes | 2048 bytes at offset 16, after the sync and the header |

The reader picks the layout from the sync pattern at the start of the image: a zero byte, ten bytes `0xff`, a zero byte (12 bytes). Without the pattern, the sectors are 2048 bytes. The sector count is the image size divided by the sector size.

In a raw image, the mode byte (offset 15) of sector 16 must be `0x01` (MODE1). A raw image with mode `0x02` is a MODE2 image (an XA image); it fails as listed in [What fails](#what-fails). Any other mode value means a damaged image.

## Volume descriptor

Sector 16 is the primary volume descriptor. The reader requires type `0x01` and the identifier `CD001` at offset 1. It takes the root directory record at offset 156 of the descriptor: the extent (first sector, offset 2) and the data length (offset 10). The root extent must lie inside the image.

## Directory records

The reader reads these record fields:

| Field | Offset |
|---|---|
| Length | 0 |
| Extent (first sector) | 2 |
| Data length | 10 |
| Flags (bit `0x02`: directory) | 25 |
| Name length | 32 |
| Name | 33 |

It follows each directory once. A path has at most two parts (`DATA/GRAPHICS.DAT`): a file in the root directory, or a subdirectory of the root directory and a file in it. A record matches a path part only if its directory flag fits the part: a directory for the first of two parts, a file for the last part. The sectors of an extent must all lie inside the image. A record that is too short for its name, or that ends outside the directory, fails the search.

## Names

A name is case-insensitive. The reader cuts the version suffix `;1`, then a final `.`, so `FTL.;1` matches `FTL` and `GRAPHICS.DAT;1` matches `GRAPHICS.DAT`.

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
