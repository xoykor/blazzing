/* SPDX-License-Identifier: MIT */
/*
 * X11 Player Bridge: window embedding, fullscreen control, playback commands,
 * progress saving, HUD layout, and failover routing for mpv.
 */
#include "x11_app_internal.h"

typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
} motif_wm_hints_t;

#define MWM_HINTS_DECORATIONS (1UL << 1)

static bool current_item_has_progress(app_t *a) {
    return a && !a->player_item_live && a->current_channel < ACTIVE_CHANNELS(a).len && a->db;
}

static void update_series_progress(app_t *a, const char *last_episode_id) {
    if (!a->series_episode_mode || !a->series_parent_id[0] || !a->db || ACTIVE_CHANNELS(a).len == 0u)
        return;
    int watched = 0;
    if (a->progress_flags) {
        for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i)
            if (a->progress_flags[i].completed)
                ++watched;
    }
    vip_channel_t *first = &ACTIVE_CHANNELS(a).items[0];
    vip_error_t error = {0};
    (void)vip_database_set_series_progress(a->db, first->provider_id, a->series_parent_id, last_episode_id,
                                           watched, (int)ACTIVE_CHANNELS(a).len, &error);
}

static bool progress_snapshot_valid(app_t *a, const vip_mpv_player_snapshot_t *snap, bool force, int64_t now) {
    if (snap->natural_end && a->progress_flags && a->current_channel < ACTIVE_CHANNELS(a).len &&
        a->progress_flags[a->current_channel].completed)
        return false;
    if (!force && !snap->natural_end && now - a->player_last_progress_save_ms < 5000)
        return false;
    return snap->position_seconds >= 0.0 && (snap->duration_seconds > 1.0 || snap->natural_end);
}

static bool progress_snapshot_completed(const vip_mpv_player_snapshot_t *snap) {
    if (snap->natural_end)
        return true;
    if (snap->duration_seconds <= 0.0)
        return false;
    double ratio = snap->position_seconds / snap->duration_seconds;
    bool near_end = snap->duration_seconds > 300.0 &&
                    snap->duration_seconds - snap->position_seconds <= 60.0;
    return ratio >= 0.95 || near_end;
}

static void update_progress_cache(app_t *a, const vip_mpv_player_snapshot_t *snap, bool completed) {
    if (!a->progress_flags || a->current_channel >= ACTIVE_CHANNELS(a).len)
        return;
    vip_watch_progress_t *progress = &a->progress_flags[a->current_channel];
    progress->position_seconds = snap->position_seconds;
    progress->duration_seconds = snap->duration_seconds;
    progress->completed = completed;
    progress->updated_at = (int64_t)time(NULL);
}

void save_current_progress(app_t *a, bool force) {
    if (!current_item_has_progress(a) || !a->player)
        return;

    int64_t now = monotonic_ms();
    vip_mpv_player_snapshot_t snap = {0};
    vip_mpv_player_snapshot(a->player, &snap);
    if (!progress_snapshot_valid(a, &snap, force, now))
        return;

    bool completed = progress_snapshot_completed(&snap);
    vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[a->current_channel];
    vip_error_t error = {0};
    if (vip_database_set_progress(a->db, ch->provider_id, ch->id, snap.position_seconds,
                                  snap.duration_seconds, completed, &error) != VIP_OK)
        return;

    update_progress_cache(a, &snap, completed);
    if (a->series_episode_mode)
        update_series_progress(a, ch->id);
    a->player_last_progress_save_ms = now;
}

bool player_hud_visible(app_t *a) {
    return !a->fullscreen || a->timeline_dragging || monotonic_ms() < a->player_hud_until_ms;
}

void show_player_hud(app_t *a) {
    a->player_hud_until_ms = monotonic_ms() + 3000;
    if (a->screen == SCREEN_PLAYER)
        layout_video_window(a);
}

void focus_player_input(app_t *a) {
    if (!a || !a->dpy || !a->win || a->screen != SCREEN_PLAYER)
        return;
    XWindowAttributes attrs;
    if (XGetWindowAttributes(a->dpy, a->win, &attrs) && attrs.map_state == IsViewable) {
        XSetInputFocus(a->dpy, a->win, RevertToParent, CurrentTime);
        XFlush(a->dpy);
    }
}

