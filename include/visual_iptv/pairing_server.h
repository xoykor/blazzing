/* SPDX-License-Identifier: MIT */
#ifndef VISUAL_IPTV_PAIRING_SERVER_H
#define VISUAL_IPTV_PAIRING_SERVER_H

#include "visual_iptv/core.h"

#include <stddef.h>
#include <stdint.h>

typedef struct vip_pairing_server vip_pairing_server_t;

typedef void (*vip_pairing_submit_fn)(const char *profile_name,
                                      const char *playlist_url,
                                      void *userdata);

vip_status_t vip_pairing_server_start(vip_pairing_server_t **out_server,
                                      vip_pairing_submit_fn on_submit,
                                      void *userdata,
                                      vip_error_t *error);

void vip_pairing_server_stop(vip_pairing_server_t *server);

uint16_t vip_pairing_server_port(const vip_pairing_server_t *server);
const char *vip_pairing_server_host(const vip_pairing_server_t *server);
const char *vip_pairing_server_token(const vip_pairing_server_t *server);

void vip_pairing_server_url(const vip_pairing_server_t *server,
                            char *out,
                            size_t out_size);

#endif
