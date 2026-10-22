/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "control.h"
#include "le.h"
#include "os.h"
#include "champion.h"
#include "dungeon.h"
#include "game.h"
#include "item.h"
#include "save.h"
#include "advanced.h"
#include "dialog.h"
#include "input.h"
#include "movie.h"
#include "play.h"
#include "session.h"
#include "video.h"
#include "state_json.h"
#include "turn.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPLY_LINE_SIZE       256 /* one piece of a reply written by put */
#define ERROR_SIZE            64
#define NAME_SIZE             32
#define WAIT_MAX_TICKS        6000
#define SCAN_CODE_COUNT       256 /* the low byte of a key code */
#define HEX_DIGITS            4   /* the digits of a \uXXXX escape */
#define HEX_BASE              16
#define UNICODE_ESCAPE_PREFIX 2    /* the backslash and the u of \uXXXX */
#define UTF8_ONE_BYTE_END     0x80 /* the first code point of 2, 3, and 4 UTF-8 bytes */
#define UTF8_TWO_BYTES_END    0x800
#define UTF8_THREE_BYTES_END  0x10000
#define UTF8_CONTINUATION     0x80 /* the marker and the payload of a continuation byte */
#define UTF8_PAYLOAD_MASK     0x3f
#define UTF8_PAYLOAD_BITS     6
#define UTF8_LEAD_MARKERS     0xf00 /* shifted right by the byte count: the high bits of a lead byte */
#define UTF8_MAX_BYTES        4
#define SURROGATE_HIGH        0xd800
#define SURROGATE_LOW         0xdc00
#define SURROGATE_END         0xe000
#define SURROGATE_BITS        10

/* A wait request: the reply goes out when g_main_loop.timer_ticks reached start + ticks. */
static struct {
    int pending;
    uint32_t start, ticks;
} waiting;

/* PC scan codes of the keys without a character; the arrows give the keypad codes that main.c gives them. */
static const struct {
    const char *name;
    uint16_t code;
} key_names[] = {
    {"esc",         0x01             },
    {"backspace",   0x0e             },
    {"tab",         0x0f             },
    {"enter",       0x1c             },
    {"space",       0x39             },
    {"f1",          0x3b             },
    {"f2",          0x3c             },
    {"f3",          0x3d             },
    {"f4",          0x3e             },
    {"f5",          0x3f             },
    {"f6",          0x40             },
    {"f7",          0x41             },
    {"f8",          0x42             },
    {"f9",          0x43             },
    {"f10",         0x44             },
    {"kp7",         0x47             },
    {"kp8",         0x48             },
    {"kp9",         0x49             },
    {"kp-",         0x4a             },
    {"kp4",         0x4b             },
    {"kp5",         0x4c             },
    {"kp6",         0x4d             },
    {"kp+",         0x4e             },
    {"kp1",         0x4f             },
    {"kp2",         0x50             },
    {"kp3",         0x51             },
    {"kp0",         0x52             },
    {"kp.",         0x53             },
    {"up",          KEYPAD_FORWARD   },
    {"down",        KEYPAD_BACK      },
    {"left",        KEYPAD_TURN_LEFT },
    {"right",       KEYPAD_TURN_RIGHT},
    {"shift+left",  KEYPAD_LEFT      },
    {"shift+right", KEYPAD_RIGHT     },
};
static const struct {
    const char *prefix;
    uint16_t bit;
} key_modifiers[] = {
    {"shift+", KEY_SHIFTED},
    {"ctrl+",  KEY_CONTROL},
    {"alt+",   KEY_ALT    },
};

/* The key code of a key name: a name of key_names, a character of the US layout, with modifier prefixes; 0 none. */
uint16_t control_key_code(const char *name)
{
    for (size_t i = 0; i < sizeof key_names / sizeof key_names[0]; i++)
        if (!strcmp(name, key_names[i].name))
            return key_names[i].code;
    for (size_t i = 0; i < sizeof key_modifiers / sizeof key_modifiers[0]; i++) {
        size_t n = strlen(key_modifiers[i].prefix);
        if (strncmp(name, key_modifiers[i].prefix, n))
            continue;
        uint16_t code = control_key_code(name + n);
        return code ? code | key_modifiers[i].bit : 0;
    }
    if (!name[0] || name[1])
        return 0;
    for (uint16_t scan = 1; scan < SCAN_CODE_COUNT; scan++) {
        if (kbd_key_to_ascii(scan) == name[0])
            return scan;
        if (kbd_key_to_ascii(scan | KEY_SHIFTED) == name[0])
            return scan | KEY_SHIFTED;
    }
    return 0;
}

