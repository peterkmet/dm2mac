/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_DATA_IDS_H
#define DM2_DATA_IDS_H
/* Prefixes: none */

/* Graphic entry numbers of GRAPHICS.DAT (one block per category and item type), layout zone numbers, minion map
   icon numbers and string numbers, one block per kind, sorted by value. */

/* The interface layout (CAT_INTERFACE, 0, 4, entry). */
#define GFX_LAYOUT 0x00

/* The interface data of type 7 (CAT_INTERFACE, 0, 7, entry): the font and the color remaps. */
#define GFX_FONT              0x00
#define GFX_ICON_SHADOW_REMAP 0x01 /* GDAT (1, 0, 7, 1): the colors of an item icon in a hand */
#define GFX_RAMP              0x02

/* The palette (CAT_INTERFACE, 0, 9, entry). */
#define GFX_PALETTE 0xfe

/* The interface raw data (CAT_INTERFACE, index, GDAT_TYPE_DATA, entry). */
#define GFX_UI_COLORS 0xfe /* (CAT_INTERFACE, index, GDAT_TYPE_DATA, entry): the interface colors */

/* Status box pictures (1, 2, 1, entry); the slot frames are also the eye and mouth icons of the inventory. */
#define GFX_BOX_ALIVE         0x00
#define GFX_BOX_DEAD          0x01
#define GFX_BOX_DAMAGE        0x03
#define GFX_BOX_SLOT_FRAME    0x04
#define GFX_BOX_SLOT_WOUNDED  0x05
#define GFX_BOX_SLOT_ACTIVE   0x06
#define GFX_ACTION_EMPTY_HAND 0x07
#define GFX_BOX_INVENTORY     0x09

/* The movement arrows (1, 3, 1, entry). */
#define GFX_ARROWS      0x02
#define GFX_PUSH_ARROWS 0x0e

/* Action area pictures (1, 4, 1, entry). */
#define GFX_ACTION_RESULT_DAMAGE    0x01
#define GFX_ACTION_HAND_BACK_FIRST  0x02
#define GFX_ACTION_SPELL_HAND_LEFT  0x06
#define GFX_ACTION_SPELL_HAND_RIGHT 0x08
#define GFX_ACTION_SPELL_RUNE_LEFT  0x0a
#define GFX_ACTION_SPELL_RUNE_RIGHT 0x0c
#define GFX_ACTION_MENU_BACK        0x0e
#define GFX_ACTION_FACE_LEFT        0x10
#define GFX_ACTION_FACE_RIGHT       0x12
#define GFX_ACTION_MENU_HEADER      0x14
#define GFX_ACTION_BUTTON           0x15
#define GFX_ACTION_RESULT_MISS      0x17
#define GFX_ACTION_RESULT_NOTHING   0x18
#define GFX_ACTION_RESULT_CANT      0x19

/* Spell area pictures (1, 5, 1, entry). */
#define GFX_SPELL_RUNE_PRESSED  0x08 /* GDAT (1, 5, 8): the pressed rune button */
#define GFX_SPELL_RUNES_BACK    0x09
#define GFX_FLASK_PICTURE       0x0b
#define GFX_RESULT_SPELL_NEEDS  0x0c
#define GFX_RESULT_SPELL_OTHER  0x0d
#define GFX_RESULT_SPELL_FIZZLE 0x0e

/* The interface texts of index 0xfe (CAT_INTERFACE, 0xfe, GDAT_TYPE_TEXT, entry). */
#define GFX_FORMAT_TITLE_2 0x06

/* Inventory panel pictures (7, 0, 1, entry); the close button is also the button of the name entry. */
#define GFX_PANEL_BACK         0x00
#define GFX_PANEL_PANEL        0x01
#define GFX_PANEL_OBJECT_NAME  0x05
#define GFX_PANEL_FOOD         0x06
#define GFX_PANEL_WATER        0x07
#define GFX_PANEL_POISON       0x08
#define GFX_PANEL_BUTTON_EXIT  0x0b
#define GFX_PANEL_BUTTON_SLEEP 0x0d
#define GFX_PANEL_BUTTON_SAVE  0x0f
#define GFX_PANEL_BUTTON_SOUND 0x11
#define GFX_PANEL_BUTTON_CLOSE 0x13
#define GFX_NAME_BUTTON_DOWN   0x14
#define GFX_PANEL_MOUTH        0x20
#define GFX_PANEL_EYE          0x25
#define GFX_AMMO_HAND_OTHER    0x34
#define GFX_AMMO_HAND_DEFAULT  0x38

