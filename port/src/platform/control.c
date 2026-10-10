#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "control.h"
#include "advanced.h"
#include "champion.h"
#include "dialog.h"
#include "dungeon.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "le.h"
#include "movie.h"
#include "save.h"
#include "ui.h"
#include "video.h"

#define INPUT_SIZE 4096               /* a request line up to its newline */
#define REPLY_SIZE 4096
#define REPLY_RESERVE 2               /* the newline and the end mark of a reply */
#define ERROR_SIZE 64
#define NAME_SIZE 32
#define WAIT_MAX_TICKS 6000
#define SCAN_CODE_COUNT 256           /* the low byte of a key code */
#define KEY_CODE_MAX 0xffff
#define HEX_DIGITS 4                  /* the digits of a \uXXXX escape */
#define HEX_BASE 16
#define UNICODE_ESCAPE_PREFIX 2       /* the backslash and the u of \uXXXX */
#define UTF8_ONE_BYTE_END 0x80        /* the first code point of 2, 3, and 4 UTF-8 bytes */
#define UTF8_TWO_BYTES_END 0x800
#define UTF8_THREE_BYTES_END 0x10000
#define UTF8_CONTINUATION 0x80        /* the marker and the payload of a continuation byte */
#define UTF8_PAYLOAD_MASK 0x3f
#define UTF8_PAYLOAD_BITS 6
#define UTF8_LEAD_MARKERS 0xf00       /* shifted right by the byte count: the high bits of a lead byte */
#define UTF8_MAX_BYTES 4
#define SURROGATE_HIGH 0xd800
#define SURROGATE_LOW 0xdc00
#define SURROGATE_END 0xe000
#define SURROGATE_BITS 10

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL       /* a closed client does not raise SIGPIPE (Linux) */
#else
#define SEND_FLAGS 0                  /* SO_NOSIGPIPE does the same (macOS) */
#endif

static int listen_fd = -1, client_fd = -1;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static char input[INPUT_SIZE];
static size_t input_length;
static int discarding;                /* the rest of a too long line goes up to its newline */
static char reply[REPLY_SIZE];
static size_t reply_length;
static int reply_overflow;

/* A wait request: the reply goes out when main_loop.timer_ticks reached start + ticks. */
static struct {
    int pending;
    uint32_t start, ticks;
} waiting;

/* The input modes by number (the roots of the input tree in input.c). */
static const char *const mode_names[] = {
    [INPUT_MODE_MENU] = "menu", [INPUT_MODE_PICTURE] = "picture", [INPUT_MODE_SLEEP] = "sleep", [INPUT_MODE_PAUSE] = "pause",
    [INPUT_MODE_DIALOG] = "dialog", [INPUT_MODE_MOVE] = "move", [INPUT_MODE_PUSH] = "push", [INPUT_MODE_RECRUIT] = "recruit",
    [INPUT_MODE_INVENTORY] = "inventory", [INPUT_MODE_NAME] = "name",
};
#define MODE_COUNT ((int)(sizeof mode_names / sizeof mode_names[0]))

/* PC scan codes of the keys without a character; the arrows give the keypad codes that main.c gives them. */
static const struct { const char *name; uint16_t code; } key_names[] = {
    {"esc", 0x01}, {"backspace", 0x0e}, {"tab", 0x0f}, {"enter", 0x1c}, {"space", 0x39},
    {"f1", 0x3b}, {"f2", 0x3c}, {"f3", 0x3d}, {"f4", 0x3e}, {"f5", 0x3f}, {"f6", 0x40}, {"f7", 0x41},
    {"f8", 0x42}, {"f9", 0x43}, {"f10", 0x44},
    {"kp7", 0x47}, {"kp8", 0x48}, {"kp9", 0x49}, {"kp-", 0x4a}, {"kp4", 0x4b}, {"kp5", 0x4c}, {"kp6", 0x4d},
    {"kp+", 0x4e}, {"kp1", 0x4f}, {"kp2", 0x50}, {"kp3", 0x51}, {"kp0", 0x52}, {"kp.", 0x53},
    {"up", KEYPAD_FORWARD}, {"down", KEYPAD_BACK}, {"left", KEYPAD_TURN_LEFT}, {"right", KEYPAD_TURN_RIGHT},
    {"shift+left", KEYPAD_LEFT}, {"shift+right", KEYPAD_RIGHT},
};
static const struct { const char *prefix; uint16_t bit; } key_modifiers[] = {
    {"shift+", KEY_SHIFT}, {"ctrl+", KEY_CONTROL}, {"alt+", KEY_ALT},
};

