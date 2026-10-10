# Movies Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task (the run of the agentic-workflow skill). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `build/dm2` without `--save` shows the Interplay splash picture, plays the SVGA movies `FTL` and `INTRO`, then shows the start menu; after a completed game it plays `END` and `CREDITS`, after QUIT GAME of a dead party it plays `CREDITS`, as the DOS launcher `IBMIOP.EXE` does.

**Architecture:** Two decoders in `data/` work from memory only: `data/pcx.c` (the splash picture) and `data/mve.c` with `data/mve_video.c` (the MVE stream and the 8-bit Interplay video codec). `ui/movie.c` holds the launcher script, the skip rules, the frame timer, and a ring of 16-bit stereo audio. `platform/main.c` runs the movie loop (events, control socket, frame update, present, audio mixer) before the start menu and after the main loop; `platform/platform.c` presents a movie picture on a 4:3 screen with its own texture.

**Tech Stack:** C11, clang, `port/Makefile` (globbed sources), SDL3 only in `platform/main.c` and `platform/platform.c`, Python 3.14 with uv, pytest with ctypes on `build/libdm2.dylib`.

**Spec:** `docs/superpowers/2026-10-10-movies.md`

## Global Constraints

- The game rules, the saves, and the game screen do not change (PRD Invariant 3). The scale filters do not apply to the movies.
- `data/mve.c`, `data/mve_video.c`, and `data/pcx.c` decode from memory only: no file reads, no SDL, no global state (Invariant 1). The decoder does not read or write outside its input data and its frame buffers, also for damaged data (Invariant 2).
- `ui/` does not include `platform/` (coding standard rule 8, Invariant 4). No SDL outside `platform/main.c` and `platform/platform.c`.
- `main` returns `session.exit_code` as before; a skipped, damaged, or missing movie does not change it (Invariant 5). `build/dm2 --save PATH` opens no movie file and no `INTRPLAY.PCX` (Invariant 6).
- `docs/coding-standard.md` is the coding standard; follow it for every change in `port/src/`. Rule 1: every number other than 0, 1, -1, 2 is a named constant with one definition. Rule 3: a function has at most 60 lines. Rule 7: each new header has a `Prefixes` line; a prefix belongs to one header; no prefix starts with the prefix of another header. Rule 9: use `le16` and `le32` of `data/le.h`.
- `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` prints `0`.
- Keep existing comments. New comments: one line, what the code does in game terms.
- Every deviation of the port goes into `docs/skull-formats.md`. A rename updates `docs/skull-symbols.csv` and the docs in the same commit.
- The author runs the build and only the tests of the changed code, never the full suite: one module `uv run pytest -q tests/test_x.py`; several modules `uv run pytest -q -n auto tests/test_x.py tests/test_y.py`. Always also `uv run pytest -q tests/test_standard.py`. Never `-p no:xdist`.
- Each test module loads its own library copy (the `lib` fixture of `tests/conftest.py`) and resets the global state it reads (`game_mode` back to vanilla, `movie_stop()`). Socket paths: `tempfile.mkdtemp(prefix="dm2c", dir="/tmp")`. Tests pass in any order and in parallel. Tests that need the game files use the `game_dir` fixture (it skips without `game/SKULL.EXE`).
- Do not download or run third-party code (libraries, CDN files, packages outside the project's lock file) to check behavior; report what you could not verify.
- Commit at the end of each task: Conventional Commits, one type per commit (`feat:`, `test:`, `docs:`).
- All docs in English, ASD-STE100 style, as `docs/coding-standard.md` and the PRD are written.
- The plan is written against `main` at `1de8faf`. The turn-based plan (`docs/superpowers/2026-10-10-turnbased-mode-plan.md`) also changes `platform/main.c` and `platform/control.c`. If `GAME_MODE_TURNBASED` is in `port/src/engine/game.h` when the run starts, the controller stops before Task 5 and asks the user.

Test list (CLAUDE.md §2). Write a test only for: a check against a reference (original program, golden snapshot), a parser or data format, a rule with more than one outcome that no reference check or type check covers (dates, parsing, prices, state machines), a regression of a real bug (one test per bug). No tests for: constants and data tables; wiring, scaffold, CLI arguments, logging; what the compiler, a linter or a standard check catches; UI and layout (the user tests them manually); what another test already covers. No mutation tests.

In this plan the tests cover: the PCX format (Task 1), the MVE stream format (Task 2), the decode of all frames inside the frame and with damaged data (Task 3), the script rules, the skip rules, and the failed steps of `ui/movie.c` (Task 4), and the control requests during a movie (Task 5). The picture, the sound, the window, and the launcher wiring in `main.c` go to the manual test checklist (Task 6).

## PRD changes confirmed

The user confirmed two PRD changes on 2026-10-10; the PRD has them (Current state, Requirements 1, 5, 7, Decisions "End movies" and "Screen of a movie"):

1. The script table of `IBMIOP.EXE` sends exit code 2 of the game (QUIT GAME after the death of the party) to `CREDITS`. The port does the same.
2. The script gives `INTRO`, `END`, and `CREDITS` the arguments `640x350 wide`: the player shows the video 4/3 wider on a 640 × 350 screen that a VGA monitor stretches to 4:3. The port shows each movie on the screen of the player, as the largest 4:3 rectangle of the window, with linear filtering.

## Decisions of this plan

These follow from the PRD, the code, and the data; the plan reviewer and the user can reject one.

| Topic | Decision | Reason |
|---|---|---|
| Restart without movies | `main` sets the environment variable `DM2_RESTARTED` before `execvp`; a start with it plays no start movies. | A restart starts the program again (`execvp`); the launcher starts only the game step again. The PRD has no option to turn the movies off. |
| Time base | `movie_update(now_ns)` with `SDL_GetTicksNS()`. Picture k (k from 0) shows at the start of the step plus k times the frame time (opcode `0x02`: microseconds × subdivisions). A late update decodes the missed pictures. | The frame times (83.328 ms, 100 ms) are not whole game ticks. The audio device plays in real time; the same clock keeps the picture with it. Tests pass their own times. |
| Video buffers | Two buffers in `MveStream`. Video data with flag bit 0 swaps them before the decode: the shown frame becomes the previous frame, and the decode draws over the frame before it. Encoding 1 keeps a block; encodings 2 and 3 read the frame being drawn. | The public MVE descriptions; every video data opcode of the four movies has flag bit 0 except the first. |
| Opcode `0x13` | Skipped. | 132 bytes of block statistics (PRD Current state). |
| Encoding 6 and a motion outside the frame | The block stays unchanged and `bad_blocks` counts it. | Encoding 6 is not part of the 8-bit codec; no movie of DM2 uses it. A test checks `bad_blocks == 0` for all frames of the four movies. |
| Damaged data | `mve_step` returns `MVE_ERROR`; the step ends and the script goes on (PRD Requirement 1). | One rule for a missing, damaged, and short movie. |
| Audio | Unsigned 8-bit samples, mono or stereo, at a rate that divides `SOUND_OUTPUT_RATE`: each sample becomes `(s - 128) * 256` and repeats `SOUND_OUTPUT_RATE / rate` times. Any other audio format plays the movie without sound. A ring of 131072 stereo frames (about 3 s) holds the queued audio. | PRD Requirement 6; the movies have 22050 Hz stereo 8-bit and up to 13 audio frames ahead of the picture. |
| Splash size | `INTRPLAY.PCX` must be 320 × 200; another size is a failed step. | The splash shows on the game screen (PRD Requirement 5). |
| Control requests during a movie | `click` and `press` skip by the rules of the mode; `release`, `key`, and `command` do nothing; `wait` counts game timer ticks, which stand still during a movie. | PRD Requirement 2: only a click skips. The game input stays empty for the start menu. |
| Movie loop rate | One `movie_update` and one present per game tick (60 Hz); `control_poll` and the events on every PIT tick. | As the modal loops of `main.c`. |
| Quit before the end movies | A closed window or a `quit` request during "THE END" ends the game with exit code 2, and `CREDITS` plays; a second quit stops it. | `session_end` gives exit code 2 also for a quit (`ui/session.c:525-527`). The PRD stops a script only on a quit during a movie; this keeps the code minimal. |
| Skip rule | A click stops a step when `game_mode != GAME_MODE_VANILLA` or the step has no `noabort`. | A later game mode (turn-based) skips as the advanced mode. |

## Review Focus

1. A movie file that ends in the middle of its stream: expected the movie to play up to the damage and the next step to start. Task 4 owns it; `test_failed_steps_are_skipped[truncated]` pins it.
2. A click in the vanilla mode during `FTL` (`noabort`): expected the click to be dropped, not kept for `INTRO`. Task 4 owns it; `test_vanilla_click_keeps_splash_and_ftl` pins it.
3. The game palette and screen after the splash: expected the start menu to show with its own colors. Task 4 owns it; `test_script_end_restores_the_game_screen` pins it.
4. A wrong, random, or cut byte in the video data: expected no crash, and the stream ends with `MVE_END` or `MVE_ERROR`. Task 3 owns it; `test_damaged_video_data_ends_without_a_crash` pins it.
5. A closed window or a `quit` request during the start movies or the end movies: expected the program to end at once with the exit code of the session and without the start menu. Task 5 owns it (wiring in `main.c`, no automated test); manual check M8 in Task 6.

---

### Task 1: The PCX decoder

**Files:**
- Create: `port/src/data/pcx.h`, `port/src/data/pcx.c`
- Test: `tests/test_pcx.py`

**Interfaces:**
- Consumes: `le16` of `data/le.h`.
- Produces: `int pcx_decode(const uint8_t *data, size_t size, uint8_t *pixels, int max_width, int max_height, int *width, int *height, uint8_t palette[PCX_PALETTE_COLORS][PCX_RGB])`; `PCX_OK` 0, `PCX_ERR_DATA` 1; `PCX_PALETTE_COLORS` 256; `PCX_RGB` 3. The palette has 8-bit values.

- [ ] **Step 1: Write the failing test**

`tests/test_pcx.py`:

```python
"""The PCX decoder (port/src/data/pcx.c): the splash picture INTRPLAY.PCX of the DOS launcher."""

import ctypes

import pytest

PCX_OK, PCX_ERR_DATA = 0, 1
WIDTH, HEIGHT, COLORS, RGB = 320, 200, 256, 3
HEADER = 128
PALETTE_SIZE = 1 + COLORS * RGB  # the marker byte and the colors


def decode(lib, data, max_w=WIDTH, max_h=HEIGHT):
    lib.pcx_decode.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                               ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    pixels = (ctypes.c_uint8 * (max_w * max_h))()
    palette = (ctypes.c_uint8 * (COLORS * RGB))()
    w, h = ctypes.c_int(), ctypes.c_int()
    result = lib.pcx_decode(data, len(data), pixels, max_w, max_h, ctypes.byref(w), ctypes.byref(h), palette)
    return result, w.value, h.value, bytes(pixels), bytes(palette)


def reference_decode(data):
    """PCX RLE in Python: a byte with the two high bits set repeats the next byte; rows have the bytes per line of the
    header, and only the first width bytes of a row are pixels."""
    word = lambda at: int.from_bytes(data[at:at + 2], "little")
    w, h, stride = word(8) - word(4) + 1, word(10) - word(6) + 1, word(66)
    out, p = bytearray(), HEADER
    while len(out) < stride * h:
        b = data[p]
        p += 1
        if b >= 0xC0:
            out += bytes([data[p]]) * (b & 0x3F)
            p += 1
        else:
            out.append(b)
    return b"".join(bytes(out[y * stride:y * stride + w]) for y in range(h))


@pytest.fixture
def splash(game_dir):
    return (game_dir / "INTRPLAY.PCX").read_bytes()


def test_splash_decodes_as_the_reference(lib, splash):
    result, w, h, pixels, palette = decode(lib, splash)
    assert (result, w, h) == (PCX_OK, WIDTH, HEIGHT)
    assert pixels == reference_decode(splash)
    assert palette == splash[-COLORS * RGB:]


@pytest.mark.parametrize("cut", [0, HEADER - 1, HEADER, 5000, -PALETTE_SIZE])
def test_short_data_is_an_error(lib, splash, cut):
    assert decode(lib, splash[:cut])[0] == PCX_ERR_DATA


@pytest.mark.parametrize("offset, value", [(0, 0), (1, 3), (3, 4), (65, 3)])
def test_other_pcx_kinds_are_an_error(lib, splash, offset, value):
    # manufacturer, version, bits per pixel, planes
    data = bytearray(splash)
    data[offset] = value
    assert decode(lib, bytes(data))[0] == PCX_ERR_DATA


def test_picture_larger_than_the_buffer_is_an_error(lib, splash):
    assert decode(lib, splash, WIDTH - 1, HEIGHT)[0] == PCX_ERR_DATA
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -C port && uv run pytest -q tests/test_pcx.py`
Expected: FAIL with `AttributeError: ... pcx_decode`.

- [ ] **Step 3: Write the decoder**

`port/src/data/pcx.h`:

```c
#ifndef PCX_H
#define PCX_H
#include <stddef.h>
#include <stdint.h>

/* Prefixes: pcx_ */

/* PCX pictures, 8 bits per pixel, RLE, palette at the end (the splash INTRPLAY.PCX); decodes from memory only. */

#define PCX_PALETTE_COLORS 256
#define PCX_RGB 3

enum { PCX_OK, PCX_ERR_DATA };

/* Decodes the picture (width bytes per row) and its 8-bit palette; PCX_ERR_DATA for another kind, size, or short data. */
int pcx_decode(const uint8_t *data, size_t size, uint8_t *pixels, int max_width, int max_height, int *width,
               int *height, uint8_t palette[PCX_PALETTE_COLORS][PCX_RGB]);

#endif
```

`port/src/data/pcx.c`:

```c
#include <string.h>
#include "le.h"
#include "pcx.h"

#define PCX_HEADER_SIZE 128
#define PCX_MANUFACTURER 0x0a          /* ZSoft */
#define PCX_VERSION_256_COLORS 5
#define PCX_ENCODING_RLE 1
#define PCX_BITS_PER_PIXEL 8
#define PCX_PLANES 1
#define PCX_PALETTE_MARKER 0x0c        /* the byte before the palette at the end of the file */
#define PCX_PALETTE_SIZE (1 + PCX_PALETTE_COLORS * PCX_RGB)
#define PCX_RUN_FLAGS 0xc0             /* a byte with both high bits set is a run length */
#define PCX_RUN_MASK 0x3f
/* Offsets in the header. */
#define PCX_AT_VERSION 1
#define PCX_AT_ENCODING 2
#define PCX_AT_BITS 3
#define PCX_AT_X_MIN 4
#define PCX_AT_Y_MIN 6
#define PCX_AT_X_MAX 8
#define PCX_AT_Y_MAX 10
#define PCX_AT_PLANES 65
#define PCX_AT_BYTES_PER_LINE 66

/* Checks the header for an 8-bit one-plane RLE picture and reads its size and the bytes of a row. */
static int read_header(const uint8_t *data, size_t size, int *width, int *height, int *stride)
{
    if (size < PCX_HEADER_SIZE + PCX_PALETTE_SIZE || data[0] != PCX_MANUFACTURER
        || data[PCX_AT_VERSION] != PCX_VERSION_256_COLORS || data[PCX_AT_ENCODING] != PCX_ENCODING_RLE
        || data[PCX_AT_BITS] != PCX_BITS_PER_PIXEL || data[PCX_AT_PLANES] != PCX_PLANES
        || data[size - PCX_PALETTE_SIZE] != PCX_PALETTE_MARKER)
        return PCX_ERR_DATA;
    *width = le16(data + PCX_AT_X_MAX) - le16(data + PCX_AT_X_MIN) + 1;
    *height = le16(data + PCX_AT_Y_MAX) - le16(data + PCX_AT_Y_MIN) + 1;
    *stride = le16(data + PCX_AT_BYTES_PER_LINE);
    return *width > 0 && *height > 0 && *stride >= *width ? PCX_OK : PCX_ERR_DATA;
}

int pcx_decode(const uint8_t *data, size_t size, uint8_t *pixels, int max_width, int max_height, int *width,
               int *height, uint8_t palette[PCX_PALETTE_COLORS][PCX_RGB])
{
    int stride;
    if (read_header(data, size, width, height, &stride) != PCX_OK || *width > max_width || *height > max_height)
        return PCX_ERR_DATA;
    const uint8_t *p = data + PCX_HEADER_SIZE, *end = data + size - PCX_PALETTE_SIZE;
    int run = 0;
    uint8_t value = 0;
    for (int y = 0; y < *height; y++)
        for (int x = 0; x < stride; x++) {
            while (!run) {  /* a run of 0 bytes gives no pixel */
                if (p >= end) return PCX_ERR_DATA;
                value = *p++;
                run = 1;
                if ((value & PCX_RUN_FLAGS) != PCX_RUN_FLAGS) continue;
                if (p >= end) return PCX_ERR_DATA;
                run = value & PCX_RUN_MASK;
                value = *p++;
            }
            run--;
            if (x < *width) pixels[y * *width + x] = value;
        }
    memcpy(palette, end + 1, PCX_PALETTE_COLORS * PCX_RGB);
    return PCX_OK;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C port && uv run pytest -q -n auto tests/test_pcx.py tests/test_standard.py`
Expected: PASS. `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` prints `0`.

- [ ] **Step 5: Commit**

```bash
git add port/src/data/pcx.h port/src/data/pcx.c tests/test_pcx.py
git commit -m "feat: decode the PCX splash picture"
```

---

### Task 2: The MVE stream parser

**Files:**
- Create: `port/src/data/mve.h`, `port/src/data/mve.c`
- Test: `tests/test_mve.py`

**Interfaces:**
- Consumes: `le16`, `le32` of `data/le.h`.
- Produces (`data/mve.h`): `MveStream` (fields below, in this order), `MVE_ERROR` -1, `MVE_OK` 0, `MVE_END` 1, `MVE_AUDIO` 2, `MVE_SHOW` 3, `MVE_BLOCK_SIZE` 8, `MVE_WIDTH_MAX` 640, `MVE_HEIGHT_MAX` 480, `MVE_PALETTE_COLORS` 256, `MVE_RGB` 3; `int mve_open(MveStream *m, const uint8_t *file, size_t size)`; `int mve_step(MveStream *m)`; `const uint8_t *mve_pixels(const MveStream *m)`.

- [ ] **Step 1: Write the failing test**

`tests/test_mve.py`:

```python
"""The MVE decoder (port/src/data/mve.c and mve_video.c): the SVGA movies FTL, INTRO, END, and CREDITS of the PC
version. No reference decoder: the tests check the stream format against a parse of the files (PRD Current state)."""

import ctypes
from collections import Counter

import pytest

MVE_ERROR, MVE_OK, MVE_END, MVE_AUDIO, MVE_SHOW = -1, 0, 1, 2, 3
WIDTH_MAX, HEIGHT_MAX, COLORS, RGB = 640, 480, 256, 3
VGA_MAX = 63
STEP_LIMIT = 1000000  # more mve_step calls than any stream needs (an opcode has at least 4 bytes)

# file, show frame opcodes, audio frames of track 0 (opcode 0x08 and the silent 0x09 of mask 0xffff), frame time in µs
MOVIES = [("FTL", 66, 77, 83328), ("INTRO", 217, 228, 83328), ("END", 600, 611, 83328), ("CREDITS", 360, 369, 100000)]


class MveStream(ctypes.Structure):
    """mve.h MveStream."""
    _fields_ = [("data", ctypes.c_void_p), ("size", ctypes.c_size_t), ("pos", ctypes.c_size_t),
                ("chunk_end", ctypes.c_size_t), ("frame_us", ctypes.c_uint32), ("mode_width", ctypes.c_int),
                ("mode_height", ctypes.c_int), ("audio_rate", ctypes.c_int), ("audio_stereo", ctypes.c_bool),
                ("audio_16bit", ctypes.c_bool), ("audio_compressed", ctypes.c_bool),
                ("audio_started", ctypes.c_bool), ("audio", ctypes.c_void_p), ("audio_size", ctypes.c_int),
                ("width", ctypes.c_int), ("height", ctypes.c_int), ("map", ctypes.c_void_p),
                ("map_size", ctypes.c_int), ("frames", ctypes.c_int), ("bad_blocks", ctypes.c_int),
                ("current", ctypes.c_int), ("palette", ctypes.c_uint8 * (COLORS * RGB)),
                ("buffers", ctypes.c_uint8 * (2 * WIDTH_MAX * HEIGHT_MAX))]


class Movie:
    """An open stream and the copy of the file bytes that it points into."""

    def __init__(self, lib, data):
        lib.mve_open.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
        lib.mve_step.argtypes = [ctypes.c_void_p]
        self.lib, self.data, self.m = lib, ctypes.create_string_buffer(data, len(data)), MveStream()
        self.opened = lib.mve_open(ctypes.byref(self.m), self.data, len(data))

    def step(self):
        return self.lib.mve_step(ctypes.byref(self.m))

    def run(self, on_show=None):
        """Steps to the end or an error; returns the count of each result."""
        counts = Counter()
        for _ in range(STEP_LIMIT):
            result = self.step()
            counts[result] += 1
            if result == MVE_SHOW and on_show:
                on_show(self)
            if result in (MVE_END, MVE_ERROR):
                return counts
        raise AssertionError("the stream does not end")

    def pixels(self):
        size = self.m.width * self.m.height
        start = self.m.current * WIDTH_MAX * HEIGHT_MAX
        return bytes(self.m.buffers[start:start + size])


@pytest.mark.parametrize("name, frames, audio, frame_us", MOVIES)
def test_movie_streams_have_their_frames_and_audio(lib, game_dir, name, frames, audio, frame_us):
    movie = Movie(lib, (game_dir / name).read_bytes())
    assert movie.opened == MVE_OK
    counts = movie.run()
    m = movie.m
    assert counts == {MVE_SHOW: frames, MVE_AUDIO: audio, MVE_END: 1}
    assert (m.frames, m.frame_us, m.mode_width, m.mode_height, m.width, m.height) == (frames, frame_us, 640, 480,
                                                                                       432, 320)
    assert (m.audio_rate, m.audio_stereo, m.audio_16bit, m.audio_compressed, m.audio_started) == (22050, True, False,
                                                                                                   False, True)
    assert max(m.palette) <= VGA_MAX and tuple(m.palette[:RGB]) == (0, 0, 0)


def test_vga_movie_sets_its_mode(lib, game_dir):
    movie = Movie(lib, (game_dir / "FTL.VGA").read_bytes())
    assert movie.opened == MVE_OK
    while movie.step() == MVE_AUDIO:
        pass
    assert (movie.m.mode_width, movie.m.mode_height) == (320, 200)


@pytest.mark.parametrize("data", [b"", b"MZ" + bytes(1000), b"Interplay MVE File\x1a\x00" + bytes(6)])
def test_data_without_an_mve_stream_does_not_open(lib, data):
    assert Movie(lib, data).opened == MVE_ERROR


@pytest.mark.parametrize("keep", [0.3, 0.6, 0.9])  # the MVE data of FTL starts after 20 % of the file
def test_cut_stream_ends_with_an_error(lib, game_dir, keep):
    data = (game_dir / "FTL").read_bytes()
    movie = Movie(lib, data[:int(len(data) * keep)])
    assert movie.opened == MVE_OK
    counts = movie.run()
    assert counts[MVE_ERROR] == 1 and counts[MVE_SHOW] < 66
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -C port && uv run pytest -q tests/test_mve.py`
Expected: FAIL with `AttributeError: ... mve_open`.

- [ ] **Step 3: Write the parser**

`port/src/data/mve.h`:

```c
#ifndef MVE_H
#define MVE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Prefixes: mve_ */

/* Interplay MVE movies: chunks, opcodes, 8-bit video, audio (docs/skull-formats.md section 15); from memory only. */

#define MVE_BLOCK_SIZE 8            /* pixels per side of a video block */
#define MVE_WIDTH_MAX 640
#define MVE_HEIGHT_MAX 480
#define MVE_PIXELS_MAX (MVE_WIDTH_MAX * MVE_HEIGHT_MAX)
#define MVE_PALETTE_COLORS 256
#define MVE_RGB 3

enum {
    MVE_ERROR = -1,   /* damaged data: the movie stops */
    MVE_OK,
    MVE_END,          /* the end of the stream */
    MVE_AUDIO,        /* an audio frame of track 0: audio and audio_size; audio NULL for silence */
    MVE_SHOW,         /* a new picture: mve_pixels and palette */
};

/* The state of one movie; the caller owns it (no global state). */
typedef struct {
    const uint8_t *data;            /* the MVE stream after the file header */
    size_t size, pos;               /* the stream size and the position of the next opcode */
    size_t chunk_end;               /* the end of the current chunk */
    uint32_t frame_us;              /* the time of one picture: timer rate times subdivisions (opcode 0x02) */
    int mode_width, mode_height;    /* the video mode of the player (opcode 0x0a) */
    int audio_rate;                 /* samples per second and channel (opcode 0x03); 0: no audio */
    bool audio_stereo, audio_16bit, audio_compressed;
    bool audio_started;             /* opcode 0x04 came: the sound plays from the next picture */
    const uint8_t *audio;           /* the samples of the last MVE_AUDIO, NULL for silence */
    int audio_size;                 /* the bytes of the last MVE_AUDIO */
    int width, height;              /* the video size in pixels (opcode 0x05) */
    const uint8_t *map;             /* the decoding map of the next video data (opcode 0x0f): 4 bits per block */
    int map_size;
    int frames;                     /* show frame opcodes so far */
    int bad_blocks;                 /* blocks with encoding 6 or a source outside the frame; they stay unchanged */
    int current;                    /* the buffer of the frame that is drawn and shown */
    uint8_t palette[MVE_PALETTE_COLORS][MVE_RGB];  /* 6-bit VGA values */
    uint8_t buffers[2][MVE_PIXELS_MAX];            /* the current frame and the frame before it */
} MveStream;

/* Finds the MVE data after the player program of a movie file; MVE_ERROR when the file has none. */
int mve_open(MveStream *m, const uint8_t *file, size_t size);
/* Runs the opcodes up to the next audio frame, picture, or the end. */
int mve_step(MveStream *m);
/* The picture of the last MVE_SHOW: width × height palette indexes. */
const uint8_t *mve_pixels(const MveStream *m);

#endif
```

`port/src/data/mve.c`:

```c
#include <string.h>
#include "le.h"
#include "mve.h"

#define MVE_SIGNATURE "Interplay MVE File\x1a"
#define SIGNATURE_SIZE (sizeof MVE_SIGNATURE)    /* with its 0 byte */
#define FILE_HEADER_SIZE 26                      /* the signature and three magic words */
#define HEADER_WORD_SIZE 0x001a
#define HEADER_WORD_VERSION 0x0100
#define HEADER_WORD_CHECK 0x1133
#define CHUNK_HEADER_SIZE 4                      /* length and type */
#define OPCODE_HEADER_SIZE 4                     /* length, type, and version */
#define OPCODE_TYPE 2
#define OPCODE_VERSION 3
#define TIMER_SIZE 6                             /* microseconds per subdivision, subdivisions per picture */
#define TIMER_SUBDIVISIONS 4
#define AUDIO_INIT_SIZE 8
#define AUDIO_INIT_FLAGS 2
#define AUDIO_INIT_RATE 4
#define AUDIO_FLAG_STEREO 0x0001
#define AUDIO_FLAG_16BIT 0x0002
#define AUDIO_FLAG_COMPRESSED 0x0004             /* only from audio init version 1 on */
#define AUDIO_FRAME_HEADER_SIZE 6                /* sequence, stream mask, and length */
#define AUDIO_FRAME_MASK 2
#define AUDIO_FRAME_LENGTH 4
#define AUDIO_TRACK 0x0001                       /* the stream mask bit of the track that the player plays */
#define VIDEO_INIT_SIZE 4                        /* width and height in blocks */
#define VIDEO_INIT_HEIGHT 2
#define MODE_SIZE 4
#define MODE_HEIGHT 2
#define PALETTE_HEADER_SIZE 4                    /* first color and color count */
#define PALETTE_COUNT 2
#define VGA_VALUE_MASK 0x3f

enum {
    OP_END_OF_STREAM = 0x00, OP_CREATE_TIMER = 0x02, OP_INIT_AUDIO = 0x03, OP_START_AUDIO = 0x04,
    OP_INIT_VIDEO = 0x05, OP_SHOW_FRAME = 0x07, OP_AUDIO_FRAME = 0x08, OP_AUDIO_SILENCE = 0x09,
    OP_VIDEO_MODE = 0x0a, OP_PALETTE = 0x0c, OP_DECODING_MAP = 0x0f, OP_VIDEO_DATA = 0x11,
};

int mve_open(MveStream *m, const uint8_t *file, size_t size)
{
    memset(m, 0, offsetof(MveStream, buffers));
    for (size_t at = size >= FILE_HEADER_SIZE ? size - FILE_HEADER_SIZE + 1 : 0; at-- > 0;) {
        const uint8_t *header = file + at;
        if (memcmp(header, MVE_SIGNATURE, SIGNATURE_SIZE)) continue;
        if (le16(header + SIGNATURE_SIZE) != HEADER_WORD_SIZE
            || le16(header + SIGNATURE_SIZE + sizeof(uint16_t)) != HEADER_WORD_VERSION
            || le16(header + SIGNATURE_SIZE + 2 * sizeof(uint16_t)) != HEADER_WORD_CHECK)
            return MVE_ERROR;
        m->data = header + FILE_HEADER_SIZE;
        m->size = size - at - FILE_HEADER_SIZE;
        return MVE_OK;
    }
    return MVE_ERROR;
}

const uint8_t *mve_pixels(const MveStream *m)
{
    return m->buffers[m->current];
}

/* Enters the next chunk; false when the stream ends inside its header or its data. */
static bool next_chunk(MveStream *m)
{
    if (m->size - m->pos < CHUNK_HEADER_SIZE) return false;
    size_t length = le16(m->data + m->pos);
    m->pos += CHUNK_HEADER_SIZE;
    if (m->size - m->pos < length) return false;
    m->chunk_end = m->pos + length;
    return true;
}

/* Reads the audio format: rate, channels, sample size, compression. */
static int init_audio(MveStream *m, int version, const uint8_t *body, int size)
{
    if (size < AUDIO_INIT_SIZE) return MVE_ERROR;
    uint16_t flags = le16(body + AUDIO_INIT_FLAGS);
    m->audio_rate = le16(body + AUDIO_INIT_RATE);
    m->audio_stereo = (flags & AUDIO_FLAG_STEREO) != 0;
    m->audio_16bit = (flags & AUDIO_FLAG_16BIT) != 0;
    m->audio_compressed = version > 0 && (flags & AUDIO_FLAG_COMPRESSED) != 0;
    return MVE_OK;
}

/* Sets the video size in blocks and clears both frames. */
static int init_video(MveStream *m, const uint8_t *body, int size)
{
    if (size < VIDEO_INIT_SIZE) return MVE_ERROR;
    int width = le16(body) * MVE_BLOCK_SIZE, height = le16(body + VIDEO_INIT_HEIGHT) * MVE_BLOCK_SIZE;
    if (!width || !height || width > MVE_WIDTH_MAX || height > MVE_HEIGHT_MAX) return MVE_ERROR;
    m->width = width, m->height = height;
    memset(m->buffers, 0, sizeof m->buffers);
    return MVE_OK;
}

/* An audio frame or a silent frame: MVE_AUDIO for track 0, else nothing. */
static int audio_frame(MveStream *m, int type, const uint8_t *body, int size)
{
    if (size < AUDIO_FRAME_HEADER_SIZE) return MVE_ERROR;
    if (!(le16(body + AUDIO_FRAME_MASK) & AUDIO_TRACK)) return MVE_OK;
    int length = le16(body + AUDIO_FRAME_LENGTH);
    if (type == OP_AUDIO_FRAME && size - AUDIO_FRAME_HEADER_SIZE < length) return MVE_ERROR;
    m->audio = type == OP_AUDIO_FRAME ? body + AUDIO_FRAME_HEADER_SIZE : NULL;
    m->audio_size = length;
    return MVE_AUDIO;
}

/* Copies colors of the palette opcode into the palette of the movie. */
static int set_palette(MveStream *m, const uint8_t *body, int size)
{
    if (size < PALETTE_HEADER_SIZE) return MVE_ERROR;
    int first = le16(body), count = le16(body + PALETTE_COUNT);
    if (first + count > MVE_PALETTE_COLORS || size - PALETTE_HEADER_SIZE < count * MVE_RGB) return MVE_ERROR;
    for (int c = 0; c < count; c++)
        for (int ch = 0; ch < MVE_RGB; ch++)
            m->palette[first + c][ch] = body[PALETTE_HEADER_SIZE + c * MVE_RGB + ch] & VGA_VALUE_MASK;
    return MVE_OK;
}

/* Runs one opcode; MVE_OK lets mve_step go on. */
static int run_opcode(MveStream *m, int type, int version, const uint8_t *body, int size)
{
    switch (type) {
    case OP_END_OF_STREAM: return MVE_END;
    case OP_CREATE_TIMER:
        if (size < TIMER_SIZE) return MVE_ERROR;
        m->frame_us = le32(body) * le16(body + TIMER_SUBDIVISIONS);
        return MVE_OK;
    case OP_INIT_AUDIO: return init_audio(m, version, body, size);
    case OP_START_AUDIO:
        m->audio_started = true;
        return MVE_OK;
    case OP_INIT_VIDEO: return init_video(m, body, size);
    case OP_SHOW_FRAME:
        m->frames++;
        return MVE_SHOW;
    case OP_AUDIO_FRAME:
    case OP_AUDIO_SILENCE: return audio_frame(m, type, body, size);
    case OP_VIDEO_MODE:
        if (size < MODE_SIZE) return MVE_ERROR;
        m->mode_width = le16(body), m->mode_height = le16(body + MODE_HEIGHT);
        return MVE_OK;
    case OP_PALETTE: return set_palette(m, body, size);
    case OP_DECODING_MAP:
        m->map = body, m->map_size = size;
        return MVE_OK;
    default: return MVE_OK;  /* end of chunk, block statistics (0x13), and opcodes that the movies of DM2 do not use */
    }
}

int mve_step(MveStream *m)
{
    for (;;) {
        if (m->pos == m->chunk_end && !next_chunk(m)) return MVE_ERROR;
        if (m->chunk_end - m->pos < OPCODE_HEADER_SIZE) return MVE_ERROR;
        const uint8_t *header = m->data + m->pos;
        int size = le16(header);
        if (m->chunk_end - m->pos - OPCODE_HEADER_SIZE < (size_t)size) return MVE_ERROR;
        m->pos += OPCODE_HEADER_SIZE + size;
        int result = run_opcode(m, header[OPCODE_TYPE], header[OPCODE_VERSION], header + OPCODE_HEADER_SIZE, size);
        if (result != MVE_OK) return result;
    }
}
```

`OP_VIDEO_DATA` stays unused in this task (an unused enum value gives no warning); Task 3 adds its case.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C port && uv run pytest -q -n auto tests/test_mve.py tests/test_standard.py`
Expected: PASS. The build has 0 warnings.

- [ ] **Step 5: Commit**

```bash
git add port/src/data/mve.h port/src/data/mve.c tests/test_mve.py
git commit -m "feat: parse the MVE stream of the SVGA movies"
```

---

### Task 3: The 8-bit Interplay video codec

**Files:**
- Create: `port/src/data/mve_video.c`
- Modify: `port/src/data/mve.h` (the declaration of `mve_decode_video`), `port/src/data/mve.c` (`run_opcode`: the case `OP_VIDEO_DATA`)
- Test: `tests/test_mve.py` (two tests)

**Interfaces:**
- Consumes: `MveStream`, `MVE_BLOCK_SIZE` of Task 2; `le16`, `BYTE_BITS`, `NIBBLE_BITS`, `NIBBLE_MASK` of `data/le.h`.
- Produces: `bool mve_decode_video(MveStream *m, const uint8_t *video, int size)`; after it, `mve_pixels` gives decoded pictures.

The codec (the public MVE descriptions; the developer checks the picture manually). The video data opcode has a header of 14 bytes; its word at offset 12 holds the flags, bit 0 swaps the frames before the decode. The blocks of 8 × 8 pixels follow in row order; the decoding map gives each block 4 bits (block i: byte i / 2, low nibble for an even i). Colors and bit fields come from the block data in order. A bit field is little-endian; its lowest bits give the first pixel or cell, in rows from the top left. "Previous" is the other buffer (the last shown frame); "current" is the buffer that the decode draws (it holds the frame before the previous one).

| Encoding | Bytes | Pixels |
|---|---|---|
| 0x0 | 0 | The block at the same place in the previous frame. |
| 0x1 | 0 | Unchanged. |
| 0x2 | 1 (B) | The block of the current frame at (x + dx, y + dy): B < 56: dx = 8 + B % 7, dy = B / 7; else dx = -14 + (B - 56) % 29, dy = 8 + (B - 56) / 29. |
| 0x3 | 1 (B) | As 0x2 with -dx, -dy. |
| 0x4 | 1 (B) | The block of the previous frame at dx = -8 + (B & 0xf), dy = -8 + (B >> 4). |
| 0x5 | 2 | The block of the previous frame at dx, dy: two signed bytes. |
| 0x6 | 0 | Not used: `bad_blocks` counts it. |
| 0x7 | P0 P1, then 8 or 2 | P0 <= P1: 64 bits, one per pixel (1: P1). Else: 16 bits, one per 2 × 2 cell. |
| 0x8 | see right | P0 <= P1: four quadrants in the order top left, bottom left, top right, bottom right; each has 2 colors and 16 bits (the first quadrant uses P0 P1). Else: P0 P1, 32 bits, P2 P3, 32 bits: P2 <= P3: the left half (4 × 8) and the right half; else the top half (8 × 4) and the bottom half. |
| 0x9 | P0-P3, then 16, 4, or 8 | 2 bits per pixel or cell. P0 <= P1 and P2 <= P3: 128 bits per pixel (the top half from the first 8 bytes). P0 <= P1, P2 > P3: 32 bits per 2 × 2 cell. P0 > P1, P2 <= P3: 64 bits per 2 × 1 cell (4 cells per row). P0 > P1, P2 > P3: 64 bits per 1 × 2 cell (8 cells per row, 4 rows). |
| 0xA | see right | P0 <= P1: four quadrants (order as 0x8), each 4 colors and 32 bits (the first uses P0-P3). Else: P0-P3, 64 bits, P4-P7, 64 bits: P4 <= P5: the left and the right half; else the top and the bottom half. |
| 0xB | 64 | One byte per pixel. |
| 0xC | 16 | One byte per 2 × 2 cell. |
| 0xD | 4 | One color per quadrant: top left, top right, bottom left, bottom right. |
| 0xE | 1 | One color. |
| 0xF | 2 | P0 P1 as a checkerboard: P0 where x + y is even. |

A block whose source block (encodings 0, 2, 3, 4, 5) is not fully inside the frame stays unchanged, and `bad_blocks` counts it.

- [ ] **Step 1: Write the failing tests**

Add `import random` to the imports of `tests/test_mve.py`, and add:

```python
DISTINCT_COLORS_MIN = 1  # a frame that the decode does not draw stays black: 1 color


@pytest.mark.parametrize("name, frames, audio, frame_us", MOVIES)
def test_all_pictures_decode_inside_the_frame(lib, game_dir, name, frames, audio, frame_us):
    movie = Movie(lib, (game_dir / name).read_bytes())
    colors = []
    counts = movie.run(lambda mv: colors.append(len(set(mv.pixels()))) if mv.m.frames == frames // 2 else None)
    assert counts[MVE_ERROR] == 0 and movie.m.bad_blocks == 0
    assert colors[0] > DISTINCT_COLORS_MIN


def test_damaged_video_data_ends_without_a_crash(lib, game_dir):
    data = (game_dir / "FTL").read_bytes()
    start = data.rfind(b"Interplay MVE File\x1a\x00")
    rng = random.Random(0)
    for _ in range(20):
        damaged = bytearray(data)
        for _ in range(200):
            damaged[rng.randrange(start + 26, len(data))] = rng.randrange(256)
        movie = Movie(lib, bytes(damaged))
        counts = movie.run()
        assert counts[MVE_END] + counts[MVE_ERROR] == 1
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make -C port && uv run pytest -q tests/test_mve.py -k "inside or damaged"`
Expected: FAIL: the pictures stay black (`colors[0] == 1`).

- [ ] **Step 3: Write the codec**

`port/src/data/mve_video.c`:

```c
#include <string.h>
#include "le.h"
#include "mve.h"

#define VIDEO_HEADER_SIZE 14            /* frame numbers, position, size, and flags before the blocks */
#define VIDEO_FLAGS 12
#define VIDEO_FLAG_SWAP 0x0001          /* the shown frame becomes the previous frame before the decode */
#define ENCODINGS_PER_BYTE 2            /* the decoding map holds 4 bits per block */
#define HALF_BLOCK (MVE_BLOCK_SIZE / 2)
#define BLOCK_PIXELS (MVE_BLOCK_SIZE * MVE_BLOCK_SIZE)
#define CELLS_PER_ROW 4                 /* 2 × 2 cells in a block row */
#define CELLS_PER_BLOCK (CELLS_PER_ROW * CELLS_PER_ROW)
#define QUADRANT_COUNT 4
#define TWO_COLORS 2
#define FOUR_COLORS 4
#define FOUR_COLOR_BITS 2               /* bits per pixel or cell for one of four colors */
#define NEAR_MOTION_LIMIT 56            /* encodings 2 and 3: a smaller byte reaches the next 7 columns */
#define NEAR_MOTION_COLUMNS 7
#define FAR_MOTION_COLUMNS 29
#define FAR_MOTION_LEFT 14
#define PREVIOUS_MOTION_CENTER 8        /* encoding 4: a nibble 0-15 gives -8 to 7 */

enum {
    ENC_PREVIOUS, ENC_UNCHANGED, ENC_NEAR_AHEAD, ENC_NEAR_BEHIND, ENC_PREVIOUS_NEAR, ENC_PREVIOUS_FAR, ENC_UNUSED,
    ENC_TWO_COLORS, ENC_TWO_COLOR_PARTS, ENC_FOUR_COLORS, ENC_FOUR_COLOR_PARTS, ENC_RAW, ENC_CELLS, ENC_QUADRANTS,
    ENC_SOLID, ENC_DITHER,
};

/* The quadrants of encodings 8 and 10 in column order, and of encoding 13 in row order. */
static const int column_quadrant_x[QUADRANT_COUNT] = { 0, 0, HALF_BLOCK, HALF_BLOCK };
static const int column_quadrant_y[QUADRANT_COUNT] = { 0, HALF_BLOCK, 0, HALF_BLOCK };
static const int row_quadrant_x[QUADRANT_COUNT] = { 0, HALF_BLOCK, 0, HALF_BLOCK };
static const int row_quadrant_y[QUADRANT_COUNT] = { 0, 0, HALF_BLOCK, HALF_BLOCK };

/* The block data of one video opcode; a read past its end gives 0 and marks the data short. */
typedef struct {
    const uint8_t *p, *end;
    bool short_data;
} Reader;

/* The block that the decode draws. */
typedef struct {
    MveStream *m;
    Reader r;
    uint8_t *dst;   /* its top left pixel in the current frame */
    int x, y;       /* its position in pixels */
} Block;

/* Cells of cell_w × cell_h pixels in cols × rows from (x, y) of a block. */
typedef struct {
    int x, y, cols, rows, cell_w, cell_h;
} Grid;

static uint8_t get8(Reader *r)
{
    if (r->p >= r->end) {
        r->short_data = true;
        return 0;
    }
    return *r->p++;
}

/* Reads count bytes as one little-endian number. */
static uint64_t get_bits(Reader *r, int count)
{
    uint64_t bits = 0;
    for (int i = 0; i < count; i++) bits |= (uint64_t)get8(r) << (i * BYTE_BITS);
    return bits;
}

static void get_colors(Reader *r, uint8_t *colors, int count)
{
    for (int i = 0; i < count; i++) colors[i] = get8(r);
}

static void fill(Block *b, int x, int y, int w, int h, uint8_t color)
{
    for (int row = y; row < y + h; row++) memset(b->dst + row * b->m->width + x, color, w);
}

/* Gives each cell of the grid the color that the next bits_per_cell bits select. */
static void pattern(Block *b, Grid g, const uint8_t *colors, uint64_t bits, int bits_per_cell)
{
    uint64_t mask = (1u << bits_per_cell) - 1;
    for (int row = 0; row < g.rows; row++)
        for (int col = 0; col < g.cols; col++, bits >>= bits_per_cell)
            fill(b, g.x + col * g.cell_w, g.y + row * g.cell_h, g.cell_w, g.cell_h, colors[bits & mask]);
}

/* Draws the left and right halves (vertical) or the top and bottom halves of the block, each with its colors. */
static void halves(Block *b, bool vertical, const uint8_t *first, uint64_t first_bits, const uint8_t *second,
                   uint64_t second_bits, int bits_per_cell)
{
    Grid g = vertical ? (Grid){ 0, 0, HALF_BLOCK, MVE_BLOCK_SIZE, 1, 1 } : (Grid){ 0, 0, MVE_BLOCK_SIZE, HALF_BLOCK, 1, 1 };
    pattern(b, g, first, first_bits, bits_per_cell);
    if (vertical) g.x = HALF_BLOCK;
    else g.y = HALF_BLOCK;
    pattern(b, g, second, second_bits, bits_per_cell);
}

/* Copies the block at (dx, dy) from its place in frame; a source outside the frame leaves the block unchanged. */
static void copy_from(Block *b, const uint8_t *frame, int dx, int dy)
{
    MveStream *m = b->m;
    int sx = b->x + dx, sy = b->y + dy;
    if (sx < 0 || sy < 0 || sx + MVE_BLOCK_SIZE > m->width || sy + MVE_BLOCK_SIZE > m->height) {
        m->bad_blocks++;
        return;
    }
    for (int row = 0; row < MVE_BLOCK_SIZE; row++)
        memmove(b->dst + row * m->width, frame + (sy + row) * m->width + sx, MVE_BLOCK_SIZE);
}

/* The motion of encodings 2 and 3: the next 7 columns of the same rows, or 29 columns of the next rows. */
static void near_motion(uint8_t v, int *dx, int *dy)
{
    if (v < NEAR_MOTION_LIMIT) {
        *dx = MVE_BLOCK_SIZE + v % NEAR_MOTION_COLUMNS;
        *dy = v / NEAR_MOTION_COLUMNS;
    } else {
        *dx = -FAR_MOTION_LEFT + (v - NEAR_MOTION_LIMIT) % FAR_MOTION_COLUMNS;
        *dy = MVE_BLOCK_SIZE + (v - NEAR_MOTION_LIMIT) / FAR_MOTION_COLUMNS;
    }
}

/* Encoding 7: two colors, one bit per pixel, or per 2 × 2 cell when the first color is the higher one. */
static void two_colors(Block *b)
{
    uint8_t p[TWO_COLORS];
    get_colors(&b->r, p, TWO_COLORS);
    if (p[0] <= p[1]) pattern(b, (Grid){ 0, 0, MVE_BLOCK_SIZE, MVE_BLOCK_SIZE, 1, 1 }, p, get_bits(&b->r, sizeof(uint64_t)), 1);
    else pattern(b, (Grid){ 0, 0, CELLS_PER_ROW, CELLS_PER_ROW, 2, 2 }, p, get_bits(&b->r, sizeof(uint16_t)), 1);
}

/* Encoding 8: two colors per quadrant, or per half of the block. */
static void two_color_parts(Block *b)
{
    uint8_t p[TWO_COLORS * 2];
    get_colors(&b->r, p, TWO_COLORS);
    if (p[0] <= p[1]) {
        for (int q = 0; q < QUADRANT_COUNT; q++) {
            if (q) get_colors(&b->r, p, TWO_COLORS);
            Grid g = { column_quadrant_x[q], column_quadrant_y[q], HALF_BLOCK, HALF_BLOCK, 1, 1 };
            pattern(b, g, p, get_bits(&b->r, sizeof(uint16_t)), 1);
        }
        return;
    }
    uint64_t first = get_bits(&b->r, sizeof(uint32_t));
    get_colors(&b->r, p + TWO_COLORS, TWO_COLORS);
    halves(b, p[TWO_COLORS] <= p[TWO_COLORS + 1], p, first, p + TWO_COLORS, get_bits(&b->r, sizeof(uint32_t)), 1);
}

/* Encoding 9: four colors per pixel, or per cell of 2 × 2, 2 × 1, or 1 × 2 pixels. */
static void four_colors(Block *b)
{
    uint8_t p[FOUR_COLORS];
    get_colors(&b->r, p, FOUR_COLORS);
    bool second_low = p[TWO_COLORS] <= p[TWO_COLORS + 1];  /* the order of P2 and P3 */
    if (p[0] <= p[1] && second_low) {
        uint64_t top = get_bits(&b->r, sizeof(uint64_t));
        halves(b, false, p, top, p, get_bits(&b->r, sizeof(uint64_t)), FOUR_COLOR_BITS);
    } else if (p[0] <= p[1]) {
        pattern(b, (Grid){ 0, 0, CELLS_PER_ROW, CELLS_PER_ROW, 2, 2 }, p, get_bits(&b->r, sizeof(uint32_t)), FOUR_COLOR_BITS);
    } else if (second_low) {
        pattern(b, (Grid){ 0, 0, CELLS_PER_ROW, MVE_BLOCK_SIZE, 2, 1 }, p, get_bits(&b->r, sizeof(uint64_t)), FOUR_COLOR_BITS);
    } else {
        pattern(b, (Grid){ 0, 0, MVE_BLOCK_SIZE, CELLS_PER_ROW, 1, 2 }, p, get_bits(&b->r, sizeof(uint64_t)), FOUR_COLOR_BITS);
    }
}

/* Encoding 10: four colors per quadrant, or per half of the block. */
static void four_color_parts(Block *b)
{
    uint8_t p[FOUR_COLORS * 2];
    get_colors(&b->r, p, FOUR_COLORS);
    if (p[0] <= p[1]) {
        for (int q = 0; q < QUADRANT_COUNT; q++) {
            if (q) get_colors(&b->r, p, FOUR_COLORS);
            Grid g = { column_quadrant_x[q], column_quadrant_y[q], HALF_BLOCK, HALF_BLOCK, 1, 1 };
            pattern(b, g, p, get_bits(&b->r, sizeof(uint32_t)), FOUR_COLOR_BITS);
        }
        return;
    }
    uint64_t first = get_bits(&b->r, sizeof(uint64_t));
    get_colors(&b->r, p + FOUR_COLORS, FOUR_COLORS);
    halves(b, p[FOUR_COLORS] <= p[FOUR_COLORS + 1], p, first, p + FOUR_COLORS, get_bits(&b->r, sizeof(uint64_t)),
           FOUR_COLOR_BITS);
}

/* Encodings 11 to 15: raw pixels, 2 × 2 cells, quadrants, one color, and a checkerboard. */
static void plain_colors(Block *b, int encoding)
{
    uint8_t p[QUADRANT_COUNT];
    switch (encoding) {
    case ENC_RAW:
        for (int i = 0; i < BLOCK_PIXELS; i++) fill(b, i % MVE_BLOCK_SIZE, i / MVE_BLOCK_SIZE, 1, 1, get8(&b->r));
        break;
    case ENC_CELLS:
        for (int i = 0; i < CELLS_PER_BLOCK; i++) fill(b, i % CELLS_PER_ROW * 2, i / CELLS_PER_ROW * 2, 2, 2, get8(&b->r));
        break;
    case ENC_QUADRANTS:
        for (int q = 0; q < QUADRANT_COUNT; q++) fill(b, row_quadrant_x[q], row_quadrant_y[q], HALF_BLOCK, HALF_BLOCK, get8(&b->r));
        break;
    case ENC_SOLID: fill(b, 0, 0, MVE_BLOCK_SIZE, MVE_BLOCK_SIZE, get8(&b->r)); break;
    default:  /* ENC_DITHER */
        get_colors(&b->r, p, TWO_COLORS);
        for (int i = 0; i < BLOCK_PIXELS; i++) {
            int x = i % MVE_BLOCK_SIZE, y = i / MVE_BLOCK_SIZE;
            fill(b, x, y, 1, 1, p[(x + y) & 1]);
        }
    }
}

/* Draws one block by its encoding. */
static void decode_block(Block *b, int encoding)
{
    MveStream *m = b->m;
    const uint8_t *previous = m->buffers[m->current ^ 1], *current = m->buffers[m->current];
    int dx, dy;
    uint8_t v;
    switch (encoding) {
    case ENC_PREVIOUS: copy_from(b, previous, 0, 0); break;
    case ENC_UNCHANGED: break;
    case ENC_NEAR_AHEAD:
    case ENC_NEAR_BEHIND:
        near_motion(get8(&b->r), &dx, &dy);
        if (encoding == ENC_NEAR_BEHIND) dx = -dx, dy = -dy;
        copy_from(b, current, dx, dy);
        break;
    case ENC_PREVIOUS_NEAR:
        v = get8(&b->r);
        copy_from(b, previous, (v & NIBBLE_MASK) - PREVIOUS_MOTION_CENTER, (v >> NIBBLE_BITS) - PREVIOUS_MOTION_CENTER);
        break;
    case ENC_PREVIOUS_FAR:
        dx = (int8_t)get8(&b->r);
        dy = (int8_t)get8(&b->r);
        copy_from(b, previous, dx, dy);
        break;
    case ENC_UNUSED: m->bad_blocks++; break;
    case ENC_TWO_COLORS: two_colors(b); break;
    case ENC_TWO_COLOR_PARTS: two_color_parts(b); break;
    case ENC_FOUR_COLORS: four_colors(b); break;
    case ENC_FOUR_COLOR_PARTS: four_color_parts(b); break;
    default: plain_colors(b, encoding);
    }
}

bool mve_decode_video(MveStream *m, const uint8_t *video, int size)
{
    int columns = m->width / MVE_BLOCK_SIZE, rows = m->height / MVE_BLOCK_SIZE;
    if (size < VIDEO_HEADER_SIZE || !columns || m->map_size * ENCODINGS_PER_BYTE < columns * rows) return false;
    if (le16(video + VIDEO_FLAGS) & VIDEO_FLAG_SWAP) m->current ^= 1;
    Block b = { .m = m, .r = { video + VIDEO_HEADER_SIZE, video + size, false } };
    for (int i = 0; i < columns * rows && !b.r.short_data; i++) {
        b.x = i % columns * MVE_BLOCK_SIZE, b.y = i / columns * MVE_BLOCK_SIZE;
        b.dst = m->buffers[m->current] + b.y * m->width + b.x;
        decode_block(&b, m->map[i / ENCODINGS_PER_BYTE] >> (i % ENCODINGS_PER_BYTE * NIBBLE_BITS) & NIBBLE_MASK);
    }
    return !b.r.short_data;
}
```

In `port/src/data/mve.h`, after the declaration of `mve_pixels`, add:

```c
/* Decodes the blocks of one video data opcode into the current frame; false when the data is too short. */
bool mve_decode_video(MveStream *m, const uint8_t *video, int size);
```

The `lib` fixture sets the return type of each `bool` function of the headers, so the declaration and the definition come in the same commit.

In `port/src/data/mve.c`, `run_opcode`, add before `default`:

```c
    case OP_VIDEO_DATA: return mve_decode_video(m, body, size) ? MVE_OK : MVE_ERROR;
```

Notes for the author: if a line is longer than the 120 columns of the file style, wrap it as the other files do. If `tests/test_standard.py` reports a function of more than 60 lines, split it; keep the encodings as the table above says.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C port && uv run pytest -q -n auto tests/test_mve.py tests/test_standard.py`
Expected: PASS; 0 warnings. If `test_all_pictures_decode_inside_the_frame` reports `bad_blocks > 0`, check the encoding with the most bad blocks against the table first (a wrong motion rule gives sources outside the frame). Save a picture with Python as a PNG file in the scratchpad (`movie.pixels()` and the palette; `zlib` and `struct` of the standard library) and look at it with the Read tool; report what you saw.

- [ ] **Step 5: Commit**

```bash
git add port/src/data/mve_video.c port/src/data/mve.c tests/test_mve.py
git commit -m "feat: decode the 8-bit video of the MVE movies"
```

---

### Task 4: The launcher script in ui/movie.c

**Files:**
- Create: `port/src/ui/movie.h`, `port/src/ui/movie.c`
- Test: `tests/test_movie.py`

**Interfaces:**
- Consumes: `mve_open`, `mve_step`, `mve_pixels`, `MveStream` (Tasks 2, 3); `pcx_decode` (Task 1); `SAVE_PATH_SIZE` (`engine/game.h`); `fb`, `palette`, `SCREEN_W`, `SCREEN_H`, `PALETTE_COLORS`, `RGB_CHANNELS` (`ui/video.h`); `SOUND_OUTPUT_RATE` (`ui/sound.h`); `EXIT_COMPLETED`, `EXIT_PARTY_DEAD` (`ui/session.h`); `game_mode`, `GAME_MODE_VANILLA` (`engine/game.h`).
- Produces (`ui/movie.h`): `MOVIE_SCRIPT_NONE` 0, `MOVIE_SCRIPT_START` 1, `MOVIE_SCRIPT_COMPLETED` 2, `MOVIE_SCRIPT_PARTY_DEAD` 3; `MOVIE_STEP_NONE` -1, `MOVIE_STEP_SPLASH` 0, `MOVIE_STEP_FTL` 1, `MOVIE_STEP_INTRO` 2, `MOVIE_STEP_END` 3, `MOVIE_STEP_CREDITS` 4; `MoviePicture`; `int movie_script_after(int exit_code)`; `bool movie_start(int script, const char *data_dir, uint64_t now_ns)`; `void movie_update(uint64_t now_ns)`; `void movie_click(void)`; `void movie_stop(void)`; `bool movie_is_playing(void)`; `int movie_current_step(void)`; `bool movie_shows_splash(void)`; `void movie_picture(MoviePicture *p)`; `void movie_render_audio(int16_t *stereo, int frames)`.

- [ ] **Step 1: Write the failing test**

`tests/test_movie.py`:

```python
"""The movie script of the DOS launcher (port/src/ui/movie.c): the order of the steps, the skip rules of the modes,
the failed steps, and the game screen after the movies."""

import ctypes

import pytest

SCRIPT_NONE, SCRIPT_START, SCRIPT_COMPLETED, SCRIPT_PARTY_DEAD = range(4)
STEP_NONE, SPLASH, FTL, INTRO, END, CREDITS = -1, 0, 1, 2, 3, 4
VANILLA, ADVANCED = 0, 1
EXIT_QUIT, EXIT_COMPLETED, EXIT_PARTY_DEAD, EXIT_RESTART, EXIT_GAME_LOST = range(5)
SECOND = 10**9
SCREEN_W, SCREEN_H, COLORS, RGB = 320, 200, 256, 3
FILES = {"INTRPLAY.PCX": "INTRPLAY.PCX", "FTL": "FTL", "INTRO": "INTRO", "END": "END", "CREDITS": "CREDITS"}


class MoviePicture(ctypes.Structure):
    """movie.h MoviePicture."""
    _fields_ = [("pixels", ctypes.c_void_p), ("width", ctypes.c_int), ("height", ctypes.c_int),
                ("shown_width", ctypes.c_int), ("screen_width", ctypes.c_int), ("screen_height", ctypes.c_int),
                ("palette", ctypes.c_void_p)]


@pytest.fixture
def movies(lib, game_dir, tmp_path):
    """Starts scripts on a data folder of links to game/: start(script, mode, **files) with files that replace a
    link (a game/ name, bytes, or None for a missing file); yields start."""
    lib.movie_start.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_uint64]
    lib.movie_update.argtypes = [ctypes.c_uint64]
    mode = ctypes.c_int.in_dll(lib, "game_mode")

    def start(script, game_mode, **files):
        folder = tmp_path / f"data{len(list(tmp_path.iterdir()))}"
        folder.mkdir()
        for name, source in {**FILES, **files}.items():
            if isinstance(source, bytes):
                (folder / name).write_bytes(source)
            elif source:
                (folder / name).symlink_to(game_dir / source)
        mode.value = game_mode
        return lib.movie_start(script, str(folder).encode(), 0)

    yield start
    lib.movie_stop()
    mode.value = VANILLA


def picture(lib):
    p = MoviePicture()
    lib.movie_picture(ctypes.byref(p))
    return p


def test_script_after_each_exit_code(lib):
    expected = {EXIT_QUIT: SCRIPT_NONE, EXIT_COMPLETED: SCRIPT_COMPLETED, EXIT_PARTY_DEAD: SCRIPT_PARTY_DEAD,
                EXIT_RESTART: SCRIPT_NONE, EXIT_GAME_LOST: SCRIPT_NONE}
    assert {code: lib.movie_script_after(code) for code in expected} == expected


def test_advanced_click_stops_each_step(lib, movies):
    assert movies(SCRIPT_START, ADVANCED)
    assert lib.movie_current_step() == SPLASH and lib.movie_shows_splash()
    screens = []
    for expected in (FTL, INTRO, STEP_NONE):
        lib.movie_click()
        lib.movie_update(1)
        assert lib.movie_current_step() == expected
        if expected != STEP_NONE:
            p = picture(lib)
            screens.append((p.width, p.height, p.shown_width, p.screen_width, p.screen_height))
    assert screens == [(432, 320, 432, 640, 480), (432, 320, 576, 640, 350)]
    assert not lib.movie_is_playing()


def test_vanilla_click_keeps_splash_and_ftl(lib, movies):
    assert movies(SCRIPT_START, VANILLA)
    lib.movie_click()
    lib.movie_update(SECOND)
    assert lib.movie_current_step() == SPLASH
    lib.movie_update(3 * SECOND)
    assert lib.movie_current_step() == FTL
    lib.movie_click()
    lib.movie_update(3 * SECOND + 1)
    assert lib.movie_current_step() == FTL
    lib.movie_update(10 * SECOND)  # FTL is about 5.5 seconds; the click of FTL does not stop INTRO
    assert lib.movie_current_step() == INTRO
    lib.movie_update(11 * SECOND)
    assert lib.movie_current_step() == INTRO
    lib.movie_click()
    lib.movie_update(11 * SECOND)
    assert lib.movie_current_step() == STEP_NONE


def test_movie_plays_to_its_end_on_time(lib, movies):
    assert movies(SCRIPT_START, ADVANCED, **{"INTRPLAY.PCX": None, "INTRO": None})
    assert lib.movie_current_step() == FTL
    lib.movie_update(5 * SECOND)
    assert lib.movie_current_step() == FTL
    lib.movie_update(6 * SECOND)
    assert lib.movie_current_step() == STEP_NONE


@pytest.mark.parametrize("case, files, first", [
    ("missing", {"FTL": None}, INTRO),
    ("vga", {"FTL": "FTL.VGA"}, INTRO),
    ("junk", {"FTL": b"MZ" + bytes(5000)}, INTRO),
    ("truncated", {"FTL": "truncated"}, INTRO),
    ("no splash", {"INTRPLAY.PCX": None}, FTL),
    ("bad splash", {"INTRPLAY.PCX": b"\x0a\x05\x01\x08" + bytes(2000)}, FTL),
])
def test_failed_steps_are_skipped(lib, movies, game_dir, case, files, first):
    if case == "truncated":
        data = (game_dir / "FTL").read_bytes()
        files = {"FTL": data[:len(data) // 2]}
    assert movies(SCRIPT_START, ADVANCED, **files)
    if lib.movie_current_step() == SPLASH:
        lib.movie_update(3 * SECOND)
    if case == "truncated":
        assert lib.movie_current_step() == FTL
        lib.movie_update(20 * SECOND)  # the movie plays up to the cut, then the next step starts
    assert lib.movie_current_step() == first


def test_no_playable_step_starts_nothing(lib, movies):
    assert not movies(SCRIPT_START, ADVANCED, **{name: None for name in FILES})
    assert lib.movie_current_step() == STEP_NONE and not lib.movie_is_playing()
    assert not movies(SCRIPT_NONE, ADVANCED)


def test_end_scripts(lib, movies):
    assert movies(SCRIPT_PARTY_DEAD, ADVANCED)
    assert lib.movie_current_step() == CREDITS
    assert movies(SCRIPT_COMPLETED, ADVANCED)
    assert lib.movie_current_step() == END
    lib.movie_click()
    lib.movie_update(1)
    assert lib.movie_current_step() == CREDITS


def test_script_end_restores_the_game_screen(lib, movies):
    fb = (ctypes.c_uint8 * (SCREEN_W * SCREEN_H)).in_dll(lib, "fb")
    pal = (ctypes.c_uint8 * (COLORS * RGB)).in_dll(lib, "palette")
    for i in range(len(fb)):
        fb[i] = i % 7
    for i in range(len(pal)):
        pal[i] = i % 64
    screen, colors = bytes(fb), bytes(pal)
    assert movies(SCRIPT_START, ADVANCED)
    assert bytes(fb) != screen and bytes(pal) != colors  # the splash shows on the game screen
    for _ in range(3):
        lib.movie_click()
        lib.movie_update(1)
    assert (bytes(fb), bytes(pal)) == (screen, colors)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -C port && uv run pytest -q tests/test_movie.py`
Expected: FAIL with `AttributeError: ... movie_start`.

- [ ] **Step 3: Write the module**

`port/src/ui/movie.h`:

```c
#ifndef MOVIE_H
#define MOVIE_H
#include <stdbool.h>
#include <stdint.h>
#include "mve.h"

/* Prefixes: movie_ */

/* The movie script of the DOS launcher IBMIOP.EXE; platform/main.c drives it (docs/skull-formats.md section 15). */

enum { MOVIE_SCRIPT_NONE, MOVIE_SCRIPT_START, MOVIE_SCRIPT_COMPLETED, MOVIE_SCRIPT_PARTY_DEAD };
enum { MOVIE_STEP_NONE = -1, MOVIE_STEP_SPLASH, MOVIE_STEP_FTL, MOVIE_STEP_INTRO, MOVIE_STEP_END, MOVIE_STEP_CREDITS };

/* The picture of a movie and the screen of the original player that shows it. */
typedef struct {
    const uint8_t *pixels;                   /* width × height palette indexes */
    int width, height;
    int shown_width;                         /* the width on the screen: 4/3 of width for a wide movie */
    int screen_width, screen_height;         /* the screen of the player */
    const uint8_t (*palette)[MVE_RGB];       /* 6-bit VGA values */
} MoviePicture;

/* The script that the launcher runs after the game ends with exit_code. */
int movie_script_after(int exit_code);
/* Starts script with the files of data_dir at now_ns; false when no step of it can play. */
bool movie_start(int script, const char *data_dir, uint64_t now_ns);
/* Shows the next picture when its time came, and starts the next step after the end or a skip of a step. */
void movie_update(uint64_t now_ns);
/* A mouse click: the next movie_update stops the step when the mode allows it. */
void movie_click(void);
/* Stops the script: the game screen and its palette come back. */
void movie_stop(void);
bool movie_is_playing(void);
/* The current step; MOVIE_STEP_NONE when no script plays. */
int movie_current_step(void);
/* True while the splash picture shows on the game screen; during a movie, movie_picture gives the picture. */
bool movie_shows_splash(void);
void movie_picture(MoviePicture *p);
/* The audio mixer during a movie: the movie sound at full volume, 16-bit stereo at SOUND_OUTPUT_RATE. */
void movie_render_audio(int16_t *stereo, int frames);

#endif
```

`port/src/ui/movie.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "movie.h"
#include "pcx.h"
#include "session.h"
#include "sound.h"
#include "video.h"

#define SPLASH_NS 3000000000ull            /* splash intrplay.pcx 3: three seconds */
#define NS_PER_US 1000
#define WIDE_SCREEN_WIDTH 640              /* the screen of the argument 640x350 */
#define WIDE_SCREEN_HEIGHT 350
#define WIDE_NUMERATOR 4                   /* wide: the player shows the video 4/3 wider */
#define WIDE_DENOMINATOR 3
#define SVGA_MODE_WIDTH 640                /* the SVGA movies set 640 × 480; the VGA movies 320 × 200 */
#define SVGA_MODE_HEIGHT 480
#define PCX_TO_VGA_SHIFT 2                 /* 8-bit palette values to the 6 bits of the VGA */
#define AUDIO_RING_FRAMES 131072           /* stereo frames at SOUND_OUTPUT_RATE: about 3 seconds */
#define STEREO_CHANNELS 2
#define U8_SILENCE 0x80                    /* the movie samples are unsigned 8-bit */
#define U8_TO_S16 256
#define SCRIPT_STEPS_MAX 4                 /* the steps of the longest script and its end mark */

/* One step of the SVGA script of IBMIOP.EXE: its file and the arguments that the launcher gives its player. */
typedef struct {
    const char *file;                      /* in the data folder */
    bool noabort;                          /* the vanilla mode does not stop it on a click */
    int screen_width, screen_height;       /* the screen of the player; 0 keeps the video mode of the movie */
    bool wide;                             /* the player shows the video 4/3 wider */
} Step;

static const Step steps[] = {
    [MOVIE_STEP_SPLASH] = { "INTRPLAY.PCX", true, 0, 0, false },
    [MOVIE_STEP_FTL] = { "FTL", true, 0, 0, false },
    [MOVIE_STEP_INTRO] = { "INTRO", false, WIDE_SCREEN_WIDTH, WIDE_SCREEN_HEIGHT, true },
    [MOVIE_STEP_END] = { "END", false, WIDE_SCREEN_WIDTH, WIDE_SCREEN_HEIGHT, true },
    [MOVIE_STEP_CREDITS] = { "CREDITS", false, WIDE_SCREEN_WIDTH, WIDE_SCREEN_HEIGHT, true },
};

/* The steps of each script in order, up to MOVIE_STEP_NONE (the next steps of the script table of IBMIOP.EXE). */
static const int8_t scripts[][SCRIPT_STEPS_MAX] = {
    [MOVIE_SCRIPT_NONE] = { MOVIE_STEP_NONE },
    [MOVIE_SCRIPT_START] = { MOVIE_STEP_SPLASH, MOVIE_STEP_FTL, MOVIE_STEP_INTRO, MOVIE_STEP_NONE },
    [MOVIE_SCRIPT_COMPLETED] = { MOVIE_STEP_END, MOVIE_STEP_CREDITS, MOVIE_STEP_NONE },
    [MOVIE_SCRIPT_PARTY_DEAD] = { MOVIE_STEP_CREDITS, MOVIE_STEP_NONE },
};

static struct {
    bool playing;
    int script, index, step;               /* the script, the index of its current step, and that step */
    bool skip;                             /* a click waits for movie_update */
    uint64_t start_ns;                     /* the start of the step */
    char data_dir[SAVE_PATH_SIZE];
    uint8_t *file;                         /* the movie file of the step */
    MveStream mve;
    int16_t ring[AUDIO_RING_FRAMES][STEREO_CHANNELS];
    int ring_start, ring_count;
    uint8_t saved_fb[SCREEN_H][SCREEN_W];
    uint8_t saved_palette[PALETTE_COLORS][RGB_CHANNELS];
} mv = { .step = MOVIE_STEP_NONE };

int movie_script_after(int exit_code)
{
    if (exit_code == EXIT_COMPLETED) return MOVIE_SCRIPT_COMPLETED;
    if (exit_code == EXIT_PARTY_DEAD) return MOVIE_SCRIPT_PARTY_DEAD;
    return MOVIE_SCRIPT_NONE;
}

/* Reads a file of the data folder into memory; NULL when it is missing, empty, or does not read. */
static uint8_t *load_file(const char *name, size_t *size)
{
    char path[SAVE_PATH_SIZE * 2];
    snprintf(path, sizeof path, "%s/%s", mv.data_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *data = NULL;
    long length = fseek(f, 0, SEEK_END) ? -1 : ftell(f);
    if (length > 0 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)length))
        && fread(data, 1, (size_t)length, f) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(f);
    *size = length > 0 ? (size_t)length : 0;
    return data;
}

/* Shows the splash picture on the game screen; false when it is missing or not a 320 × 200 picture. */
static bool start_splash(void)
{
    size_t size;
    uint8_t *file = load_file(steps[MOVIE_STEP_SPLASH].file, &size);
    uint8_t colors[PCX_PALETTE_COLORS][PCX_RGB];
    int width, height;
    bool ok = file && pcx_decode(file, size, &fb[0][0], SCREEN_W, SCREEN_H, &width, &height, colors) == PCX_OK
              && width == SCREEN_W && height == SCREEN_H;
    free(file);
    if (ok)
        for (int c = 0; c < PALETTE_COLORS; c++)
            for (int ch = 0; ch < RGB_CHANNELS; ch++) palette[c][ch] = colors[c][ch] >> PCX_TO_VGA_SHIFT;
    return ok;
}

/* Adds an audio frame (NULL: silence) to the ring as 16-bit stereo; another format than unsigned 8-bit plays none. */
static void queue_audio(const uint8_t *samples, int bytes)
{
    const MveStream *m = &mv.mve;
    if (!m->audio_rate || m->audio_16bit || m->audio_compressed || SOUND_OUTPUT_RATE % m->audio_rate) return;
    int channels = m->audio_stereo ? STEREO_CHANNELS : 1, repeat = SOUND_OUTPUT_RATE / m->audio_rate;
    for (int i = 0; i + channels <= bytes; i += channels) {
        int16_t left = samples ? (int16_t)((samples[i] - U8_SILENCE) * U8_TO_S16) : 0;
        int16_t right = samples ? (int16_t)((samples[i + channels - 1] - U8_SILENCE) * U8_TO_S16) : 0;
        for (int r = 0; r < repeat && mv.ring_count < AUDIO_RING_FRAMES; r++, mv.ring_count++) {
            int16_t *out = mv.ring[(mv.ring_start + mv.ring_count) % AUDIO_RING_FRAMES];
            out[0] = left, out[1] = right;
        }
    }
}

/* Runs the movie to its next picture and queues its audio frames on the way; MVE_SHOW, MVE_END, or MVE_ERROR. */
static int next_picture(void)
{
    int result;
    while ((result = mve_step(&mv.mve)) == MVE_AUDIO) queue_audio(mv.mve.audio, mv.mve.audio_size);
    return result;
}

/* Loads the movie of step and decodes its first picture; false when it is missing, damaged, or not an SVGA movie. */
static bool start_movie(int step)
{
    size_t size;
    mv.file = load_file(steps[step].file, &size);
    if (!mv.file || mve_open(&mv.mve, mv.file, size) != MVE_OK) return false;
    if (next_picture() != MVE_SHOW || !mv.mve.frame_us) return false;
    return mv.mve.mode_width == SVGA_MODE_WIDTH && mv.mve.mode_height == SVGA_MODE_HEIGHT;
}

void movie_stop(void)
{
    if (!mv.playing) return;
    free(mv.file);
    mv.file = NULL;
    mv.playing = false;
    mv.step = MOVIE_STEP_NONE;
    memcpy(fb, mv.saved_fb, sizeof fb);
    memcpy(palette, mv.saved_palette, sizeof palette);
}

/* Ends the current step and starts the next step of the script that can play; after the last step the script stops. */
static void next_step(uint64_t now_ns)
{
    for (;;) {
        free(mv.file);
        mv.file = NULL;
        mv.ring_start = mv.ring_count = 0;
        mv.step = scripts[mv.script][++mv.index];
        mv.start_ns = now_ns;
        if (mv.step == MOVIE_STEP_NONE) {
            movie_stop();
            return;
        }
        if (mv.step == MOVIE_STEP_SPLASH ? start_splash() : start_movie(mv.step)) return;
    }
}

bool movie_start(int script, const char *data_dir, uint64_t now_ns)
{
    movie_stop();
    if (script == MOVIE_SCRIPT_NONE) return false;
    snprintf(mv.data_dir, sizeof mv.data_dir, "%s", data_dir);
    memcpy(mv.saved_fb, fb, sizeof fb);
    memcpy(mv.saved_palette, palette, sizeof palette);
    mv.script = script, mv.index = -1, mv.skip = false, mv.playing = true;
    next_step(now_ns);
    return mv.playing;
}

void movie_update(uint64_t now_ns)
{
    if (!mv.playing) return;
    bool skip = mv.skip;
    mv.skip = false;  /* a click that the mode does not allow is dropped */
    if (skip && (game_mode != GAME_MODE_VANILLA || !steps[mv.step].noabort)) {
        next_step(now_ns);
        return;
    }
    if (mv.step == MOVIE_STEP_SPLASH) {
        if (now_ns - mv.start_ns >= SPLASH_NS) next_step(now_ns);
        return;
    }
    /* picture k shows k frame times after the start of the step; a late update decodes the missed pictures */
    while (mv.playing && mv.step != MOVIE_STEP_SPLASH
           && now_ns - mv.start_ns >= (uint64_t)mv.mve.frames * mv.mve.frame_us * NS_PER_US)
        if (next_picture() != MVE_SHOW) next_step(now_ns);
}

void movie_click(void)
{
    if (mv.playing) mv.skip = true;
}

bool movie_is_playing(void)
{
    return mv.playing;
}

int movie_current_step(void)
{
    return mv.playing ? mv.step : MOVIE_STEP_NONE;
}

bool movie_shows_splash(void)
{
    return mv.playing && mv.step == MOVIE_STEP_SPLASH;
}

void movie_picture(MoviePicture *p)
{
    const Step *s = &steps[mv.step];
    p->pixels = mve_pixels(&mv.mve);
    p->width = mv.mve.width, p->height = mv.mve.height;
    p->shown_width = s->wide ? p->width * WIDE_NUMERATOR / WIDE_DENOMINATOR : p->width;
    p->screen_width = s->screen_width ? s->screen_width : mv.mve.mode_width;
    p->screen_height = s->screen_height ? s->screen_height : mv.mve.mode_height;
    p->palette = mv.mve.palette;
}

void movie_render_audio(int16_t *stereo, int frames)
{
    if (!mv.playing || mv.step == MOVIE_STEP_SPLASH || !mv.mve.audio_started) return;
    for (int i = 0; i < frames && mv.ring_count; i++, mv.ring_count--) {
        const int16_t *in = mv.ring[mv.ring_start];
        mv.ring_start = (mv.ring_start + 1) % AUDIO_RING_FRAMES;
        stereo[i * STEREO_CHANNELS] = in[0];
        stereo[i * STEREO_CHANNELS + 1] = in[1];
    }
}
```

Note for the author: `plat_audio_pump` clears its buffer before the music mixer runs, so `movie_render_audio` writes only the frames that it has.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make -C port && uv run pytest -q -n auto tests/test_movie.py tests/test_standard.py`
Expected: PASS; 0 warnings.

- [ ] **Step 5: Commit**

```bash
git add port/src/ui/movie.h port/src/ui/movie.c tests/test_movie.py
git commit -m "feat: run the movie script of the DOS launcher"
```

---

### Task 5: The movie loop, the movie screen, and the control socket

**Files:**
- Modify: `port/src/platform/platform.h`, `port/src/platform/platform.c` (`plat_present_movie`)
- Modify: `port/src/platform/main.c` (`play_movies`, the start and end scripts, the restart)
- Modify: `port/src/platform/control.c` (`send_state`, `handle_key`, `handle_button`, the `command` request)
- Modify: `docs/llm-control.md`
- Test: `tests/test_movie.py` (one test)

**Interfaces:**
- Consumes: everything that `ui/movie.h` produces (Task 4); `plat_poll`, `plat_wait_tick`, `plat_present`, `plat_audio_music`, `control_poll` (existing).
- Produces: `void plat_present_movie(const MoviePicture *p)`; the state field `"mode": "movie"`.

- [ ] **Step 1: Write the failing test**

Add `import os`, `import shutil`, `import tempfile`, `import threading`, `import time`, and `from dm2tools.control_client import request` to the imports of `tests/test_movie.py`, and add:

```python
def test_control_state_and_click_during_a_movie(lib, movies):
    folder = tempfile.mkdtemp(prefix="dm2c", dir="/tmp")  # sun_path holds about 100 bytes
    path = os.path.join(folder, "control.sock")
    lib.control_open.argtypes = [ctypes.c_char_p]
    assert lib.control_open(path.encode())
    stop = threading.Event()

    def loop():
        while not stop.is_set():
            lib.control_poll()
            time.sleep(0.0005)

    thread = threading.Thread(target=loop)
    thread.start()
    try:
        assert movies(SCRIPT_START, ADVANCED)
        state = request({"cmd": "state"}, path)
        assert state["ok"] and state["mode"] == "movie" and state["dialog"] is None
        assert request({"cmd": "key", "key": "esc"}, path) == {"ok": True}
        lib.movie_update(1)
        assert lib.movie_current_step() == SPLASH  # a key does not skip
        assert request({"cmd": "click", "x": 10, "y": 10}, path) == {"ok": True}
        lib.movie_update(1)
        assert lib.movie_current_step() == FTL
        lib.movie_stop()
        assert request({"cmd": "state"}, path)["mode"] != "movie"
    finally:
        stop.set()
        thread.join()
        lib.control_close()
        shutil.rmtree(folder)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make -C port && uv run pytest -q tests/test_movie.py -k control`
Expected: FAIL: `state["mode"]` is an input mode, not `movie`.

- [ ] **Step 3: The control socket during a movie**

In `port/src/platform/control.c`:

- Add `#include "movie.h"` to the includes (alphabetic place after `"le.h"`).
- In `send_state`, replace the first three lines and the dialog test:

```c
    int mode = input_get_mode();
    bool movie = movie_is_playing();
    const char *mode_name = movie ? "movie" : mode >= 0 && mode < MODE_COUNT ? mode_names[mode] : "unknown";
    put("{\"ok\":true,\"time\":%u,\"ticks\":%u,\"mode\":\"%s\",\"dialog\":", game.game_time, main_loop.timer_ticks,
        mode_name);
    if (movie || mode != INPUT_MODE_DIALOG) put("null");
```

- In `handle_key`, replace `input_key_down((uint16_t)code);` with:

```c
    if (!movie_is_playing()) input_key_down((uint16_t)code);  /* a key does not skip a movie */
```

- In `handle_button`, replace the three lines before `send_ok();` (`input_mouse_x = x, ...`, `if (down) ...`, `if (up) ...`; `control.c:458-460`) with:

```c
    if (movie_is_playing()) {
        if (down) movie_click();  /* a movie takes only clicks; the game input stays empty */
    } else {
        input_mouse_x = x, input_mouse_y = y;
        if (down) input_mouse_down(x, y, bit);
        if (up) input_mouse_up(x, y, bit);
    }
```

- In `handle_request`, the `command` branch: replace `input_simulate_command_click((uint16_t)id);` with `if (!movie_is_playing()) input_simulate_command_click((uint16_t)id);`.

Update the comment of `send_state` to "The state of the game: time, party, champions, input mode (`movie` while a movie plays), open dialog and panels, hand, volumes, end, game mode, automap, cheats."

- [ ] **Step 4: Run the test to verify it passes**

Run: `make -C port && uv run pytest -q -n auto tests/test_movie.py tests/test_control.py tests/test_standard.py`
Expected: PASS.

- [ ] **Step 5: The movie screen**

In `port/src/platform/platform.h`, add `#include "movie.h"` after `#include <stdint.h>`, and after the declaration of `plat_present`:

```c
/* Shows a movie picture on the screen of its player, in the largest 4:3 rectangle of the window. */
void plat_present_movie(const MoviePicture *p);
```

In `port/src/platform/platform.c`, add the includes `"movie.h"`, the constants after `AUDIO_QUEUE_FRAMES`:

```c
#define SCREEN_ASPECT_W 4             /* a VGA monitor shows every mode on a 4:3 tube */
#define SCREEN_ASPECT_H 3
```

the statics after `music_mixer`:

```c
static SDL_Texture *movie_tex;
static int movie_tex_w, movie_tex_h;
```

and after `plat_present`:

```c
/* The largest 4:3 rectangle in the middle of the window: the tube of a VGA monitor. */
static SDL_FRect movie_screen(int w, int h)
{
    float sw = (float)w, sh = (float)w * SCREEN_ASPECT_H / SCREEN_ASPECT_W;
    if (sh > h) sh = (float)h, sw = (float)h * SCREEN_ASPECT_W / SCREEN_ASPECT_H;
    return (SDL_FRect){ (w - sw) / 2, (h - sh) / 2, sw, sh };
}

void plat_present_movie(const MoviePicture *p)
{
    uint32_t pal[PALETTE_COLORS], *px;
    int pitch, w, h;
    if (!movie_tex || movie_tex_w != p->width || movie_tex_h != p->height) {
        if (movie_tex) SDL_DestroyTexture(movie_tex);
        movie_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, p->width, p->height);
        if (!movie_tex) return;
        movie_tex_w = p->width, movie_tex_h = p->height;
        SDL_SetTextureScaleMode(movie_tex, SDL_SCALEMODE_LINEAR);
    }
    for (int i = 0; i < PALETTE_COLORS; i++)
        pal[i] = video_argb_pack(video_vga_to_8bit(p->palette[i][0]), video_vga_to_8bit(p->palette[i][1]),
                                 video_vga_to_8bit(p->palette[i][2]));
    if (!SDL_LockTexture(movie_tex, NULL, (void **)&px, &pitch)) return;
    for (int y = 0; y < p->height; y++)
        for (int x = 0; x < p->width; x++) px[y * (pitch / ARGB_BYTES) + x] = pal[p->pixels[y * p->width + x]];
    SDL_UnlockTexture(movie_tex);

    SDL_GetCurrentRenderOutputSize(ren, &w, &h);
    SDL_FRect screen = movie_screen(w, h);
    float kx = screen.w / p->screen_width, ky = screen.h / p->screen_height;
    SDL_FRect dst = { screen.x + (p->screen_width - p->shown_width) / 2 * kx,
                      screen.y + (p->screen_height - p->height) / 2 * ky, p->shown_width * kx, p->height * ky };
    SDL_SetRenderDrawColor(ren, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, movie_tex, NULL, &dst);
    SDL_RenderPresent(ren);
}
```

- [ ] **Step 6: The movie loop in main.c**

In `port/src/platform/main.c`:

- Add `#include <stdlib.h>` after `#include <stdio.h>` and `#include "movie.h"` after `#include "item.h"`.
- Add after `PIT_TICKS_PER_GAME_TICK`:

```c
#define RESTART_ENV "DM2_RESTARTED"   /* set for the program that a RESTART starts: the launcher plays no start movies */
```

- Add after `modal_frame`:

```c
/* The events of a movie: a click skips by the rules of the mode, a closed window ends the program. */
static void pump_movie_events(void)
{
    PlatEvent e;
    while (plat_poll(&e)) {
        if (e.type == PLAT_QUIT) main_loop.quit = 1;
        else if (e.type == PLAT_MOUSE) movie_click();
    }
    control_poll();
}

/* Shows the splash on the game screen or the movie picture on the screen of its player. */
static void present_movie(void)
{
    MoviePicture picture;
    if (movie_shows_splash()) {
        plat_present();
        return;
    }
    movie_picture(&picture);
    plat_present_movie(&picture);
}

/* Plays a script of the launcher with the movie sound instead of the music, until its end or a quit. */
static void play_movies(int script)
{
    if (!movie_start(script, session.data_dir, SDL_GetTicksNS())) return;
    plat_audio_music(movie_render_audio);
    for (int pit = 0; movie_is_playing() && !main_loop.quit;) {
        pump_movie_events();
        if (++pit == PIT_TICKS_PER_GAME_TICK) {
            pit = 0;
            movie_update(SDL_GetTicksNS());
            if (movie_is_playing()) present_movie();
        }
        plat_wait_tick();
    }
    movie_stop();
    plat_audio_music(music_render);
}
```

- In `main`, replace:

```c
    play_frame_hook = modal_frame;
    if (!save && !session_init()) main_loop.quit = 1;
```

with:

```c
    play_frame_hook = modal_frame;
    if (!save && !getenv(RESTART_ENV)) play_movies(MOVIE_SCRIPT_START);
    if (!save && !main_loop.quit && !session_init()) main_loop.quit = 1;
```

- After the main loop (before `control_close();`), add:

```c
    main_loop.quit = 0;  /* the end movies run until their end, a closed window, or a quit request */
    play_movies(movie_script_after(session.exit_code));
```

- In the restart branch, before `execvp(argv[0], argv);`, add:

```c
        setenv(RESTART_ENV, "1", 1);
```

`main` stays shorter than 60 lines; if `tests/test_standard.py` reports it, move the end lines (`main_loop.quit = 0;` to the restart) into a function `end_program(int argc, char **argv)` that returns the exit code.

- [ ] **Step 7: The control docs**

In `docs/llm-control.md`:

- In the `state` table, the `mode` row: after the list of input modes, add "; `movie` while the splash picture or a movie plays (see "Movies")".
- In "Start the game", item 2, the `--save PATH` line: replace "Without it, the game starts with the start menu." with "Without it, the game shows the splash picture and the movies `FTL` and `INTRO`, then the start menu (see "Movies")."
- Add a section before "## Errors":

```markdown
## Movies

Without `--save`, the program shows the splash picture (3 seconds), then the movies `FTL` and `INTRO`, then the start menu. After a completed game it plays `END` and `CREDITS`; after QUIT GAME of a dead party it plays `CREDITS` (`docs/skull-formats.md` section 15). While one of them plays:

- `state` gives `"mode": "movie"` and `"dialog": null`.
- `click` and `press` skip: in the `advanced` mode always; in the `vanilla` mode only `INTRO`, `END`, and `CREDITS`. `release`, `key`, and `command` reply `{"ok":true}` and do nothing.
- `wait` counts game timer ticks. They stand still during a movie, so the reply comes after the movies.
- `screenshot` saves the game screen (320 × 200), not the movie.
- `quit` stops the movies and ends the program. A `quit` during "THE END" of a dead party ends the game with exit code 2, and `CREDITS` then plays: send `quit` again to stop it.

To reach the start menu fast: `click` until `state` gives another mode. In the `vanilla` mode, the splash and `FTL` take about 8.5 seconds.
```

- [ ] **Step 8: Build and run the tests**

Run: `rm -f build/dm2 build/libdm2.dylib && make -C port 2>&1 | grep -c warning` (prints `0`), then `uv run pytest -q -n auto tests/test_movie.py tests/test_control.py tests/test_modes.py tests/test_standard.py`.
Expected: PASS.

Smoke run (report the result, do not judge the picture): `SDL_AUDIO_DRIVER=dummy SDL_VIDEO_DRIVER=dummy build/dm2 --control /tmp/dm2c-movie.sock &` and `export DM2_CONTROL=/tmp/dm2c-movie.sock`, then `uv run dm2-play state` (expect `"mode":"movie"`), `uv run dm2-play click '{"x":10,"y":10}'` three times with `state` between them, `state` (expect `"mode":"menu"`), `uv run dm2-play quit`; the program ends with exit code 0.

- [ ] **Step 9: Commit**

```bash
git add port/src/platform/platform.h port/src/platform/platform.c port/src/platform/main.c port/src/platform/control.c docs/llm-control.md tests/test_movie.py
git commit -m "feat: play the launcher movies before the start menu and after the game"
```

---

### Task 6: The format docs and the manual checks

**Files:**
- Modify: `docs/skull-formats.md` (section 3, a new section 15)
- Modify: `docs/manual-test-checklist.md`

**Interfaces:**
- Consumes: the facts of the PRD (Current state) and the decisions of this plan.
- Produces: docs only.

- [ ] **Step 1: Section 3**

In `docs/skull-formats.md` section 3, "Impact on the port", replace "The port implements no SVGA mode. Movies are out of scope." with "The port implements no SVGA mode for the game. The port plays the SVGA movies (section 15)." Add to "Evidence": "- The script records and the arguments of the movie player: section 15."

- [ ] **Step 2: Section 15**

Append to `docs/skull-formats.md`:

```markdown
## 15. Movies

### Launcher script (`IBMIOP.EXE`)

- The SVGA script table at the decompressed offset `0x3f62` has one record of 53 bytes per step: the program name (14 bytes; `@1` is the game), the arguments (32 bytes), two flag bytes, the highest exit code that the record handles (offset `0x30`), and the next step for each exit code (offset `0x31`, 1-based; 0 ends the launcher). An exit code above the highest gives the next step of exit code 0 (code at `0x3cea`-`0x3d17`).
- Steps: 1 `splash intrplay.pcx 3`, 2 `ftl noabort`, 3 `intro 640x350 wide`, 4 the game, 5 `end 640x350 wide`, 6 `credits 640x350 wide`.
- The game: exit code 0 (quit) ends; 1 (completed) goes to `END`, then `CREDITS`; 2 (QUIT GAME of a dead party) goes to `CREDITS`; 3 (RESTART) starts the game step again; every other code ends.

### Movie player

- Each SVGA movie file is the same DOS player program with the MVE data after it. The last `Interplay MVE File` signature of the file starts the data.
- Arguments (the 32-bit code of the player): a first argument that names a screen size replaces the video mode of the movie (`640x350`: a 640 × 350 screen). `wide` shows the video 4/3 wider (432 → 576 pixels). `noabort` turns off the stop by the player. The player centers the video on its screen.
- `FTL` plays on 640 × 480 (VESA `0x101`, from its opcode `0x0a`): the 432 × 320 video fills about 67 % of the width and the height of a 4:3 monitor. `INTRO`, `END`, and `CREDITS` play on 640 × 350, 576 × 320 wide: about 90 %.

### MVE stream

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
| `0x0a` | word width, word height, word VESA mode | `(640, 480, 0x101)`; the VGA movies `(320, 200, 0x13)`. |
| `0x0c` | word first color, word count, 6-bit RGB | Palette. |
| `0x0f` | 4 bits per block | Decoding map of the next video data. |
| `0x11` v3 | 14-byte header (word flags at 12), block data | Video data; flag bit 0 swaps the frames before the decode. |
| `0x13` | 132 bytes | Block statistics: counts of the encodings. Skipped. |

- The 8-bit video codec: the table of encodings `0x0` to `0xf` is in `docs/superpowers/2026-10-10-movies-plan.md` Task 3 and in `port/src/data/mve_video.c`.
- The first audio frames come before the first picture (about 1 second); the sound starts with the first picture.

### Splash picture

- `INTRPLAY.PCX`: PCX version 5, RLE, 8 bits per pixel, 1 plane, 320 × 200, a 256-color palette of 8-bit values after the byte `0x0c` at the end of the file. The port shows it for 3 seconds on the game screen with its palette (values shifted right by 2).

### Deviations of the C port

- The VGA movies and `CREDITS.PCX` of the VGA script are not played.
- The movie screen is the largest 4:3 rectangle of the window with black borders and linear filtering: the scale is not an integer, and no scale filter applies.
- A click skips (the player and the splash program of DOS also take keys); a key does not skip. In the vanilla mode a click does not stop the splash picture: the script gives it a fixed 3 seconds.
- The movie sound plays at full volume through the sound output of the port at 44100 Hz: each sample repeats two times. Another audio format plays the movie without sound.
- A missing file, damaged data, a splash picture of another size than 320 × 200, or a movie whose mode is not 640 × 480 is a failed step: the script goes on with the next step. Damage in the middle of a movie stops it there.
- A block whose source is outside the frame, or with encoding 6, stays unchanged.
- `--save PATH` plays no start movies. A RESTART starts the program again with the environment variable `DM2_RESTARTED`: no start movies, as the launcher starts only the game step again.
- The control socket gives `"mode": "movie"` during a movie; `wait` counts the game timer ticks, which stand still (`docs/llm-control.md`).
```

- [ ] **Step 3: The manual test checklist**

In `docs/manual-test-checklist.md`, before "## Known deviations", add:

```markdown
## Movies (2026-10-10)

Start `build/dm2` without `--save` unless an item says otherwise. Use a window with sound.

- [ ] M1. Start. Expected: the Interplay picture for 3 seconds, then `FTL` with sound, then `INTRO` with sound, then the start menu with its music and its colors.
- [ ] M2. Look at the size of the pictures. Expected: `FTL` small in the middle of a 4:3 area; `INTRO` fills about 90 % of the 4:3 area; black borders on the sides of a wide window.
- [ ] M3. `--mode vanilla`: click during the splash and during `FTL`. Expected: they go on. Click during `INTRO`. Expected: the start menu shows.
- [ ] M4. `--mode advanced`: click during the splash, `FTL`, and `INTRO`. Expected: each stops at once. Press keys during a movie. Expected: nothing; the start menu does not get the keys.
- [ ] M5. Start with `--save game/DATA/SKSAVE0.DAT`. Expected: no movie; the game starts at once.
- [ ] M6. Complete the game (the end sensor). Expected: the screen goes black, `END` plays, then `CREDITS`, then the program ends with exit code 1. A click in `END` starts `CREDITS`.
- [ ] M7. Let the party die and choose QUIT GAME. Expected: `CREDITS` plays, then the program ends with exit code 2.
- [ ] M8. Close the window during `INTRO`, and during `CREDITS` of M7. Expected: the program ends at once (exit code 0 and 2); no start menu. Send `quit` over the control socket during `INTRO`. Expected: the same.
- [ ] M9. Choose RESTART in the save menu. Expected: the start menu, no movie.
- [ ] M10. Resize the window during a movie. Expected: the picture stays 4:3 with black borders.
- [ ] M11. Watch `INTRO`, `END`, and `CREDITS` to the end. Expected: clean pictures without broken blocks; the sound fits the picture.
- [ ] M12. Rename `game/FTL` and start. Expected: the splash, then `INTRO`, then the start menu. Rename it back.
- [ ] M13. With the control socket (`docs/llm-control.md`, section "Movies"): `state` during the splash gives `"mode":"movie"`; `click` skips by the rules of M3 and M4.
- [ ] M14. Optional, DOSBox (`tools/dosbox-dm2.conf`): compare the size and the position of the `INTRO` picture with the port.
```

In "Build and start", item 2, replace "Start the port with the start menu:" with "Start the port: the splash picture, the movies `FTL` and `INTRO`, then the start menu:". In A1, replace "shows the start menu title" with "shows the splash picture and the movies, then the start menu title". In J16, replace "(PRD Epic 5; the ending movie is out of scope)" with "after the movies `END` and `CREDITS` (M6)".

In "## Known deviations", replace the line "- No intro, ending movie, or credits; a completed game ends the program with exit code 1 (PRD scope; section 14)." with:

```markdown
- Only the SVGA movies; the movie screen is the largest 4:3 rectangle of the window with linear filtering; a key does not skip a movie; RESTART plays no movies (section 15).
```

- [ ] **Step 4: Check the docs**

Run: `grep -n "out of scope" docs/skull-formats.md` (no line about movies), `uv run pytest -q tests/test_standard.py`.
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add docs/skull-formats.md docs/manual-test-checklist.md
git commit -m "docs: movie formats, launcher script, and manual checks"
```
