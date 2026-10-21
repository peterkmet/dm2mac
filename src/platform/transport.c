/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The control socket: accepts one client, cuts its bytes into request lines, and sends the replies of control.c. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "transport.h"
#include "control.h"
#include "state_json.h"

#define INPUT_SIZE 4096               /* a request line up to its newline */
#define SOCKET_DIR_MODE 0700

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
static bool reply_held;               /* a wait or a turn of the client gives its reply later */

static void close_client(void)
{
    if (client_fd >= 0) close(client_fd);
    client_fd = -1;
    input_length = 0;
    discarding = 0;
    reply_held = false;
    control_drop_client();
}

/* Sends reply as one line and frees it. */
static void send_line(Json *reply)
{
    json_raw(reply, "\n");
    for (size_t sent = 0; client_fd >= 0 && sent < reply->len;) {
        ssize_t n = send(client_fd, reply->s + sent, reply->len - sent, SEND_FLAGS);
        if (n <= 0) close_client();
        else sent += (size_t)n;
    }
    free(reply->s);
    *reply = (Json){ 0 };
}

/* An error reply of the line layer, in the form of the protocol errors. */
static void send_error(const char *error)
{
    Json reply = { 0 };
    json_raw(&reply, "{\"ok\":false,\"error\":");
    json_str(&reply, error);
    json_raw(&reply, "}");
    send_line(&reply);
}

/* Answers one request line, or holds its reply until the wait or the turn ends. */
static void answer_line(const char *line)
{
    Json reply = { 0 };
    if (control_handle_line(line, &reply)) send_line(&reply);
    else reply_held = true;
    free(reply.s);
}

/* One frame of the wait or the turn: its reply goes out when it ends. */
static void send_held_reply(void)
{
    Json reply = { 0 };
    if (control_end_turn(&reply)) {
        reply_held = false;
        send_line(&reply);
    }
    free(reply.s);
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

/* Closes the client when it closed its connection and has no unread byte. */
static void drop_closed_client(void)
{
    char next;
    if (client_fd < 0) return;
    ssize_t n = recv(client_fd, &next, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) close_client(); /* the client left */
}

/* Takes the waiting connections: the first becomes the client, the others get a busy error. */
static void accept_clients(void)
{
    static const char busy[] = "{\"ok\":false,\"error\":\"busy\"}\n";
    for (int fd; (fd = accept(listen_fd, NULL, NULL)) >= 0;) {
        prepare_connection(fd);
        drop_closed_client();
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
    while (client_fd >= 0 && !reply_held && !control_is_turn_pending() && (end = memchr(input, '\n', input_length))) {
        size_t used = (size_t)(end + 1 - input);
        *end = 0;
        if (discarding) discarding = 0;
        else if (memchr(input, 0, used - 1)) send_error("request line has a NUL byte");
        else if (end > input) answer_line(input);
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

bool transport_is_available(void) { return true; }

bool transport_open(const char *path)
{
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    struct stat old;
    char dir[sizeof socket_path];
    if (strlen(path) >= sizeof address.sun_path) return false;
    if (!lstat(path, &old) && !S_ISSOCK(old.st_mode)) return false; /* never remove another file */
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
    if (listen_fd < 0) return false;
    if (bind(listen_fd, (struct sockaddr *)&address, sizeof address) || listen(listen_fd, 1) ||
        fcntl(listen_fd, F_SETFL, O_NONBLOCK)) {
        transport_close();
        return false;
    }
    return true;
}

void transport_poll(void)
{
    if (listen_fd < 0) return;
    if (reply_held && !control_is_turn_pending()) drop_closed_client();  /* a wait */
    if (reply_held && !control_is_turn_pending()) send_held_reply();
    if (!reply_held && !control_is_turn_pending()) read_requests();
    if (control_is_turn_pending()) {  /* a client that left gets no reply; the reply goes out when the turn ends */
        drop_closed_client();
        send_held_reply();
    }
    accept_clients();
}

void transport_close(void)
{
    close_client();
    if (listen_fd >= 0) {
        close(listen_fd);
        unlink(socket_path);
    }
    listen_fd = -1;
    control_reset();
}
