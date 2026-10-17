/* SPDX-License-Identifier: MIT */
#ifndef VIP_X11_APP_INTERNAL_H
#define VIP_X11_APP_INTERNAL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "visual_iptv/core.h"
#include "visual_iptv/database.h"
#include "visual_iptv/decoder.h"
#include "visual_iptv/player_mpv.h"
#include "visual_iptv/pairing_relay.h"
#include "visual_iptv/provider.h"
#include "visual_iptv/provider_m3u.h"
#include "visual_iptv/server_resolver.h"
#include "visual_iptv/thumbnails.h"
#include "visual_iptv/ui_motion.h"
#include "visual_iptv/ui_render.h"
#include "x11_keyring.h"

#include <X11/XF86keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cairo/cairo.h>
#include <curl/curl.h>
#include <qrencode.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <limits.h>
#include <jpeglib.h>
#include <pthread.h>
#include <setjmp.h>
#ifndef _WIN32
#include <spawn.h>
#endif
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/select.h>
#include <sys/wait.h>
#endif
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define APP_TITLE "Blazzing"
#define DEFAULT_W 1600
#define DEFAULT_H 900
#define SIDEBAR_W 250
#define TOPBAR_H 76
#define PLAYER_HEADER_H 64
#define PLAYER_CONTROLS_H 82
#define GRID_GAP 18
#define DETAILS_PANEL_W 410
#define CACHE_SLOTS 96
#define THUMB_PREFETCH_BATCH 96u
#define THUMB_PREFETCH_INTERVAL_MS 150LL
#define THUMB_BACKGROUND_PRIORITY 10000LL
#define INPUT_SERVER 1
#define INPUT_SERVER_ALT 2
#define INPUT_USERNAME 3
#define INPUT_PASSWORD 4
#define INPUT_SEARCH 5
#define INPUT_PROFILE_NAME 6
#define INPUT_PHONE 7
#define INPUT_CONNECT 8
#define INPUT_MODE 9
#define INPUT_SAVED_PROFILE 10
#define BROWSE_FOCUS_GRID 0
#define BROWSE_FOCUS_SIDEBAR 1
#define BROWSE_FOCUS_TOP 2
#define BROWSE_TOP_TV 0
#define BROWSE_TOP_MOVIES 1
#define BROWSE_TOP_SERIES 2
#define BROWSE_TOP_SEARCH 3
#define BROWSE_TOP_FAVORITES 4
#define BROWSE_TOP_LISTS 5
#define HOVER_NONE 0
#define HOVER_TAB_BASE 10
#define HOVER_SEARCH 20
#define HOVER_FAVORITES 21
#define HOVER_LISTS 22
#define HOVER_BACK 23
#define HOVER_CATEGORY_ALL 30
#define HOVER_CATEGORY_BASE 1000

typedef enum { SCREEN_LOGIN = 0, SCREEN_BROWSE, SCREEN_PLAYER } screen_t;
typedef enum { CONTENT_LIVE = 0, CONTENT_VOD = 1, CONTENT_SERIES = 2 } content_kind_t;
typedef enum { LOGIN_XTREAM = 0, LOGIN_M3U = 1 } login_mode_t;
typedef enum { ART_LANDSCAPE = 0, ART_PORTRAIT = 1, ART_SQUARE = 2 } artwork_mode_t;

typedef struct {
    artwork_mode_t mode;
    int card_w;
    int art_h;
    int card_h;
    int cols;
    int row_step;
} card_layout_t;

typedef struct {
    vip_category_list_t categories;
    vip_channel_list_t channels;
    bool loaded;
} catalog_t;

typedef struct {
    unsigned long bg;
    unsigned long panel;
    unsigned long panel2;
    unsigned long hover;
    unsigned long border;
    unsigned long text;
    unsigned long muted;
    unsigned long accent;
    unsigned long accent2;
    unsigned long danger;
    unsigned long black;
} palette_t;

