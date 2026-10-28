/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DM2_UI_DIALOG_H
#define DM2_UI_DIALOG_H
/* Prefixes: dialog_ */

/* Dialogs: message, question and progress pictures with buttons, and the volume settings. */

#include <stdbool.h>
#include <stdint.h>
#include "gdat.h"

/* Dialogs: a picture of CAT_DIALOG in the viewport with up to 4 buttons and a message. */
#define DIALOG_ERROR      0x00
#define DIALOG_LOADED     0x06
#define DIALOG_NOT_SAVED  0x0c
#define DIALOG_SAVING     0x0d
#define DIALOG_LOADING    0x0e
#define DIALOG_THE_END    0x10
#define DIALOG_SAVE_MENU  0x1b
#define DIALOG_LOAD_SLOTS 0x80
#define DIALOG_SAVE_SLOTS 0x81
#define DIALOG_VOLUME     0x87

/* Error messages: text entries of dialog 0. */
#define MESSAGE_NONE         0x00
#define MESSAGE_SAVE_FAILED  0x1b
#define MESSAGE_GAME_DAMAGED 0x1f

/* The commands of the dialog buttons: the first button, then the next ones. */
#define CMD_DIALOG_BUTTON_FIRST 0xdb
#define CMD_DIALOG_BUTTON_LAST  0xde

/* The texts of a dialog picture. */
#define DIALOG_VERSION_TEXT "V1.0"
#define DIALOG_TEXT_COUNT   20 /* text entries 0 to 19: the buttons */

typedef struct {
    int screen_mode;  /* 1: the dialogs draw on a black screen; 0: in the game viewport */
    int button_count; /* the buttons of the open dialog (7 for the volume dialog) */
    int progress_enabled;
    int16_t progress_width;
    int id;           /* the dialog of the last dialog_draw */
    int right_choice; /* the advanced mode: the choice of a right click in the open dialog, 0 for none */
} DialogState;
extern DialogState g_dialog;

/* Drawing */
/* Begins (true) or ends (false) the drawing of a dialog: in screen mode 1 the screen goes black first. */
void dialog_screen_mode(bool begin);
/* Shows the viewport with the dialog on the screen. */
void dialog_present(void);
/* Draws dialog id with an error message entry (0 none) in front of it; returns the dialog drawn. */
int dialog_draw(int id, int message);
/* Draws the pressed button picture over the clicked button (the rectangle of g_input_match). */
void dialog_draw_button(bool highlighted, GdatKey picture);
/* Draws the bar of a progress dialog at permille of its length. */
void dialog_draw_progress_bar(int permille); /* exported for the tests */

/* Running */
/* Waits until the player clicks a button of dialog id; returns the text entry of the button (the volume dialog: the
   command). */
int dialog_run(int id);
/* dialog_run, and in the advanced mode a right click chooses the button of text entry cancel. */
int dialog_run_cancel(int id, int cancel);

/* Volume */
/* The dialog of the sound and music volumes. */
void dialog_volume(void);

#endif /* DM2_UI_DIALOG_H */
