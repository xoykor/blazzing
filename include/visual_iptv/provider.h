/* SPDX-License-Identifier: MIT */
/**
 * @file provider.h
 * @brief Xtream Codes provider client and JSON parsing API.
 */
#ifndef VISUAL_IPTV_PROVIDER_H
#define VISUAL_IPTV_PROVIDER_H

#include "visual_iptv/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vip_xtream_client vip_xtream_client_t;

/** Create a client by deep-copying normalized credentials. */
vip_status_t vip_xtream_client_create(vip_xtream_client_t **out, const vip_credentials_t *credentials,
                                      vip_error_t *error);
/* Destroy the requested state in the xtream client. */
void vip_xtream_client_destroy(vip_xtream_client_t *client);
/* Handle the xtream provider id operation. */
const char *vip_xtream_provider_id(const vip_xtream_client_t *client);

/* Authenticate the requested state using the Xtream provider. */
vip_status_t vip_xtream_authenticate(vip_xtream_client_t *client, vip_error_t *error);
/* Handle the xtream live categories operation. */
vip_status_t vip_xtream_live_categories(vip_xtream_client_t *client, vip_category_list_t *out,
                                        vip_error_t *error);
/* Handle the xtream live streams operation. */
vip_status_t vip_xtream_live_streams(vip_xtream_client_t *client, vip_channel_list_t *out,
                                     vip_error_t *error);
/* Handle the xtream vod categories operation. */
vip_status_t vip_xtream_vod_categories(vip_xtream_client_t *client, vip_category_list_t *out,
                                       vip_error_t *error);
/* Handle the xtream vod streams operation. */
vip_status_t vip_xtream_vod_streams(vip_xtream_client_t *client, vip_channel_list_t *out, vip_error_t *error);
/** Fetch rich metadata for a single VOD title. */
vip_status_t vip_xtream_vod_info(vip_xtream_client_t *client, const char *vod_id,
                                 vip_media_metadata_t *metadata_out, vip_error_t *error);
/* Handle the xtream series categories operation. */
vip_status_t vip_xtream_series_categories(vip_xtream_client_t *client, vip_category_list_t *out,
                                          vip_error_t *error);
/* Handle the xtream series operation. */
vip_status_t vip_xtream_series(vip_xtream_client_t *client, vip_channel_list_t *out, vip_error_t *error);
/** Fetch only the lightweight series metadata fields. */
vip_status_t vip_xtream_series_metadata(vip_xtream_client_t *client, const char *series_id,
                                        vip_media_metadata_t *metadata_out, vip_error_t *error);
/** Fetch series metadata, seasons and episode catalog in one request. */
vip_status_t vip_xtream_series_info(vip_xtream_client_t *client, const char *series_id,
                                    vip_media_metadata_t *metadata_out, vip_category_list_t *seasons_out,
                                    vip_channel_list_t *episodes_out, vip_error_t *error);
/** Fetch seasons and episodes when rich metadata is not required. */
vip_status_t vip_xtream_series_episodes(vip_xtream_client_t *client, const char *series_id,
                                        vip_category_list_t *seasons_out, vip_channel_list_t *episodes_out,
                                        vip_error_t *error);

/* Parsing functions are public so unit tests can validate provider payloads without network access. */
vip_status_t vip_xtream_parse_auth_json(const char *json, vip_error_t *error);
/* Parse categories json using the Xtream provider. */
vip_status_t vip_xtream_parse_categories_json(const char *json, const char *provider_id,
                                              vip_category_list_t *out, vip_error_t *error);
/* Parse streams json using the Xtream provider. */
vip_status_t vip_xtream_parse_streams_json(const char *json, const vip_credentials_t *credentials,
                                           vip_channel_list_t *out, vip_error_t *error);
/* Parse vod info json using the Xtream provider. */
vip_status_t vip_xtream_parse_vod_info_json(const char *json, vip_media_metadata_t *metadata_out,
                                            vip_error_t *error);
/* Parse series metadata json using the Xtream provider. */
vip_status_t vip_xtream_parse_series_metadata_json(const char *json, vip_media_metadata_t *metadata_out,
                                                   vip_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