typedef struct {
    char *path;
    time_t mtime;
    XImage *image;
    XImage *scaled;
    int scaled_box_w;
    int scaled_box_h;
    int scaled_w;
    int scaled_h;
    uint64_t age;
} image_slot_t;

typedef struct app app_t;

typedef struct {
    app_t *app;
    login_mode_t mode;
    char *server;
    char *server_alt;
    char *username;
    char *password;
    char *profile_name;
} login_job_t;

typedef struct {
    app_t *app;
    char *server;
    char *username;
    char *password;
    char *series_id;
    char *title;
    char *logo_url;
} series_job_t;

typedef struct {
    app_t *app;
    char *server;
    char *username;
    char *password;
    char *provider_id;
    char *media_id;
    char *title;
    content_kind_t kind;
} details_job_t;

/* Central UI state. Worker threads publish results through atomics/mutexed
 * data; all Xlib drawing and event handling stays on the main thread. */
struct app {
    Display *dpy;
    int screen_num;
    Window win;
    Window video_win;
    Window player_input_win;
    bool video_mapped;
    Drawable draw;
    Pixmap backbuffer;
    int backbuffer_w;
    int backbuffer_h;
    Visual *visual;
    int depth;
    Colormap cmap;
    GC gc;
    vip_ui_renderer_t renderer;
    XFontStruct *font;
    XFontStruct *font_title;
    XFontStruct *font_heading;
    XFontStruct *font_small;
    Atom wm_delete;
    Atom clipboard;
    Atom utf8;
    Atom paste_property;
    int paste_target;
    int width;
    int height;
    palette_t colors;
    screen_t screen;
    bool quit;
    bool fullscreen;
    bool fullscreen_requested;
    bool fullscreen_fallback;
    int fullscreen_attempts;
    int64_t fullscreen_retry_at_ms;
    bool windowed_geometry_valid;
    int windowed_x;
    int windowed_y;
    int windowed_w;
    int windowed_h;
    bool mouse_down;
    int mouse_x;
    int mouse_y;
    bool mouse_inside;
    bool hovered_card_valid;
    size_t hovered_filtered;
    vip_ui_motion_t hover_motion;
    int hovered_control;
    vip_ui_motion_t control_motion;
    bool ui_motion_active;
    int grid_scroll_target;
    bool grid_scroll_animating;
    int64_t grid_scroll_last_ms;

    login_mode_t login_mode;
    char profile_name[128];
    char active_profile_id[64];
    /* Keep the already parsed M3U catalog alive while this app instance is
     * running. Returning to the Lists screen must not force another download. */
    char cached_m3u_server[512];
    bool cached_m3u_valid;
    char server[512];
    char server_alt[512];
    char username[256];
    char password[256];
    char search[256];
    int input_focus;
    char status[512];
    bool active_server_alt;
    vip_profile_list_t profiles;
    int profile_scroll;
    int profile_focus;

    vip_pairing_relay_t *pairing_relay;
    atomic_bool pairing_submission;
    char pairing_pending_url[512];
    char pairing_pending_name[128];
    char pairing_page_url[1024];
    QRcode *pairing_qr;
    bool pairing_retry_ready;

    pthread_t login_thread;
    bool login_thread_started;
    atomic_bool login_running;
    atomic_bool login_done;
    atomic_bool login_success;
    pthread_t series_thread;
    bool series_thread_started;
    atomic_bool series_running;
    atomic_bool series_done;
    atomic_bool series_success;
    pthread_t details_thread;
    bool details_thread_started;
    atomic_bool details_running;
    atomic_bool details_done;
    atomic_bool details_success;
    pthread_mutex_t data_mutex;
    catalog_t catalogs[3];
    vip_category_list_t episode_categories;
    vip_channel_list_t episode_channels;
    vip_channel_list_t season_channels;
    content_kind_t content_kind;
    bool series_episode_mode;
    bool series_season_select;
    char series_title[256];
    char series_parent_id[128];
    vip_media_metadata_t details_metadata;
    char details_media_id[128];
    char details_title[256];
    char details_status[256];
    size_t *category_counts;