/* Requests: a flat search for "key": value. */

const char *control_find_value(const char *line, const char *key)
{
    char pattern[NAME_SIZE + 2];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    for (const char *p = strstr(line, pattern); p; p = strstr(p, pattern)) {
        p += strlen(pattern);
        while (*p == ' ')
            p++;
        if (*p != ':')
            continue; /* a string value equal to the key, for example "cmd": "key" */
        for (p++; *p == ' ';)
            p++;
        return p;
    }
    return NULL;
}

/* Four hex digits at p, -1 when they are not. */
static long hex4(const char *p)
{
    long value = 0;
    for (int i = 0; i < HEX_DIGITS; i++) {
        if (!isxdigit((unsigned char)p[i]))
            return -1;
        value = value * HEX_BASE + (isdigit((unsigned char)p[i]) ? p[i] - '0'
                                                                 : tolower((unsigned char)p[i]) - 'a' +
                                                                       DECIMAL_BASE); /* a to f follow the ten digits */
    }
    return value;
}

/* Appends code point c as UTF-8; 0 when it does not fit. */
static int put_utf8(char *out, size_t size, size_t *n, long c)
{
    char bytes[UTF8_MAX_BYTES];
    int count = c < UTF8_ONE_BYTE_END ? 1 : c < UTF8_TWO_BYTES_END ? 2 : c < UTF8_THREE_BYTES_END ? 3 : UTF8_MAX_BYTES;
    if (count == 1)
        bytes[0] = (char)c;
    else {
        for (int i = count - 1; i > 0; i--, c >>= UTF8_PAYLOAD_BITS)
            bytes[i] = (char)(UTF8_CONTINUATION | (c & UTF8_PAYLOAD_MASK));
        bytes[0] = (char)((UTF8_LEAD_MARKERS >> count) | c);
    }
    if (*n + count >= size)
        return 0;
    memcpy(out + *n, bytes, count);
    *n += count;
    return 1;
}

/* The JSON string value of key, decoded (escapes, \uXXXX to UTF-8); 0 when it is missing, bad, too long, or has a
   NUL character. */
