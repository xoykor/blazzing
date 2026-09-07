/* SPDX-License-Identifier: MIT */
/**
 * @file player_mpv.h
 * @brief Persistent mpv process controlled through JSON IPC.
 *
 * mpv owns the video rendering path.  On X11, the backend discovers mpv's
 * native window and reparents it into the UI video container instead of
 * copying decoded frames through the application.
 */
#ifndef VISUAL_IPTV_PLAYER_MPV_H
#define VISUAL_IPTV_PLAYER_MPV_H

#include "visual_iptv/core.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vip_mpv_player vip_mpv_player_t;

/** Player construction options. */
typedef struct {
    const char *mpv_path;       /**< Executable path/name; NULL selects "mpv". */
    unsigned long window_id;    /**< X11 container window used for reparenting. */
    bool audio;                 /**< Whether audio output should be enabled. */
    unsigned startup_grace_ms;  /**< Grace period for IPC/runtime startup. */
} vip_mpv_player_config_t;

/** Thread-safe copy of the latest observed mpv state. */
typedef struct {
    vip_player_state_t state;
    bool running;
    bool paused;
    bool buffering;
    bool seekable;
    bool natural_end;
    double position_seconds;
    double duration_seconds;
    double percent_pos;
    double volume;
    double cache_duration_seconds;
    bool has_video;
    int video_width;
    int video_height;
    char video_codec[64];
    char vo[64];
    char hwdec[64];
    char last_event[64];
    bool exit_code_valid;
    int exit_code;
    uint64_t serial; /**< Incremented when observable state changes. */
} vip_mpv_player_snapshot_t;

/** Create the adapter; the mpv process is started lazily on first load. */
vip_status_t vip_mpv_player_create(vip_mpv_player_t **out,
                                   const vip_mpv_player_config_t *config,
                                   vip_error_t *error);
void vip_mpv_player_destroy(vip_mpv_player_t *player);

/** Load media at the beginning into the persistent runtime. */
vip_status_t vip_mpv_player_load(vip_mpv_player_t *player,
                                 const char *url,
                                 vip_error_t *error);
/** Load media and request an initial absolute seek after file load. */
vip_status_t vip_mpv_player_load_at(vip_mpv_player_t *player,
                                    const char *url,
                                    double start_seconds,
                                    vip_error_t *error);
void vip_mpv_player_stop(vip_mpv_player_t *player);
void vip_mpv_player_set_paused(vip_mpv_player_t *player, bool paused);
bool vip_mpv_player_is_paused(vip_mpv_player_t *player);
bool vip_mpv_player_is_running(vip_mpv_player_t *player);
vip_player_state_t vip_mpv_player_state(vip_mpv_player_t *player);
/** Copy the current player state without exposing internal locks. */
void vip_mpv_player_snapshot(vip_mpv_player_t *player, vip_mpv_player_snapshot_t *out);
vip_status_t vip_mpv_player_seek(vip_mpv_player_t *player, double position_seconds, vip_error_t *error);
vip_status_t vip_mpv_player_seek_relative(vip_mpv_player_t *player, double delta_seconds, vip_error_t *error);
vip_status_t vip_mpv_player_set_volume(vip_mpv_player_t *player, double volume, vip_error_t *error);
const char *vip_mpv_player_state_name(vip_player_state_t state);
/** Return a sanitized diagnostic string; stream URLs are redacted. */
const char *vip_mpv_player_last_error(vip_mpv_player_t *player);

#ifdef __cplusplus
}
#endif

#endif
