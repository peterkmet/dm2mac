# ZIP archives

The reader of a ZIP archive with the game files (PKWARE APPNOTE), in `src/data/zip.c`. It reads the central directory and the stored and deflated entries. It opens the archive with `"rb"` and writes nothing. The use of the reader is in [game-files.md](game-files.md).

## Records

All fields are little-endian.

| Record | Signature | Read |
|---|---|---|
| End of central directory (22 bytes + comment) | `0x06054b50` | entry count (offset 10), directory size (12), directory offset (16), comment size (20) |
| Central directory entry (46 bytes + name, extra, comment) | `0x02014b50` | flags (8), method (10), compressed size (20), size (24), name size (28), extra size (30), comment size (32), local header offset (42) |
| Local header (30 bytes + name, extra) | `0x04034b50` | flags (6), name size (26), extra size (28) |

The reader searches backward from the end of the file through the last 22 + 65535 bytes and takes the first signature it finds whose comment ends inside the file. It reads the whole central directory into memory. Sizes and the method come from the central directory only; the sizes of the local header are not used. An archive written to a stream (flag bit 3, a data descriptor) therefore reads as well.

## Support

| Case | Result |
|---|---|
| Method 0 (stored) | read; the compressed size must equal the size |
| Method 8 (deflate) | read |
| Different method | the archive opens; reading the entry fails |
| Encrypted entry (flag bit 0) | the archive fails to open |
| ZIP64 (entry count `0xffff`, or a size or offset `0xffffffff`) | the archive fails to open |
| Entry CRC-32 | not checked; the check of the game file (size and CRC-32) catches wrong bytes |

## Names

An entry name matches without case. The root of the game files is the prefix of the first entry in byte order of the names whose name is `DATA/GRAPHICS.DAT` or ends with `/DATA/GRAPHICS.DAT`. The prefix is empty or ends with `/`. The port reads a game file at `<root><name>`. An archive without such an entry is no source.

## Bounds checks

The reader fails on a mismatch instead of reading on:

- The end record with its directory (offset and size) must lie before the end record in the file.
- Every central directory entry must fit into the directory, with its name, extra field, and comment.
- At reading: the size of the entry must equal the size of the game file in the table; the local header must lie inside the file; its signature must match; it must not be encrypted; the data (compressed size) must lie inside the file.
- Deflate stops at the size of the game file and must produce exactly that many bytes.

## Deflate

The inflate is tinf 1.2.1 by Jørgen Ibsen (zlib license), vendored in `src/third_party/tinf/` (`tinflate.c`, `tinf.h`). Its bit reader stops at the end of the input and of the output (`TINF_DATA_ERROR`, `TINF_BUF_ERROR`). The port uses no system zlib.

## Deviations from the original

None, because only the port reads ZIP archives.