int control_get_string(const char *line, const char *key, char *out, size_t size)
{
    static const char escapes[] = "\"\"\\\\//b\bf\fn\nr\rt\t";
    const char *p = control_find_value(line, key);
    size_t n = 0;
    if (!p || *p++ != '"')
        return 0;
    while (*p != '"') {
        long c = (unsigned char)*p++;
        if (!c)
            return 0;
        if (c == '\\') {
            const char *e = *p ? strchr(escapes, *p) : NULL;
            if (e && (e - escapes) % 2 == 0)
                c = e[1], p++;
            else if (*p++ != 'u' || (c = hex4(p)) < 0)
                return 0;
            else {
                p += HEX_DIGITS;
                if (c >= SURROGATE_HIGH && c < SURROGATE_LOW && p[0] == '\\' && p[1] == 'u') { /* a surrogate pair */
                    long low = hex4(p + UNICODE_ESCAPE_PREFIX);
                    if (low < SURROGATE_LOW || low >= SURROGATE_END)
                        return 0;
                    c = UTF8_THREE_BYTES_END + ((c - SURROGATE_HIGH) << SURROGATE_BITS) + (low - SURROGATE_LOW);
                    p += UNICODE_ESCAPE_PREFIX + HEX_DIGITS;
                }
                if (!c || (c >= SURROGATE_HIGH && c < SURROGATE_END))
                    return 0;
                if (!put_utf8(out, size, &n, c))
                    return 0;
                continue;
            }
        }
        if (n + 1 >= size)
            return 0;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return 1;
}

long control_get_int(const char *line, const char *key, long missing)
{
    const char *p = control_find_value(line, key);
    char *end;
    if (!p)
        return missing;
    long value = strtol(p, &end, DECIMAL_BASE);
    return end == p ? missing : value;
}

/* Replies: JSON text in reply. */

/* Appends a short formatted piece to reply. */
/* Each piece stays under REPLY_LINE_SIZE; text of the game or the request goes through json_strn (put_string). */
static void put(Json *reply, const char *format, ...)
{
    char piece[REPLY_LINE_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(piece, sizeof piece, format, args);
    va_end(args);
    json_raw(reply, piece);
}

/* A JSON string of the first n bytes of text (up to its end mark); bytes from 0x80 go as they are (UTF-8). The game
   texts of GRAPHICS.DAT (701 entries) are ASCII, so only request text can bring other bytes. */
static void put_string(Json *reply, const char *text, size_t n)
{
    json_strn(reply, text, n);
}

static void put_ok(Json *reply)
{
    put(reply, "{\"ok\":true}");
}

static void put_error(Json *reply, const char *error)
{
    put(reply, "{\"ok\":false,\"error\":");
    put_string(reply, error, strlen(error));
    put(reply, "}");
}

/* The state of the game: time, party, champions, input mode (`movie` while a movie plays), open dialog and panels,
   hand, volumes, end, game mode, automap, cheats. */
static void put_state(Json *reply)
{
    int mode = input_get_mode();
    bool movie = movie_is_playing();
    const char *mode_name = movie ? "movie" : state_json_mode_name(mode);
    put(reply, "{\"ok\":true,\"time\":%u,\"ticks\":%u,\"mode\":\"%s\",\"dialog\":", g_game.game_time,
        g_main_loop.timer_ticks, mode_name);
    if (movie || mode != INPUT_MODE_DIALOG)
        put(reply, "null");
    else
        put(reply, "%d", dialog.id);
    put(reply,
        ",\"party\":{\"map\":%u,\"x\":%u,\"y\":%u,\"dir\":%u},\"champion_count\":%u,\"leader\":%d,\"champions\":[",
        g_game.party_map, g_game.party_x, g_game.party_y, g_game.party_dir, g_game.champion_count,
        (int16_t)g_game.leader);
    for (int i = 0; i < g_game.champion_count && i < CHAMPION_MAX; i++) {
        const Champion *c = champion(i);
        put(reply, "%s{\"name\":", i ? "," : "");
        put_string(reply, c->name, CHAMPION_NAME_SIZE);
        put(reply, ",\"health\":[%d,%d],\"stamina\":[%d,%d],\"mana\":[%d,%d],\"load\":%d}", c->health, c->max_health,
            c->stamina, c->max_stamina, c->mana, c->max_mana, champ_get_load(i));
    }
    put(reply, "],\"inventory\":%d,\"candidate\":%d,\"hand\":", g_ui.inventory - 1, (int)g_ui.candidate - 1);
    if (!g_game.champion_count || g_game.leader_hand == THING_NONE)
        put(reply, "null");
    else {
        const char *name = item_name(g_game.leader_hand);
        put(reply, "{\"thing\":%u,\"name\":", g_game.leader_hand);
        put_string(reply, name, strlen(name));
        put(reply, "}");
    }
    put(reply, ",\"sound_volume\":%u,\"music_volume\":%u,\"end\":%d,\"quit\":%d,\"game_mode\":\"%s\",\"automap\":%d",
        g_game.sound_volume, g_game.music_volume, g_main_loop.end, g_main_loop.quit, g_game_mode_names[g_game_mode],
        advanced_automap_open);
    put(reply, ",\"cheat_immortal\":%d,\"cheat_food\":%d,\"cheat_light\":%d}", g_cheat_immortal, g_cheat_food,
        g_cheat_light);
}

/* Screenshots: the screen with the pointer, as a PNG file. */

#define CRC32_POLYNOMIAL         0xedb88320u /* reflected CRC-32 of PNG */
#define ADLER_MODULUS            65521
#define ZLIB_METHOD              0x78 /* deflate, 32 KB window */
#define ZLIB_FLAGS               0x01 /* no dictionary; header check bits */
#define STORED_BLOCK_MAX         65535
#define PNG_BIT_DEPTH            8
#define PNG_COLOR_RGB            2
#define PNG_SIGNATURE            "\x89PNG\r\n\x1a\n"
#define PNG_SIGNATURE_SIZE       8
#define CHUNK_FIELD_SIZE         4 /* the length, the type, and the CRC of a chunk */
#define IHDR_SIZE                13
#define IHDR_BIT_DEPTH           8 /* the offsets of the fields after the width and the height */
#define IHDR_COLOR_TYPE          9
#define ZLIB_HEADER_SIZE         2
#define STORED_BLOCK_HEADER_SIZE 5 /* the last block flag, the length, and its complement */
#define ADLER_SIZE               4
#define CRC_TABLE_SIZE           256
#define ADLER_SUM_SHIFT          16   /* the Adler-32 checksum: the second sum in the high half */
#define FULL_PATH_SIZE           1024 /* the absolute path of a screenshot, as PATH_MAX of macOS */

static uint32_t crc_table[CRC_TABLE_SIZE];

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < CRC_TABLE_SIZE; i++) {
            uint32_t c = i;
            for (int k = 0; k < BYTE_BIT_COUNT; k++)
                c = c & 1 ? CRC32_POLYNOMIAL ^ c >> 1 : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--)
        crc = crc_table[(crc ^ *p++) & BYTE_MASK] ^ crc >> BYTE_BIT_COUNT;
    return ~crc;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t head[2 * CHUNK_FIELD_SIZE];
    put_be32(head, n);
    memcpy(head + CHUNK_FIELD_SIZE, type, CHUNK_FIELD_SIZE);
    uint32_t crc = crc32_update(crc32_update(0, head + CHUNK_FIELD_SIZE, CHUNK_FIELD_SIZE), data, n);
    fwrite(head, 1, sizeof head, f);
    if (n)
        fwrite(data, 1, n, f);
    put_be32(head, crc);
    fwrite(head, 1, CHUNK_FIELD_SIZE, f);
}

