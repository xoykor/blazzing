/* SPDX-License-Identifier: MIT */
#include "test_common.h"
#include "visual_iptv/streaming_services.h"

#include <string.h>

int main(void) {
    TEST_CHECK(vip_streaming_service_count() == (size_t)VIP_SERVICE_COUNT);

    const vip_streaming_service_t *pluto = vip_streaming_service_get(VIP_SERVICE_PLUTO);
    const vip_streaming_service_t *prime = vip_streaming_service_get(VIP_SERVICE_PRIME_VIDEO);
    const vip_streaming_service_t *max = vip_streaming_service_get(VIP_SERVICE_MAX);
    const vip_streaming_service_t *globoplay = vip_streaming_service_get(VIP_SERVICE_GLOBOPLAY);

    TEST_CHECK(pluto != NULL && pluto->playback_mode == VIP_SERVICE_PLAY_NATIVE);
    TEST_CHECK(prime != NULL && prime->account_required);
    TEST_CHECK(max != NULL && max->playback_mode == VIP_SERVICE_PLAY_EXTERNAL_WEB);
    TEST_CHECK(globoplay != NULL && strstr(globoplay->home_url, "globoplay") != NULL);
    return 0;
}