    size_t *filtered;
    size_t filtered_len;
    size_t filtered_cap;
    size_t focused_filtered;
    bool *favorite_flags;
    vip_watch_progress_t *progress_flags;
    int *series_watched;
    int *series_total;
    bool favorites_only;
    int selected_category;
    int category_scroll;
    int grid_scroll;
    int browse_focus;
    int browse_top_focus;
    int browse_sidebar_focus;
    size_t current_channel;

    vip_database_t *db;
    char db_path[1024];
    char cache_dir[1024];
    vip_thumbnail_decoder_t *decoder;
    vip_thumbnail_capture_context_t capture_context;
    vip_thumbnail_scheduler_t *thumbs;
    atomic_bool thumbs_dirty;
    image_slot_t image_cache[CACHE_SLOTS];
    uint64_t image_age;
    size_t thumb_prefetch_cursor[3];
    size_t episode_prefetch_cursor;
    int64_t thumb_prefetch_next_ms;

    vip_mpv_player_t *player;
    char player_status[256];
    int64_t player_open_ms;
    int64_t player_last_progress_save_ms;
    int64_t player_hud_until_ms;
    size_t player_fallback_attempt;
    bool player_xtream_alt_attempted;
    bool timeline_dragging;
    bool player_item_live;

    char toast[160];
    int64_t toast_until_ms;

    bool test_autoplay;
    bool test_series;
    int test_autoback_delay_ms;
    bool test_autoback_done;
    int64_t test_exit_at_ms;
};

static inline const vip_category_list_t *active_categories_const(const app_t *a) {
    return a->series_episode_mode ? &a->episode_categories : &a->catalogs[(int)a->content_kind].categories;
}

static inline const vip_channel_list_t *active_channels_const(const app_t *a) {
    if (a->series_season_select)
        return &a->season_channels;
    if (a->series_episode_mode)
        return &a->episode_channels;
    return &a->catalogs[(int)a->content_kind].channels;
}

#define active_categories(a) _Generic((a), \
    const app_t*: active_categories_const(a), \
    app_t*: (vip_category_list_t*)active_categories_const(a))

#define active_channels(a) _Generic((a), \
    const app_t*: active_channels_const(a), \
    app_t*: (vip_channel_list_t*)active_channels_const(a))

#define ACTIVE_CATEGORIES(a) (*active_categories((a)))
#define ACTIVE_CHANNELS(a) (*active_channels((a)))

static inline int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L);
}

static inline bool point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static inline void bounded_text(char *dst, size_t cap, const char *src, size_t max_bytes) {
    if (!dst || cap == 0)
        return;
    if (!src)
        src = "";
    size_t n = strlen(src);
    if (n > max_bytes)
        n = max_bytes;
    if (n >= cap)
        n = cap - 1;
    while (n > 0u && (((unsigned char)src[n] & 0xc0u) == 0x80u))
        --n;
    memcpy(dst, src, n);
    dst[n] = '\0';
    if (strlen(src) > n && cap >= 4) {
        if (n > cap - 4)
            n = cap - 4;
        memcpy(dst + n, "...", 4);
    }
}

static inline bool is_navigation_back(KeySym sym) {
    return sym == XK_Escape || sym == XF86XK_Back;
}

static inline bool is_activate_key(KeySym sym) {
    return sym == XK_Return || sym == XK_KP_Enter || sym == XK_Select;
}