/* The menu pictures (CAT_MENU, 0, GDAT_TYPE_IMAGE, entry). */
#define GFX_MENU_PICTURE 0x01 /* GDAT (5, 0, 1, 1): the picture of the menu command 0xda */
#define GFX_MENU_TITLE   0x04 /* GDAT (5, 0, 1, 4): the title picture */

/* The end pictures (CAT_END, 0, GDAT_TYPE_IMAGE, entry). */
#define GFX_END_PICTURE 0x01 /* GDAT (6, 0, 1, 1): the picture after the player quits */

/* Wall set pictures (CAT_WALLSET, graphics set, 1, entry). */
#define GFX_MAP_FLOOR            0x00
#define GFX_MAP_CEILING          0x01
#define GFX_MAP_FONT             0x03
#define GFX_MAP_DOOR_SIDE        0x06
#define GFX_MAP_WALL             0x22
#define GFX_MAP_FAR_WALL         0x32
#define GFX_MAP_STAIRS_SIDE_UP   0x39
#define GFX_MAP_STAIRS_SIDE_DOWN 0x4d
#define GFX_MAP_WALL_FLIPPED     0xb0
#define GFX_WALLSET_PARTY_BACK   0xf5 /* behind the party */
#define GFX_WALLSET_FACE_BACK    0xf6 /* behind the action champion */
#define GFX_MAP_PLATE_FRONT      0xfc
#define GFX_MAP_PLATE_SIDE       0xfd
#define GFX_MAP_PLATE_RIGHT      0xfe

/* Wall set color remaps (CAT_WALLSET, graphics set, GDAT_TYPE_REMAP, entry). */
#define GFX_REMAP_WALK_BASE 0x09 /* + depth: the depth shading table of the walking view */

/* Ornament pictures (CAT_WALL_ORNAMENT, ornament, 1, entry); the wet background also of floor ornaments. */
#define GFX_ORN_OVERLAY        0x0f /* wall ornament picture drawn over the ornament */
#define GFX_ORN_SHOP_COINS     0x10 /* a strip of coin pictures, COIN_WIDTH wide each */
#define GFX_ORN_SHOP_EMPTY     0x11 /* the picture of a display without its item */
#define GFX_ORN_WET_BACKGROUND 0xc8 /* + entry: the picture behind a wet ornament */
#define GFX_MOUTH_CLOSED       0xfa /* the mouth of the inventory: ornament 0x5b */
#define GFX_MOUTH_OPEN         0xfb /* also the entry of its sound */

/* Ornament tables (CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_TABLE, entry). */
#define GFX_ORN_SHOP_TABLE 0x13 /* (CAT_WALL_ORNAMENT, ornament, GDAT_TYPE_TABLE, 0x13): the item order */

/* Door pictures (CAT_DOOR, door, GDAT_TYPE_IMAGE, entry). */
#define GFX_DOOR_DAMAGE 0x41 /* door picture of the damage mask */

/* The pictures of a dialog (CAT_DIALOG, dialog, GDAT_TYPE_IMAGE, entry). */
#define GFX_DIALOG_PICTURE   0x00
#define GFX_VOLUME_BUTTON    0x02 /* the volume dialog: a setting button, and the button of the current setting */
#define GFX_VOLUME_BUTTON_ON 0x03

/* The texts of a dialog (CAT_DIALOG, dialog, GDAT_TYPE_TEXT, entry). */
#define GFX_DIALOG_MESSAGE     0x14
#define GFX_VOLUME_SOUND_LABEL 0x28 /* the volume dialog: the labels and the texts of setting 0 */
#define GFX_VOLUME_SOUND_OFF   0x29
#define GFX_VOLUME_MUSIC_LABEL 0x3c
#define GFX_VOLUME_MUSIC_OFF   0x3d

/* The pictures of a creature (CAT_CREATURE, creature, GDAT_TYPE_IMAGE, entry). */
#define GFX_ICON              0x09 /* the front view */
#define GFX_CREATURE_SIDE     0xfa /* pictures 0xfa to 0xfd: the creature seen from each side */
#define GFX_CREATURE_ANY_SIDE 0xfc

