/* SPDX-License-Identifier: MIT */
#include "test_common.h"
#include "visual_iptv/provider_pluto.h"

#include <string.h>

int main(void) {
    const char *json =
        "{\"data\":["
        "{\"id\":\"abc123\",\"name\":\"Canal Teste\",\"number\":42,"
        "\"images\":[{\"type\":\"colorLogoPNG\",\"url\":\"https://images.example/logo.png\"}]},"
        "{\"id\":\"def456\",\"name\":\"Outro Canal\",\"number\":43}"
        "]}";

    vip_channel_list_t channels;
    vip_channel_list_init(&channels);
    vip_error_t error;
    vip_error_clear(&error);

    TEST_ASSERT(vip_pluto_parse_channels_json(json, "token-test", "https://stitcher.example",
                                               &channels, &error) == VIP_OK);
    TEST_ASSERT(channels.len == 2u);
    TEST_ASSERT(strcmp(channels.items[0].provider_id, "pluto-tv") == 0);
    TEST_ASSERT(strcmp(channels.items[0].id, "abc123") == 0);
    TEST_ASSERT(strcmp(channels.items[0].name, "Canal Teste") == 0);
    TEST_ASSERT(strcmp(channels.items[0].category_id, "pluto-live") == 0);
    TEST_ASSERT(strcmp(channels.items[0].logo_url, "https://images.example/logo.png") == 0);
    TEST_ASSERT(strstr(channels.items[0].stream_url, "/channel/abc123/master.m3u8") != NULL);
    TEST_ASSERT(strstr(channels.items[0].stream_url, "jwt=token-test") != NULL);
    TEST_ASSERT(channels.items[0].position == 42);
    TEST_ASSERT(channels.items[1].logo_url == NULL);

    vip_channel_list_clear(&channels);
    return 0;
}
