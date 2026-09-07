/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/provider_m3u.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/visual-iptv-m3u-XXXXXX";
    int fd = mkstemp(path);
    TEST_CHECK(fd >= 0);
    FILE *fp = fdopen(fd, "w");
    TEST_CHECK(fp != NULL);
    fputs("#EXTM3U\n"
          "#EXTINF:-1 tvg-logo=\"https://img/a.jpg\" group-title=\"Notícias\",Canal A\n"
          "https://stream/a.m3u8\n"
          "#EXTINF:-1 group-title=\"Filmes\",Canal B\n"
          "https://stream/b.ts\n", fp);
    TEST_CHECK(fclose(fp) == 0);

    vip_category_list_t cats; vip_category_list_init(&cats);
    vip_channel_list_t channels; vip_channel_list_init(&channels);
    char provider_id[17] = {0};
    vip_error_t error = {0};
    TEST_STATUS(vip_m3u_load(path, &cats, &channels, provider_id, &error), VIP_OK, &error);
    TEST_CHECK(strlen(provider_id) == 16);
    TEST_CHECK(cats.len == 2);
    TEST_CHECK(channels.len == 2);
    TEST_CHECK(strcmp(channels.items[0].name, "Canal A") == 0);
    TEST_CHECK(strcmp(channels.items[0].logo_url, "https://img/a.jpg") == 0);
    TEST_CHECK(strcmp(channels.items[1].stream_url, "https://stream/b.ts") == 0);

    vip_channel_list_clear(&channels);
    vip_category_list_clear(&cats);
    unlink(path);
    return 0;
}
