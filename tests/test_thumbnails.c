/* SPDX-License-Identifier: MIT */
/*
 * Regression tests for thumbnails.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/thumbnails.h"
#include "test_common.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct ready_state {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
};

/* Capture the requested state in the fake. */
static vip_status_t fake_capture(const vip_thumbnail_request_t *request, char **path_out, vip_error_t *error,
                                 void *userdata) {
    (void)request;
    (void)error;
    (void)userdata;
    *path_out = vip_strdup("/tmp/fake.jpg");
    return *path_out ? VIP_OK : VIP_ERR_NOMEM;
}

/* Handle the fake ready operation. */
static void fake_ready(const vip_thumbnail_request_t *request, vip_status_t status, const char *path,
                       const vip_error_t *error, void *userdata) {
    (void)request;
    (void)status;
    (void)path;
    (void)error;
    struct ready_state *s = userdata;
    pthread_mutex_lock(&s->mutex);
    s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}

/* Run this executable's main entry point. */
int main(void) {
    vip_error_t error = {0};
    uint8_t black[64 * 36 * 3] = {0};
    TEST_STATUS(vip_thumbnail_validate_rgb(black, 64, 36, 64 * 3, &error), VIP_ERR_INVALID_FRAME, &error);
    uint8_t pattern[64 * 36 * 3];
    for (size_t i = 0; i < sizeof(pattern); ++i)
        pattern[i] = (uint8_t)((i * 37u) & 0xffu);
    TEST_STATUS(vip_thumbnail_validate_rgb(pattern, 64, 36, 64 * 3, &error), VIP_OK, &error);

    char *a = vip_thumbnail_cache_path("/tmp/cache", "p", "42", &error);
    char *b = vip_thumbnail_cache_path("/tmp/cache", "p", "42", &error);
    TEST_CHECK(a && b && strcmp(a, b) == 0);
    free(a);
    free(b);

    char bad_logo[] = "/tmp/blazzing-bad-jpeg-XXXXXX";
    int bad_fd = mkstemp(bad_logo);
    TEST_CHECK(bad_fd >= 0);
    FILE *bad = fdopen(bad_fd, "wb");
    TEST_CHECK(bad != NULL);
    const unsigned char broken_jpeg[16] = {0xff, 0xd8, 0xff, 0xe0, 0, 16, 'J', 'F',
                                           'I',  'F',  0,    1,    2, 3,  4,   5};
    TEST_CHECK(fwrite(broken_jpeg, 1u, sizeof(broken_jpeg), bad) == sizeof(broken_jpeg));
    TEST_CHECK(fclose(bad) == 0);
    vip_thumbnail_decoder_t *decoder = NULL;
    vip_ffmpeg_decoder_config_t cfg = {.ffmpeg_path = "ffmpeg",
                                       .timeout_ms = 1000,
                                       .candidate_frames = 1,
                                       .output_width = 64,
                                       .output_height = 36};
    TEST_STATUS(vip_ffmpeg_decoder_create(&decoder, &cfg, &error), VIP_OK, &error);
    vip_thumbnail_capture_context_t context = {0};
    TEST_STATUS(
        vip_thumbnail_capture_context_init(&context, decoder, "/tmp/blazzing-thumb-test-cache", 82, &error),
        VIP_OK, &error);
    vip_thumbnail_request_t bad_req = {.provider_id = "p",
                                       .channel_id = "broken-jpeg",
                                       .logo_url = bad_logo,
                                       .stream_url = "http://unused",
                                       .priority = 999999};
    char *bad_path = NULL;
    vip_status_t bad_status = vip_thumbnail_capture_with_decoder(&bad_req, &bad_path, &error, &context);
    TEST_CHECK(bad_status != VIP_OK);
    free(bad_path);
    vip_thumbnail_capture_context_clear(&context);
    vip_thumbnail_decoder_destroy(decoder);
    unlink(bad_logo);

    struct ready_state state = {0};
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.cond, NULL);
    vip_thumbnail_scheduler_t *scheduler = NULL;
    TEST_STATUS(vip_thumbnail_scheduler_create(&scheduler, 1, fake_capture, NULL, fake_ready, &state, &error),
                VIP_OK, &error);
    vip_thumbnail_request_t req = {
        .provider_id = "p", .channel_id = "42", .stream_url = "http://x", .priority = 1};
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