/* The animation data of a creature (CAT_CREATURE, creature, GDAT_TYPE_CREATURE_DATA, entry). */
#define GFX_CREATURE_STEPS  0xfc /* data: the animation steps (4 bytes each) */
#define GFX_CREATURE_FRAMES 0xfd /* data: the animation frames */
#define GFX_CREATURE_SIZES  0xfe /* creature data: picture scale by frame size row and side */

/* The animation table of a creature (CAT_CREATURE, creature, GDAT_TYPE_TABLE, entry). */
#define GFX_CREATURE_SEQUENCES 0xfb /* table: (action, first step) pairs, ended by -1 */

/* The texts of a creature (CAT_CREATURE, creature, GDAT_TYPE_TEXT, entry). */
#define GFX_CREATURE_CLASS_TEXT 0x10 /* GDAT texts (0x0f, creature, 5, 0x10 + list): the item classes of a merchant */

/* The pictures of an explosion (CAT_EXPLOSION, type, GDAT_TYPE_IMAGE, entry); the sparkles also of a teleporter. */
#define GFX_ALCOVE_STILL_PICTURE 0x0c
#define GFX_SPARKLE_PICTURE      0x14 /* an explosion with this picture is a sparkle field */
#define GFX_EXPLOSION_PICTURE    0x41
#define GFX_EXPLOSION_TINT       0x42 /* pictures 0x42 to 0x44: the tint of an explosion on the party square */

/* The pictures of a champion (CAT_CHAMPION, champion, GDAT_TYPE_IMAGE, entry). */
#define GFX_PORTRAIT 0x01

/* The texts of a champion (CAT_CHAMPION, portrait, GDAT_TYPE_TEXT, entry). */
#define GFX_PORTRAIT_TEXT 0x18 /* GDAT text (0x16, portrait, 5, 0x18): "NAME TITLE" */

/* The pictures of every item (item category, index, GDAT_TYPE_IMAGE, entry). */
#define GFX_OBJECT_PICTURE_CLOSED   0x00 /* object pictures by the open state of a chest: side view, front view */
#define GFX_OBJECT_PICTURE_OPEN     0x04
#define GFX_MISSILE_PICTURE_FRONT   0x08 /* the pictures of an item in flight */
#define GFX_MISSILE_PICTURE_FRONT_2 0x09
#define GFX_MISSILE_PICTURE_BACK    0x0a
#define GFX_MISSILE_PICTURE_SIDE    0x0c
#define GFX_OBJECT_PICTURE          0x11 /* (category, index, 1, 0x11): the big picture of an item */
#define GFX_ITEM_ICON               0x18 /* the first icon of an item */

/* GDAT texts (category, index, 5, entry) of an item: its name and its actions. */
#define GFX_ACTION_FIRST 0x08
#define GFX_ACTION_LAST  0x0b
#define GFX_ITEM_NAME    0x18

/* The pictures of a scroll (CAT_SCROLL, 0, GDAT_TYPE_IMAGE, entry). */
#define GFX_SCROLL_PICTURE 0x10

/* The pictures of a container: a chest, a money box, a minion map (CAT_CONTAINER, index, GDAT_TYPE_IMAGE, entry). */
#define GFX_CONTAINER_PICTURE       0x10
#define GFX_CONTAINER_LID           0x12
#define GFX_MAP_ITEM_ICONS          0x40 /* the image of the map item with the party, minion, and marker icons */
#define GFX_MAP_ACTION_PICTURE_BASE 0x41
#define GFX_CHEST_INSIDE_PICTURE    0x44 /* the inside of an open chest: side view, front view (0x45) */
#define GFX_CHEST_OPEN_PICTURE      0x48 /* the lid of an open chest: side view, front view (0x49) */
#define GFX_OPTION_BUTTON_UP        0x49
#define GFX_OPTION_BUTTON_DOWN      0x4a

/* The texts of a container (CAT_CONTAINER, index, GDAT_TYPE_TEXT, entry). */
#define GFX_MONEYBOX_TEXT 0x40 /* GDAT text (0x14, index, 5, 0x40): the coin slots of a money box */