/* --- Player Bridge Functions (x11_player_bridge.c) --- */
void enter_player(app_t *a, size_t channel_index);
void leave_player(app_t *a);
void maybe_failover_player(app_t *a);
void save_current_progress(app_t *a, bool force);
void show_player_hud(app_t *a);
bool player_hud_visible(app_t *a);
void focus_player_input(app_t *a);
void recover_player_focus_if_needed(app_t *a, const XFocusChangeEvent *focus_event);
void layout_video_window(app_t *a);
void set_video_visible(app_t *a, bool visible);
void sync_video_window(app_t *a);
bool wm_reports_fullscreen(app_t *a);
void set_fullscreen(app_t *a, bool fullscreen);
void maybe_enforce_fullscreen(app_t *a);
void timeline_geometry(app_t *a, int *out_x, int *out_y, int *out_w, int *out_h);
void format_clock(double seconds, char out[32]);
void switch_relative_channel(app_t *a, int delta);
void adjust_player_volume(app_t *a, double delta);
bool handle_player_horizontal_key(app_t *a, KeySym sym);
void handle_player_key(app_t *a, KeySym sym);
void handle_player_click(app_t *a, int x, int y);
bool handle_player_surface_press(app_t *a, const XButtonEvent *event);
void handle_timeline_motion(app_t *a, const XMotionEvent *event);

/* --- Worker Functions (x11_workers.c) --- */
void cancel_browse_async_workers(app_t *a);
bool cached_m3u_matches(const app_t *a);
void start_login(app_t *a, bool force_reload);
void stop_phone_pairing(app_t *a);
void start_phone_pairing(app_t *a);
void start_series_load(app_t *a, size_t channel_index);
void start_details_load(app_t *a, size_t channel_index);
void maybe_start_details_load(app_t *a);
bool start_m3u_series_load(app_t *a, size_t channel_index);
void handle_async(app_t *a);

/* --- Core App Actions (x11_app.c) --- */
void rebuild_filter(app_t *a);
void save_active_profile(app_t *a);
void recalc_category_counts(app_t *a);
void load_media_state(app_t *a);
void switch_content(app_t *a, content_kind_t kind);
void clear_details_view(app_t *a);
bool m3u_series_root(const app_t *a);
uint64_t folded_name_hash(const char *text);
void sort_episode_channels(vip_channel_list_t *channels);
void sort_season_channels(vip_channel_list_t *channels);
void activate_item(app_t *a, size_t channel_index);
void load_profile_into_form(app_t *a, size_t index);
void refresh_profiles(app_t *a);
void return_from_episode_list(app_t *a);
void toggle_favorite(app_t *a, size_t channel_index);
void choose_category(app_t *a, int index);
const char *m3u_series_display_name(const app_t *a, const vip_channel_t *ch, char *buffer, size_t cap);
void enqueue_thumbnail(app_t *a, const vip_channel_t *ch, int64_t priority);
size_t favorite_count(app_t *a);

/* --- Views & Layout Functions (x11_views.c) --- */
void init_palette(app_t *a);
void redraw(app_t *a);
bool step_browse_animations(app_t *a, int64_t now);
void image_cache_clear(app_t *a);
bool details_panel_active(const app_t *a);
void details_panel_geometry(const app_t *a, int *x, int *y, int *w, int *h);
card_layout_t browse_layout(app_t *a);
int browse_sidebar_category_y(const app_t *a);
int category_visible_rows(app_t *a);

/* --- Input & Event Functions (x11_input.c) --- */
void process_event(app_t *a, XEvent *e);
void update_browse_hover(app_t *a, int x, int y);
void browse_sync_input_focus(app_t *a);
void browse_focus_top(app_t *a, int item);
void browse_focus_grid(app_t *a);
void browse_sidebar_ensure_visible(app_t *a);
void browse_focus_sidebar(app_t *a, int item);
void login_move_focus(app_t *a, int direction);
void login_select_mode(app_t *a, login_mode_t mode);
void login_profile_ensure_visible(app_t *a);
void login_focus_saved_profiles(app_t *a);

#endif /* VIP_X11_APP_INTERNAL_H */