/* Writes screen with the palette as an RGB PNG with stored (not compressed) deflate blocks. */
static int write_png(const char *path, uint8_t screen[SCREEN_H][SCREEN_W])
{
    enum { ROW = 1 + RGB_CHANNELS * SCREEN_W, RAW = ROW * SCREEN_H };
    enum {
        BLOCKS = (RAW + STORED_BLOCK_MAX - 1) / STORED_BLOCK_MAX,
        ZLIB = ZLIB_HEADER_SIZE + RAW + STORED_BLOCK_HEADER_SIZE * BLOCKS + ADLER_SIZE
    };
    static uint8_t raw[RAW], z[ZLIB];
    for (int y = 0; y < SCREEN_H; y++) {
        uint8_t *p = raw + y * ROW;
        *p++ = 0; /* filter: none */
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t argb = video_argb(screen[y][x]);
            *p++ = video_argb_red(argb), *p++ = video_argb_green(argb), *p++ = video_argb_blue(argb);
        }
    }
    uint8_t *q = z;
    *q++ = ZLIB_METHOD, *q++ = ZLIB_FLAGS;
    uint32_t a = 1, b = 0;
    for (size_t done = 0; done < RAW;) {
        size_t n = RAW - done < STORED_BLOCK_MAX ? RAW - done : STORED_BLOCK_MAX;
        *q++ = done + n == RAW;
        put_le16(q, (uint16_t)n);
        put_le16(q + sizeof(uint16_t), (uint16_t)~n);
        q += 2 * sizeof(uint16_t);
        memcpy(q, raw + done, n);
        for (size_t i = 0; i < n; i++)
            a = (a + raw[done + i]) % ADLER_MODULUS, b = (b + a) % ADLER_MODULUS;
        q += n;
        done += n;
    }
    put_be32(q, b << ADLER_SUM_SHIFT | a);
    q += ADLER_SIZE;
    uint8_t ihdr[IHDR_SIZE] = {0};
    put_be32(ihdr, SCREEN_W);
    put_be32(ihdr + CHUNK_FIELD_SIZE, SCREEN_H);
    ihdr[IHDR_BIT_DEPTH] = PNG_BIT_DEPTH;
    ihdr[IHDR_COLOR_TYPE] = PNG_COLOR_RGB;
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    fwrite(PNG_SIGNATURE, 1, PNG_SIGNATURE_SIZE, f);
    png_chunk(f, "IHDR", ihdr, sizeof ihdr);
    png_chunk(f, "IDAT", z, (uint32_t)(q - z));
    png_chunk(f, "IEND", NULL, 0);
    return fclose(f) == 0;
}

