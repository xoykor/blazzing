/* SPDX-License-Identifier: MIT */
#include "visual_iptv/ui_motion.h"

static float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

void vip_ui_motion_init(vip_ui_motion_t *motion, float value, int64_t now_ms) {
    if (!motion) return;
    motion->value = clamp01(value);
    motion->target = motion->value;
    motion->last_ms = now_ms;
}

void vip_ui_motion_set_target(vip_ui_motion_t *motion, float target, int64_t now_ms) {
    if (!motion) return;
    motion->target = clamp01(target);
    if (motion->last_ms <= 0) motion->last_ms = now_ms;
}

bool vip_ui_motion_step(vip_ui_motion_t *motion, int64_t now_ms, int duration_ms) {
    if (!motion) return false;
    if (duration_ms <= 0) {
        motion->value = motion->target;
        motion->last_ms = now_ms;
        return false;
    }
    if (motion->last_ms <= 0) motion->last_ms = now_ms;
    int64_t elapsed = now_ms - motion->last_ms;
    if (elapsed < 0) elapsed = 0;
    motion->last_ms = now_ms;

    float delta = (float)elapsed / (float)duration_ms;
    if (motion->target > motion->value) {
        motion->value += delta;
        if (motion->value >= motion->target) motion->value = motion->target;
    } else if (motion->target < motion->value) {
        motion->value -= delta;
        if (motion->value <= motion->target) motion->value = motion->target;
    }
    motion->value = clamp01(motion->value);
    return motion->value != motion->target;
}

float vip_ui_ease_out_cubic(float value) {
    float t = clamp01(value);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}