void recover_player_focus_if_needed(app_t *a, const XFocusChangeEvent *focus_event) {
    if (!a || !focus_event || a->screen != SCREEN_PLAYER)
        return;
    if (focus_event->detail == NotifyInferior) {
        if (getenv("VIPTV_MPV_DEBUG"))
            fprintf(stderr, "[mpv-debug] foco entrou na subárvore do player; devolvendo a ui\n");
        focus_player_input(a);
    }
}

void format_clock(double seconds, char out[32]) {
    if (seconds < 0.0)
        seconds = 0.0;
    long s = (long)(seconds + 0.5);
    long h = s / 3600;
    long m = (s % 3600) / 60;
    long sec = s % 60;
    if (h > 0)
        snprintf(out, 32, "%ld:%02ld:%02ld", h, m, sec);
    else
        snprintf(out, 32, "%02ld:%02ld", m, sec);
}

void timeline_geometry(app_t *a, int *x, int *y, int *w, int *h) {
    int left = 230;
    int right = 170;
    *x = left;
    *w = a->width - left - right;
    if (*w < 120)
        *w = 120;
    *y = a->height - PLAYER_CONTROLS_H + 30;
    *h = 14;
}

bool wm_reports_fullscreen(app_t *a) {
    if (!a || !a->dpy || !a->win)
        return false;
    Atom wm_state = XInternAtom(a->dpy, "_NET_WM_STATE", False);
    Atom fs = XInternAtom(a->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *data = NULL;
    bool found = false;
    if (XGetWindowProperty(a->dpy, a->win, wm_state, 0, 64, False, XA_ATOM, &actual_type, &actual_format,
                           &nitems, &bytes_after, &data) == Success &&
        data && actual_format == 32) {
        Atom *atoms = (Atom *)data;
        for (unsigned long i = 0; i < nitems; ++i) {
            if (atoms[i] == fs) {
                found = true;
                break;
            }
        }
    }
    if (data)
        XFree(data);
    return found;
}

static void set_window_decorations(app_t *a, bool enabled) {
    if (!a || !a->dpy || !a->win)
        return;
    Atom motif = XInternAtom(a->dpy, "_MOTIF_WM_HINTS", False);
    motif_wm_hints_t hints = {0};
    hints.flags = MWM_HINTS_DECORATIONS;
    hints.decorations = enabled ? 1UL : 0UL;
    XChangeProperty(a->dpy, a->win, motif, motif, 32, PropModeReplace, (unsigned char *)&hints, 5);
}

static void remember_windowed_geometry(app_t *a) {
    if (!a || !a->dpy || !a->win || a->windowed_geometry_valid)
        return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(a->dpy, a->win, &wa))
        return;
    Window child = None;
    int root_x = 0, root_y = 0;
    if (!XTranslateCoordinates(a->dpy, a->win, RootWindow(a->dpy, a->screen_num), 0, 0, &root_x, &root_y,
                               &child)) {
        root_x = wa.x;
        root_y = wa.y;
    }
    a->windowed_x = root_x;
    a->windowed_y = root_y;
    a->windowed_w = wa.width;
    a->windowed_h = wa.height;
    a->windowed_geometry_valid = wa.width > 0 && wa.height > 0;
}

