/* SPDX-License-Identifier: MIT */
/**
 * @file provider_m3u.h
 * @brief Loader for local and HTTP(S) M3U/M3U8 playlists.
 */
#ifndef VISUAL_IPTV_PROVIDER_M3U_H
#define VISUAL_IPTV_PROVIDER_M3U_H

#include "visual_iptv/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load a local M3U/M3U8 file or HTTP(S) playlist into the shared catalog
 * model. provider_id_out receives a stable 16-hex identifier derived from the
 * playlist source.
 */
vip_status_t vip_m3u_load(const char *source,
                          vip_category_list_t *categories_out,
                          vip_channel_list_t *channels_out,
                          char provider_id_out[17],
                          vip_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