/* The key code of a key name: a name of key_names, a character of the US layout, with modifier prefixes; 0 none. */
static uint16_t key_code(const char *name)
{
    for (size_t i = 0; i < sizeof key_names / sizeof key_names[0]; i++)
        if (!strcmp(name, key_names[i].name)) return key_names[i].code;
    for (size_t i = 0; i < sizeof key_modifiers / sizeof key_modifiers[0]; i++) {
        size_t n = strlen(key_modifiers[i].prefix);
        if (strncmp(name, key_modifiers[i].prefix, n)) continue;
        uint16_t code = key_code(name + n);
        return code ? code | key_modifiers[i].bit : 0;
    }
    if (!name[0] || name[1]) return 0;
    for (uint16_t scan = 1; scan < SCAN_CODE_COUNT; scan++) {
        if (kbd_key_to_ascii(scan) == name[0]) return scan;
        if (kbd_key_to_ascii(scan | KEY_SHIFT) == name[0]) return scan | KEY_SHIFT;
    }
    return 0;
}

/* Requests: a flat search for "key": value. */

static const char *find_value(const char *line, const char *key)
{
    char pattern[NAME_SIZE + 2];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    for (const char *p = strstr(line, pattern); p; p = strstr(p, pattern)) {
        p += strlen(pattern);
        while (*p == ' ') p++;
        if (*p != ':') continue; /* a string value equal to the key, for example "cmd": "key" */
        for (p++; *p == ' ';) p++;
        return p;
    }
    return NULL;
}

/* Four hex digits at p, -1 when they are not. */
static long hex4(const char *p)
{
    long value = 0;
    for (int i = 0; i < HEX_DIGITS; i++) {
        if (!isxdigit((unsigned char)p[i])) return -1;
        value = value * HEX_BASE + (isdigit((unsigned char)p[i]) ? p[i] - '0' : tolower((unsigned char)p[i]) - 'a' + DECIMAL_BASE);  /* a to f follow the ten digits */
    }
    return value;
}

/* Appends code point c as UTF-8; 0 when it does not fit. */
static int put_utf8(char *out, size_t size, size_t *n, long c)
{
    char bytes[UTF8_MAX_BYTES];
    int count = c < UTF8_ONE_BYTE_END ? 1 : c < UTF8_TWO_BYTES_END ? 2 : c < UTF8_THREE_BYTES_END ? 3 : UTF8_MAX_BYTES;
    if (count == 1) bytes[0] = (char)c;
    else {
        for (int i = count - 1; i > 0; i--, c >>= UTF8_PAYLOAD_BITS)
            bytes[i] = (char)(UTF8_CONTINUATION | (c & UTF8_PAYLOAD_MASK));
        bytes[0] = (char)((UTF8_LEAD_MARKERS >> count) | c);
    }
    if (*n + count >= size) return 0;
    memcpy(out + *n, bytes, count);
    *n += count;
    return 1;
}

/* The JSON string value of key, decoded (escapes, \uXXXX to UTF-8); 0 when it is missing, bad, too long, or has a
   NUL character. */
