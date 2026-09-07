/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/decoder.h"
#include "visual_iptv/thumbnails.h"
#include "test_common.h"

#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int done;
    vip_status_t status;
    char *path;
} ready_state_t;

static void scheduler_ready(const vip_thumbnail_request_t *request,
                            vip_status_t status,
                            const char *path,
                            const vip_error_t *error,
                            void *userdata) {
    (void)request;
    (void)error;
    ready_state_t *state = userdata;
    pthread_mutex_lock(&state->mutex);
    state->status = status;
    free(state->path);
    state->path = path ? vip_strdup(path) : NULL;
    state->done = 1;
    pthread_cond_signal(&state->cond);
    pthread_mutex_unlock(&state->mutex);
}

static int write_ppm(const char *path, int black) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    const size_t width = 64, height = 36;
    if (fprintf(fp, "P6\n%zu %zu\n255\n", width, height) < 0) { fclose(fp); return 0; }
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            unsigned char pixel[3];
            if (black) {
                pixel[0] = pixel[1] = pixel[2] = 0;
            } else {
                pixel[0] = (unsigned char)((x * 4u) & 0xffu);
                pixel[1] = (unsigned char)((y * 7u) & 0xffu);
                pixel[2] = (unsigned char)(((x + y) * 3u) & 0xffu);
            }
            if (fwrite(pixel, 1, sizeof(pixel), fp) != sizeof(pixel)) { fclose(fp); return 0; }
        }
    }
    return fclose(fp) == 0;
}

int main(void) {
    char temp[] = "/tmp/viptv-decoder-XXXXXX";
    char *dir = mkdtemp(temp);
    TEST_CHECK(dir != NULL);

    char input[512], black[512], cache[512];
    snprintf(input, sizeof(input), "%s/input.ppm", dir);
    snprintf(black, sizeof(black), "%s/black.ppm", dir);
    snprintf(cache, sizeof(cache), "%s/cache", dir);
    TEST_CHECK(write_ppm(input, 0));
    TEST_CHECK(write_ppm(black, 1));

    vip_error_t error = {0};
    vip_thumbnail_decoder_t *decoder = NULL;
    vip_ffmpeg_decoder_config_t config = {
        .timeout_ms = 5000,
        .candidate_frames = 3,
        .output_width = 320,
        .output_height = 180,
    };
    TEST_STATUS(vip_ffmpeg_decoder_create(&decoder, &config, &error), VIP_OK, &error);
    TEST_CHECK(strcmp(vip_thumbnail_decoder_name(decoder), "ffmpeg-cli") == 0);

    vip_rgb_frame_t frame = {0};
    TEST_STATUS(vip_thumbnail_decoder_capture(decoder, input, &frame, &error), VIP_OK, &error);
    TEST_CHECK(frame.data != NULL && frame.width == 320 && frame.height == 180 && frame.stride == 960);
    TEST_STATUS(vip_thumbnail_validate_rgb(frame.data, frame.width, frame.height, frame.stride, &error), VIP_OK, &error);
    vip_rgb_frame_clear(&frame);

    vip_status_t black_status = vip_thumbnail_decoder_capture(decoder, black, &frame, &error);
    TEST_CHECK(black_status == VIP_ERR_INVALID_FRAME);
    vip_rgb_frame_clear(&frame);

    vip_thumbnail_capture_context_t capture = {0};
    TEST_STATUS(vip_thumbnail_capture_context_init(&capture, decoder, cache, 82, &error), VIP_OK, &error);
    vip_thumbnail_request_t request = {
        .provider_id = "test-provider",
        .channel_id = "channel-1",
        .stream_url = input,
        .priority = 100,
    };
    ready_state_t ready = {0};
    pthread_mutex_init(&ready.mutex, NULL);
    pthread_cond_init(&ready.cond, NULL);
    vip_thumbnail_scheduler_t *scheduler = NULL;
    TEST_STATUS(vip_thumbnail_scheduler_create(&scheduler, 1, vip_thumbnail_capture_with_decoder,
                                               &capture, scheduler_ready, &ready, &error), VIP_OK, &error);
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &request, &error), VIP_OK, &error);

    pthread_mutex_lock(&ready.mutex);
    if (!ready.done) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 6;
        (void)pthread_cond_timedwait(&ready.cond, &ready.mutex, &until);
    }
    int done = ready.done;
    vip_status_t ready_status = ready.status;
    char *ready_path = ready.path ? vip_strdup(ready.path) : NULL;
    pthread_mutex_unlock(&ready.mutex);

    TEST_CHECK(done);
    TEST_CHECK(ready_status == VIP_OK);
    TEST_CHECK(ready_path != NULL);
    struct stat st = {0};
    TEST_CHECK(stat(ready_path, &st) == 0 && st.st_size > 0);
    unlink(ready_path);
    free(ready_path);

    vip_thumbnail_scheduler_destroy(scheduler);
    free(ready.path);
    pthread_cond_destroy(&ready.cond);
    pthread_mutex_destroy(&ready.mutex);
    vip_thumbnail_capture_context_clear(&capture);
    vip_thumbnail_decoder_destroy(decoder);
    unlink(input);
    unlink(black);
    rmdir(cache);
    rmdir(dir);
    return 0;
}
