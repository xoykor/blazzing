/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/pairing_server.h"
#include "test_common.h"

#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static atomic_bool received;
static char received_name[128];
static char received_url[512];

static void on_submit(const char *profile_name, const char *playlist_url, void *userdata) {
    (void)userdata;
    snprintf(received_name, sizeof(received_name), "%s", profile_name ? profile_name : "");
    snprintf(received_url, sizeof(received_url), "%s", playlist_url ? playlist_url : "");
    atomic_store(&received, true);
}

static int connect_loopback(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1 ||
        connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int exchange(uint16_t port, const char *request, char *response, size_t response_size) {
    int fd = connect_loopback(port);
    if (fd < 0)
        return -1;
    size_t request_len = strlen(request);
    if (send(fd, request, request_len, 0) != (ssize_t)request_len) {
        close(fd);
        return -1;
    }
    size_t used = 0u;
    while (used + 1u < response_size) {
        ssize_t n = recv(fd, response + used, response_size - used - 1u, 0);
        if (n <= 0)
            break;
        used += (size_t)n;
    }
    response[used] = '\0';
    close(fd);
    return (int)used;
}

int main(void) {
    vip_error_t error = {0};
    vip_pairing_server_t *server = NULL;
    atomic_init(&received, false);

    TEST_STATUS(vip_pairing_server_start(&server, on_submit, NULL, &error), VIP_OK, &error);
    TEST_CHECK(server != NULL);
    TEST_CHECK(vip_pairing_server_port(server) != 0u);
    TEST_CHECK(vip_pairing_server_token(server)[0] != '\0');

    char url[256];
    vip_pairing_server_url(server, url, sizeof(url));
    TEST_CHECK(strncmp(url, "http://", 7) == 0);
    TEST_CHECK(strstr(url, vip_pairing_server_token(server)) != NULL);

    char request[1024];
    char response[8192];
    snprintf(request, sizeof(request),
             "GET /%s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
             vip_pairing_server_token(server));
    TEST_CHECK(exchange(vip_pairing_server_port(server), request, response, sizeof(response)) > 0);
    TEST_CHECK(strstr(response, "200 OK") != NULL);
    TEST_CHECK(strstr(response, "Adicionar playlist ao Blazzing") != NULL);
    TEST_CHECK(strstr(response, "Cache-Control: no-store") != NULL);
    TEST_CHECK(strstr(response, "Referrer-Policy: no-referrer") != NULL);
    TEST_CHECK(strstr(response, "X-Frame-Options: DENY") != NULL);
    TEST_CHECK(strstr(response, "Content-Security-Policy:") != NULL);

    snprintf(request, sizeof(request),
             "GET /token-invalido HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    TEST_CHECK(exchange(vip_pairing_server_port(server), request, response, sizeof(response)) > 0);
    TEST_CHECK(strstr(response, "404 Not Found") != NULL);

    const char *invalid_body = "name=Teste&url=ftp%3A%2F%2Fexample.com%2Flista.m3u8";
    snprintf(request, sizeof(request),
             "POST /submit/%s HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             vip_pairing_server_token(server), strlen(invalid_body), invalid_body);
    TEST_CHECK(exchange(vip_pairing_server_port(server), request, response, sizeof(response)) > 0);
    TEST_CHECK(strstr(response, "400 Bad Request") != NULL);
    TEST_CHECK(!atomic_load(&received));

    const char *body = "name=Teste&url=https%3A%2F%2Fexample.com%2Flista.m3u8";
    snprintf(request, sizeof(request),
             "POST /submit/%s HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             vip_pairing_server_token(server), strlen(body), body);
    TEST_CHECK(exchange(vip_pairing_server_port(server), request, response, sizeof(response)) > 0);
    TEST_CHECK(strstr(response, "200 OK") != NULL);
    TEST_CHECK(atomic_load(&received));
    TEST_CHECK(strcmp(received_name, "Teste") == 0);
    TEST_CHECK(strcmp(received_url, "https://example.com/lista.m3u8") == 0);

    vip_pairing_server_stop(server);
    return 0;
}