static int get_string(const char *line, const char *key, char *out, size_t size)
{
    static const char escapes[] = "\"\"\\\\//b\bf\fn\nr\rt\t";
    const char *p = find_value(line, key);
    size_t n = 0;
    if (!p || *p++ != '"') return 0;
    while (*p != '"') {
        long c = (unsigned char)*p++;
        if (!c) return 0;
        if (c == '\\') {
            const char *e = *p ? strchr(escapes, *p) : NULL;
            if (e && (e - escapes) % 2 == 0) c = e[1], p++;
            else if (*p++ != 'u' || (c = hex4(p)) < 0) return 0;
            else {
                p += HEX_DIGITS;
                if (c >= SURROGATE_HIGH && c < SURROGATE_LOW && p[0] == '\\' && p[1] == 'u') { /* a surrogate pair */
                    long low = hex4(p + UNICODE_ESCAPE_PREFIX);
                    if (low < SURROGATE_LOW || low >= SURROGATE_END) return 0;
                    c = UTF8_THREE_BYTES_END + ((c - SURROGATE_HIGH) << SURROGATE_BITS) + (low - SURROGATE_LOW);
                    p += UNICODE_ESCAPE_PREFIX + HEX_DIGITS;
                }
                if (!c || (c >= SURROGATE_HIGH && c < SURROGATE_END)) return 0;
                if (!put_utf8(out, size, &n, c)) return 0;
                continue;
            }
        }
        if (n + 1 >= size) return 0;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return 1;
}

static long get_int(const char *line, const char *key, long missing)
{
    const char *p = find_value(line, key);
    char *end;
    if (!p) return missing;
    long value = strtol(p, &end, DECIMAL_BASE);
    return end == p ? missing : value;
}

/* Replies: JSON text in reply. */

/* Appends to reply; a reply that does not fit becomes an error reply in send_reply. */
static void put(const char *format, ...)
{
    size_t room = sizeof reply - reply_length - REPLY_RESERVE + 1;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(reply + reply_length, room, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= room) reply_overflow = 1;
    else reply_length += (size_t)n;
}

/* A JSON string of the first n bytes of text (up to its end mark); bytes from 0x80 go as they are (UTF-8). The game
   texts of GRAPHICS.DAT (701 entries) are ASCII, so only request text can bring other bytes. */
static void put_string(const char *text, size_t n)
{
    put("\"");
    for (size_t i = 0; i < n && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') put("\\%c", c);
        else if (c < ' ') put("\\u%04x", c);
        else put("%c", c);
    }
    put("\"");
}

static void close_client(void)
{
    if (client_fd >= 0) close(client_fd);
    client_fd = -1;
    input_length = 0;
    discarding = 0;
    waiting.pending = 0;
}

/* Sends reply as one line and clears it. */
static void send_reply(void)
{
    if (reply_overflow) {
        reply_overflow = 0;
        reply_length = 0;
        put("{\"ok\":false,\"error\":\"reply too long\"}");
    }
    reply[reply_length++] = '\n';
    for (size_t sent = 0; client_fd >= 0 && sent < reply_length;) {
        ssize_t n = send(client_fd, reply + sent, reply_length - sent, SEND_FLAGS);
        if (n <= 0) close_client();
        else sent += (size_t)n;
    }
    reply_length = 0;
}

static void send_ok(void)
{
    put("{\"ok\":true}");
    send_reply();
}

static void send_error(const char *error)
{
    put("{\"ok\":false,\"error\":");
    put_string(error, strlen(error));
    put("}");
    send_reply();
}

/* The state of the game: time, party, champions, input mode (`movie` while a movie plays), open dialog and panels,
   hand, volumes, end, game mode, automap, cheats. */
static void send_state(void)
{
    int mode = input_get_mode();
    bool movie = movie_is_playing();
    const char *mode_name = movie ? "movie" : mode >= 0 && mode < MODE_COUNT ? mode_names[mode] : "unknown";
    put("{\"ok\":true,\"time\":%u,\"ticks\":%u,\"mode\":\"%s\",\"dialog\":", game.game_time, main_loop.timer_ticks,
        mode_name);
    if (movie || mode != INPUT_MODE_DIALOG) put("null");
    else put("%d", dialog.id);
    put(",\"party\":{\"map\":%u,\"x\":%u,\"y\":%u,\"dir\":%u},\"champion_count\":%u,\"leader\":%d,\"champions\":[",
        game.party_map, game.party_x, game.party_y, game.party_dir, game.champion_count, (int16_t)game.leader);
    for (int i = 0; i < game.champion_count && i < CHAMPION_MAX; i++) {
        const Champion *c = champion(i);
        put("%s{\"name\":", i ? "," : "");
        put_string(c->name, CHAMPION_NAME_SIZE);
        put(",\"health\":[%d,%d],\"stamina\":[%d,%d],\"mana\":[%d,%d],\"load\":%d}", c->health, c->max_health,
            c->stamina, c->max_stamina, c->mana, c->max_mana, champ_get_load(i));
    }
    put("],\"inventory\":%d,\"candidate\":%d,\"hand\":", ui.inventory - 1, (int)ui.candidate - 1);
    if (!game.champion_count || game.leader_hand == THING_NONE) put("null");
    else {
        const char *name = item_name(game.leader_hand);
        put("{\"thing\":%u,\"name\":", game.leader_hand);
        put_string(name, strlen(name));
        put("}");
    }
    put(",\"sound_volume\":%u,\"music_volume\":%u,\"end\":%d,\"quit\":%d,\"game_mode\":\"%s\",\"automap\":%d", game.sound_volume,
        game.music_volume, main_loop.end, main_loop.quit, game_mode_names[game_mode], advanced_automap_open);
    put(",\"cheat_immortal\":%d,\"cheat_food\":%d,\"cheat_light\":%d}", cheat_immortal, cheat_food, cheat_light);
    send_reply();
}

/* Screenshots: the screen with the pointer, as a PNG file. */

#define CRC32_POLYNOMIAL 0xedb88320u   /* reflected CRC-32 of PNG */
#define ADLER_MODULUS 65521
#define ZLIB_METHOD 0x78               /* deflate, 32 KB window */
#define ZLIB_FLAGS 0x01                /* no dictionary; header check bits */
#define STORED_BLOCK_MAX 65535
#define PNG_BIT_DEPTH 8
#define PNG_COLOR_RGB 2
#define PNG_SIGNATURE "\x89PNG\r\n\x1a\n"
#define PNG_SIGNATURE_SIZE 8
#define CHUNK_FIELD_SIZE 4             /* the length, the type, and the CRC of a chunk */
#define IHDR_SIZE 13
#define IHDR_BIT_DEPTH 8               /* the offsets of the fields after the width and the height */
#define IHDR_COLOR_TYPE 9
#define ZLIB_HEADER_SIZE 2
#define STORED_BLOCK_HEADER_SIZE 5     /* the last block flag, the length, and its complement */
#define ADLER_SIZE 4
#define CRC_TABLE_SIZE 256
#define ADLER_SUM_SHIFT 16             /* the Adler-32 checksum: the second sum in the high half */
#define SOCKET_DIR_MODE 0700

static uint32_t crc_table[CRC_TABLE_SIZE];

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < CRC_TABLE_SIZE; i++) {
            uint32_t c = i;
            for (int k = 0; k < BYTE_BITS; k++) c = c & 1 ? CRC32_POLYNOMIAL ^ c >> 1 : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & BYTE_MASK] ^ crc >> BYTE_BITS;
    return ~crc;
}

