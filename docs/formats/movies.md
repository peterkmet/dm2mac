# Movies

The movies of the game: the launcher script of `IBMIOP.EXE`, the movie player files, the Interplay MVE stream, and the splash picture `INTRPLAY.PCX`. The SVGA option of the installer selects the movie files ([runtime.md](runtime.md#svga-option)). The addresses and the notation follow the conventions of [skull-map.md](../reference/skull-map.md).

## Launcher script (`IBMIOP.EXE`)

- The SVGA script table at the decompressed offset `0x3f62` has one record of 53 bytes per step: the program name (14 bytes; `@1` is the game), the arguments (32 bytes), two flag bytes, the highest exit code that the record handles (offset `0x30`), and the next step for each exit code (offset `0x31`, 1-based; 0 ends the launcher). An exit code above the highest gives the next step of exit code 0 (code at `0x3cea`-`0x3d17`).
- Steps: 1 `splash intrplay.pcx 3`, 2 `ftl noabort`, 3 `intro 640x350 wide`, 4 the game, 5 `end 640x350 wide`, 6 `credits 640x350 wide`.
- The VGA script table at the decompressed offset `0x40a0` has the same steps with `noabort` and `splash credits.pcx 10` at the end.
- The game: exit code 0 (quit) ends; 1 (completed) goes to `END`, then `CREDITS`; 2 (QUIT GAME of a dead party) goes to `CREDITS`; 3 (RESTART) starts the game step again; every other code ends.

## Movie player

- Each SVGA movie file is the same DOS player program with the MVE data after it. The last `Interplay MVE File` signature of the file starts the data.
- Arguments (the 32-bit code of the player): a first argument that names a screen size replaces the video mode of the movie (`640x350`: a 640 × 350 screen). `wide` shows the video 4/3 wider (432 → 576 pixels). `noabort` turns off the stop by the player. The player centers the video on its screen.
- `FTL` plays on 640 × 480 (VESA `0x101`, from its opcode `0x0a`): the 432 × 320 video fills about 67 % of the width and the height of a 4:3 monitor. `INTRO`, `END`, and `CREDITS` play on 640 × 350, 576 × 320 wide: about 90 %.

## MVE stream

- File header: the signature `Interplay MVE File\x1a\0` (20 bytes) and the words `0x001a`, `0x0100`, `0x1133`.
- Chunks: a word length and a word type, then opcodes up to the length. Opcode: a word length, a type byte, a version byte, then the body.
- Opcodes of the DM2 movies:

| Opcode | Body | Port |
|---|---|---|
| `0x00` | - | End of stream. |
| `0x01` | - | End of chunk. |
| `0x02` | dword µs per subdivision, word subdivisions | Time of one picture: 83.328 ms (`FTL`, `INTRO`, `END`), 100 ms (`CREDITS`). |
| `0x03` v0 | word ?, word flags, word rate, word buffer size | Bit 0 stereo, bit 1 16-bit, bit 2 compressed (version 1 only). The movies: 22050 Hz, stereo, unsigned 8-bit. |
| `0x04` | - | Starts the sound. |
| `0x05` v2 | word width, word height in blocks of 8 × 8 | 54 × 40 blocks: 432 × 320. |
| `0x07` v1 | word first color, word count, word ? | Shows the picture. |
| `0x08`, `0x09` | word sequence, word stream mask, word length, samples (`0x08` only) | Audio and silence; the port plays stream mask bit 0. |
| `0x0a` | word width, word height, word VESA mode | `INTRO`, `END`, `FTL`, `CREDITS`: `(640, 480, 0x101)`; the VGA movies `*.VGA`: `(320, 200, 0x13)`. |
| `0x0c` | word first color, word count, 6-bit RGB | Palette. |
| `0x0f` | 4 bits per block | Decoding map of the next video data. |
| `0x11` v3 | 14-byte header (word flags at 12), block data | Video data; flag bit 0 swaps the frames before the decode. |
| `0x13` | 132 bytes | Block statistics: counts of the encodings. Skipped. |

- The 8-bit video codec: the table of encodings `0x0` to `0xf` is in `src/data/mve_video.c`.
- The first audio frames come before the first picture (about 1 second); the sound starts with the first picture.

## Splash picture

- `INTRPLAY.PCX`: PCX version 5, RLE, 8 bits per pixel, 1 plane, 320 × 200, a 256-color palette of 8-bit values after the byte `0x0c` at the end of the file. The port shows it for 3 seconds on the game screen with its palette (values shifted right by 2).

## Deviations from the original

- The VGA movies and `CREDITS.PCX` of the VGA script are not played.
- The movie screen is the largest 4:3 rectangle of the window with black borders and linear filtering: the scale is not an integer, and no scale filter applies.
- A click skips; a key does not skip. In the vanilla mode a click does not stop the splash picture: the script gives it a fixed 3 seconds.
- The movie sound plays at full volume through the sound output of the port at 44100 Hz: each sample repeats two times. Another audio format plays the movie without sound.
- A missing file, damaged data, a splash picture of another size than 320 × 200, or a movie whose mode is not 640 × 480 is a failed step: the script goes on with the next step. Damage in the middle of a movie stops it there.
- A block whose source is outside the frame, or with encoding 6, stays unchanged.
- `--save PATH` plays no start movies; the end movies play after a game that started with `--save`. A RESTART starts the program again with the environment variable `DM2_RESTARTED`: no start movies, as the launcher starts only the game step again.
- A closed window or a control `quit` ends the program without the end movies; QUIT GAME plays them.
- The control socket gives `"mode": "movie"` during a movie; `wait` counts the game timer ticks, which stand still ([llm-control.md](../llm-control.md)).
