"""The ctypes mirrors of the C structs that more than one test file uses; tests/port/test_struct_layout.py checks them."""

import ctypes

from dm2test import constants as C


class Bitmap(ctypes.Structure):
    """gfx.h Bitmap: palette indices at 8 or 4 bpp."""
    _fields_ = [("pixels", ctypes.POINTER(ctypes.c_uint8)), ("width", ctypes.c_int16), ("height", ctypes.c_int16),
                ("bpp", ctypes.c_uint8)]


class GdatKey(ctypes.Structure):
    """gdat.h GdatKey: category, index, entry."""
    _fields_ = [("category", ctypes.c_int), ("index", ctypes.c_int), ("entry", ctypes.c_int)]


class ImageInfo(ctypes.Structure):
    """gdat.h ImageInfo: width, height, bits per pixel, decoded byte size."""
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int), ("bpp", ctypes.c_int), ("size", ctypes.c_size_t)]


class ViewSquare(ctypes.Structure):
    """aspect.h ViewSquare: what the view shows of one square (18 bytes)."""
    _fields_ = [("x", ctypes.c_int8), ("y", ctypes.c_int8), ("aspect", ctypes.c_uint16), ("square", ctypes.c_uint16),
                ("first_thing", ctypes.c_uint16), ("side", ctypes.c_uint16 * C.VIEW_SIDE_COUNT),
                ("text", ctypes.c_uint16)]


class TimelineEvent(ctypes.Structure):
    """timeline.h TimelineEvent: time_map shares its bytes with next_free."""
    _fields_ = [("time_map", ctypes.c_uint32), ("type", ctypes.c_uint8), ("priority", ctypes.c_uint8),
                ("data", ctypes.c_uint8 * C.TIMELINE_EVENT_DATA_SIZE)]


class Timeline(ctypes.Structure):
    """timeline.h Timeline: the events and the heap of their indices."""
    _fields_ = [("events", ctypes.POINTER(TimelineEvent)), ("queue", ctypes.POINTER(ctypes.c_uint16)),
                ("fix_slot", ctypes.c_int16), ("first_free", ctypes.c_int16), ("capacity", ctypes.c_uint16),
                ("count", ctypes.c_uint16), ("used_end", ctypes.c_int16)]


class Sound(ctypes.Structure):
    """sound_request.h Sound: signed 8-bit mono PCM."""
    _fields_ = [("data", ctypes.POINTER(ctypes.c_int8)), ("length", ctypes.c_uint32)]


class ProjectileLaunch(ctypes.Structure):
    """projectile.h ProjectileLaunch: a launch of a projectile."""
    _fields_ = [("payload", ctypes.c_uint16), ("x", ctypes.c_int16), ("y", ctypes.c_int16), ("cell", ctypes.c_int16),
                ("dir", ctypes.c_int16), ("energy", ctypes.c_uint8), ("hit_chance", ctypes.c_uint8),
                ("step", ctypes.c_uint8), ("from_sensor", ctypes.c_bool)]


class LeaderHand(ctypes.Structure):
    """champion.h LeaderHand: the leader hand object and its cursor icon."""
    _fields_ = [("attr", ctypes.c_uint16), ("weight", ctypes.c_uint16), ("icon", ctypes.c_uint8),
                ("cursor", ctypes.c_uint8 * C.HAND_ICON_SIZE ** 2), ("palette", ctypes.c_uint8 * C.HAND_ICON_COLORS)]


class ActionArea(ctypes.Structure):
    """action.h ActionArea: the action area state."""
    _fields_ = [("champion", ctypes.c_int16), ("hand", ctypes.c_int16), ("mode", ctypes.c_int16),
                ("dirty", ctypes.c_int16), ("redraw_delay", ctypes.c_int16), ("flags", ctypes.c_uint16),
                ("result", ctypes.c_int16), ("result_flag", ctypes.c_int16), ("result_ammo", ctypes.c_uint16),
                ("flash", ctypes.c_int16), ("flash_hand", ctypes.c_int16), ("map_item", ctypes.c_uint8),
                ("open_items", ctypes.c_uint16 * C.CONTAINER_SLOTS), ("open_container", ctypes.c_uint16),
                ("hand_items", ctypes.c_uint16 * C.SLOT_HAND_COUNT * C.CHAMPION_MAX)]


