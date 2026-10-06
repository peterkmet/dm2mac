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
#include "champion.h"
#include "dialog.h"
#include "dungeon.h"
#include "game.h"
#include "input.h"
#include "item.h"
#include "save.h"
#include "ui.h"
#include "video.h"

#define INPUT_SIZE 4096               /* longest request line */
#define REPLY_SIZE 4096
#define NAME_SIZE 32
#define WAIT_MAX_TICKS 6000
#define SCAN_CODE_COUNT 256           /* the low byte of a key code */
#define KEY_CODE_MAX 0xffff

#ifdef MSG_NOSIGNAL
#define SEND_FLAGS MSG_NOSIGNAL       /* a closed client does not raise SIGPIPE (Linux) */
#else
#define SEND_FLAGS 0                  /* SO_NOSIGPIPE does the same (macOS) */
#endif

static int listen_fd = -1, client_fd = -1;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static char input[INPUT_SIZE];
static size_t input_length;
static char reply[REPLY_SIZE];
static size_t reply_length;

/* A wait request: the reply goes out when main_loop.timer_ticks reached start + ticks. */
static struct {
    int pending;
    uint32_t start, ticks;
} waiting;

/* The input modes by number (the roots of the input tree in input.c). */
static const char *const mode_names[] = {
    "menu", "picture", "sleep", "pause", "dialog", "move", "push", "recruit", "inventory", "name",
};
#define MODE_COUNT ((int)(sizeof mode_names / sizeof mode_names[0]))

/* PC scan codes of the keys without a character; the arrows give the keypad codes that main.c gives them. */
static const struct { const char *name; uint16_t code; } key_names[] = {
    {"esc", 0x01}, {"backspace", 0x0e}, {"tab", 0x0f}, {"enter", 0x1c}, {"space", 0x39},
    {"f1", 0x3b}, {"f2", 0x3c}, {"f3", 0x3d}, {"f4", 0x3e}, {"f5", 0x3f}, {"f6", 0x40}, {"f7", 0x41},
    {"f8", 0x42}, {"f9", 0x43}, {"f10", 0x44},
    {"kp7", 0x47}, {"kp8", 0x48}, {"kp9", 0x49}, {"kp-", 0x4a}, {"kp4", 0x4b}, {"kp5", 0x4c}, {"kp6", 0x4d},
    {"kp+", 0x4e}, {"kp1", 0x4f}, {"kp2", 0x50}, {"kp3", 0x51}, {"kp0", 0x52}, {"kp.", 0x53},
    {"up", 0x4c}, {"down", 0x50}, {"left", 0x4b}, {"right", 0x4d}, {"shift+left", 0x4f}, {"shift+right", 0x51},
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

static int get_string(const char *line, const char *key, char *out, size_t size)
{
    const char *p = find_value(line, key);
    size_t n = 0;
    if (!p || *p++ != '"') return 0;
    while (*p && *p != '"' && n + 1 < size) out[n++] = *p++;
    out[n] = 0;
    return 1;
}

static long get_int(const char *line, const char *key, long missing)
{
    const char *p = find_value(line, key);
    char *end;
    if (!p) return missing;
    long value = strtol(p, &end, 10);
    return end == p ? missing : value;
}

/* Replies: JSON text in reply. */

static void put(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(reply + reply_length, sizeof reply - reply_length, format, args);
    va_end(args);
    if (n > 0) reply_length += (size_t)n < sizeof reply - reply_length ? (size_t)n : sizeof reply - 1 - reply_length;
}

/* A JSON string of the first n characters of text (up to its end mark). */
static void put_string(const char *text, size_t n)
{
    put("\"");
    for (size_t i = 0; i < n && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') put("\\%c", c);
        else if (c < ' ' || c > '~') put("\\u%04x", c);
        else put("%c", c);
    }
    put("\"");
}

static void close_client(void)
{
    if (client_fd >= 0) close(client_fd);
    client_fd = -1;
    input_length = 0;
    waiting.pending = 0;
}

/* Sends reply as one line and clears it. */
static void send_reply(void)
{
    put("\n");
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

/* The state of the game: time, party, champions, input mode, open dialog and panels, hand, volumes, end. */
static void send_state(void)
{
    int mode = input_get_mode();
    const char *mode_name = mode >= 0 && mode < MODE_COUNT ? mode_names[mode] : "unknown";
    put("{\"ok\":true,\"time\":%u,\"ticks\":%u,\"mode\":\"%s\",\"dialog\":", game.game_time, main_loop.timer_ticks,
        mode_name);
    if (strcmp(mode_name, "dialog")) put("null");
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
    put(",\"sound_volume\":%u,\"music_volume\":%u,\"end\":%d,\"quit\":%d}", game.sound_volume, game.music_volume,
        main_loop.end, main_loop.quit);
    send_reply();
}

/* Screenshots: the screen with the pointer, as a PNG file. */

#define CRC32_POLYNOMIAL 0xedb88320u   /* reflected CRC-32 of PNG */
#define ADLER_MODULUS 65521
#define ZLIB_METHOD 0x78               /* deflate, 32 KB window */
#define ZLIB_FLAGS 0x01                /* no dictionary; header check bits */
#define STORED_BLOCK_MAX 65535
#define BYTE_MASK 0xff
#define PNG_BIT_DEPTH 8
#define PNG_COLOR_RGB 2

static uint32_t crc_table[256];

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? CRC32_POLYNOMIAL ^ c >> 1 : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *p++) & BYTE_MASK] ^ crc >> 8;
    return ~crc;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
    uint8_t head[8];
    be32(head, n);
    memcpy(head + 4, type, 4);
    uint32_t crc = crc32_update(crc32_update(0, head + 4, 4), data, n);
    fwrite(head, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    be32(head, crc);
    fwrite(head, 1, 4, f);
}

