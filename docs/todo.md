# TODO

- Manual test D15: take the bones of a dead champion to an altar (needs long play).
- Manual test J16: complete the final task and check the end of the game (needs long play).
- Manual test H6: an animated wall ornament with a sound (only on levels 2 and 4).
- Coding standard cleanup, module by module: remove the `open: module cleanup` entries of `tests/standard_allowlist.txt` (hex and decimal literals, prefixes, long functions, `engine/` includes of `ui/` headers). Replace the hand-built 16-bit and 32-bit words in `engine/` with `le16` and `le32` of `data/le.h`, for example `magic.c:153`, `projectile.c:156`, `ai.c:80`, `save.c:717`, and `creature.c:335`.
