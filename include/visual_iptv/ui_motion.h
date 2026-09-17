/* SPDX-License-Identifier: MIT */
/*
 * Public API contract for ui motion.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#ifndef VISUAL_IPTV_UI_MOTION_H
#define VISUAL_IPTV_UI_MOTION_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float value;
    float target;
    int64_t last_ms;
} vip_ui_motion_t;

/* Initialize the requested state in the ui motion. */
void vip_ui_motion_init(vip_ui_motion_t *motion, float value, int64_t now_ms);
/* Set target in the ui motion. */
void vip_ui_motion_set_target(vip_ui_motion_t *motion, float target, int64_t now_ms);
/* Handle the ui motion step operation. */
bool vip_ui_motion_step(vip_ui_motion_t *motion, int64_t now_ms, int duration_ms);
/* Handle the ui ease out cubic operation. */
float vip_ui_ease_out_cubic(float value);

#endif