/* The screen as the window shows it, with the cursor image at the pointer (none while the pointer is unset). */
static void screenshot(const char *path, Json *reply)
{
    static uint8_t screen[SCREEN_H][SCREEN_W];
    char absolute[FULL_PATH_SIZE];
    memcpy(screen, g_video_fb, sizeof screen);
    if (g_input_mouse_x >= 0 && g_input_mouse_y >= 0)
        input_draw_cursor(&screen[0][0], g_input_mouse_x, g_input_mouse_y);
    if (!write_png(path, screen) || !os_get_full_path(path, absolute, sizeof absolute)) {
        put_error(reply, "cannot write the PNG file");
        return;
    }
    put(reply, "{\"ok\":true,\"path\":");
    put_string(reply, absolute, strlen(absolute));
    put(reply, "}");
}

/* A screen position of a request; 0 with an error reply when it is missing or off the screen. */
static int get_position(const char *request, int *x, int *y, Json *reply)
{
    long px = control_get_int(request, "x", -1), py = control_get_int(request, "y", -1);
    char error[ERROR_SIZE];
    if (px >= 0 && py >= 0 && px < SCREEN_W && py < SCREEN_H) {
        *x = (int)px, *y = (int)py;
        return 1;
    }
    snprintf(error, sizeof error, "x must be 0-%d and y 0-%d", SCREEN_W - 1, SCREEN_H - 1);
    put_error(reply, error);
    return 0;
}

static void handle_key(const char *request, Json *reply)
{
    char name[NAME_SIZE];
    long code = control_get_int(request, "key", 0);
    if (control_get_string(request, "key", name, sizeof name))
        code = control_key_code(name);
    if (code <= 0 || code > KEY_CODE_MAX) {
        put_error(reply, "unknown key");
        return;
    }
    if (!movie_is_playing())
        input_key_down((uint16_t)code); /* a key does not skip a movie */
    put_ok(reply);
}

/* A click, a press, or a release of a mouse button moves the pointer there, like the mouse. A click presses and
   releases the button; a press holds it down until a release. */
static void handle_button(const char *request, int down, int up, Json *reply)
{
    char button[NAME_SIZE] = "left";
    int x, y;
    if (!get_position(request, &x, &y, reply))
        return;
    control_get_string(request, "button", button, sizeof button);
    int right = !strcmp(button, "right");
    if (!right && strcmp(button, "left")) {
        put_error(reply, "button must be left or right");
        return;
    }
    int bit = right ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT;
    if (movie_is_playing()) {
        if (down)
            movie_click(); /* a movie takes only clicks; the game input stays empty */
    } else {
        g_input_mouse_x = x, g_input_mouse_y = y;
        if (down)
            input_mouse_down(x, y, bit);
        if (up)
            input_mouse_up(x, y, bit);
    }
    put_ok(reply);
}

/* A reply {"ok":true,"<key>":<part>} with a part of the game state. */
static void put_part(Json *reply, const char *key, void (*part)(Json *))
{
    json_open(reply, '{');
    json_key(reply, "ok");
    json_bool(reply, 1);
    json_key(reply, key);
    part(reply);
    json_close(reply, '}');
}

/* Writes the error of a turn request, or nothing when the turn started (the reply comes at its end). A batch that
   ended before its first turn gets its reply now. */
static void start_turn_or_fail(const char *error, Json *reply)
{
    if (error)
        put_error(reply, error);
    else if (!turn_is_pending()) {
        turn_reply(reply);
    }
}

