/* SPDX-License-Identifier: MIT */
/**
 * @file provider_m3u.h
 * @brief Loader and catalog helpers for local and HTTP(S) M3U/M3U8 playlists.
 */
#ifndef VISUAL_IPTV_PROVIDER_M3U_H
#define VISUAL_IPTV_PROVIDER_M3U_H

#include "visual_iptv/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIP_M3U_CONTENT_LIVE = 0,
    VIP_M3U_CONTENT_VOD = 1,
    VIP_M3U_CONTENT_SERIES = 2
} vip_m3u_content_kind_t;

/**
 * Load a local M3U/M3U8 file or HTTP(S) playlist into the shared catalog
 * model. provider_id_out receives a stable 16-hex identifier derived from the
 * playlist source.
 */
vip_status_t vip_m3u_load(const char *source, vip_category_list_t *categories_out,
                          vip_channel_list_t *channels_out, char provider_id_out[17], vip_error_t *error);

/** Classify a group-title into live TV, VOD/movie or series content. */
vip_m3u_content_kind_t vip_m3u_classify_group(const char *group_name);

/**
 * Parse common episode suffixes such as S01E02, S01 E02, T01E02 and 1x02.
 * series_out receives the title before the episode suffix.
 */
bool vip_m3u_parse_episode_label(const char *name, char *series_out, size_t series_cap, int *season_out,
                                 int *episode_out);

/**
 * Resolve one zero-based static fallback alternative for an M3U item.
 *
 * The function lazily fetches only the shard referenced by fallback_id and
 * never contacts a playback proxy/Worker. The primary stream_url is skipped.
 * Caller owns returned strings and must free them.
 */
vip_status_t vip_m3u_fallback_variant(const vip_channel_t *channel, size_t alternative_index,
                                      char **url_out, char **referer_out, char **user_agent_out,
                                      vip_error_t *error);

/**
 * Deep-copy a flat M3U catalog into three content catalogs based on group-title.
 * All output lists must already be initialized and empty.
 */
vip_status_t vip_m3u_split_catalog(const vip_category_list_t *source_categories,
                                   const vip_channel_list_t *source_channels,
                                   vip_category_list_t *live_categories, vip_channel_list_t *live_channels,
                                   vip_category_list_t *vod_categories, vip_channel_list_t *vod_channels,
                                   vip_category_list_t *series_categories,
                                   vip_channel_list_t *series_channels, vip_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
