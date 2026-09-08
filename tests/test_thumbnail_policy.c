/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/thumbnails.h"
#include "test_common.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

struct ready_state {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
};

static vip_status_t fake_capture(const vip_thumbnail_request_t *request,
                                 char **path_out,
                                 vip_error_t *error,
                                 void *userdata) {
    (void)request; (void)error; (void)userdata;
    *path_out = vip_strdup("/tmp/policy-fake.jpg");
    return *path_out ? VIP_OK : VIP_ERR_NOMEM;
}

static void fake_ready(const vip_thumbnail_request_t *request,
                       vip_status_t status,
                       const char *path,
                       const vip_error_t *error,
                       void *userdata) {
    (void)request; (void)status; (void)path; (void)error;
    struct ready_state *state = userdata;
    pthread_mutex_lock(&state->mutex);
    state->count++;
    pthread_cond_broadcast(&state->cond);
    pthread_mutex_unlock(&state->mutex);
}

static int wait_for_count(struct ready_state *state, int wanted, long milliseconds) {
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += milliseconds / 1000L;
    until.tv_nsec += (milliseconds % 1000L) * 1000000L;
    if (until.tv_nsec >= 1000000000L) {
        until.tv_sec += 1;
        until.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&state->mutex);
    while (state->count < wanted) {
        if (pthread_cond_timedwait(&state->cond, &state->mutex, &until) != 0) break;
    }
    int count = state->count;
    pthread_mutex_unlock(&state->mutex);
    return count;
}

int main(void) {
    vip_error_t error = {0};
    struct ready_state state = {0};
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.cond, NULL);

    vip_thumbnail_scheduler_t *scheduler = NULL;
    TEST_STATUS(vip_thumbnail_scheduler_create(&scheduler, 1, fake_capture, NULL,
                                               fake_ready, &state, &error), VIP_OK, &error);

    /* Background requests without provider artwork must not enter the worker
       queue: otherwise a large live-TV catalog can monopolize workers with
       slow FFmpeg stream captures. */
    vip_thumbnail_request_t no_art = {
        .provider_id = "p",
        .channel_id = "no-art-background",
        .logo_url = NULL,
        .stream_url = "http://stream.invalid/live",
        .priority = 10000
    };
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &no_art, &error), VIP_OK, &error);
    TEST_CHECK(wait_for_count(&state, 1, 100L) == 0);

    /* The same no-logo item is allowed once it is visible/interactive. */
    no_art.priority = 600000;
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &no_art, &error), VIP_OK, &error);
    TEST_CHECK(wait_for_count(&state, 1, 1000L) == 1);

    /* cancel_pending is intentionally non-destructive at the application
       policy layer. Simulate a scroll while a background artwork request is
       paused, then ensure it still runs after resume. */
    vip_thumbnail_scheduler_set_paused(scheduler, true);
    vip_thumbnail_request_t artwork = {
        .provider_id = "p",
        .channel_id = "artwork-kept-across-scroll",
        .logo_url = "https://example.invalid/poster.jpg",
        .stream_url = "http://stream.invalid/vod",
        .priority = 10000
    };
    TEST_STATUS(vip_thumbnail_scheduler_enqueue(scheduler, &artwork, &error), VIP_OK, &error);
    vip_thumbnail_scheduler_cancel_pending(scheduler);
    vip_thumbnail_scheduler_set_paused(scheduler, false);
    TEST_CHECK(wait_for_count(&state, 2, 1000L) == 2);

    vip_thumbnail_scheduler_destroy(scheduler);
    pthread_cond_destroy(&state.cond);
    pthread_mutex_destroy(&state.mutex);
    return 0;
}