static int game_lost; /* a damaged save and a failed restore left no game to play */

/* The input modes in which the game runs a modal loop: save, load, and a new game wait until it closes. */
static bool modal_mode(void)
{
    int mode = input_get_mode();
    return mode == INPUT_MODE_MENU || mode == INPUT_MODE_PICTURE || mode == INPUT_MODE_DIALOG ||
           mode == INPUT_MODE_NAME;
}

/* Replies the error and returns true while a modal loop of the game runs. */
static bool refuse_in_modal_loop(Json *reply)
{
    if (!modal_mode())
        return false;
    put_error(reply, "close the dialog first (act click or act key)");
    return true;
}

/* After a load or a new game: the end of the last game is over. */
static void game_restarts(void)
{
    g_main_loop.end = 0;
    g_session.completed = 0;
    game_lost = 0;
}

/* Loads QUICK.DAT of the save folder; a missing or damaged file keeps the game, a failed restore loses it. */
static void handle_load(Json *reply)
{
    if (refuse_in_modal_loop(reply))
        return;
    int end = g_main_loop.end;
    g_main_loop.end = 0; /* the loaded game starts with a full game step, as after the load of a slot */
    int result = advanced_quick_load();
    if (result == QUICK_LOST)
        game_lost = 1;
    if (result != QUICK_LOADED) {
        g_main_loop.end = end;
        put_error(reply, result == QUICK_MISSING   ? "no saved game (use save first)"
                         : result == QUICK_BLOCKED ? "cannot write QUICK.TMP: nothing loaded"
                                                   : "the saved game is damaged");
        return;
    }
    game_restarts();
    put_part(reply, "state", state_json_state);
}

/* Starts a new game from DUNGEON.DAT, as the menu choice NEW GAME does. */
static void handle_new_game(Json *reply)
{
    if (refuse_in_modal_loop(reply))
        return;
    if (!session_start_new_game()) {
        game_lost = 1;
        put_error(reply, "cannot start the new game");
        return;
    }
    game_restarts();
    put_part(reply, "state", state_json_state);
}

/* Writes the game to QUICK.DAT of the save folder; the old file stays on an error. */
static void handle_save(Json *reply)
{
    if (refuse_in_modal_loop(reply))
        return;
    if (g_ui.candidate)
        put_error(reply, "answer the candidate first (act recruit)");
    else if (!advanced_quick_save())
        put_error(reply, "cannot write the save file");
    else
        put_ok(reply);
}

/* The commands of a running game in the turn-based mode: the parts of the game state, and after a game over only
   those; save and the turns. Returns false for a command that the other modes share (state, screenshot) or do not
   know. */
static bool handle_turnbased_game_request(const char *request, const char *cmd, Json *reply)
{
    static const char *const turns[] = {"act", "act_many", "wait", "save"};
    if (!strcmp(cmd, "get_state"))
        put_part(reply, "state", state_json_state);
    else if (!strcmp(cmd, "get_view"))
        put_part(reply, "view", state_json_view);
    else if (!strcmp(cmd, "get_map"))
        put_part(reply, "map", state_json_map);
    else {
        for (size_t i = 0; i < sizeof turns / sizeof turns[0] && state_json_game_over(); i++)
            if (!strcmp(cmd, turns[i])) {
                put_error(reply, "game over");
                return true;
            }
        if (!strcmp(cmd, "save"))
            handle_save(reply);
        else if (!strcmp(cmd, "act"))
            start_turn_or_fail(turn_act(request), reply);
        else if (!strcmp(cmd, "act_many"))
            start_turn_or_fail(turn_act_many(request), reply);
        else if (!strcmp(cmd, "wait"))
            start_turn_or_fail(turn_wait(request), reply);
        else
            return false;
    }
    return true;
}

/* The commands of the turn-based mode: load and new_game also after a game over or a lost game, the refusal of every
   other command after a lost game and of the direct input commands. Returns false for quit, state, screenshot, and
   the commands that no mode knows. */
