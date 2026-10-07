# TODO

- Manual test D15: take the bones of a dead champion to an altar (needs long play).
- Manual test J16: complete the final task and check the end of the game (needs long play).
- Manual test H6: an animated wall ornament with a sound (only on levels 2 and 4).
- Coding standard cleanup, module by module: remove the `open: module cleanup` entries of `tests/standard_allowlist.txt` (hex and decimal literals, prefixes, long functions, `engine/` includes of `ui/` headers).
- Rule 9 of `docs/coding-standard.md`: one header for `le16` and `le32`. Today `gdat.c`, `zone.c`, `save.c`, `recruit.c`, and `dungeon.c` each define their own copy.
