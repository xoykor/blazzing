/* SPDX-License-Identifier: MIT */
/**
 * @file thumbnails.h
 * @brief Concurrent artwork download, decode and thumbnail cache pipeline.
 */
#ifndef VISUAL_IPTV_THUMBNAILS_H
#define VISUAL_IPTV_THUMBNAILS_H

#include "visual_iptv/core.h"
#include "visual_iptv/decoder.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One scheduler job. Higher priority values are processed first. */
typedef struct {
    char *provider_id;
    char *channel_id;
    char *logo_url;
    char *stream_url;
    int64_t priority;
} vip_thumbnail_request_t;

typedef struct vip_thumbnail_scheduler vip_thumbnail_scheduler_t;

/** Shared data used by the default capture callback. */
typedef struct {
    vip_thumbnail_decoder_t *decoder;
    char *cache_dir;
    int jpeg_quality;
} vip_thumbnail_capture_context_t;

typedef vip_status_t (*vip_thumbnail_capture_fn)(const vip_thumbnail_request_t *request,
                                                 char **path_out,
                                                 vip_error_t *error,
                                                 void *userdata);
typedef void (*vip_thumbnail_ready_fn)(const vip_thumbnail_request_t *request,
                                       vip_status_t status,
                                       const char *path,
                                       const vip_error_t *error,
                                       void *userdata);

/** Initialize the default artwork/captured-frame implementation context. */
vip_status_t vip_thumbnail_capture_context_init(vip_thumbnail_capture_context_t *context,
                                                vip_thumbnail_decoder_t *decoder,
                                                const char *cache_dir,
                                                int jpeg_quality,
                                                vip_error_t *error);
void vip_thumbnail_capture_context_clear(vip_thumbnail_capture_context_t *context);
/** Download/decode artwork or capture a fallback frame, then cache it as JPEG. */
vip_status_t vip_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                char **path_out,
                                                vip_error_t *error,
                                                void *userdata);

/** Create a priority scheduler with worker_count background pthreads. */
vip_status_t vip_thumbnail_scheduler_create(vip_thumbnail_scheduler_t **out,
                                            size_t worker_count,
                                            vip_thumbnail_capture_fn capture,
                                            void *capture_userdata,
                                            vip_thumbnail_ready_fn ready,
                                            void *ready_userdata,
                                            vip_error_t *error);
void vip_thumbnail_scheduler_destroy(vip_thumbnail_scheduler_t *scheduler);
/** Pause/resume workers without discarding queued requests. */
void vip_thumbnail_scheduler_set_paused(vip_thumbnail_scheduler_t *scheduler, bool paused);
/** Invalidate jobs that have not started; running jobs are allowed to finish. */
void vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler);
/** Enqueue or reprioritize a provider/item request. */
vip_status_t vip_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *scheduler,
                                             const vip_thumbnail_request_t *request,
                                             vip_error_t *error);

/** Build a deterministic cache file path for a provider/item pair. */
char *vip_thumbnail_cache_path(const char *cache_dir,
                               const char *provider_id,
                               const char *channel_id,
                               vip_error_t *error);

/** Validate RGB dimensions/stride before image encoding. */
vip_status_t vip_thumbnail_validate_rgb(const uint8_t *rgb,
                                        size_t width,
                                        size_t height,
                                        size_t stride,
                                        vip_error_t *error);

/** Save packed RGB data as a JPEG, creating parent directories as needed. */
vip_status_t vip_thumbnail_save_rgb_jpeg(const uint8_t *rgb,
                                         size_t width,
                                         size_t height,
                                         size_t stride,
                                         const char *path,
                                         int quality,
                                         vip_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