/* Writes screen with the palette as an RGB PNG with stored (not compressed) deflate blocks. */
static int write_png(const char *path, uint8_t screen[SCREEN_H][SCREEN_W])
{
    enum { ROW = 1 + RGB_CHANNELS * SCREEN_W, RAW = ROW * SCREEN_H };
    enum { BLOCKS = (RAW + STORED_BLOCK_MAX - 1) / STORED_BLOCK_MAX, ZLIB = 2 + RAW + 5 * BLOCKS + 4 };
    static uint8_t raw[RAW], z[ZLIB];
    for (int y = 0; y < SCREEN_H; y++) {
        uint8_t *p = raw + y * ROW;
        *p++ = 0; /* filter: none */
        for (int x = 0; x < SCREEN_W; x++) {
            uint32_t argb = video_argb(screen[y][x]);
            *p++ = (uint8_t)(argb >> 16), *p++ = (uint8_t)(argb >> 8), *p++ = (uint8_t)argb;
        }
    }
    uint8_t *q = z;
    *q++ = ZLIB_METHOD, *q++ = ZLIB_FLAGS;
    uint32_t a = 1, b = 0;
    for (size_t done = 0; done < RAW;) {
        size_t n = RAW - done < STORED_BLOCK_MAX ? RAW - done : STORED_BLOCK_MAX;
        *q++ = done + n == RAW;
        *q++ = (uint8_t)n, *q++ = (uint8_t)(n >> 8), *q++ = (uint8_t)~n, *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + done, n);
        for (size_t i = 0; i < n; i++) a = (a + raw[done + i]) % ADLER_MODULUS, b = (b + a) % ADLER_MODULUS;
        q += n;
        done += n;
    }
    be32(q, b << 16 | a);
    q += 4;
    uint8_t ihdr[13] = { 0 };
    be32(ihdr, SCREEN_W);
    be32(ihdr + 4, SCREEN_H);
    ihdr[8] = PNG_BIT_DEPTH;
    ihdr[9] = PNG_COLOR_RGB;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    png_chunk(f, "IHDR", ihdr, sizeof ihdr);
    png_chunk(f, "IDAT", z, (uint32_t)(q - z));
    png_chunk(f, "IEND", NULL, 0);
    return fclose(f) == 0;
}

/* The screen as the window shows it, with the cursor image at the pointer (none before the first pointer move). */
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
    *x = (int)get_int(request, "x", -1);
    *y = (int)get_int(request, "y", -1);
    if (*x >= 0 && *y >= 0 && *x < SCREEN_W && *y < SCREEN_H) return 1;
    send_error("x must be 0-319 and y 0-199");
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
    input_key_down((uint16_t)code);
    send_ok();
}

/* A click moves the pointer there, like the mouse; a right click also gives the release that main.c gives. */
static void handle_click(const char *request)
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
    input_mouse_x = x, input_mouse_y = y;
    input_mouse_click(x, y, right ? INPUT_BUTTON_RIGHT : INPUT_BUTTON_LEFT);
    if (right) input_mouse_click(x, y, INPUT_RIGHT_RELEASED);
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
        handle_click(request);
    } else if (!strcmp(cmd, "move")) {
        if (!get_position(request, &x, &y)) return;
        input_mouse_x = x, input_mouse_y = y;
        send_ok();
    } else if (!strcmp(cmd, "command")) {
        long id = get_int(request, "id", -1);
        if (id < 0 || id > KEY_CODE_MAX) send_error("missing id");
        else {
            input_simulate_command_click((uint16_t)id);
            send_ok();
        }
    } else if (!strcmp(cmd, "wait")) {
        long ticks = get_int(request, "ticks", 0);
        if (ticks < 1 || ticks > WAIT_MAX_TICKS) send_error("ticks must be 1-6000");
        else {
            waiting.pending = 1;
            waiting.start = main_loop.timer_ticks;
            waiting.ticks = (uint32_t)ticks;
        }
    } else if (!strcmp(cmd, "screenshot")) {
        if (!get_string(request, "path", path, sizeof path)) send_error("missing path");
        else screenshot(path);
    } else if (!strcmp(cmd, "quit")) {
        main_loop.quit = 1;
        send_ok();
    } else {
        send_error("unknown cmd");
    }
}

int control_open(const char *path)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    char dir[sizeof socket_path];
    if (strlen(path) >= sizeof address.sun_path) return 0;
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = 0;
        mkdir(dir, 0700);
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
        uint32_t ran = main_loop.timer_ticks - waiting.start;
        if (ran < waiting.ticks) return;
        waiting.pending = 0;
        put("{\"ok\":true,\"ticks\":%u}", ran);
        send_reply();
    }
    if (client_fd < 0) {
        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) return;
        fcntl(client_fd, F_SETFL, 0); /* blocking writes; reads use MSG_DONTWAIT */
#ifdef SO_NOSIGPIPE
        int on = 1;
        setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    }
    ssize_t n = recv(client_fd, input + input_length, sizeof input - 1 - input_length, MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close_client();
        return;
    }
    if (n > 0) input_length += (size_t)n;
    input[input_length] = 0;
    char *end;
    while (client_fd >= 0 && !waiting.pending && (end = strchr(input, '\n'))) {
        *end = 0;
        if (end > input) handle_request(input);
        if (client_fd < 0) return;
        size_t used = (size_t)(end + 1 - input);
        memmove(input, end + 1, input_length - used + 1);
        input_length -= used;
    }
    if (input_length == sizeof input - 1) {
        send_error("request line too long");
        input_length = 0;
    }
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
