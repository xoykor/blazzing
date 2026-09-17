/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/pairing_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define REQUEST_MAX 16384u

struct vip_pairing_server {
    int listen_fd;
    uint16_t port;
    char host[INET_ADDRSTRLEN];
    char token[16];
    pthread_t thread;
    atomic_bool stop;
    vip_pairing_submit_fn on_submit;
    void *userdata;
};

static bool starts_with(const char *text, const char *prefix) {
    return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static void discover_ipv4(char out[INET_ADDRSTRLEN]) {
    snprintf(out, INET_ADDRSTRLEN, "127.0.0.1");
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0) return;

    for (struct ifaddrs *it = ifaddr; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        const struct sockaddr_in *addr = (const struct sockaddr_in *)it->ifa_addr;
        char candidate[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &addr->sin_addr, candidate, sizeof(candidate))) continue;
        if (starts_with(candidate, "127.") || starts_with(candidate, "169.254.")) continue;
        snprintf(out, INET_ADDRSTRLEN, "%s", candidate);
        break;
    }

    freeifaddrs(ifaddr);
}

static void make_token(char out[16]) {
    uint32_t value = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t n = read(fd, &value, sizeof(value));
        close(fd);
        if (n != (ssize_t)sizeof(value)) value = 0;
    }
    if (value == 0) {
        struct timespec ts = {0};
        (void)clock_gettime(CLOCK_MONOTONIC, &ts);
        value = (uint32_t)ts.tv_nsec ^ (uint32_t)getpid() ^ (uint32_t)ts.tv_sec;
    }
    snprintf(out, 16, "%06u", 100000u + (value % 900000u));
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool url_decode(const char *src, size_t src_len, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0u) return false;
    size_t w = 0;
    for (size_t r = 0; r < src_len; ++r) {
        unsigned char ch = (unsigned char)src[r];
        if (ch == '+') {
            ch = ' ';
        } else if (ch == '%' && r + 2u < src_len) {
            int hi = hex_value(src[r + 1u]);
            int lo = hex_value(src[r + 2u]);
            if (hi >= 0 && lo >= 0) {
                ch = (unsigned char)((hi << 4) | lo);
                r += 2u;
            }
        }
        if (w + 1u >= dst_size) return false;
        dst[w++] = (char)ch;
    }
    dst[w] = '\0';
    return true;
}

static bool form_value(const char *body,
                       const char *key,
                       char *out,
                       size_t out_size) {
    if (!body || !key || !out || out_size == 0u) return false;
    size_t key_len = strlen(key);
    const char *p = body;
    while (*p) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end - p));
        if (eq && (size_t)(eq - p) == key_len && strncmp(p, key, key_len) == 0)
            return url_decode(eq + 1, (size_t)(end - eq - 1), out, out_size);
        p = *end ? end + 1 : end;
    }
    out[0] = '\0';
    return false;
}

static void send_response(int fd,
                          const char *status,
                          const char *content_type,
                          const char *body) {
    if (!body) body = "";
    char header[512];
    size_t body_len = strlen(body);
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "Cache-Control: no-store\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "\r\n",
                     status, content_type, body_len);
    if (n > 0) (void)send(fd, header, (size_t)n, MSG_NOSIGNAL);
    if (body_len > 0u) (void)send(fd, body, body_len, MSG_NOSIGNAL);
}

static void send_form(vip_pairing_server_t *server, int fd) {
    char action[96];
    snprintf(action, sizeof(action), "/submit/%s", server->token);
    char body[4096];
    snprintf(body, sizeof(body),
             "<!doctype html><html lang=\"pt-BR\"><head>"
             "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<title>Blazzing</title><style>"
             "body{font-family:sans-serif;background:#0b1220;color:#f6f8fc;margin:0;padding:24px}"
             ".card{max-width:560px;margin:8vh auto;background:#0e1420;border:1px solid #2b3950;border-radius:18px;padding:24px}"
             "h1{margin-top:0}label{display:block;margin:18px 0 6px;color:#b9c5d6}"
             "input{box-sizing:border-box;width:100%%;padding:14px;border-radius:10px;border:1px solid #43536d;background:#151e2d;color:#fff;font-size:16px}"
             "button{width:100%%;margin-top:22px;padding:14px;border:0;border-radius:10px;background:#62a9ff;color:#07101d;font-weight:700;font-size:16px}"
             "small{color:#91a0b7}</style></head><body><div class=\"card\">"
             "<h1>Adicionar playlist ao Blazzing</h1>"
             "<small>Envie uma URL M3U/M3U8 para este computador.</small>"
             "<form method=\"post\" action=\"%s\">"
             "<label>Nome da lista (opcional)</label><input name=\"name\" maxlength=\"127\" autocomplete=\"off\">"
             "<label>URL M3U/M3U8</label><input name=\"url\" type=\"url\" maxlength=\"511\" required autofocus placeholder=\"https://.../lista.m3u8\">"
             "<button type=\"submit\">Enviar para o Blazzing</button>"
             "</form></div></body></html>",
             action);
    send_response(fd, "200 OK", "text/html; charset=utf-8", body);
}

static size_t request_content_length(const char *request) {
    const char *p = strstr(request, "\r\nContent-Length:");
    if (!p) p = strstr(request, "\r\ncontent-length:");
    if (!p) return 0u;
    p = strchr(p + 2, ':');
    if (!p) return 0u;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    errno = 0;
    unsigned long v = strtoul(p, NULL, 10);
    if (errno != 0 || v > REQUEST_MAX) return REQUEST_MAX + 1u;
    return (size_t)v;
}

