/* SPDX-License-Identifier: MIT */
/*
 * Regression tests for provider.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#include "visual_iptv/provider.h"
#include "test_common.h"
#include <curl/curl.h>
#include <string.h>

/* Run this executable's main entry point. */
int main(void) {
    TEST_CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    vip_error_t error = {0};
    TEST_STATUS(vip_xtream_parse_auth_json("{\"user_info\":{\"auth\":1,\"status\":\"Active\"}}", &error),
                VIP_OK, &error);
    TEST_STATUS(vip_xtream_parse_auth_json("{\"user_info\":{\"auth\":0}}", &error), VIP_ERR_AUTH, &error);

    vip_credentials_t creds = {0};
    TEST_STATUS(vip_credentials_init(&creds, "https://iptv.example", "user name", "p@ss", &error), VIP_OK,
                &error);
    const char *json = "["
                       "{\"stream_id\":1234,\"name\":\"News\",\"stream_icon\":\"https://img/logo.png\","
                       " \"category_id\":\"7\",\"epg_channel_id\":\"news.id\",\"stream_type\":\"live\","
                       "\"direct_source\":\"\"},"
                       "{\"stream_id\":\"9\",\"name\":\"VOD\",\"stream_type\":\"movie\"}"
                       "]";
    vip_channel_list_t channels;
    vip_channel_list_init(&channels);
    TEST_STATUS(vip_xtream_parse_streams_json(json, &creds, &channels, &error), VIP_OK, &error);
    TEST_CHECK(channels.len == 1);
    TEST_CHECK(strcmp(channels.items[0].id, "1234") == 0);
    TEST_CHECK(strstr(channels.items[0].stream_url, "/live/") != NULL);
    TEST_CHECK(strstr(channels.items[0].stream_url, "1234.ts") != NULL);
    vip_channel_list_clear(&channels);

    vip_media_metadata_t vod;
    vip_media_metadata_init(&vod);
    const char *vod_info = "{\"info\":{\"plot\":\"Uma sinopse\",\"movie_image\":\"https://img/cover.jpg\","
                           "\"backdrop_path\":[\"https://img/backdrop.jpg\"],\"genre\":\"Drama\","
                           "\"release_date\":\"2025-03-01\",\"rating\":\"8.4\",\"duration\":\"01:42:00\","
                           "\"cast\":\"A, B\",\"director\":\"Diretor\"}}";
    TEST_STATUS(vip_xtream_parse_vod_info_json(vod_info, &vod, &error), VIP_OK, &error);
    TEST_CHECK(vod.plot && strcmp(vod.plot, "Uma sinopse") == 0);
    TEST_CHECK(vod.backdrop_url && strstr(vod.backdrop_url, "backdrop.jpg"));
    TEST_CHECK(vod.genre && strcmp(vod.genre, "Drama") == 0);
    vip_media_metadata_clear(&vod);

    /* Preserve primary fields, but fill missing values from movie_data.
     * movie_data intentionally prefers stream_icon over movie_image. */
    vip_media_metadata_init(&vod);
    const char *vod_fallback =
        "{\"info\":{\"plot\":\"Primário\",\"movie_image\":\"\"},"
        "\"movie_data\":{\"plot\":\"Fallback\",\"stream_icon\":\"https://img/fallback-icon.jpg\","
        "\"movie_image\":\"https://img/fallback-movie.jpg\",\"runtime\":\"97\"}}";
    TEST_STATUS(vip_xtream_parse_vod_info_json(vod_fallback, &vod, &error), VIP_OK, &error);
    TEST_CHECK(vod.plot && strcmp(vod.plot, "Primário") == 0);
    TEST_CHECK(vod.cover_url && strstr(vod.cover_url, "fallback-icon.jpg"));
    TEST_CHECK(vod.duration && strcmp(vod.duration, "97") == 0);
    vip_media_metadata_clear(&vod);

    /* When info itself is unusable, movie_data becomes the primary source and
     * keeps the primary cover precedence: movie_image before stream_icon. */
    vip_media_metadata_init(&vod);
    const char *vod_primary_from_fallback =
        "{\"info\":[],\"movie_data\":{\"plot\":\"Só fallback\","
        "\"movie_image\":\"https://img/movie.jpg\",\"stream_icon\":\"https://img/icon.jpg\"}}";
    TEST_STATUS(vip_xtream_parse_vod_info_json(vod_primary_from_fallback, &vod, &error), VIP_OK, &error);
    TEST_CHECK(vod.plot && strcmp(vod.plot, "Só fallback") == 0);
    TEST_CHECK(vod.cover_url && strstr(vod.cover_url, "movie.jpg"));
    vip_media_metadata_clear(&vod);

    vip_media_metadata_init(&vod);
    TEST_STATUS(vip_xtream_parse_vod_info_json("{}", &vod, &error), VIP_ERR_MALFORMED, &error);
    vip_media_metadata_clear(&vod);
    TEST_STATUS(vip_xtream_parse_vod_info_json("[]", &vod, &error), VIP_ERR_MALFORMED, &error);

    vip_media_metadata_t series;
    vip_media_metadata_init(&series);
    const char *series_info =
        "{\"info\":{\"name\":\"Série X\",\"plot\":\"Resumo\",\"cover\":\"https://img/series.jpg\","
        "\"backdrop_path\":[\"https://img/series-bg.jpg\"],\"genre\":\"Sci-Fi\",\"rating\":\"9.1\"},"
        "\"episodes\":{\"1\":[]}}";
    TEST_STATUS(vip_xtream_parse_series_metadata_json(series_info, &series, &error), VIP_OK, &error);
    TEST_CHECK(series.plot && strcmp(series.plot, "Resumo") == 0);
    TEST_CHECK(series.cover_url && strstr(series.cover_url, "series.jpg"));
    TEST_CHECK(series.rating && strcmp(series.rating, "9.1") == 0);
    vip_media_metadata_clear(&series);

    vip_credentials_clear(&creds);
    curl_global_cleanup();
    return 0;
}
