# ZIP archives

`src/data/zip.c` reads a ZIP archive with the game files (the PKWARE APPNOTE format): the central directory and the stored and deflated entries. It opens the archive with `"rb"` and writes nothing. The install uses the reader as a source ([install.md](install.md#sources)).

## Records

All fields are little-endian.

| Record | Signature | Read |
|---|---|---|
| End of central directory (22 bytes + comment) | `0x06054b50` | entry count (offset 10), directory size (12), directory offset (16), comment size (20) |
| Central directory entry (46 bytes + name, extra, comment) | `0x02014b50` | flags (8), method (10), compressed size (20), size (24), name size (28), extra size (30), comment size (32), local header offset (42) |
| Local header (30 bytes + name, extra) | `0x04034b50` | flags (6), name size (26), extra size (28) |

The reader searches backward from the end of the file through the last 22 + 65535 bytes and takes the first signature it finds whose comment ends inside the file. It reads the whole central directory into memory. Sizes and the method come from the central directory only, so the reader also reads an archive written to a stream (flag bit 3, a data descriptor).

## Support

| Case | Result |
|---|---|
| Method 0 (stored) | read; the compressed size must equal the size |
| Method 8 (deflate) | read |
| Different method | the archive opens; reading the entry fails |
| Encrypted entry (flag bit 0) | the archive fails to open |
| ZIP64 (entry count `0xffff`, or a size or offset `0xffffffff`) | the archive fails to open |
| Entry CRC-32 | not checked; the game file check (size and CRC-32) catches wrong bytes |

## Names

An entry name is case-insensitive. The reader sorts the entry names in byte order and takes the first name that is `DATA/GRAPHICS.DAT` or ends with `/DATA/GRAPHICS.DAT`. The root is the part before `DATA/GRAPHICS.DAT`: empty, or a path that ends with `/`. The port reads a game file at `<root><name>`. An archive without such an entry is not a source.

## Bounds checks

The reader fails on a mismatch instead of continuing to read:

- The central directory (offset and size) must lie before the end record in the file.
- Every central directory entry must fit into the directory, with its name, extra field, and comment.
- When reading an entry: its size must equal the game file's size in the table; the local header must lie inside the file; its signature must match; the entry must not be encrypted; the data (compressed size) must lie inside the file.
- Deflate stops at the game file's size and must produce exactly that many bytes.

## Deflate

The deflate decompressor is tinf 1.2.1 by Jørgen Ibsen (zlib license), vendored in `src/third_party/tinf/` (`tinflate.c`, `tinf.h`). Its bit reader stops at the end of the input and of the output (`TINF_DATA_ERROR`, `TINF_BUF_ERROR`). The port does not use a system zlib.

## Deviations from the original

None, because only the port reads ZIP archives.