static bool handle_turnbased_request(const char *request, const char *cmd, Json *reply)
{
    static const char *const direct[] = {"key", "click", "press", "release", "command", "move"};
    if (!strcmp(cmd, "quit"))
        return false;
    if (!strcmp(cmd, "load")) {
        handle_load(reply);
        return true;
    }
    if (!strcmp(cmd, "new_game")) {
        handle_new_game(reply);
        return true;
    }
    if (game_lost) {
        put_error(reply, "no game is running: use load or new_game");
        return true;
    }
    for (size_t i = 0; i < sizeof direct / sizeof direct[0]; i++)
        if (!strcmp(cmd, direct[i])) {
            put_error(reply, "use act in turnbased mode");
            return true;
        }
    return handle_turnbased_game_request(request, cmd, reply);
}

static void handle_request(const char *request, Json *reply)
{
    char cmd[NAME_SIZE], path[PATH_MAX];
    int x, y;
    if (!control_get_string(request, "cmd", cmd, sizeof cmd)) {
        put_error(reply, "missing cmd");
    } else if (g_game_mode == GAME_MODE_TURNBASED && handle_turnbased_request(request, cmd, reply)) {
        return;
    } else if (!strcmp(cmd, "state")) {
        put_state(reply);
    } else if (!strcmp(cmd, "key")) {
        handle_key(request, reply);
    } else if (!strcmp(cmd, "click")) {
        handle_button(request, 1, 1, reply);
    } else if (!strcmp(cmd, "press")) {
        handle_button(request, 1, 0, reply);
    } else if (!strcmp(cmd, "release")) {
        handle_button(request, 0, 1, reply);
    } else if (!strcmp(cmd, "move")) {
        if (!get_position(request, &x, &y, reply))
            return;
        g_input_mouse_x = x, g_input_mouse_y = y;
        put_ok(reply);
    } else if (!strcmp(cmd, "command")) {
        long id = control_get_int(request, "id", -1);
        if (id < 0 || id > KEY_CODE_MAX)
            put_error(reply, "missing id");
        else {
            if (!movie_is_playing())
                input_simulate_command_click((uint16_t)id);
            put_ok(reply);
        }
    } else if (!strcmp(cmd, "wait")) {
        long ticks = control_get_int(request, "ticks", 0);
        if (ticks < 1 || ticks > WAIT_MAX_TICKS) {
            char error[ERROR_SIZE];
            snprintf(error, sizeof error, "ticks must be 1-%d", WAIT_MAX_TICKS);
            put_error(reply, error);
        } else {
            waiting.pending = 1;
            waiting.start = g_main_loop.timer_ticks;
            waiting.ticks = (uint32_t)ticks;
        }
    } else if (!strcmp(cmd, "screenshot")) {
        if (!control_get_string(request, "path", path, sizeof path))
            put_error(reply, "missing path");
        else
            screenshot(path, reply);
    } else if (!strcmp(cmd, "quit")) {
        g_main_loop.quit = g_main_loop.closed = 1;
        put_ok(reply);
    } else {
        put_error(reply, "unknown cmd");
    }
}

bool control_handle_line(const char *line, Json *reply)
{
    handle_request(line, reply);
    return reply->len > 0;
}

bool control_end_turn(Json *reply)
{
    if (waiting.pending) {
        uint32_t ran = g_main_loop.timer_ticks - waiting.start;
        if (ran < waiting.ticks)
            return false;
        waiting.pending = 0;
        put(reply, "{\"ok\":true,\"ticks\":%u}", ran);
        return true;
    }
    if (!turn_poll())
        return false;
    turn_reply(reply);
    return true;
}

bool control_is_turn_pending(void)
{
    return turn_is_pending();
}

void control_drop_client(void)
{
    waiting.pending = 0;
    if (turn_is_pending())
        turn_drop_reply();
}

void control_reset(void)
{
    game_lost = 0;
}

void control_frame(void)
{
    if (!turn_is_pending())
        return;
    uint32_t time = g_game.game_time;
    for (int i = 0; i < STEP_TICKS && g_game.game_time == time && !g_main_loop.end && !g_main_loop.quit; i++)
        play_tick();
}
