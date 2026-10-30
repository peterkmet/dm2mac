"""The MVE decoder (src/data/mve.c and mve_video.c): the SVGA movies FTL, INTRO, END, and CREDITS of the PC
version. No reference decoder: the tests check the stream format against a parse of the files (PRD Current state)."""

import ctypes
import random
from collections import Counter

import pytest
from dm2test import constants as C


MVE_ERROR, MVE_OK, MVE_END, MVE_AUDIO, MVE_SHOW = C.MVE_ERROR, C.MVE_OK, C.MVE_END, C.MVE_AUDIO, C.MVE_SHOW
WIDTH_MAX, HEIGHT_MAX, COLORS, RGB = C.MVE_WIDTH_MAX, C.MVE_HEIGHT_MAX, C.MVE_PALETTE_COLORS, C.MVE_RGB
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
    assert (m.frames, m.frame_us, m.mode_width, m.mode_height, m.width, m.height) == (frames, frame_us, WIDTH_MAX, HEIGHT_MAX,
                                                                                       432, 320)
    assert (m.audio_rate, m.audio_stereo, m.audio_16bit, m.audio_compressed, m.audio_started) == (22050, True, False,
                                                                                                   False, True)
    assert max(m.palette) <= VGA_MAX and tuple(m.palette[:RGB]) == (0, 0, 0)


def test_vga_movie_sets_its_mode(lib, game_dir):
    movie = Movie(lib, (game_dir / "FTL.VGA").read_bytes())
    assert movie.opened == MVE_OK
    while movie.step() == MVE_AUDIO:
        pass
    assert (movie.m.mode_width, movie.m.mode_height) == (C.SCREEN_W, C.SCREEN_H)


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