static ssize_t recv_request(int fd, char *buf, size_t cap) {
    size_t used = 0;
    size_t expected_total = 0;
    while (used + 1u < cap) {
        ssize_t n = recv(fd, buf + used, cap - used - 1u, 0);
        if (n <= 0) break;
        used += (size_t)n;
        buf[used] = '\0';
        char *headers_end = strstr(buf, "\r\n\r\n");
        if (headers_end) {
            size_t header_len = (size_t)(headers_end + 4 - buf);
            size_t content_len = request_content_length(buf);
            if (content_len > REQUEST_MAX) return -1;
            expected_total = header_len + content_len;
            if (used >= expected_total) break;
        }
    }
    buf[used] = '\0';
    return (ssize_t)used;
}

static void handle_client(vip_pairing_server_t *server, int fd) {
    char request[REQUEST_MAX + 1u];
    ssize_t n = recv_request(fd, request, sizeof(request));
    if (n <= 0) return;

    char expected_get[64];
    char expected_post[80];
    snprintf(expected_get, sizeof(expected_get), "GET /%s ", server->token);
    snprintf(expected_post, sizeof(expected_post), "POST /submit/%s ", server->token);

    if (starts_with(request, expected_get)) {
        send_form(server, fd);
        return;
    }

    if (!starts_with(request, expected_post)) {
        send_response(fd, "404 Not Found", "text/plain; charset=utf-8", "Not found\n");
        return;
    }

    char *body = strstr(request, "\r\n\r\n");
    if (!body) {
        send_response(fd, "400 Bad Request", "text/plain; charset=utf-8", "Invalid request\n");
        return;
    }
    body += 4;

    char url[512] = {0};
    char name[128] = {0};
    if (!form_value(body, "url", url, sizeof(url)) || url[0] == '\0' ||
        !(starts_with(url, "http://") || starts_with(url, "https://"))) {
        send_response(fd, "400 Bad Request", "text/plain; charset=utf-8",
                      "Informe uma URL HTTP/HTTPS valida.\n");
        return;
    }
    (void)form_value(body, "name", name, sizeof(name));

    if (server->on_submit) server->on_submit(name, url, server->userdata);
    send_response(fd, "200 OK", "text/html; charset=utf-8",
                  "<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<body style=\"font-family:sans-serif;background:#0b1220;color:#f6f8fc;padding:32px\">"
                  "<h2>Playlist enviada</h2><p>Volte para o Blazzing. A lista sera carregada automaticamente.</p></body>");
}

static void *server_thread(void *userdata) {
    vip_pairing_server_t *server = userdata;
    while (!atomic_load(&server->stop)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(server->listen_fd, &rfds);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 250000};
        int rc = select(server->listen_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc <= 0) continue;

        int client = accept(server->listen_fd, NULL, NULL);
        if (client < 0) continue;
        struct timeval timeout = {.tv_sec = 3, .tv_usec = 0};
        (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        handle_client(server, client);
        close(client);
    }
    return NULL;
}

vip_status_t vip_pairing_server_start(vip_pairing_server_t **out_server,
                                      vip_pairing_submit_fn on_submit,
                                      void *userdata,
                                      vip_error_t *error) {
    if (!out_server || !on_submit) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "argumentos inválidos para pareamento");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    *out_server = NULL;

    vip_pairing_server_t *server = calloc(1, sizeof(*server));
    if (!server) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para servidor de pareamento");
        return VIP_ERR_NOMEM;
    }
    server->listen_fd = -1;
    server->on_submit = on_submit;
    server->userdata = userdata;
    atomic_init(&server->stop, false);
    discover_ipv4(server->host);
    make_token(server->token);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível criar socket de pareamento");
        free(server);
        return VIP_ERR_IO;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(0);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 4) != 0) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível abrir uma porta local para pareamento");
        close(fd);
        free(server);
        return VIP_ERR_IO;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível descobrir a porta de pareamento");
        close(fd);
        free(server);
        return VIP_ERR_IO;
    }
    server->listen_fd = fd;
    server->port = ntohs(addr.sin_port);

    if (pthread_create(&server->thread, NULL, server_thread, server) != 0) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível iniciar servidor de pareamento");
        close(fd);
        free(server);
        return VIP_ERR_IO;
    }

    *out_server = server;
    return VIP_OK;
}

void vip_pairing_server_stop(vip_pairing_server_t *server) {
    if (!server) return;
    atomic_store(&server->stop, true);
    if (server->listen_fd >= 0) {
        shutdown(server->listen_fd, SHUT_RDWR);
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    pthread_join(server->thread, NULL);
    free(server);
}

uint16_t vip_pairing_server_port(const vip_pairing_server_t *server) {
    return server ? server->port : 0u;
}

const char *vip_pairing_server_host(const vip_pairing_server_t *server) {
    return server ? server->host : "";
}

const char *vip_pairing_server_token(const vip_pairing_server_t *server) {
    return server ? server->token : "";
}

void vip_pairing_server_url(const vip_pairing_server_t *server,
                            char *out,
                            size_t out_size) {
    if (!out || out_size == 0u) return;
    if (!server) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "http://%s:%u/%s",
             server->host, (unsigned)server->port, server->token);
}
