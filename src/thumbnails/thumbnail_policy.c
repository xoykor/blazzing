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
#include <stdint.h>

#define THUMB_INTERACTIVE_PRIORITY INT64_C(500000)
#define THUMB_MAX_FRAME_CAPTURES 2u

static atomic_uint active_frame_captures = 0u;

extern vip_status_t __real_vip_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *scheduler,
                                                           const vip_thumbnail_request_t *request,
                                                           vip_error_t *error);
extern void __real_vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler);
extern vip_status_t __real_vip_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                               char **path_out,
                                                               vip_error_t *error,
                                                               void *userdata);

static bool has_artwork(const vip_thumbnail_request_t *request) {
    return request && request->logo_url && request->logo_url[0] != '\0';
}

static bool frame_slot_try_acquire(void) {
    unsigned current = atomic_load_explicit(&active_frame_captures, memory_order_relaxed);
    while (current < THUMB_MAX_FRAME_CAPTURES) {
        if (atomic_compare_exchange_weak_explicit(&active_frame_captures,
                                                  &current,
                                                  current + 1u,
                                                  memory_order_acquire,
                                                  memory_order_relaxed))
            return true;
    }
    return false;
}

vip_status_t __wrap_vip_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *scheduler,
                                                     const vip_thumbnail_request_t *request,
                                                     vip_error_t *error) {
    if (!request) return VIP_ERR_INVALID_ARGUMENT;

    /* Never let background catalog warming turn into stream opens.  A
       logo-less channel may still request a frame when its card becomes
       visible because viewport priorities are above the interactive cutoff. */
    if (!has_artwork(request) && request->priority < THUMB_INTERACTIVE_PRIORITY) {
        vip_error_clear(error);
        return VIP_OK;
    }

    return __real_vip_thumbnail_scheduler_enqueue(scheduler, request, error);
}

void __wrap_vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler) {
    /* Scroll/filter rebuilds used to erase the entire queue. Keeping queued
       requests alive means cache warming continues while the user navigates.
       Requests own copies of their strings, so retaining them is safe across
       viewport changes and even provider/profile switches. */
    (void)scheduler;
}

vip_status_t __wrap_vip_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                        char **path_out,
                                                        vip_error_t *error,
                                                        void *userdata) {
    if (!request) return VIP_ERR_INVALID_ARGUMENT;

    if (has_artwork(request))
        return __real_vip_thumbnail_capture_with_decoder(request, path_out, error, userdata);

    /* Belt-and-suspenders guard: background logo-less work should have been
       filtered by enqueue already. Do not open a stream if it reaches here. */
    if (request->priority < THUMB_INTERACTIVE_PRIORITY) {
        vip_error_set(error, VIP_ERR_CANCELLED, "captura de frame ignorada fora do viewport");
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
