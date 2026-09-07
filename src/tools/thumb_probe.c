/* SPDX-License-Identifier: MIT */
/* Small developer utility for exercising the thumbnail capture pipeline. */
#include "visual_iptv/decoder.h"
#include "visual_iptv/thumbnails.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "uso: %s <fonte> <thumbnail.jpg>\n", argv[0]);
        return 2;
    }

    vip_error_t error = {0};
    vip_thumbnail_decoder_t *decoder = NULL;
    vip_ffmpeg_decoder_config_t config = {
        .timeout_ms = 12000,
        .candidate_frames = 6,
        .output_width = 320,
        .output_height = 180,
    };
    vip_status_t st = vip_ffmpeg_decoder_create(&decoder, &config, &error);
    if (st != VIP_OK) {
        fprintf(stderr, "decoder: %s\n", error.message);
        return 1;
    }

    vip_rgb_frame_t frame = {0};
    st = vip_thumbnail_decoder_capture(decoder, argv[1], &frame, &error);
    if (st == VIP_OK)
        st = vip_thumbnail_save_rgb_jpeg(frame.data, frame.width, frame.height, frame.stride,
                                         argv[2], 82, &error);

    vip_rgb_frame_clear(&frame);
    vip_thumbnail_decoder_destroy(decoder);
    if (st != VIP_OK) {
        fprintf(stderr, "captura: %s\n", error.message);
        return 1;
    }
    printf("thumbnail criada: %s\n", argv[2]);
    return 0;
}
