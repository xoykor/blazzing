/* SPDX-License-Identifier: MIT */
#include "visual_iptv/ui_motion.h"

#include <assert.h>

int main(void) {
    vip_ui_motion_t motion;
    vip_ui_motion_init(&motion, 0.0f, 1000);
    vip_ui_motion_set_target(&motion, 1.0f, 1000);

    assert(vip_ui_motion_step(&motion, 1050, 200));
    assert(motion.value > 0.24f && motion.value < 0.26f);
    assert(vip_ui_motion_step(&motion, 1150, 200));
    assert(motion.value > 0.74f && motion.value < 0.76f);
    assert(!vip_ui_motion_step(&motion, 1250, 200));
    assert(motion.value == 1.0f);

    vip_ui_motion_set_target(&motion, 0.0f, 1250);
    assert(vip_ui_motion_step(&motion, 1350, 200));
    assert(motion.value > 0.49f && motion.value < 0.51f);
    assert(!vip_ui_motion_step(&motion, 1450, 200));
    assert(motion.value == 0.0f);

    assert(vip_ui_ease_out_cubic(0.0f) == 0.0f);
    assert(vip_ui_ease_out_cubic(1.0f) == 1.0f);
    assert(vip_ui_ease_out_cubic(0.5f) > 0.8f);
    return 0;
}
