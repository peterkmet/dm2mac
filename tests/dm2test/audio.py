"""The sound and music records of the C port, and the sound mix."""

import ctypes

from dm2test import constants as C
from dm2test.structs import mirror


UPSAMPLE = 4


@mirror("sound_request.h")
class SoundOffset(ctypes.Structure):
    _fields_ = [("lateral", ctypes.c_int), ("forward", ctypes.c_int)]


@mirror("music.h")
class Music(ctypes.Structure):
    _fields_ = [("enabled", ctypes.c_bool), ("songlist_loaded", ctypes.c_bool), ("songlist", ctypes.c_uint8 * C.SONGLIST_MAX),
                ("songlist_size", ctypes.c_int), ("fade", ctypes.c_int16), ("paused", ctypes.c_bool),
                ("playing", ctypes.c_bool), ("started", ctypes.c_bool), ("requested_song", ctypes.c_int16),
                ("current_song", ctypes.c_int16), ("next_song", ctypes.c_int16), ("volume", ctypes.c_int16)]


def mix(lib, frames):
    buf = (ctypes.c_int16 * (2 * frames))()
    lib.sound_mix(buf, frames)
    return list(buf)
