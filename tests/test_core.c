/* SPDX-License-Identifier: MIT */
#include "visual_iptv/core.h"
#include "test_common.h"
#include <string.h>
#include <stdio.h>

int main(void) {
    vip_error_t error = {0};
    vip_credentials_t creds = {0};
    TEST_STATUS(vip_credentials_init(&creds, "https://example.com///", "alice", "secret", &error), VIP_OK, &error);
    TEST_CHECK(strcmp(creds.server, "https://example.com/") == 0);
    TEST_CHECK(strlen(creds.provider_id) == 16);
    char alice_id[17];
    snprintf(alice_id, sizeof(alice_id), "%s", creds.provider_id);
    vip_credentials_t other = {0};
    TEST_STATUS(vip_credentials_init(&other, "https://example.com///", "bob", "secret", &error), VIP_OK, &error);
    TEST_CHECK(strcmp(alice_id, other.provider_id) != 0);
    vip_credentials_clear(&other);

    vip_channel_list_t channels;
    vip_channel_list_init(&channels);
    vip_channel_t c = {
        .provider_id = creds.provider_id,
        .id = "42",
        .name = "Canal Teste",
        .stream_url = "https://example.com/live/u/p/42.ts",
        .position = 0,
    };
    TEST_STATUS(vip_channel_list_push(&channels, &c, &error), VIP_OK, &error);
    TEST_CHECK(channels.len == 1);
    TEST_CHECK(strcmp(channels.items[0].name, "Canal Teste") == 0);
    vip_channel_list_clear(&channels);
    vip_credentials_clear(&creds);
    return 0;
}
