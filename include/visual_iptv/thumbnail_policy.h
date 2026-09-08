/* SPDX-License-Identifier: MIT */
#ifndef VISUAL_IPTV_THUMBNAIL_POLICY_H
#define VISUAL_IPTV_THUMBNAIL_POLICY_H

#include "visual_iptv/thumbnails.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIP_APP_THUMB_INTERACTIVE_PRIORITY INT64_C(500000)
#define VIP_APP_THUMB_MAX_FRAME_CAPTURES 2u

vip_status_t vip_app_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *scheduler,
                                                 const vip_thumbnail_request_t *request,
                                                 vip_error_t *error);

void vip_app_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler);

vip_status_t vip_app_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                     char **path_out,
                                                     vip_error_t *error,
                                                     void *userdata);

bool vip_app_thumbnail_frame_slot_try_acquire(void);
void vip_app_thumbnail_frame_slot_release(void);
unsigned vip_app_thumbnail_frame_slots_active(void);

#ifdef __cplusplus
}
#endif

#endif