/* PNG numbers are big-endian. */
static void be32(uint8_t *p, uint32_t v)
{
    for (int i = (int)sizeof v - 1; i >= 0; i--, v >>= BYTE_BITS) p[i] = (uint8_t)v;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t head[2 * CHUNK_FIELD_SIZE];
    be32(head, n);
    memcpy(head + CHUNK_FIELD_SIZE, type, CHUNK_FIELD_SIZE);
    uint32_t crc = crc32_update(crc32_update(0, head + CHUNK_FIELD_SIZE, CHUNK_FIELD_SIZE), data, n);
    fwrite(head, 1, sizeof head, f);
    if (n) fwrite(data, 1, n, f);
    be32(head, crc);
    fwrite(head, 1, CHUNK_FIELD_SIZE, f);
}

/* Writes screen with the palette as an RGB PNG with stored (not compressed) deflate blocks. */
static int write_png(const char *path, uint8_t screen[SCREEN_H][SCREEN_W])
{
    enum { ROW = 1 + RGB_CHANNELS * SCREEN_W, RAW = ROW * SCREEN_H };
    enum { BLOCKS = (RAW + STORED_BLOCK_MAX - 1) / STORED_BLOCK_MAX, ZLIB = ZLIB_HEADER_SIZE + RAW + STORED_BLOCK_HEADER_SIZE * BLOCKS + ADLER_SIZE };
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
        for (size_t i = 0; i < n; i++) a = (a + raw[done + i]) % ADLER_MODULUS, b = (b + a) % ADLER_MODULUS;
        q += n;
        done += n;
    }
    be32(q, b << ADLER_SUM_SHIFT | a);
    q += ADLER_SIZE;
    uint8_t ihdr[IHDR_SIZE] = { 0 };
    be32(ihdr, SCREEN_W);
    be32(ihdr + CHUNK_FIELD_SIZE, SCREEN_H);
    ihdr[IHDR_BIT_DEPTH] = PNG_BIT_DEPTH;
    ihdr[IHDR_COLOR_TYPE] = PNG_COLOR_RGB;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(PNG_SIGNATURE, 1, PNG_SIGNATURE_SIZE, f);
    png_chunk(f, "IHDR", ihdr, sizeof ihdr);
    png_chunk(f, "IDAT", z, (uint32_t)(q - z));
    png_chunk(f, "IEND", NULL, 0);
    return fclose(f) == 0;
}