/* Sky objects (CAT_SKY, graphics set, GDAT_TYPE_IMAGE, entry). */
#define GFX_SKY_LIGHTNING     0x64
#define GFX_SKY_RAIN_LIGHT    0x67
#define GFX_SKY_RAIN_MEDIUM   0x68
#define GFX_SKY_RAIN_HEAVY    0x69
#define GFX_SKY_CLOUDS_LIGHT  0x6a
#define GFX_SKY_CLOUDS_MEDIUM 0x6b
#define GFX_SKY_CLOUDS_HEAVY  0x6c

/* The minion map icon strip of a thing (category, index, GDAT_TYPE_IMAGE, entry). */
#define GFX_STRIP 0xf9

/* The image that stands in for a missing image (CAT_MISC, 0xfe, GDAT_TYPE_IMAGE, entry). */
#define GFX_PLACEHOLDER 0xfe

/* Layout zones of the screen, the panels, the dialogs and the view. */
#define ZONE_SCREEN                0x0002
#define ZONE_DIALOG_BACK           0x0004 /* the viewport of the dialog picture */
#define ZONE_DIALOG_SCREEN         0x0005 /* where the dialog viewport goes on a black screen */
#define ZONE_VIEW_TEXT             0x0006 /* the layout zone of a message in the viewport */
#define ZONE_VIEWPORT              0x0007
#define ZONE_MOVEMENT_ARROWS       0x0009
#define ZONE_ACTION_AREA           0x000b
#define ZONE_MESSAGE_AREA          0x000f
#define ZONE_ACTION_ORIGIN         0x0012
#define ZONE_ARROW_FIRST           0x0028
#define ZONE_PARTY_ICONS           0x002f
#define ZONE_PARTY_FACE_FIRST      0x0035
#define ZONE_RESULT                0x0039
#define ZONE_MENU_BACK             0x003b
#define ZONE_MENU_HEADER           0x003c
#define ZONE_MENU_NAME             0x003d
#define ZONE_ACTION_BUTTON_FIRST   0x003f
#define ZONE_ACTION_NAME_FIRST     0x0042
#define ZONE_ACTION_HAND_FIRST     0x0046
#define ZONE_READY_HAND_FIRST      0x004a
#define ZONE_SPELL_ICON_FIRST      0x004f
#define ZONE_SPELL_RUNE_ICON_FIRST 0x0053
#define ZONE_SPELL_HAND_ICON_FIRST 0x0057
#define ZONE_PANEL                 0x005c /* the panel of the action area */
#define ZONE_FACE_BACK             0x005d
#define ZONE_FACE                  0x005e
#define ZONE_FACE_LEFT             0x0060
#define ZONE_FACE_RIGHT            0x0061
#define ZONE_MAP_GRID              0x0063
#define ZONE_OPTION_BUTTON_FIRST   0x0065
#define ZONE_OPTION_LABEL_FIRST    0x0069
#define ZONE_MAP_ACTION_FIRST      0x006e
#define ZONE_AMMO_ITEM             0x007d
#define ZONE_AMMO_HAND             0x007e
#define ZONE_STATUS_BOX            0x00a1 /* the status box of a champion: + champion */
#define ZONE_STATUS_NAME           0x00a5 /* + champion */
#define ZONE_STATUS_PORTRAIT       0x00ad /* + champion */
#define ZONE_STATUS_DAMAGE         0x00b1 /* + champion */
#define ZONE_STATUS_BARS           0x00b9 /* + champion */
#define ZONE_STATUS_BAR_FIRST      0x00c1 /* + champion + 4 * bar */
#define ZONE_COIN_PILE_FIRST       0x00dd
#define ZONE_CONTAINER_LID         0x00e3
#define ZONE_CONTAINER_SLOT_FIRST  0x00e5
#define ZONE_RUNES_BACK            0x00fc
#define ZONE_RUNE_CHOICE_FIRST     0x00ff
#define ZONE_RUNE_ENTERED_FIRST    0x0105
#define ZONE_SOUND_LABEL           0x015f
#define ZONE_SOUND_BUTTON          0x0160
#define ZONE_SOUND_TEXT            0x0168
#define ZONE_MUSIC_LABEL           0x0170
#define ZONE_MUSIC_BUTTON          0x0171
#define ZONE_MUSIC_TEXT            0x0179
#define ZONE_DIALOG_VERSION        0x01c2 /* the version text of the dialogs and of the slot lists */
#define ZONE_SLOT_LIST             0x01c3
#define ZONE_SLOT_NAME             0x01c5
#define ZONE_SLOT_TEXT_1           0x01d2
#define ZONE_SLOT_TEXT_2           0x01d3
#define ZONE_DIALOG_MESSAGE_WIDE   0x01d5 /* the message zone of a dialog with fewer than 2 buttons */
#define ZONE_DIALOG_MESSAGE        0x01d7
#define ZONE_PROGRESS_BAR          0x01da
#define ZONE_INVENTORY_PANEL       0x01ee /* the picture panel of the inventory */
#define ZONE_FOOD_GAUGE            0x01f0
#define ZONE_WATER_GAUGE           0x01f1
#define ZONE_POISON_GAUGE          0x01f3
#define ZONE_FOOD_LABEL            0x01f4
#define ZONE_WATER_LABEL           0x01f5
#define ZONE_POISON_LABEL          0x01f6
#define ZONE_OBJECT_NAME_BACK      0x01f8
#define ZONE_OBJECT_NAME           0x01fa
#define ZONE_EYE                   0x0221
#define ZONE_MOUTH                 0x0222
#define ZONE_VALUES_BACK           0x0225
#define ZONE_HEALTH_VALUE          0x0226
#define ZONE_STAMINA_VALUE         0x0227
#define ZONE_MANA_VALUE            0x0228
#define ZONE_NAME                  0x0229
#define ZONE_LOAD_BACK             0x022a
#define ZONE_LOAD                  0x022b
#define ZONE_DESCRIPTION           0x022c
#define ZONE_SKILLS                0x022d
#define ZONE_STATS                 0x022f
#define ZONE_SCROLL_TEXT           0x0230
#define ZONE_BUTTON_SAVE           0x0232
#define ZONE_BUTTON_SLEEP          0x0234
#define ZONE_BUTTON_EXIT           0x0236
#define ZONE_BUTTON_SOUND          0x0238
#define ZONE_COIN_TOTAL            0x0239
#define ZONE_NAME_BUTTON           0x0267
#define ZONE_COINS_FIRST           0x026a
#define ZONE_OBJECT_GAUGE_FIRST    0x0270
#define ZONE_TITLE                 0x0273
#define ZONE_CEILING               0x02bc
#define ZONE_FLOOR                 0x02bd
#define ZONE_WALL                  0x02be
#define ZONE_DROP_CELL_FIRST       0x02f8 /* + cell: where a click drops an object */
#define ZONE_FRONT_WALL_LOW        0x02fd /* the parts of the view that show the wall in front of the party */
#define ZONE_FRONT_WALL_HIGH       0x02fe
#define ZONE_PARTY_STAIRS_UP       0x032b
#define ZONE_PARTY_STAIRS_DOWN     0x0338
#define ZONE_DOOR_ORNAMENT         0x079e
#define ZONE_ORNAMENT_ROWS         0x07d0
#define ZONE_VIEW_ELEMENTS         0x0c1c
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

/* String numbers of the interface (CAT_INTERFACE, 0, GDAT_TYPE_TEXT, entry). */
#define TEXT_LEVEL_FIGHTER 0x06 /* GDAT text (1, 0, 6): a champion gains a fighter level */
#define TEXT_LEVEL_NINJA   0x07 /* GDAT text (1, 0, 7): a champion gains a ninja level */
#define TEXT_LEVEL_PRIEST  0x08 /* GDAT text (1, 0, 8): a champion gains a priest level */
#define TEXT_LEVEL_WIZARD  0x09 /* GDAT text (1, 0, 9): a champion gains a wizard level */
#define TEXT_JOINED        0x0e /* GDAT text (1, 0, 0x0e): a champion joins the party */
#define TEXT_SLEEPING      0x11 /* GDAT text (1, 0, 0x11): the sleep message */
#define TEXT_PAUSED        0x12 /* GDAT text (1, 0, 0x12): the pause message */

/* Inventory texts (7, 0, 5, entry). */
#define TEXT_WEIGHT     0x1f
#define TEXT_STAT_FIRST 0x20
#define TEXT_LOAD       0x2a

#endif /* DM2_DATA_IDS_H */
