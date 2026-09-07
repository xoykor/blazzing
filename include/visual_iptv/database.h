/* SPDX-License-Identifier: MIT */
/**
 * @file database.h
 * @brief SQLite persistence for catalogs, profiles, favorites and progress.
 *
 * Passwords are deliberately excluded from this module.  The X11 UI stores
 * Xtream passwords through Secret Service when available.
 */
#ifndef VISUAL_IPTV_DATABASE_H
#define VISUAL_IPTV_DATABASE_H

#include "visual_iptv/core.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vip_database vip_database_t;

typedef enum {
    VIP_PROFILE_XTREAM = 0,
    VIP_PROFILE_M3U = 1
} vip_profile_type_t;

/** Non-secret connection profile persisted in SQLite. */
typedef struct {
    char *profile_id;
    char *name;
    vip_profile_type_t type;
    char *server;
    char *server_alt;
    char *username;
    int64_t last_used;
} vip_profile_t;

typedef struct {
    vip_profile_t *items;
    size_t len;
    size_t cap;
} vip_profile_list_t;

/** Playback progress for one VOD title or episode. */
typedef struct {
    double position_seconds;
    double duration_seconds;
    bool completed;
    int64_t updated_at;
} vip_watch_progress_t;

/** Aggregate series progress used by the catalog UI. */
typedef struct {
    char *last_episode_id;
    int watched_count;
    int total_count;
    int64_t updated_at;
} vip_series_progress_t;

/** Open/create the persistent SQLite database at path and run migrations. */
vip_status_t vip_database_open(vip_database_t **out, const char *path, vip_error_t *error);
/** Open an in-memory database; primarily useful for tests. */
vip_status_t vip_database_open_memory(vip_database_t **out, vip_error_t *error);
void vip_database_close(vip_database_t *db);

/** Atomically replace one provider's cached category/channel catalog. */
vip_status_t vip_database_replace_catalog(vip_database_t *db,
                                          const char *provider_id,
                                          const vip_category_list_t *categories,
                                          const vip_channel_list_t *channels,
                                          vip_error_t *error);

/** Load cached catalog items for a provider. */
vip_status_t vip_database_load_channels(vip_database_t *db,
                                        const char *provider_id,
                                        vip_channel_list_t *out,
                                        vip_error_t *error);

/** Set or clear a favorite flag for one item. */
vip_status_t vip_database_set_favorite(vip_database_t *db,
                                       const char *provider_id,
                                       const char *channel_id,
                                       bool favorite,
                                       vip_error_t *error);

/** Persist the file and provenance of a generated/cached thumbnail. */
vip_status_t vip_database_set_thumbnail(vip_database_t *db,
                                        const char *provider_id,
                                        const char *channel_id,
                                        const char *path,
                                        vip_thumbnail_source_t source,
                                        int64_t generated_at,
                                        int64_t last_verified,
                                        vip_error_t *error);

/** Bulk-load favorite state aligned with the supplied channel list. */
vip_status_t vip_database_load_favorite_flags(vip_database_t *db,
                                               const char *provider_id,
                                               const vip_channel_list_t *channels,
                                               bool *flags,
                                               size_t flags_len,
                                               vip_error_t *error);

/** Return a heap-allocated cached thumbnail path, or NULL when absent. */
char *vip_database_thumbnail_path(vip_database_t *db,
                                  const char *provider_id,
                                  const char *channel_id,
                                  vip_error_t *error);

/* Saved list/profile metadata. Passwords are intentionally not stored here. */
void vip_profile_list_init(vip_profile_list_t *list);
void vip_profile_list_clear(vip_profile_list_t *list);
vip_status_t vip_database_save_profile(vip_database_t *db,
                                       const vip_profile_t *profile,
                                       vip_error_t *error);
vip_status_t vip_database_list_profiles(vip_database_t *db,
                                        vip_profile_list_t *out,
                                        vip_error_t *error);
vip_status_t vip_database_touch_profile(vip_database_t *db,
                                        const char *profile_id,
                                        vip_error_t *error);

/* Playback progress for VOD/episodes. */
vip_status_t vip_database_set_progress(vip_database_t *db,
                                       const char *provider_id,
                                       const char *channel_id,
                                       double position_seconds,
                                       double duration_seconds,
                                       bool completed,
                                       vip_error_t *error);
vip_status_t vip_database_get_progress(vip_database_t *db,
                                       const char *provider_id,
                                       const char *channel_id,
                                       vip_watch_progress_t *out,
                                       vip_error_t *error);
/** Bulk-load progress aligned with the supplied channel list. */
vip_status_t vip_database_load_progress(vip_database_t *db,
                                        const char *provider_id,
                                        const vip_channel_list_t *channels,
                                        vip_watch_progress_t *progress,
                                        size_t progress_len,
                                        vip_error_t *error);

vip_status_t vip_database_set_series_progress(vip_database_t *db,
                                              const char *provider_id,
                                              const char *series_id,
                                              const char *last_episode_id,
                                              int watched_count,
                                              int total_count,
                                              vip_error_t *error);
vip_status_t vip_database_get_series_progress(vip_database_t *db,
                                              const char *provider_id,
                                              const char *series_id,
                                              vip_series_progress_t *out,
                                              vip_error_t *error);
void vip_series_progress_clear(vip_series_progress_t *progress);

/* Lazy rich metadata cache. Passwords/stream URLs are not stored here. */
vip_status_t vip_database_set_media_metadata(vip_database_t *db,
                                             const char *provider_id,
                                             const char *media_id,
                                             const vip_media_metadata_t *metadata,
                                             vip_error_t *error);
vip_status_t vip_database_get_media_metadata(vip_database_t *db,
                                             const char *provider_id,
                                             const char *media_id,
                                             vip_media_metadata_t *metadata_out,
                                             int64_t *updated_at_out,
                                             bool *found_out,
                                             vip_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
