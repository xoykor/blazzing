/* SPDX-License-Identifier: MIT */
#include "visual_iptv/database.h"
#include "test_common.h"
#include <stdlib.h>
#include <string.h>

int main(void) {
    vip_error_t error = {0};
    vip_database_t *db = NULL;
    TEST_STATUS(vip_database_open_memory(&db, &error), VIP_OK, &error);

    vip_category_list_t categories;
    vip_category_list_init(&categories);
    vip_category_t cat = {.provider_id="p", .id="7", .name="Notícias", .position=0};
    TEST_STATUS(vip_category_list_push(&categories, &cat, &error), VIP_OK, &error);

    vip_channel_list_t channels;
    vip_channel_list_init(&channels);
    vip_channel_t ch = {
        .provider_id="p", .id="42", .category_id="7", .name="News",
        .logo_url="https://example/logo.png", .stream_url="https://example/live.ts",
        .epg_channel_id="news", .position=0
    };
    TEST_STATUS(vip_channel_list_push(&channels, &ch, &error), VIP_OK, &error);
    TEST_STATUS(vip_database_replace_catalog(db, "p", &categories, &channels, &error), VIP_OK, &error);

    vip_channel_list_t loaded;
    vip_channel_list_init(&loaded);
    TEST_STATUS(vip_database_load_channels(db, "p", &loaded, &error), VIP_OK, &error);
    TEST_CHECK(loaded.len == 1);
    TEST_CHECK(strcmp(loaded.items[0].id, "42") == 0);
    TEST_STATUS(vip_database_set_favorite(db, "p", "42", true, &error), VIP_OK, &error);
    bool favorite_flags[1] = {false};
    TEST_STATUS(vip_database_load_favorite_flags(db, "p", &loaded, favorite_flags, 1, &error), VIP_OK, &error);
    TEST_CHECK(favorite_flags[0]);
    TEST_STATUS(vip_database_set_favorite(db, "p", "42", false, &error), VIP_OK, &error);
    favorite_flags[0] = true;
    TEST_STATUS(vip_database_load_favorite_flags(db, "p", &loaded, favorite_flags, 1, &error), VIP_OK, &error);
    TEST_CHECK(!favorite_flags[0]);
    TEST_STATUS(vip_database_set_thumbnail(db, "p", "42", "/tmp/a.jpg", VIP_THUMB_CAPTURED_FRAME, 1, 2, &error), VIP_OK, &error);
    char *path = vip_database_thumbnail_path(db, "p", "42", &error);
    TEST_CHECK(path != NULL);
    TEST_CHECK(strcmp(path, "/tmp/a.jpg") == 0);
    free(path);

    vip_profile_t profile = {
        .profile_id = "p", .name = "Principal", .type = VIP_PROFILE_XTREAM,
        .server = "https://example", .server_alt = "https://alt", .username = "alice"
    };
    TEST_STATUS(vip_database_save_profile(db, &profile, &error), VIP_OK, &error);
    vip_profile_list_t profiles; vip_profile_list_init(&profiles);
    TEST_STATUS(vip_database_list_profiles(db, &profiles, &error), VIP_OK, &error);
    TEST_CHECK(profiles.len == 1);
    TEST_CHECK(strcmp(profiles.items[0].name, "Principal") == 0);
    TEST_CHECK(strcmp(profiles.items[0].username, "alice") == 0);
    vip_profile_list_clear(&profiles);

    TEST_STATUS(vip_database_set_progress(db, "p", "42", 55.0, 100.0, false, &error), VIP_OK, &error);
    vip_watch_progress_t progress = {0};
    TEST_STATUS(vip_database_get_progress(db, "p", "42", &progress, &error), VIP_OK, &error);
    TEST_CHECK(progress.position_seconds == 55.0);
    TEST_CHECK(progress.duration_seconds == 100.0);
    TEST_CHECK(!progress.completed);
    vip_watch_progress_t bulk[1] = {{0}};
    TEST_STATUS(vip_database_load_progress(db, "p", &loaded, bulk, 1, &error), VIP_OK, &error);
    TEST_CHECK(bulk[0].position_seconds == 55.0);

    TEST_STATUS(vip_database_set_series_progress(db, "p", "series:7", "episode:2", 2, 10, &error), VIP_OK, &error);
    vip_series_progress_t series_progress = {0};
    TEST_STATUS(vip_database_get_series_progress(db, "p", "series:7", &series_progress, &error), VIP_OK, &error);
    TEST_CHECK(series_progress.watched_count == 2);
    TEST_CHECK(series_progress.total_count == 10);
    TEST_CHECK(series_progress.last_episode_id && strcmp(series_progress.last_episode_id, "episode:2") == 0);
    vip_series_progress_clear(&series_progress);

    vip_media_metadata_t metadata = {
        .plot = "Sinopse persistida",
        .cover_url = "https://example/cover.jpg",
        .backdrop_url = "https://example/backdrop.jpg",
        .genre = "Aventura",
        .release_date = "2026",
        .rating = "8.8",
        .duration = "95 min",
        .cast = "Pessoa A",
        .director = "Pessoa B",
        .youtube_trailer = "abc123"
    };
    TEST_STATUS(vip_database_set_media_metadata(db, "p", "42", &metadata, &error), VIP_OK, &error);
    vip_media_metadata_t loaded_metadata; vip_media_metadata_init(&loaded_metadata);
    int64_t metadata_updated = 0; bool metadata_found = false;
    TEST_STATUS(vip_database_get_media_metadata(db, "p", "42", &loaded_metadata,
                                                &metadata_updated, &metadata_found, &error), VIP_OK, &error);
    TEST_CHECK(metadata_found);
    TEST_CHECK(metadata_updated > 0);
    TEST_CHECK(loaded_metadata.plot && strcmp(loaded_metadata.plot, "Sinopse persistida") == 0);
    TEST_CHECK(loaded_metadata.backdrop_url && strstr(loaded_metadata.backdrop_url, "backdrop.jpg"));
    vip_media_metadata_clear(&loaded_metadata);

    vip_channel_list_clear(&loaded);
    vip_channel_list_clear(&channels);
    vip_category_list_clear(&categories);
    vip_database_close(db);
    return 0;
}
