/* SPDX-License-Identifier: MIT */
/*
 * Application-level thumbnail scheduling policy.
 *
 * The generic scheduler intentionally knows nothing about IPTV semantics.
 * These GNU ld --wrap hooks apply Blazzing's policy without coupling the
 * scheduler to the X11 layer:
 *   - background prefetch only downloads provider artwork;
 *   - stream-frame generation is interactive-only;
 *   - at most two FFmpeg frame captures may run at once;
 *   - viewport changes do not destructively flush pending cache warming.
 */
#include "visual_iptv/thumbnails.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define THUMB_INTERACTIVE_PRIORITY INT64_C(500000)
#define THUMB_MAX_FRAME_CAPTURES 2u

static atomic_uint active_frame_captures = 0u;

/* Handle the real thumbnail scheduler enqueue operation. */
extern vip_status_t __real_vip_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *scheduler,
                                                           const vip_thumbnail_request_t *request,
                                                           vip_error_t *error);
/* Handle the real thumbnail scheduler cancel pending operation. */
extern void __real_vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler);
/* Capture with decoder in the thumbnail subsystem. */
extern vip_status_t __real_vip_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                              char **path_out, vip_error_t *error,
                                                              void *userdata);

/* Return whether artwork. */
static bool has_artwork(const vip_thumbnail_request_t *request) {
    return request && request->logo_url && request->logo_url[0] != '\0';
}

/* Handle the remote stream operation. */
static bool remote_stream(const vip_thumbnail_request_t *request) {
    const char *url = request ? request->stream_url : NULL;
    return url && (!strncmp(url, "http://", 7u) || !strncmp(url, "https://", 8u));
}

/* Capture enabled in the remote. */
static bool remote_capture_enabled(void) {
    const char *value = getenv("VIPTV_ALLOW_REMOTE_THUMB_CAPTURE");
    return value && value[0] && strcmp(value, "0") != 0;
}

/* Handle the frame slot try acquire operation. */
static bool frame_slot_try_acquire(void) {
    unsigned current = atomic_load_explicit(&active_frame_captures, memory_order_relaxed);
    while (current < THUMB_MAX_FRAME_CAPTURES) {
        if (atomic_compare_exchange_weak_explicit(&active_frame_captures, &current, current + 1u,
                                                  memory_order_acquire, memory_order_relaxed))
            return true;
    }
    return false;
}

/* Handle the wrap thumbnail scheduler enqueue operation. */
vip_status_t __wrap_vip_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *scheduler,
                                                    const vip_thumbnail_request_t *request,
                                                    vip_error_t *error) {
    if (!request)
        return VIP_ERR_INVALID_ARGUMENT;

    /* Never let background catalog warming turn into stream opens.  A
       logo-less channel may still request a frame when its card becomes
       visible because viewport priorities are above the interactive cutoff. */
    if (!has_artwork(request) && request->priority < THUMB_INTERACTIVE_PRIORITY) {
        vip_error_clear(error);
        return VIP_OK;
    }
    /* Passing authenticated stream URLs to ffmpeg via argv exposes them to
       local process inspection and slow stream opens can starve artwork jobs.
       Keep remote frame capture disabled unless the user explicitly opts in. */
    if (!has_artwork(request) && remote_stream(request) && !remote_capture_enabled()) {
        vip_error_clear(error);
        return VIP_OK;
    }

    return __real_vip_thumbnail_scheduler_enqueue(scheduler, request, error);
}

/* Handle the wrap thumbnail scheduler cancel pending operation. */
void __wrap_vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler) {
    /* Callers use cancellation only at provider/list boundaries. Viewport and
       search changes no longer call this function, so ordinary navigation
       keeps cache warming while a provider switch drops stale queued jobs. */
    __real_vip_thumbnail_scheduler_cancel_pending(scheduler);
}

/* Capture with decoder in the thumbnail subsystem. */
vip_status_t __wrap_vip_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                       char **path_out, vip_error_t *error, void *userdata) {
    if (!request)
        return VIP_ERR_INVALID_ARGUMENT;

    if (has_artwork(request))
        return __real_vip_thumbnail_capture_with_decoder(request, path_out, error, userdata);

    /* Belt-and-suspenders guard: background logo-less work should have been
       filtered by enqueue already. Do not open a stream if it reaches here. */
    if (request->priority < THUMB_INTERACTIVE_PRIORITY) {
        vip_error_set(error, VIP_ERR_CANCELLED, "captura de frame ignorada fora do viewport");
        return VIP_ERR_CANCELLED;
    }
    if (remote_stream(request) && !remote_capture_enabled()) {
        vip_error_set(error, VIP_ERR_CANCELLED, "captura remota de frame desativada por privacidade");
        return VIP_ERR_CANCELLED;
    }

    /* FFmpeg stream opens can take seconds. Limit them independently from
       artwork downloads so two bad/no-logo channels cannot occupy the whole
       8-16 worker pool. Busy items are retried by normal viewport redraws. */
    if (!frame_slot_try_acquire()) {
        vip_error_set(error, VIP_ERR_CANCELLED, "captura de frame adiada: limite de FFmpeg ativo");
        return VIP_ERR_CANCELLED;
    }

    vip_status_t st = __real_vip_thumbnail_capture_with_decoder(request, path_out, error, userdata);
    atomic_fetch_sub_explicit(&active_frame_captures, 1u, memory_order_release);
    return st;
}
