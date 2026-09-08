/* SPDX-License-Identifier: MIT */
#ifndef VISUAL_IPTV_STREAMING_SERVICES_H
#define VISUAL_IPTV_STREAMING_SERVICES_H

#include "visual_iptv/core.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIP_SERVICE_PLUTO = 0,
    VIP_SERVICE_PRIME_VIDEO,
    VIP_SERVICE_MAX,
    VIP_SERVICE_GLOBOPLAY,
    VIP_SERVICE_COUNT
} vip_streaming_service_id_t;

typedef enum {
    VIP_SERVICE_PLAY_NATIVE = 0,
    VIP_SERVICE_PLAY_EXTERNAL_WEB
} vip_streaming_playback_mode_t;

typedef struct {
    vip_streaming_service_id_t id;
    const char *slug;
    const char *name;
    const char *home_url;
    vip_streaming_playback_mode_t playback_mode;
    bool account_required;
} vip_streaming_service_t;

size_t vip_streaming_service_count(void);
const vip_streaming_service_t *vip_streaming_service_at(size_t index);
const vip_streaming_service_t *vip_streaming_service_get(vip_streaming_service_id_t id);

/* Launch an official service page with xdg-open. This intentionally delegates
 * authentication and DRM to the service's supported web environment instead
 * of collecting provider passwords inside Blazzing. */
vip_status_t vip_streaming_service_open(vip_streaming_service_id_t id,
                                        const char *url_override,
                                        vip_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
