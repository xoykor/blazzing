/* SPDX-License-Identifier: MIT */
/* Internal vtable used to keep thumbnail callers independent of FFmpeg details. */
#ifndef VISUAL_IPTV_DECODER_INTERNAL_H
#define VISUAL_IPTV_DECODER_INTERNAL_H

#include "visual_iptv/decoder.h"

typedef vip_status_t (*vip_decoder_capture_impl_fn)(void *impl,
                                                    const char *source,
                                                    vip_rgb_frame_t *frame_out,
                                                    vip_error_t *error);
typedef void (*vip_decoder_destroy_impl_fn)(void *impl);

struct vip_thumbnail_decoder {
    const char *name;
    void *impl;
    vip_decoder_capture_impl_fn capture_impl;
    vip_decoder_destroy_impl_fn destroy_impl;
};

#endif
