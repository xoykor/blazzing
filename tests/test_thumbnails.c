/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/thumbnails.h"
#include "test_common.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct ready_state {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
};

static vip_status_t fake_capture(const vip_thumbnail_request_t *request, char **path_out,
                                 vip_error_t *error, void *userdata) {
    (void)request; (void)error; (void)userdata;
    *path_out = vip_strdup("/tmp/fake.jpg");
    return *path_out ? VIP_OK : VIP_ERR_NOMEM;
}

static void fake_ready(const vip_thumbnail_request_t *request, vip_status_t status,
                       const char *path, const vip_error_t *error, void *userdata) {
    (void)request; (void)status; (void)path; (void)error;
    struct ready_state *s = userdata;
    pthread_mutex_lock(&s->mutex);
    s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}

int main(void) {
    vip_error_t error = {0};
    uint8_t black[64 * 36 * 3] = {0};
    TEST_STATUS(vip_thumbnail_validate_rgb(black, 64, 36, 64 * 3, &error), VIP_ERR_INVALID_FRAME, &error);
    uint8_t pattern[64 * 36 * 3];
    for (size_t i = 0; i < sizeof(pattern); ++i) pattern[i] = (uint8_t)((i * 37u) & 0xffu);
    TEST_STATUS(vip_thumbnail_validate_rgb(pattern, 64, 36, 64 * 3, &error), VIP_OK, &error);

    char *a = vip_thumbnail_cache_path("/tmp/cache", "p", "42", &error);
    char *b = vip_thumbnail_cache_path("/tmp/cache", "p", "42", &error);
    TEST_CHECK(a && b && strcmp(a, b) == 0);
    free(a); free(b);

    struct ready_state state = {0};
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.cond, NULL);
    vip_thumbnail_scheduler_t *scheduler = NULL;
    TEST_STATUS(vip_thumbnail_scheduler_create(&scheduler, 1, fake_capture, NULL, fake_ready, &state, &error), VIP_OK, &error);
    vip_thumbnail_request_t req = {.provider_id="p", .channel_id="42", .stream_url="http://x", .priority=1};
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &req, &error), VIP_OK, &error);
    req.priority = 99;
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &req, &error), VIP_OK, &error);

    pthread_mutex_lock(&state.mutex);
    if (state.count == 0) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 2;
        pthread_cond_timedwait(&state.cond, &state.mutex, &until);
    }
    int count = state.count;
    pthread_mutex_unlock(&state.mutex);
    TEST_CHECK(count == 1);
    vip_thumbnail_scheduler_destroy(scheduler);
    pthread_cond_destroy(&state.cond);
    pthread_mutex_destroy(&state.mutex);
    return 0;
}