/* The screen as the window shows it, with the cursor image at the pointer (none while the pointer is unset). */
static void screenshot(const char *path)
{
    static uint8_t screen[SCREEN_H][SCREEN_W];
    char absolute[PATH_MAX];
    memcpy(screen, fb, sizeof screen);
    if (input_mouse_x >= 0 && input_mouse_y >= 0) input_draw_cursor(&screen[0][0], input_mouse_x, input_mouse_y);
    if (!write_png(path, screen) || !realpath(path, absolute)) {
        send_error("cannot write the PNG file");
        return;
    }
    put("{\"ok\":true,\"path\":");
    put_string(absolute, strlen(absolute));
    put("}");
    send_reply();
}

/* A screen position of a request; 0 with an error reply when it is missing or off the screen. */
static int get_position(const char *request, int *x, int *y)
{
    long px = get_int(request, "x", -1), py = get_int(request, "y", -1);
    char error[ERROR_SIZE];
    if (px >= 0 && py >= 0 && px < SCREEN_W && py < SCREEN_H) {
        *x = (int)px, *y = (int)py;
        return 1;
    }
    snprintf(error, sizeof error, "x must be 0-%d and y 0-%d", SCREEN_W - 1, SCREEN_H - 1);
    send_error(error);
    return 0;
}

static void handle_key(const char *request)
{
    char name[NAME_SIZE];
    long code = get_int(request, "key", 0);
    if (get_string(request, "key", name, sizeof name)) code = key_code(name);
    if (code <= 0 || code > KEY_CODE_MAX) {
        send_error("unknown key");
        return;
    }
    if (!movie_is_playing()) input_key_down((uint16_t)code);  /* a key does not skip a movie */
    send_ok();
}

/* A click, a press, or a release of a mouse button moves the pointer there, like the mouse. A click presses and
   releases the button; a press holds it down until a release. */
static void handle_button(const char *request, int down, int up)
{
    char button[NAME_SIZE] = "left";
    int x, y;
    if (!get_position(request, &x, &y)) return;
    get_string(request, "button", button, sizeof button);
    int right = !strcmp(button, "right");
    if (!right && strcmp(button, "left")) {
        send_error("button must be left or right");
        return;
    }
    int bit = right ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT;
    if (movie_is_playing()) {
        if (down) movie_click();  /* a movie takes only clicks; the game input stays empty */
    } else {
        input_mouse_x = x, input_mouse_y = y;
        if (down) input_mouse_down(x, y, bit);
        if (up) input_mouse_up(x, y, bit);
    }
    send_ok();
}

