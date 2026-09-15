/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/streaming_services.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static const vip_streaming_service_t SERVICES[] = {
    {VIP_SERVICE_PLUTO, "pluto", "Pluto TV", "https://pluto.tv/", VIP_SERVICE_PLAY_NATIVE, false},
    {VIP_SERVICE_PRIME_VIDEO, "prime-video", "Prime Video", "https://www.primevideo.com/", VIP_SERVICE_PLAY_EXTERNAL_WEB, true},
    {VIP_SERVICE_MAX, "max", "Max", "https://www.max.com/", VIP_SERVICE_PLAY_EXTERNAL_WEB, true},
    {VIP_SERVICE_GLOBOPLAY, "globoplay", "Globoplay", "https://globoplay.globo.com/", VIP_SERVICE_PLAY_EXTERNAL_WEB, true}
};

size_t vip_streaming_service_count(void) {
    return sizeof(SERVICES) / sizeof(SERVICES[0]);
}

const vip_streaming_service_t *vip_streaming_service_at(size_t index) {
    return index < vip_streaming_service_count() ? &SERVICES[index] : NULL;
}

const vip_streaming_service_t *vip_streaming_service_get(vip_streaming_service_id_t id) {
    for (size_t i = 0; i < vip_streaming_service_count(); ++i)
        if (SERVICES[i].id == id) return &SERVICES[i];
    return NULL;
}

static bool allowed_http_url(const char *url) {
    return url && (!strncmp(url, "https://", 8u) || !strncmp(url, "http://", 7u));
}

vip_status_t vip_streaming_service_open(vip_streaming_service_id_t id,
                                        const char *url_override,
                                        vip_error_t *error) {
    const vip_streaming_service_t *service = vip_streaming_service_get(id);
    if (!service) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "serviço de streaming desconhecido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *url = (url_override && url_override[0]) ? url_override : service->home_url;
    if (!allowed_http_url(url)) {
        vip_error_set(error, VIP_ERR_INVALID_URL, "URL externa inválida");
        return VIP_ERR_INVALID_URL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível abrir o navegador: %s", strerror(errno));
        return VIP_ERR_IO;
    }
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
        _exit(127);
    }
    vip_error_clear(error);
    return VIP_OK;
}
