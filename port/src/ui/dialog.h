#ifndef DIALOG_H
#define DIALOG_H
#include <stdint.h>

/* Prefixes: dialog_ */

/* Dialogs: a picture of GDAT category 0x1a in the viewport with up to 4 buttons and a message. */
enum dialog_id {
    DIALOG_ERROR = 0x00, DIALOG_LOADED = 0x06, DIALOG_NOT_SAVED = 0x0c, DIALOG_SAVING = 0x0d, DIALOG_LOADING = 0x0e,
    DIALOG_THE_END = 0x10, DIALOG_SAVE_MENU = 0x1b, DIALOG_LOAD_SLOTS = 0x80, DIALOG_SAVE_SLOTS = 0x81,
    DIALOG_VOLUME = 0x87
};
/* Error messages: text entries of dialog 0. */
enum dialog_message { MESSAGE_NONE = 0, MESSAGE_SAVE_FAILED = 0x1b, MESSAGE_GAME_DAMAGED = 0x1f };
/* The commands of the dialog buttons: the first button, then the next ones. */
#define CMD_DIALOG_BUTTON_FIRST 0xdb
#define CMD_DIALOG_BUTTON_LAST 0xde
#define ZONE_DIALOG_BACK 4      /* the viewport of the dialog picture */

typedef struct {
    int screen_mode;            /* 1: the dialogs draw on a black screen; 0: in the game viewport */
    int button_count;           /* the buttons of the open dialog (7 for the volume dialog) */
    uint8_t button_category, button_index, button_entry; /* the picture of a pressed button */
    int progress_enabled;
    int16_t progress_width;
    int id;                     /* the dialog of the last dialog_draw */
} DialogState;
extern DialogState dialog;

/* Begins (1) or ends (0) the drawing of a dialog: in screen mode 1 the screen goes black first. */
void dialog_screen_mode(int begin);
/* Shows the viewport with the dialog on the screen. */
void dialog_present(void);
/* Draws dialog id with an error message entry (0 none) in front of it; returns the dialog drawn. */
int dialog_draw(int id, int message);
/* Waits until the player clicks a button of dialog id; returns the text entry of the button (the volume dialog: the
   command). */
int dialog_run(int id);
/* Draws the pressed button picture over the clicked button of zone. */
void dialog_draw_button(int zone, int highlighted);
/* Draws the bar of a progress dialog at permille of its length. */
void dialog_draw_progress_bar(int permille);
/* The dialog of the sound and music volumes. */
void dialog_volume(void);
/* Sets the sound (DEVICE_SOUND, plays a test sound) or music (DEVICE_MUSIC) volume setting, 0 to 7. */
enum { DEVICE_SOUND = 0, DEVICE_MUSIC = 10 };

#endif
