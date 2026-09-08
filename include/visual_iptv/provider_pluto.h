/* SPDX-License-Identifier: MIT */
#ifndef VISUAL_IPTV_PROVIDER_PLUTO_H
#define VISUAL_IPTV_PROVIDER_PLUTO_H

#include "visual_iptv/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vip_pluto_client vip_pluto_client_t;

/* Pluto TV does not require an end-user account. The client acquires an
 * anonymous web session from Pluto and normalizes its live catalog into the
 * same structures used by Xtream/M3U. */
vip_status_t vip_pluto_client_create(vip_pluto_client_t **out, vip_error_t *error);
void vip_pluto_client_destroy(vip_pluto_client_t *client);

/* Stable provider identifier for database/favorites separation. */
const char *vip_pluto_provider_id(void);

/* Refresh the anonymous session token used by current Pluto endpoints. */
vip_status_t vip_pluto_boot(vip_pluto_client_t *client, vip_error_t *error);

/* Fetch live channels. Categories are deliberately provider-neutral; when the
 * current category endpoint is unavailable, all channels are placed under a
 * single "Pluto TV" category rather than failing the whole catalog. */
vip_status_t vip_pluto_live_catalog(vip_pluto_client_t *client,
                                    vip_category_list_t *categories_out,
                                    vip_channel_list_t *channels_out,
                                    vip_error_t *error);

/* Public for deterministic parser tests without network access. */
vip_status_t vip_pluto_parse_channels_json(const char *json,
                                            const char *session_token,
                                            const char *stitcher_base,
                                            vip_channel_list_t *out,
                                            vip_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
