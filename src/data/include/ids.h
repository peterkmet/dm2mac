/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_IDS_H
#define DM2_DATA_IDS_H
/* Prefixes: GFX_ ZONE_ ICON_ TEXT_ */

/* Layout zone numbers and minion map icon numbers, one block per kind, sorted by value. */

/* Layout zones of the screen, the panels, the dialogs and the view. */
#define ZONE_SCREEN                0x02
#define ZONE_DIALOG_BACK           0x04 /* the viewport of the dialog picture */
#define ZONE_DIALOG_SCREEN         0x05 /* where the dialog viewport goes on a black screen */
#define ZONE_VIEW_TEXT             0x06 /* the layout zone of a message in the viewport */
#define ZONE_VIEWPORT              0x07
#define ZONE_MOVEMENT_ARROWS       0x09
#define ZONE_ACTION_AREA           0x0b
#define ZONE_MESSAGE_AREA          0x0f
#define ZONE_ACTION_ORIGIN         0x12
#define ZONE_ARROW_FIRST           0x28
#define ZONE_PARTY_ICONS           0x2f
#define ZONE_PARTY_FACE_FIRST      0x35
#define ZONE_RESULT                0x39
#define ZONE_MENU_BACK             0x3b
#define ZONE_MENU_HEADER           0x3c
#define ZONE_MENU_NAME             0x3d
#define ZONE_ACTION_BUTTON_FIRST   0x3f
#define ZONE_ACTION_NAME_FIRST     0x42
#define ZONE_ACTION_HAND_FIRST     0x46
#define ZONE_READY_HAND_FIRST      0x4a
#define ZONE_SPELL_ICON_FIRST      0x4f
#define ZONE_SPELL_RUNE_ICON_FIRST 0x53
#define ZONE_SPELL_HAND_ICON_FIRST 0x57
#define ZONE_PANEL                 0x5c /* the panel of the action area */
#define ZONE_FACE_BACK             0x5d
#define ZONE_FACE                  0x5e
#define ZONE_FACE_LEFT             0x60
#define ZONE_FACE_RIGHT            0x61
#define ZONE_MAP_GRID              0x63
#define ZONE_OPTION_BUTTON_FIRST   0x65
#define ZONE_OPTION_LABEL_FIRST    0x69
#define ZONE_MAP_ACTION_FIRST      0x6e
#define ZONE_AMMO_ITEM             0x7d
#define ZONE_AMMO_HAND             0x7e
#define ZONE_STATUS_BOX            0xa1 /* the status box of a champion: + champion */
#define ZONE_STATUS_NAME           0xa5 /* + champion */
#define ZONE_STATUS_PORTRAIT       0xad /* + champion */
#define ZONE_STATUS_DAMAGE         0xb1 /* + champion */
#define ZONE_STATUS_BARS           0xb9 /* + champion */
#define ZONE_STATUS_BAR_FIRST      0xc1 /* + champion + 4 * bar */
#define ZONE_COIN_PILE_FIRST       0xdd
#define ZONE_CONTAINER_LID         0xe3
#define ZONE_CONTAINER_SLOT_FIRST  0xe5
#define ZONE_RUNES_BACK            0xfc
#define ZONE_RUNE_CHOICE_FIRST     0xff
#define ZONE_RUNE_ENTERED_FIRST    0x105
#define ZONE_SOUND_LABEL           0x15f
#define ZONE_SOUND_BUTTON          0x160
#define ZONE_SOUND_TEXT            0x168
#define ZONE_MUSIC_LABEL           0x170
#define ZONE_MUSIC_BUTTON          0x171
#define ZONE_MUSIC_TEXT            0x179
#define ZONE_DIALOG_VERSION        0x1c2 /* the version text of the dialogs and of the slot lists */
#define ZONE_SLOT_LIST             0x1c3
#define ZONE_SLOT_NAME             0x1c5
#define ZONE_SLOT_TEXT_1           0x1d2
#define ZONE_SLOT_TEXT_2           0x1d3
#define ZONE_DIALOG_MESSAGE_WIDE   0x1d5 /* the message zone of a dialog with fewer than 2 buttons */
#define ZONE_DIALOG_MESSAGE        0x1d7
#define ZONE_PROGRESS_BAR          0x1da
#define ZONE_INVENTORY_PANEL       0x1ee /* the picture panel of the inventory */
#define ZONE_FOOD_GAUGE            0x1f0
#define ZONE_WATER_GAUGE           0x1f1
#define ZONE_POISON_GAUGE          0x1f3
#define ZONE_FOOD_LABEL            0x1f4
#define ZONE_WATER_LABEL           0x1f5
#define ZONE_POISON_LABEL          0x1f6
#define ZONE_OBJECT_NAME_BACK      0x1f8
#define ZONE_OBJECT_NAME           0x1fa
#define ZONE_EYE                   0x221
#define ZONE_MOUTH                 0x222
#define ZONE_VALUES_BACK           0x225
#define ZONE_HEALTH_VALUE          0x226
#define ZONE_STAMINA_VALUE         0x227
#define ZONE_MANA_VALUE            0x228
#define ZONE_NAME                  0x229
#define ZONE_LOAD_BACK             0x22a
#define ZONE_LOAD                  0x22b
#define ZONE_DESCRIPTION           0x22c
#define ZONE_SKILLS                0x22d
#define ZONE_STATS                 0x22f
#define ZONE_SCROLL_TEXT           0x230
#define ZONE_BUTTON_SAVE           0x232
#define ZONE_BUTTON_SLEEP          0x234
#define ZONE_BUTTON_EXIT           0x236
#define ZONE_BUTTON_SOUND          0x238
#define ZONE_COIN_TOTAL            0x239
#define ZONE_NAME_BUTTON           0x267
#define ZONE_COIN_COUNT_FIRST      0x26a
#define ZONE_OBJECT_GAUGE_FIRST    0x270
#define ZONE_TITLE                 0x273
#define ZONE_CEILING               0x2bc
#define ZONE_FLOOR                 0x2bd
#define ZONE_WALL                  0x2be
#define ZONE_DROP_CELL_FIRST       0x2f8 /* + cell: where a click drops an object */
#define ZONE_FRONT_WALL_LOW        0x2fd /* the parts of the view that show the wall in front of the party */
#define ZONE_FRONT_WALL_HIGH       0x2fe
#define ZONE_PARTY_STAIRS_UP       0x32b
#define ZONE_PARTY_STAIRS_DOWN     0x338
#define ZONE_DOOR_ORNAMENT         0x79e
#define ZONE_ORNAMENT_ROWS         0x7d0
#define ZONE_VIEW_ELEMENTS         0xc1c
#define ZONE_VIEW_CELLS            0x1388
#define ZONE_SKY_HIGH              0x1771
#define ZONE_NONE                  0xffff

/* Icons of the square strip of the minion map. */
#define ICON_WALL            0x00
#define ICON_FAKE_WALL_BLINK 0x01
#define ICON_FLOOR           0x02
#define ICON_PIT             0x03
#define ICON_PIT_VARIANT     0x04
#define ICON_PIT_BLINK       0x05
#define ICON_STAIRS_FIRST    0x06
#define ICON_DOOR_FRONT      0x0a
#define ICON_DOOR_SIDE       0x0b
#define ICON_PIT_ABOVE       0x12

/* Icons of the map item strip of the minion map: the party, the minions and the marker. */
#define ICON_MARKER          0x02
#define ICON_MINION_FIRST    0x03
#define ICON_PARTY_FIRST     0x05
#define ICON_PARTY_INVISIBLE 0x09
#define ICON_MARKER_BELOW    0x0a
#define ICON_MARKER_ABOVE    0x0b

#endif /* DM2_DATA_IDS_H */