static void send_fullscreen_request(app_t *a, bool enable) {
    if (!a || !a->dpy || !a->win)
        return;
    Atom wm_state = XInternAtom(a->dpy, "_NET_WM_STATE", False);
    Atom fs = XInternAtom(a->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    XEvent e;
    memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.window = a->win;
    e.xclient.message_type = wm_state;
    e.xclient.format = 32;
    e.xclient.data.l[0] = enable ? 1 : 0; /* _NET_WM_STATE_ADD / REMOVE */
    e.xclient.data.l[1] = (long)fs;
    e.xclient.data.l[2] = 0;
    e.xclient.data.l[3] = 1; /* normal application, per EWMH source indication */
    e.xclient.data.l[4] = 0;
    XSendEvent(a->dpy, RootWindow(a->dpy, a->screen_num), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XRaiseWindow(a->dpy, a->win);
    XFlush(a->dpy);
}

static void apply_borderless_fullscreen(app_t *a) {
    if (!a || !a->dpy || !a->win)
        return;
    set_window_decorations(a, false);
    int sw = DisplayWidth(a->dpy, a->screen_num);
    int sh = DisplayHeight(a->dpy, a->screen_num);
    if (sw < 1)
        sw = a->width;
    if (sh < 1)
        sh = a->height;
    XMoveResizeWindow(a->dpy, a->win, 0, 0, (unsigned)sw, (unsigned)sh);
    XRaiseWindow(a->dpy, a->win);
    XSync(a->dpy, False);
    a->fullscreen_fallback = true;
    a->fullscreen = true;
    layout_video_window(a);
}

void set_fullscreen(app_t *a, bool enable) {
    if (!a || !a->dpy || !a->win)
        return;
    a->fullscreen_requested = enable;
    a->fullscreen_attempts = 0;
    a->fullscreen_retry_at_ms = 0;
    if (enable) {
        remember_windowed_geometry(a);
        a->fullscreen_fallback = false;
        send_fullscreen_request(a, true);
        ++a->fullscreen_attempts;
        XSync(a->dpy, False);
        a->fullscreen = wm_reports_fullscreen(a);
        a->fullscreen_retry_at_ms = monotonic_ms() + 140;
    } else {
        send_fullscreen_request(a, false);
        a->fullscreen_fallback = false;
        a->fullscreen = false;
        set_window_decorations(a, true);
        XSync(a->dpy, False);
        if (a->windowed_geometry_valid) {
            XMoveResizeWindow(a->dpy, a->win, a->windowed_x, a->windowed_y, (unsigned)a->windowed_w,
                              (unsigned)a->windowed_h);
        }
        XFlush(a->dpy);
        a->windowed_geometry_valid = false;
    }
}

void maybe_enforce_fullscreen(app_t *a) {
    if (!a || !a->fullscreen_requested || a->fullscreen || monotonic_ms() < a->fullscreen_retry_at_ms)
        return;
    if (wm_reports_fullscreen(a)) {
        a->fullscreen = true;
        layout_video_window(a);
        return;
    }
    if (a->fullscreen_attempts < 4) {
        send_fullscreen_request(a, true);
        ++a->fullscreen_attempts;
        a->fullscreen_retry_at_ms = monotonic_ms() + 160;
        return;
    }
    fprintf(stderr, "[player] window manager não confirmou fullscreen; usando fallback sem bordas\n");
    apply_borderless_fullscreen(a);
}

static bool stream_needs_forced_hls(const char *url) {
    if (!url || !url[0])
        return false;
    const char *end = strpbrk(url, "?#");
    size_t n = end ? (size_t)(end - url) : strlen(url);
    return n >= 4u && strncasecmp(url + n - 4u, ".txt", 4u) == 0;
}

static void debug_player_container(app_t *a) {
    if (!getenv("VIPTV_MPV_DEBUG"))
        return;
    XWindowAttributes wa;
    if (XGetWindowAttributes(a->dpy, a->video_win, &wa))
        fprintf(stderr, "[mpv-debug] container-window mapped=%s size=%dx%d\n",
                wa.map_state == IsViewable ? "yes" : "no", wa.width, wa.height);
}

static double saved_resume_position(app_t *a, const vip_channel_t *ch) {
    if (a->player_item_live || !a->db)
        return 0.0;
    vip_watch_progress_t saved = {0};
    vip_error_t error = {0};
    if (vip_database_get_progress(a->db, ch->provider_id, ch->id, &saved, &error) != VIP_OK)
        return 0.0;
    if (saved.completed || saved.position_seconds <= 5.0)
        return 0.0;
    if (saved.duration_seconds > 0.0 && saved.position_seconds >= saved.duration_seconds - 20.0)
        return 0.0;
    return saved.position_seconds;
}

static vip_status_t load_player_stream(app_t *a, const vip_channel_t *ch, double resume, vip_error_t *error) {
    if (a->player_item_live && stream_needs_forced_hls(ch->stream_url))
        return vip_mpv_player_load_hls(a->player, ch->stream_url, error);
    if (resume > 0.0)
        return vip_mpv_player_load_at(a->player, ch->stream_url, resume, error);
    return vip_mpv_player_load(a->player, ch->stream_url, error);
}

static void prepare_player_screen(app_t *a, size_t channel_index) {
    if (a->screen == SCREEN_PLAYER)
        save_current_progress(a, true);
    a->current_channel = channel_index;
    a->player_item_live = a->content_kind == CONTENT_LIVE && !a->series_episode_mode;
    a->screen = SCREEN_PLAYER;
    if (!a->fullscreen_requested)
        set_fullscreen(a, true);
    snprintf(a->player_status, sizeof(a->player_status), "Abrindo stream...");
    a->player_open_ms = monotonic_ms();
    a->player_last_progress_save_ms = a->player_open_ms;
    a->player_hud_until_ms = a->player_open_ms + 3000;
    a->player_fallback_attempt = 0u;
    a->player_xtream_alt_attempted = false;
    a->timeline_dragging = false;
    set_video_visible(a, true);
    focus_player_input(a);
    XSync(a->dpy, False);
}

void enter_player(app_t *a, size_t channel_index) {
    if (channel_index >= ACTIVE_CHANNELS(a).len || !a->player)
        return;

    prepare_player_screen(a, channel_index);
    debug_player_container(a);

    vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[channel_index];
    double resume = saved_resume_position(a, ch);
    vip_error_t error = {0};
    vip_status_t st = load_player_stream(a, ch, resume, &error);
    if (st != VIP_OK)
        snprintf(a->player_status, sizeof(a->player_status), "%s", error.message);
    fprintf(stderr, "[player] %s%s\n", ch->name, resume > 0.0 ? " (retomado)" : "");
}

void leave_player(app_t *a) {
    save_current_progress(a, true);
    if (a->player)
        vip_mpv_player_stop(a->player);
    set_video_visible(a, false);
    if (a->thumbs)
        vip_thumbnail_scheduler_set_paused(a->thumbs, false);
    if (a->fullscreen_requested || a->fullscreen)
        set_fullscreen(a, false);
    a->screen = SCREEN_BROWSE;
    browse_focus_grid(a);
    a->timeline_dragging = false;
    rebuild_filter(a);
}

static size_t normalized_server_prefix(const char *server, char *out, size_t cap) {
    if (!server || !server[0] || !out || cap < 2u)
        return 0;
    while (isspace((unsigned char)*server))
        ++server;
    size_t n = strlen(server);
    while (n > 0u && isspace((unsigned char)server[n - 1u]))
        --n;
    while (n > 0u && server[n - 1u] == '/')
        --n;
    if (n + 2u > cap)
        return 0;
    memcpy(out, server, n);
    out[n++] = '/';
    out[n] = '\0';
    return n;
}

static char *alternate_stream_url(app_t *a, const char *url) {
    if (!a || !url || !url[0] || !a->server_alt[0] || a->active_server_alt)
        return NULL;
    char primary[512], alternate[512];
    size_t pn = normalized_server_prefix(a->server, primary, sizeof(primary));
    size_t an = normalized_server_prefix(a->server_alt, alternate, sizeof(alternate));
    if (pn == 0u || an == 0u || strncmp(url, primary, pn) != 0)
        return NULL;
    const char *suffix = url + pn;
    size_t need = an + strlen(suffix) + 1u;
    char *out = malloc(need);
    if (!out)
        return NULL;
    snprintf(out, need, "%s%s", alternate, suffix);
    return out;
}

static bool channel_has_static_fallback(const vip_channel_t *channel) {
    return channel && channel->fallback_id && channel->fallback_id[0] &&
           channel->fallback_base && channel->fallback_base[0];
}

static vip_status_t load_static_fallback(app_t *a, const char *url,
                                         const char *referer, const char *user_agent,
                                         vip_error_t *error) {
    bool plain_hls = a->player_item_live && stream_needs_forced_hls(url) &&
                     (!referer || !referer[0]) && (!user_agent || !user_agent[0]);
    if (plain_hls)
        return vip_mpv_player_load_hls(a->player, url, error);
    return vip_mpv_player_load_http(a->player, url, referer, user_agent, error);
}

static bool try_static_player_fallback(app_t *a, vip_channel_t *channel) {
    if (!channel_has_static_fallback(channel))
        return false;

    char *url = NULL;
    char *referer = NULL;
    char *user_agent = NULL;
    vip_error_t fallback_error = {0};
    vip_status_t resolved = vip_m3u_fallback_variant(
        channel, a->player_fallback_attempt, &url, &referer, &user_agent, &fallback_error);
    if (resolved != VIP_OK || !url || !url[0]) {
        free(url);
        free(referer);
        free(user_agent);
        return false;
    }

    ++a->player_fallback_attempt;
    fprintf(stderr, "[player] fonte falhou; tentando fallback estático %zu\n",
            a->player_fallback_attempt);
    snprintf(a->player_status, sizeof(a->player_status),
             "Fonte indisponível; tentando alternativa %zu...",
             a->player_fallback_attempt);
    a->player_open_ms = monotonic_ms();

    vip_error_t error = {0};
    vip_status_t st = load_static_fallback(a, url, referer, user_agent, &error);
    if (st != VIP_OK)
        snprintf(a->player_status, sizeof(a->player_status), "%s", error.message);

    free(url);
    free(referer);
    free(user_agent);
    return true;
}

static void try_xtream_player_mirror(app_t *a, const vip_channel_t *channel) {
    if (a->player_xtream_alt_attempted)
        return;

    char *url = alternate_stream_url(a, channel->stream_url);
    a->player_xtream_alt_attempted = true;
    if (!url)
        return;

    fprintf(stderr, "[player] stream primário falhou; tentando servidor Xtream alternativo\n");
    snprintf(a->player_status, sizeof(a->player_status),
             "Servidor indisponível; tentando espelho...");
    a->player_open_ms = monotonic_ms();
    vip_error_t error = {0};
    if (vip_mpv_player_load(a->player, url, &error) != VIP_OK)
        snprintf(a->player_status, sizeof(a->player_status), "%s", error.message);
    free(url);
}

void maybe_failover_player(app_t *a) {
    if (!a || a->screen != SCREEN_PLAYER || !a->player)
        return;
    if (vip_mpv_player_state(a->player) != VIP_PLAYER_ERROR)
        return;
    if (a->current_channel >= ACTIVE_CHANNELS(a).len)
        return;

    vip_channel_t *channel = &ACTIVE_CHANNELS(a).items[a->current_channel];
    if (try_static_player_fallback(a, channel))
        return;
    try_xtream_player_mirror(a, channel);
}

void layout_video_window(app_t *a) {
    if (!a || !a->dpy || !a->video_win || a->width <= 0 || a->height <= 0)
        return;
    bool hud = player_hud_visible(a);
    int top = a->fullscreen ? 0 : PLAYER_HEADER_H;
    int bottom = hud ? PLAYER_CONTROLS_H : 0;
    int video_h = a->height - top - bottom;
    if (video_h < 1)
        video_h = 1;
    XMoveResizeWindow(a->dpy, a->video_win, 0, top, (unsigned)a->width, (unsigned)video_h);
    if (a->player_input_win) {
        XMoveResizeWindow(a->dpy, a->player_input_win, 0, top, (unsigned)a->width, (unsigned)video_h);
        if (a->video_mapped)
            XRaiseWindow(a->dpy, a->player_input_win);
    }
}

void set_video_visible(app_t *a, bool visible) {
    if (!a || !a->dpy || !a->video_win || a->video_mapped == visible)
        return;
    if (visible) {
        layout_video_window(a);
        XMapRaised(a->dpy, a->video_win);
        if (a->player_input_win)
            XMapRaised(a->dpy, a->player_input_win);
    } else {
        if (a->player_input_win)
            XUnmapWindow(a->dpy, a->player_input_win);
        XUnmapWindow(a->dpy, a->video_win);
    }
    a->video_mapped = visible;
    XFlush(a->dpy);
}

void sync_video_window(app_t *a) {
    if (!a || a->screen != SCREEN_PLAYER || !a->player) {
        set_video_visible(a, false);
        return;
    }
    vip_player_state_t state = vip_mpv_player_state(a->player);
    bool visible = state == VIP_PLAYER_OPENING || state == VIP_PLAYER_BUFFERING ||
                   state == VIP_PLAYER_PLAYING || state == VIP_PLAYER_PAUSED ||
                   state == VIP_PLAYER_RECONNECTING;
    set_video_visible(a, visible);
    if (visible)
        layout_video_window(a);
}

void switch_relative_channel(app_t *a, int delta) {
    if (a->filtered_len == 0)
        return;
    size_t pos = 0;
    bool found = false;
    for (size_t i = 0; i < a->filtered_len; ++i) {
        if (a->filtered[i] == a->current_channel) {
            pos = i;
            found = true;
            break;
        }
    }
    if (!found)
        return;
    long next = (long)pos + delta;
    if (next < 0)
        next = (long)a->filtered_len - 1;
    if ((size_t)next >= a->filtered_len)
        next = 0;
    enter_player(a, a->filtered[(size_t)next]);
}

bool handle_player_horizontal_key(app_t *a, KeySym sym) {
    if (sym != XK_Left && sym != XK_Right)
        return false;
    int delta = sym == XK_Left ? -1 : 1;
    if (a->player_item_live)
        switch_relative_channel(a, delta);
    else if (a->player) {
        vip_error_t e = {0};
        (void)vip_mpv_player_seek_relative(a->player, (double)delta * 10.0, &e);
    }
    return true;
}

void adjust_player_volume(app_t *a, double delta) {
    if (!a->player)
        return;
    vip_mpv_player_snapshot_t sn = {0};
    vip_mpv_player_snapshot(a->player, &sn);
    vip_error_t e = {0};
    double volume = sn.volume + delta;
    if (volume < 0.0)
        volume = 0.0;
    if (volume > 100.0)
        volume = 100.0;
    (void)vip_mpv_player_set_volume(a->player, volume, &e);
}

void handle_player_key(app_t *a, KeySym sym) {
    show_player_hud(a);
    if (is_navigation_back(sym) || sym == XK_BackSpace) {
        leave_player(a);
        return;
    }
    if (sym == XK_space && a->player) {
        vip_mpv_player_set_paused(a->player, !vip_mpv_player_is_paused(a->player));
        save_current_progress(a, true);
        return;
    }
    if (handle_player_horizontal_key(a, sym))
        return;
    if (sym == XK_Up) {
        adjust_player_volume(a, 5.0);
        return;
    }
    if (sym == XK_Down)
        adjust_player_volume(a, -5.0);
}

void handle_player_click(app_t *a, int x, int y) {
    show_player_hud(a);
    if (!a->fullscreen && point_in(x, y, 12, 10, 132, 44)) {
        leave_player(a);
        return;
    }

    int cy = a->height - PLAYER_CONTROLS_H;
    if (point_in(x, y, 16, cy + 18, 52, 46) && a->player) {
        vip_mpv_player_set_paused(a->player, !vip_mpv_player_is_paused(a->player));
        save_current_progress(a, true);
        return;
    }
    if (point_in(x, y, 76, cy + 18, 82, 46)) {
        leave_player(a);
        return;
    }
    if (a->player_item_live || !a->player)
        return;

    int tx, ty, tw, th;
    timeline_geometry(a, &tx, &ty, &tw, &th);
    if (!point_in(x, y, tx, ty, tw, th + 12))
        return;

    vip_mpv_player_snapshot_t snap = {0};
    vip_mpv_player_snapshot(a->player, &snap);
    if (snap.duration_seconds <= 0.0)
        return;

    double pos = ((double)(x - tx) / (double)tw) * snap.duration_seconds;
    vip_error_t err = {0};
    (void)vip_mpv_player_seek(a->player, pos, &err);
    a->timeline_dragging = true;
}

void handle_timeline_motion(app_t *a, const XMotionEvent *event) {
    if (!a->timeline_dragging || a->player_item_live || !a->player ||
        event->window != a->win)
        return;

    int tx, ty, tw, th;
    timeline_geometry(a, &tx, &ty, &tw, &th);
    vip_mpv_player_snapshot_t sn = {0};
    vip_mpv_player_snapshot(a->player, &sn);
    if (sn.duration_seconds <= 0 || event->x < tx || event->x > tx + tw)
        return;

    double pos = ((double)(event->x - tx) / (double)tw) * sn.duration_seconds;
    vip_error_t error = {0};
    (void)vip_mpv_player_seek(a->player, pos, &error);
}

bool handle_player_surface_press(app_t *a, const XButtonEvent *event) {
    if (a->screen != SCREEN_PLAYER ||
        (event->window != a->video_win && event->window != a->player_input_win))
        return false;
    show_player_hud(a);
    focus_player_input(a);
    if (getenv("VIPTV_MPV_DEBUG"))
        fprintf(stderr, "[mpv-debug] input ButtonPress window=%lu button=%u\n",
                (unsigned long)event->window, event->button);
    return true;
}
