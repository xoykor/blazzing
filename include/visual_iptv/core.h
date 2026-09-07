/* SPDX-License-Identifier: MIT */
/**
 * @file core.h
 * @brief Shared data types, error handling and ownership helpers.
 *
 * The core module intentionally has no dependency on UI, network, database or
 * player code.  Other modules exchange catalog data through the structures in
 * this header, which keeps provider-specific parsing outside the application UI.
 */
#ifndef VISUAL_IPTV_CORE_H
#define VISUAL_IPTV_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Result codes returned by the public C API. */
typedef enum {
    VIP_OK = 0,
    VIP_ERR_INVALID_ARGUMENT,
    VIP_ERR_INVALID_URL,
    VIP_ERR_NOMEM,
    VIP_ERR_NETWORK,
    VIP_ERR_AUTH,
    VIP_ERR_MALFORMED,
    VIP_ERR_DATABASE,
    VIP_ERR_IO,
    VIP_ERR_PLAYER,
    VIP_ERR_CANCELLED,
    VIP_ERR_INVALID_FRAME
} vip_status_t;

/** Small caller-owned error object used together with vip_status_t. */
typedef struct {
    vip_status_t code;
    char message[256];
} vip_error_t;

/**
 * Normalized Xtream credentials.
 *
 * The strings are heap-owned by this structure after vip_credentials_init().
 * Clear them with vip_credentials_clear(); the password buffer is overwritten
 * before it is freed.
 */
typedef struct {
    char *server;
    char *username;
    char *password;
    char provider_id[17]; /**< Stable 64-bit account identifier encoded as hex. */
} vip_credentials_t;

/** A provider category used by live TV, VOD, series and episode views. */
typedef struct {
    char *provider_id;
    char *id;
    char *name;
    int position;
} vip_category_t;

/**
 * Common catalog item representation.
 *
 * Despite the historic name, vip_channel_t is also used for VOD titles,
 * series and episodes.  Provider-specific IDs remain strings so the shared
 * catalog model does not depend on one API's numeric conventions.
 */
typedef struct {
    char *provider_id;
    char *id;
    char *category_id;
    char *name;
    char *logo_url;
    char *stream_url;
    char *epg_channel_id;
    int position;
} vip_channel_t;

/**
 * Optional rich metadata for VOD and series entries.
 *
 * This is intentionally separate from vip_channel_t so large catalogs stay
 * cheap to load.  Rich fields are fetched lazily when the user focuses or
 * opens an item.
 */
typedef struct {
    char *plot;
    char *cover_url;
    char *backdrop_url;
    char *genre;
    char *release_date;
    char *rating;
    char *duration;
    char *cast;
    char *director;
    char *youtube_trailer;
} vip_media_metadata_t;

/** Growable list of categories. */
typedef struct {
    vip_category_t *items;
    size_t len;
    size_t cap;
} vip_category_list_t;

/** Growable list of catalog items. */
typedef struct {
    vip_channel_t *items;
    size_t len;
    size_t cap;
} vip_channel_list_t;

/** Origin of a cached thumbnail. */
typedef enum {
    VIP_THUMB_SERVER_LOGO = 0,
    VIP_THUMB_CAPTURED_FRAME = 1,
    VIP_THUMB_PLACEHOLDER = 2
} vip_thumbnail_source_t;

/** Policy used when deciding between provider artwork and captured frames. */
typedef enum {
    VIP_IMAGE_ALWAYS_LOGO = 0,
    VIP_IMAGE_LOGO_OR_FRAME = 1,
    VIP_IMAGE_ALWAYS_FRAME = 2
} vip_channel_image_policy_t;

/** High-level playback state exposed by the mpv adapter. */
typedef enum {
    VIP_PLAYER_IDLE = 0,
    VIP_PLAYER_OPENING,
    VIP_PLAYER_BUFFERING,
    VIP_PLAYER_PLAYING,
    VIP_PLAYER_PAUSED,
    VIP_PLAYER_ERROR,
    VIP_PLAYER_RECONNECTING,
    VIP_PLAYER_STOPPED
} vip_player_state_t;

/** Reset an error object to VIP_OK and an empty message. */
void vip_error_clear(vip_error_t *error);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
/** Set an error code and formatted human-readable message. */
void vip_error_set(vip_error_t *error, vip_status_t code, const char *fmt, ...);

/** Validate/normalize Xtream account data and derive its provider ID. */
vip_status_t vip_credentials_init(vip_credentials_t *out,
                                  const char *server,
                                  const char *username,
                                  const char *password,
                                  vip_error_t *error);
/** Release credential strings and wipe the password buffer before freeing it. */
void vip_credentials_clear(vip_credentials_t *credentials);

void vip_category_list_init(vip_category_list_t *list);
void vip_category_list_clear(vip_category_list_t *list);
/** Deep-copy one category into the growable list. */
vip_status_t vip_category_list_push(vip_category_list_t *list,
                                    const vip_category_t *category,
                                    vip_error_t *error);

void vip_channel_list_init(vip_channel_list_t *list);
void vip_channel_list_clear(vip_channel_list_t *list);
/** Deep-copy one catalog item into the growable list. */
vip_status_t vip_channel_list_push(vip_channel_list_t *list,
                                   const vip_channel_t *channel,
                                   vip_error_t *error);

void vip_media_metadata_init(vip_media_metadata_t *metadata);
void vip_media_metadata_clear(vip_media_metadata_t *metadata);
/** Deep-copy optional metadata fields from src to dst. */
vip_status_t vip_media_metadata_copy(vip_media_metadata_t *dst,
                                     const vip_media_metadata_t *src,
                                     vip_error_t *error);

/** strdup() equivalent using the project's NULL/ownership conventions. */
char *vip_strdup(const char *text);
/** Duplicate text when non-NULL; return NULL for a NULL input. */
char *vip_strdup_nullable(const char *text);

#ifdef __cplusplus
}
#endif

#endif
