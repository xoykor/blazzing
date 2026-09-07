/* SPDX-License-Identifier: MIT */
/**
 * @file decoder.h
 * @brief Small abstraction used to capture RGB thumbnail frames from media.
 */
#ifndef VISUAL_IPTV_DECODER_H
#define VISUAL_IPTV_DECODER_H

#include "visual_iptv/core.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vip_thumbnail_decoder vip_thumbnail_decoder_t;

/** Heap-owned packed RGB frame returned by a thumbnail decoder. */
typedef struct {
    uint8_t *data;
    size_t width;
    size_t height;
    size_t stride;
} vip_rgb_frame_t;

/** Configuration for the FFmpeg CLI decoder backend. */
typedef struct {
    const char *ffmpeg_path;
    unsigned timeout_ms;
    unsigned candidate_frames;
    size_t output_width;
    size_t output_height;
} vip_ffmpeg_decoder_config_t;

void vip_rgb_frame_clear(vip_rgb_frame_t *frame);
const char *vip_thumbnail_decoder_name(const vip_thumbnail_decoder_t *decoder);
/** Capture one representative frame from source into frame_out. */
vip_status_t vip_thumbnail_decoder_capture(vip_thumbnail_decoder_t *decoder,
                                           const char *source,
                                           vip_rgb_frame_t *frame_out,
                                           vip_error_t *error);
void vip_thumbnail_decoder_destroy(vip_thumbnail_decoder_t *decoder);

/** Construct the subprocess-based FFmpeg decoder backend. */
vip_status_t vip_ffmpeg_decoder_create(vip_thumbnail_decoder_t **out,
                                       const vip_ffmpeg_decoder_config_t *config,
                                       vip_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
