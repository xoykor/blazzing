/* SPDX-License-Identifier: MIT */
/*
 * Regression tests for m3u.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/provider_m3u.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Run this executable's main entry point. */
int main(void) {
    char path[] = "/tmp/visual-iptv-m3u-XXXXXX";
    int fd = mkstemp(path);
    TEST_CHECK(fd >= 0);
    FILE *fp = fdopen(fd, "w");
    TEST_CHECK(fp != NULL);
    fputs(
        "#EXTM3U\n"
        "#EXTINF:-1 tvg-id=\"canal-a\" tvg-logo=\"https://img/a.jpg\" group-title=\"Notícias\",Canal A, HD\n"
        "https://stream/a.m3u8\n"
        "#EXTINF:-1 group-title=\"Filmes | Ação\",Filme Exemplo\n"
        "https://stream/movie.ts\n"
        "#EXTINF:-1 tvg-logo=\"https://img/dragon.jpg\" group-title=\"Series | Crunchyroll\",Dragon Ball "
        "Super S01 E01\n"
        "https://stream/dbs-s01e01.ts\n"
        "#EXTINF:-1 tvg-logo=\"https://img/dragon.jpg\" group-title=\"Series | Crunchyroll\",Dragon Ball "
        "Super S01 E02\n"
        "https://stream/dbs-s01e02.ts\n",
        fp);
    TEST_CHECK(fclose(fp) == 0);

    vip_category_list_t cats;
    vip_category_list_init(&cats);
    vip_channel_list_t channels;
    vip_channel_list_init(&channels);
    char provider_id[17] = {0};
    vip_error_t error = {0};
    TEST_STATUS(vip_m3u_load(path, &cats, &channels, provider_id, &error), VIP_OK, &error);
    TEST_CHECK(strlen(provider_id) == 16);
    TEST_CHECK(cats.len == 3);
    TEST_CHECK(channels.len == 4);
    TEST_CHECK(strcmp(channels.items[0].name, "Canal A, HD") == 0);
    TEST_CHECK(strcmp(channels.items[0].logo_url, "https://img/a.jpg") == 0);

    TEST_CHECK(vip_m3u_classify_group("Notícias") == VIP_M3U_CONTENT_LIVE);
    TEST_CHECK(vip_m3u_classify_group("Filmes | Ação") == VIP_M3U_CONTENT_VOD);
    TEST_CHECK(vip_m3u_classify_group("Séries | Max") == VIP_M3U_CONTENT_SERIES);
    TEST_CHECK(vip_m3u_classify_group("Series | Crunchyroll") == VIP_M3U_CONTENT_SERIES);

    char series_name[128];
    int season = -1, episode = -1;
    TEST_CHECK(vip_m3u_parse_episode_label("Dragon Ball Super S01 E12", series_name, sizeof(series_name),
                                           &season, &episode));
    TEST_CHECK(strcmp(series_name, "Dragon Ball Super") == 0);
    TEST_CHECK(season == 1 && episode == 12);
    TEST_CHECK(
        vip_m3u_parse_episode_label("The Show - 2x03", series_name, sizeof(series_name), &season, &episode));
    TEST_CHECK(strcmp(series_name, "The Show") == 0);
    TEST_CHECK(season == 2 && episode == 3);

    vip_category_list_t live_cats, vod_cats, series_cats;
    vip_channel_list_t live_channels, vod_channels, series_channels;
    vip_category_list_init(&live_cats);
    vip_channel_list_init(&live_channels);
    vip_category_list_init(&vod_cats);
    vip_channel_list_init(&vod_channels);
    vip_category_list_init(&series_cats);
    vip_channel_list_init(&series_channels);
    TEST_STATUS(vip_m3u_split_catalog(&cats, &channels, &live_cats, &live_channels, &vod_cats, &vod_channels,
                                      &series_cats, &series_channels, &error),
                VIP_OK, &error);
    TEST_CHECK(live_channels.len == 1 && live_cats.len == 1);
    TEST_CHECK(vod_channels.len == 1 && vod_cats.len == 1);
    TEST_CHECK(series_channels.len == 2 && series_cats.len == 1);
    TEST_CHECK(strcmp(series_channels.items[0].name, "Dragon Ball Super S01 E01") == 0);

    char first_id[32];
    snprintf(first_id, sizeof(first_id), "%s", channels.items[0].id);

    vip_category_list_clear(&live_cats);
    vip_channel_list_clear(&live_channels);
    vip_category_list_clear(&vod_cats);
    vip_channel_list_clear(&vod_channels);
    vip_category_list_clear(&series_cats);
    vip_channel_list_clear(&series_channels);
    vip_channel_list_clear(&channels);
    vip_category_list_clear(&cats);

    fp = fopen(path, "w");
    TEST_CHECK(fp != NULL);
    fputs("#EXTM3U\n"
          "#EXTINF:-1 tvg-id=\"canal-a\" group-title=\"Notícias\",Canal A, HD\n"
          "https://other-host/new-token/a.m3u8\n",
          fp);
    TEST_CHECK(fclose(fp) == 0);
    vip_category_list_init(&cats);
    vip_channel_list_init(&channels);
    TEST_STATUS(vip_m3u_load(path, &cats, &channels, provider_id, &error), VIP_OK, &error);
    TEST_CHECK(channels.len == 1);
    TEST_CHECK(strcmp(channels.items[0].id, first_id) == 0);

    vip_channel_list_clear(&channels);
    vip_category_list_clear(&cats);
    unlink(path);
    return 0;
}