class CurrentMap(ctypes.Structure):
    """The current map of the square reads (dungeon.h, CurrentMap)."""
    _fields_ = [(name, ctypes.c_int16) for name in ("map", "square_x", "square_y", "square_map", "ref_dir")] + [
        ("alt_view_active", ctypes.c_int), ("alt_view_x", ctypes.c_int16), ("alt_view_y", ctypes.c_int16),
        ("alt_view_dir", ctypes.c_int16), ("alt_view_map", ctypes.c_int16)]


class Party(ctypes.Structure):
    """The movement state of the party (move.h, Party)."""
    _fields_ = [
        ("move_steps", ctypes.c_int16), ("move_command", ctypes.c_uint16), ("view_x", ctypes.c_int16),
        ("view_y", ctypes.c_int16), ("view_dir", ctypes.c_int16),
        ("new_map", ctypes.c_int16), ("fell", ctypes.c_int16), ("dest_x", ctypes.c_int16), ("dest_y", ctypes.c_int16),
        ("dest_map", ctypes.c_int16), ("dest_cell", ctypes.c_int16), ("dest_dir", ctypes.c_int16),
        ("teleporter_flag", ctypes.c_int), ("redraw", ctypes.c_uint32), ("push_pending_group", ctypes.c_uint16),
        ("push_mode", ctypes.c_bool), ("push_arrow", ctypes.c_int16)]


class Game(ctypes.Structure):
    """save.h Game: the game state of the save beside the dungeon, the timeline, and the random seed."""
    _fields_ = [
        ("game_time", ctypes.c_uint32), ("champion_count", ctypes.c_uint16), ("party_x", ctypes.c_uint16),
        ("party_y", ctypes.c_uint16), ("party_dir", ctypes.c_uint16), ("party_map", ctypes.c_uint16),
        ("leader", ctypes.c_uint16), ("party_move_cooldown", ctypes.c_uint16), ("last_combat_time", ctypes.c_uint32),
        ("first_recruit_time", ctypes.c_uint32), ("throw_cooldown", ctypes.c_uint16), ("throw_dir", ctypes.c_uint16)] + [
        (name, ctypes.c_uint8) for name in (
            "sound_volume", "music_volume", "weather_visible", "heavy_rain", "wind_dir", "rain_intensity",
            "sky_darkness", "cloud_cover", "storm_multiplier", "storm_step", "storm_curve")] + [
        ("storm_intensity", ctypes.c_uint16), ("next_hour_time", ctypes.c_uint32),
        ("global_flags", ctypes.c_uint8 * C.GLOBAL_FLAG_BYTES), ("global_bytes", ctypes.c_uint8 * C.GLOBAL_BYTE_COUNT),
        ("global_words", ctypes.c_uint16 * C.GLOBAL_WORD_COUNT),
        ("champions", ctypes.c_uint8 * C.CHAMPION_SIZE * C.CHAMPION_MAX), ("leader_hand", ctypes.c_uint16),
        ("magic_light", ctypes.c_uint8 * C.MAGIC_LIGHT_SIZE)]


# Each mirror and the header that defines its C type.
LAYOUT = {
    Bitmap: "gfx.h", GdatKey: "gdat.h", ImageInfo: "gdat.h", ViewSquare: "aspect.h", TimelineEvent: "timeline.h",
    Timeline: "timeline.h", Sound: "sound_request.h", ProjectileLaunch: "projectile.h", LeaderHand: "champion.h",
    ActionArea: "action.h", CurrentMap: "dungeon.h", Party: "move.h", Game: "save.h",
}
