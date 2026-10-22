/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The control socket: accepts one client, cuts its bytes into request lines, and sends the replies of control.c. */
#include "transport.h"
#include "control.h"
#include "state_json.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

/* The size of the request buffer. */
#define INPUT_SIZE 4096 /* a request line up to its newline */

/* The mode of the socket folder: only the user reads, writes and enters it. */
#define SOCKET_DIR_MODE 0700

#ifdef MSG_NOSIGNAL
/* The flags of a send to the client. */
#define SEND_FLAGS MSG_NOSIGNAL /* a closed client does not raise SIGPIPE (Linux) */
#else
/* The flags of a send to the client. */
#define SEND_FLAGS 0x0 /* SO_NOSIGPIPE does the same (macOS) */
#endif

/* The listening socket, the one client, and the bytes of its next request line. */
static struct {
    int listen_fd;
    int client_fd;
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char input[INPUT_SIZE];
    size_t input_length;
    bool discarding; /* the rest of a too long line goes up to its newline */
    bool reply_held; /* a wait or a turn of the client gives its reply later */
} transport = {.listen_fd = -1, .client_fd = -1};

/* Closes the client connection and forgets its unread bytes; a wait or a turn of the client ends without a reply. */
static void close_client(void)
{
    if (transport.client_fd >= 0)
        close(transport.client_fd);
    transport.client_fd = -1;
    transport.input_length = 0;
    transport.discarding = false;
    transport.reply_held = false;
    control_drop_client();
}

/* Sends reply as one line and frees it. */
static void send_line(Json *reply)
{
    json_raw(reply, "\n");
    for (size_t sent = 0; transport.client_fd >= 0 && sent < reply->len;) {
        ssize_t n = send(transport.client_fd, reply->s + sent, reply->len - sent, SEND_FLAGS);
        if (n <= 0)
            close_client();
        else
            sent += (size_t)n;
    }
    free(reply->s);
    *reply = (Json){0};
}

/* An error reply of the line layer, in the form of the protocol errors. */
static void send_error(const char *error)
{
    Json reply = {0};
    json_raw(&reply, "{\"ok\":false,\"error\":");
    json_str(&reply, error);
    json_raw(&reply, "}");
    send_line(&reply);
}

/* Answers one request line, or holds its reply until the wait or the turn ends. */
static void answer_line(const char *line)
{
    Json reply = {0};
    if (control_handle_line(line, &reply))
        send_line(&reply);
    else
        transport.reply_held = true;
    free(reply.s);
}

/* One frame of the wait or the turn: its reply goes out when it ends. */
static void send_held_reply(void)
{
    Json reply = {0};
    if (control_end_turn(&reply)) {
        transport.reply_held = false;
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
    if (transport.client_fd < 0)
        return;
    ssize_t n = recv(transport.client_fd, &next, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        close_client(); /* the client left */
}

/* Takes the waiting connections: the first becomes the client, the others get a busy error. */
static void accept_clients(void)
{
    static const char busy[] = "{\"ok\":false,\"error\":\"busy\"}\n";
    for (int fd; (fd = accept(transport.listen_fd, NULL, NULL)) >= 0;) {
        prepare_connection(fd);
        drop_closed_client();
        if (transport.client_fd < 0) {
            transport.client_fd = fd;
            continue;
        }
        send(fd, busy, sizeof busy - 1, SEND_FLAGS);
        close(fd);
    }
}

/* Reads the bytes of the client and answers its complete lines; a too long line gets one error reply. */
static void read_requests(void)
{
    if (transport.client_fd < 0)
        return;
    ssize_t n = recv(transport.client_fd, transport.input + transport.input_length,
                     sizeof transport.input - transport.input_length, MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        close_client();
        return;
    }
    if (n > 0)
        transport.input_length += (size_t)n;
    char *end;
    while (transport.client_fd >= 0 && !transport.reply_held && !control_is_turn_pending() &&
           (end = memchr(transport.input, '\n', transport.input_length))) {
        size_t used = (size_t)(end + 1 - transport.input);
        *end = 0;
        if (transport.discarding)
            transport.discarding = false;
        else if (memchr(transport.input, 0, used - 1))
            send_error("request line has a NUL byte");
        else if (end > transport.input)
            answer_line(transport.input);
        if (transport.client_fd < 0)
            return;
        memmove(transport.input, end + 1, transport.input_length - used);
        transport.input_length -= used;
    }
    if (transport.input_length == sizeof transport.input) {
        if (!transport.discarding)
            send_error("request line too long");
        transport.discarding = true;
        transport.input_length = 0;
    }
}

/* Setup */

bool transport_open(const char *path)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    struct stat old;
    char dir[sizeof transport.socket_path];
    if (strlen(path) >= sizeof address.sun_path)
        return false;
    if (!lstat(path, &old) && !S_ISSOCK(old.st_mode))
        return false; /* never remove another file */
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = 0;
        mkdir(dir, SOCKET_DIR_MODE);
    }
    snprintf(address.sun_path, sizeof address.sun_path, "%s", path);
    snprintf(transport.socket_path, sizeof transport.socket_path, "%s", path);
    unlink(path);
    transport.listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (transport.listen_fd < 0)
        return false;
    if (bind(transport.listen_fd, (struct sockaddr *)&address, sizeof address) || listen(transport.listen_fd, 1) ||
        fcntl(transport.listen_fd, F_SETFL, O_NONBLOCK)) {
        transport_close();
        return false;
    }
    return true;
}

void transport_close(void)
{
    close_client();
    if (transport.listen_fd >= 0) {
        close(transport.listen_fd);
        unlink(transport.socket_path);
    }
    transport.listen_fd = -1;
    control_reset();
}

/* Queries */

bool transport_is_available(void)
{
    return true;
}

/* Requests */

void transport_poll(void)
{
    if (transport.listen_fd < 0)
        return;
    if (transport.reply_held && !control_is_turn_pending())
        drop_closed_client(); /* a wait */
    if (transport.reply_held && !control_is_turn_pending())
        send_held_reply();
    if (!transport.reply_held && !control_is_turn_pending())
        read_requests();
    if (control_is_turn_pending()) { /* a client that left gets no reply; the reply goes out when the turn ends */
        drop_closed_client();
        send_held_reply();
    }
    accept_clients();
}