static void handle_request(const char *request)
{
    char cmd[NAME_SIZE], path[PATH_MAX];
    int x, y;
    if (!get_string(request, "cmd", cmd, sizeof cmd)) {
        send_error("missing cmd");
    } else if (!strcmp(cmd, "state")) {
        send_state();
    } else if (!strcmp(cmd, "key")) {
        handle_key(request);
    } else if (!strcmp(cmd, "click")) {
        handle_button(request, 1, 1);
    } else if (!strcmp(cmd, "press")) {
        handle_button(request, 1, 0);
    } else if (!strcmp(cmd, "release")) {
        handle_button(request, 0, 1);
    } else if (!strcmp(cmd, "move")) {
        if (!get_position(request, &x, &y)) return;
        input_mouse_x = x, input_mouse_y = y;
        send_ok();
    } else if (!strcmp(cmd, "command")) {
        long id = get_int(request, "id", -1);
        if (id < 0 || id > KEY_CODE_MAX) send_error("missing id");
        else {
            if (!movie_is_playing()) input_simulate_command_click((uint16_t)id);
            send_ok();
        }
    } else if (!strcmp(cmd, "wait")) {
        long ticks = get_int(request, "ticks", 0);
        if (ticks < 1 || ticks > WAIT_MAX_TICKS) {
            char error[ERROR_SIZE];
            snprintf(error, sizeof error, "ticks must be 1-%d", WAIT_MAX_TICKS);
            send_error(error);
        } else {
            waiting.pending = 1;
            waiting.start = main_loop.timer_ticks;
            waiting.ticks = (uint32_t)ticks;
        }
    } else if (!strcmp(cmd, "screenshot")) {
        if (!get_string(request, "path", path, sizeof path)) send_error("missing path");
        else screenshot(path);
    } else if (!strcmp(cmd, "quit")) {
        main_loop.quit = main_loop.closed = 1;
        send_ok();
    } else {
        send_error("unknown cmd");
    }
}

/* A connection that does not raise SIGPIPE when the client is gone, with blocking writes (reads use MSG_DONTWAIT). */
static void prepare_connection(int fd)
{
    fcntl(fd, F_SETFL, 0);
#ifdef SO_NOSIGPIPE
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}

/* Takes the waiting connections: the first becomes the client, the others get a busy error. */
static void accept_clients(void)
{
    static const char busy[] = "{\"ok\":false,\"error\":\"busy\"}\n";
    for (int fd; (fd = accept(listen_fd, NULL, NULL)) >= 0;) {
        prepare_connection(fd);
        if (client_fd < 0) {
            client_fd = fd;
            continue;
        }
        send(fd, busy, sizeof busy - 1, SEND_FLAGS);
        close(fd);
    }
}

/* Reads the bytes of the client and answers its complete lines; a too long line gets one error reply. */
static void read_requests(void)
{
    if (client_fd < 0) return;
    ssize_t n = recv(client_fd, input + input_length, sizeof input - input_length, MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close_client();
        return;
    }
    if (n > 0) input_length += (size_t)n;
    char *end;
    while (client_fd >= 0 && !waiting.pending && (end = memchr(input, '\n', input_length))) {
        size_t used = (size_t)(end + 1 - input);
        *end = 0;
        if (discarding) discarding = 0;
        else if (memchr(input, 0, used - 1)) send_error("request line has a NUL byte");
        else if (end > input) handle_request(input);
        if (client_fd < 0) return;
        memmove(input, end + 1, input_length - used);
        input_length -= used;
    }
    if (input_length == sizeof input) {
        if (!discarding) send_error("request line too long");
        discarding = 1;
        input_length = 0;
    }
}

int control_open(const char *path)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    struct stat old;
    char dir[sizeof socket_path];
    if (strlen(path) >= sizeof address.sun_path) return 0;
    if (!lstat(path, &old) && !S_ISSOCK(old.st_mode)) return 0; /* never remove another file */
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = 0;
        mkdir(dir, SOCKET_DIR_MODE);
    }
    snprintf(address.sun_path, sizeof address.sun_path, "%s", path);
    snprintf(socket_path, sizeof socket_path, "%s", path);
    unlink(path);
    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) return 0;
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof address) || listen(listen_fd, 1) ||
        fcntl(listen_fd, F_SETFL, O_NONBLOCK)) {
        control_close();
        return 0;
    }
    return 1;
}

void control_poll(void)
{
    if (listen_fd < 0) return;
    if (waiting.pending) {
        char next;
        ssize_t n = recv(client_fd, &next, 1, MSG_PEEK | MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) close_client(); /* the client left */
    }
    if (waiting.pending) {
        uint32_t ran = main_loop.timer_ticks - waiting.start;
        if (ran >= waiting.ticks) {
            waiting.pending = 0;
            put("{\"ok\":true,\"ticks\":%u}", ran);
            send_reply();
        }
    }
    if (!waiting.pending) read_requests();
    accept_clients();
}

void control_close(void)
{
    close_client();
    if (listen_fd >= 0) {
        close(listen_fd);
        unlink(socket_path);
    }
    listen_fd = -1;
}
