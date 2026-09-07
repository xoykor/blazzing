/* SPDX-License-Identifier: MIT */
/* Thin virtual-interface wrapper around concrete thumbnail decoder backends. */
#include "visual_iptv/decoder.h"
#include "decoder_internal.h"

#include <stdlib.h>
#include <string.h>

void vip_rgb_frame_clear(vip_rgb_frame_t *frame) {
    if (!frame) return;
    free(frame->data);
    memset(frame, 0, sizeof(*frame));
}

const char *vip_thumbnail_decoder_name(const vip_thumbnail_decoder_t *decoder) {
    return decoder && decoder->name ? decoder->name : "unknown";
}

vip_status_t vip_thumbnail_decoder_capture(vip_thumbnail_decoder_t *decoder,
                                           const char *source,
                                           vip_rgb_frame_t *frame_out,
                                           vip_error_t *error) {
    if (!decoder || !decoder->capture_impl || !source || !frame_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "decoder ou fonte inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    vip_rgb_frame_clear(frame_out);
    return decoder->capture_impl(decoder->impl, source, frame_out, error);
}

void vip_thumbnail_decoder_destroy(vip_thumbnail_decoder_t *decoder) {
    if (!decoder) return;
    if (decoder->destroy_impl) decoder->destroy_impl(decoder->impl);
    free(decoder);
}
