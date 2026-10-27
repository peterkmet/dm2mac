"""The sound and music records of the C port, and the sound mix."""

import ctypes


UPSAMPLE = 4


class Sound(ctypes.Structure):
    _fields_ = [("data", ctypes.POINTER(ctypes.c_int8)), ("length", ctypes.c_uint32)]


class SoundCue(ctypes.Structure):
    _fields_ = [(name, ctypes.c_int) for name in ("category", "index", "entry", "priority", "volume")]


class SoundOffset(ctypes.Structure):
    _fields_ = [("lateral", ctypes.c_int), ("forward", ctypes.c_int)]


class Music(ctypes.Structure):
    _fields_ = [("enabled", ctypes.c_bool), ("songlist_loaded", ctypes.c_bool), ("songlist", ctypes.c_uint8 * 63),
                ("songlist_size", ctypes.c_int), ("fade", ctypes.c_int16), ("paused", ctypes.c_bool),
                ("playing", ctypes.c_bool), ("started", ctypes.c_bool), ("requested_song", ctypes.c_int16),
                ("current_song", ctypes.c_uint8), ("next_song", ctypes.c_uint8), ("volume", ctypes.c_int16)]


def mix(lib, frames):
    buf = (ctypes.c_int16 * (2 * frames))()
    lib.sound_mix(buf, frames)
    return list(buf)
