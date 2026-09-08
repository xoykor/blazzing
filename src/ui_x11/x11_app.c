/* SPDX-License-Identifier: MIT */
/*
 * Xlib application shell and integration layer.
 *
 * This module owns screens, input, drawing, asynchronous UI jobs and the
 * coordination between providers, SQLite, thumbnails and mpv.  Heavy network
 * and image work stays off the X11 event loop to keep navigation responsive.
 */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/core.h"
#include "visual_iptv/database.h"
#include "visual_iptv/decoder.h"
#include "visual_iptv/player_mpv.h"
#include "visual_iptv/provider.h"
#include "visual_iptv/provider_m3u.h"
#include "visual_iptv/thumbnails.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <jpeglib.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define APP_TITLE "Visual IPTV"
#define DEFAULT_W 1600
#define DEFAULT_H 900
#define SIDEBAR_W 270
#define TOPBAR_H 70
#define PLAYER_HEADER_H 64
#define PLAYER_CONTROLS_H 82
#define GRID_GAP 18
#define DETAILS_PANEL_W 410
#define CACHE_SLOTS 96
#define THUMB_PREFETCH_BATCH 192u
#define THUMB_PREFETCH_INTERVAL_MS 120LL
#define THUMB_BACKGROUND_PRIORITY 10000LL
#define INPUT_SERVER 1
#define INPUT_SERVER_ALT 2
#define INPUT_USERNAME 3
#define INPUT_PASSWORD 4
#define INPUT_SEARCH 5
#define INPUT_PROFILE_NAME 6

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

/* Central UI state.  Worker threads publish results through atomics/mutexed
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
    XFontStruct *font;
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
    bool mouse_down;

    login_mode_t login_mode;
    char profile_name[128];
    char active_profile_id[64];
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
    content_kind_t content_kind;
    bool series_episode_mode;
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
    bool player_alt_attempted;
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

static vip_category_list_t *active_categories(app_t *a) {
    return a->series_episode_mode ? &a->episode_categories : &a->catalogs[(int)a->content_kind].categories;
}

static vip_channel_list_t *active_channels(app_t *a) {
    return a->series_episode_mode ? &a->episode_channels : &a->catalogs[(int)a->content_kind].channels;
}

#define ACTIVE_CATEGORIES(a) (*active_categories((a)))
#define ACTIVE_CHANNELS(a) (*active_channels((a)))

static void enter_player(app_t *a, size_t channel_index);
static void set_video_visible(app_t *a, bool visible);
static void layout_video_window(app_t *a);
static void focus_player_input(app_t *a);
static void clear_details_view(app_t *a);

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L);
}

static unsigned long alloc_color(app_t *a, const char *hex) {
    XColor exact = {0}, screen = {0};
    if (XAllocNamedColor(a->dpy, a->cmap, hex, &screen, &exact)) return screen.pixel;
    return BlackPixel(a->dpy, a->screen_num);
}

static void init_palette(app_t *a) {
    a->colors.bg = alloc_color(a, "#111416");
    a->colors.panel = alloc_color(a, "#202428");
    a->colors.panel2 = alloc_color(a, "#171a1d");
    a->colors.border = alloc_color(a, "#394047");
    a->colors.text = alloc_color(a, "#f1f3f5");
    a->colors.muted = alloc_color(a, "#9aa0a6");
    a->colors.accent = alloc_color(a, "#4ba3ff");
    a->colors.accent2 = alloc_color(a, "#275d84");
    a->colors.danger = alloc_color(a, "#ff6b6b");
    a->colors.black = BlackPixel(a->dpy, a->screen_num);
}

static Drawable draw_target(app_t *a) { return a->draw ? a->draw : a->win; }
static void set_fg(app_t *a, unsigned long color) { XSetForeground(a->dpy, a->gc, color); }
static void fill_rect(app_t *a, int x, int y, unsigned w, unsigned h, unsigned long color) {
    set_fg(a, color); XFillRectangle(a->dpy, draw_target(a), a->gc, x, y, w, h);
}
static void stroke_rect(app_t *a, int x, int y, unsigned w, unsigned h, unsigned long color) {
    set_fg(a, color); XDrawRectangle(a->dpy, draw_target(a), a->gc, x, y, w, h);
}

static size_t utf8_to_latin1(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0u) return 0u;
    if (!src) src = "";
    size_t out = 0u;
    const unsigned char *p = (const unsigned char *)src;
    while (*p && out + 1u < cap) {
        uint32_t cp = 0u;
        size_t advance = 1u;
        if (*p < 0x80u) {
            cp = *p;
        } else if ((*p & 0xe0u) == 0xc0u && (p[1] & 0xc0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x1fu) << 6) | (uint32_t)(p[1] & 0x3fu);
            advance = 2u;
        } else if ((*p & 0xf0u) == 0xe0u && (p[1] & 0xc0u) == 0x80u && (p[2] & 0xc0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x0fu) << 12) | ((uint32_t)(p[1] & 0x3fu) << 6) | (uint32_t)(p[2] & 0x3fu);
            advance = 3u;
        } else if ((*p & 0xf8u) == 0xf0u && (p[1] & 0xc0u) == 0x80u &&
                   (p[2] & 0xc0u) == 0x80u && (p[3] & 0xc0u) == 0x80u) {
            cp = ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3fu) << 12) |
                 ((uint32_t)(p[2] & 0x3fu) << 6) | (uint32_t)(p[3] & 0x3fu);
            advance = 4u;
        } else {
            cp = (uint32_t)'?';
        }
        if (cp <= 0xffu) dst[out++] = (char)cp;
        else if (cp == 0x2018u || cp == 0x2019u) dst[out++] = '\'';
        else if (cp == 0x201cu || cp == 0x201du) dst[out++] = '"';
        else if (cp == 0x2013u || cp == 0x2014u) dst[out++] = '-';
        else dst[out++] = '?';
        p += advance;
    }
    dst[out] = '\0';
    return out;
}

static int text_width(app_t *a, const char *text) {
    if (!text) return 0;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    if (a->font) return XTextWidth(a->font, latin, (int)len);
    return (int)len * 8;
}

static void draw_text(app_t *a, int x, int y, const char *text, unsigned long color) {
    if (!text) return;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    set_fg(a, color);
    XDrawString(a->dpy, draw_target(a), a->gc, x, y, latin, (int)len);
}

static void draw_centered(app_t *a, int x, int y, int w, const char *text, unsigned long color) {
    int tw = text_width(a, text);
    draw_text(a, x + (w - tw) / 2, y, text, color);
}

static void bounded_text(char *dst, size_t cap, const char *src, size_t max_bytes) {
    if (!dst || cap == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n > max_bytes) n = max_bytes;
    if (n >= cap) n = cap - 1;
    while (n > 0u && (((unsigned char)src[n] & 0xc0u) == 0x80u)) --n;
    memcpy(dst, src, n);
    dst[n] = '\0';
    if (strlen(src) > n && cap >= 4) {
        if (n > cap - 4) n = cap - 4;
        memcpy(dst + n, "...", 4);
    }
}

static bool point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static int mkdir_one(const char *path) {
    if (mkdir(path, 0700) == 0 || errno == EEXIST) return 0;
    return -1;
}

static int mkdir_parents(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir_one(tmp) != 0) return -1;
            *p = '/';
        }
    }
    return mkdir_one(tmp);
}

static void init_paths(app_t *a) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(a->cache_dir, sizeof(a->cache_dir), "%s/.cache/visual-iptv-x11/thumbnails", home);
    char data_dir[1024];
    snprintf(data_dir, sizeof(data_dir), "%s/.local/share/visual-iptv-x11", home);
    (void)mkdir_parents(a->cache_dir);
    (void)mkdir_parents(data_dir);
    size_t dn = strlen(data_dir);
    if (dn + sizeof("/catalog.db") <= sizeof(a->db_path)) {
        memcpy(a->db_path, data_dir, dn);
        memcpy(a->db_path + dn, "/catalog.db", sizeof("/catalog.db"));
    } else {
        snprintf(a->db_path, sizeof(a->db_path), "/tmp/visual-iptv-catalog.db");
    }
}

static unsigned long pixel_from_rgb(app_t *a, uint8_t r, uint8_t g, uint8_t b) {
    unsigned long rm = a->visual->red_mask, gm = a->visual->green_mask, bm = a->visual->blue_mask;
    unsigned rs = 0, gs = 0, bs = 0;
    while (rs < 32u && ((rm >> rs) & 1u) == 0u) ++rs;
    while (gs < 32u && ((gm >> gs) & 1u) == 0u) ++gs;
    while (bs < 32u && ((bm >> bs) & 1u) == 0u) ++bs;
    unsigned long rmax = rm >> rs, gmax = gm >> gs, bmax = bm >> bs;
    unsigned long rv = ((unsigned long)r * rmax + 127u) / 255u;
    unsigned long gv = ((unsigned long)g * gmax + 127u) / 255u;
    unsigned long bv = ((unsigned long)b * bmax + 127u) / 255u;
    return ((rv << rs) & rm) | ((gv << gs) & gm) | ((bv << bs) & bm);
}

typedef struct { struct jpeg_error_mgr pub; jmp_buf env; } jpeg_err_t;
static void jpeg_fail(j_common_ptr cinfo) { jpeg_err_t *e = (jpeg_err_t *)cinfo->err; longjmp(e->env, 1); }

static XImage *load_jpeg_ximage(app_t *a, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    struct jpeg_decompress_struct cinfo;
    jpeg_err_t jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_fail;
    if (setjmp(jerr.env)) {
        jpeg_destroy_decompress(&cinfo); fclose(fp); return NULL;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    (void)jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    (void)jpeg_start_decompress(&cinfo);
    unsigned w = cinfo.output_width, h = cinfo.output_height;
    if (w == 0 || h == 0 || w > 2048u || h > 2048u) {
        jpeg_destroy_decompress(&cinfo); fclose(fp); return NULL;
    }
    XImage *img = XCreateImage(a->dpy, a->visual, (unsigned)a->depth, ZPixmap, 0, NULL, w, h, 32, 0);
    if (!img) { jpeg_destroy_decompress(&cinfo); fclose(fp); return NULL; }
    size_t bytes = (size_t)img->bytes_per_line * h;
    img->data = calloc(1, bytes);
    if (!img->data) { XDestroyImage(img); jpeg_destroy_decompress(&cinfo); fclose(fp); return NULL; }
    uint8_t *row = malloc((size_t)w * 3u);
    if (!row) { XDestroyImage(img); jpeg_destroy_decompress(&cinfo); fclose(fp); return NULL; }
    while (cinfo.output_scanline < h) {
        JSAMPROW rp = row;
        JDIMENSION y = cinfo.output_scanline;
        (void)jpeg_read_scanlines(&cinfo, &rp, 1);
        for (unsigned x = 0; x < w; ++x) {
            XPutPixel(img, (int)x, (int)y, pixel_from_rgb(a, row[x*3u], row[x*3u+1u], row[x*3u+2u]));
        }
    }
    free(row);
    (void)jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return img;
}

static void image_slot_clear(image_slot_t *slot) {
    if (!slot) return;
    free(slot->path);
    if (slot->image) XDestroyImage(slot->image);
    if (slot->scaled) XDestroyImage(slot->scaled);
    memset(slot, 0, sizeof(*slot));
}

static image_slot_t *image_cache_slot_get(app_t *a, const char *path) {
    struct stat st;
    if (!path || stat(path, &st) != 0 || st.st_size <= 0) return NULL;
    ++a->image_age;
    image_slot_t *victim = &a->image_cache[0];
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        image_slot_t *slot = &a->image_cache[i];
        if (slot->path && strcmp(slot->path, path) == 0 && slot->mtime == st.st_mtime) {
            slot->age = a->image_age;
            return slot;
        }
        if (!slot->path || slot->age < victim->age) victim = slot;
    }
    XImage *img = load_jpeg_ximage(a, path);
    if (!img) return NULL;
    image_slot_clear(victim);
    victim->path = vip_strdup(path);
    victim->mtime = st.st_mtime;
    victim->image = img;
    victim->age = a->image_age;
    return victim;
}

static XImage *scale_ximage(app_t *a, const XImage *src, int width, int height) {
    if (!src || width < 1 || height < 1) return NULL;
    XImage *dst = XCreateImage(a->dpy, a->visual, (unsigned)a->depth, ZPixmap, 0, NULL,
                               (unsigned)width, (unsigned)height, 32, 0);
    if (!dst) return NULL;
    size_t bytes = (size_t)dst->bytes_per_line * (size_t)height;
    dst->data = calloc(1, bytes);
    if (!dst->data) { XDestroyImage(dst); return NULL; }
    for (int y = 0; y < height; ++y) {
        int sy = (int)((int64_t)y * src->height / height);
        if (sy >= src->height) sy = src->height - 1;
        for (int x = 0; x < width; ++x) {
            int sx = (int)((int64_t)x * src->width / width);
            if (sx >= src->width) sx = src->width - 1;
            XPutPixel(dst, x, y, XGetPixel((XImage *)src, sx, sy));
        }
    }
    return dst;
}

static bool draw_cached_image_contain(app_t *a, const char *path,
                                      int x, int y, int box_w, int box_h) {
    image_slot_t *slot = image_cache_slot_get(a, path);
    if (!slot || !slot->image || box_w < 1 || box_h < 1) return false;
    double sx = (double)box_w / (double)slot->image->width;
    double sy = (double)box_h / (double)slot->image->height;
    double scale = sx < sy ? sx : sy;
    if (scale <= 0.0) return false;
    int dw = (int)((double)slot->image->width * scale + 0.5);
    int dh = (int)((double)slot->image->height * scale + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dw > box_w) dw = box_w;
    if (dh > box_h) dh = box_h;
    if (!slot->scaled || slot->scaled_box_w != box_w || slot->scaled_box_h != box_h ||
        slot->scaled_w != dw || slot->scaled_h != dh) {
        if (slot->scaled) { XDestroyImage(slot->scaled); slot->scaled = NULL; }
        slot->scaled = scale_ximage(a, slot->image, dw, dh);
        slot->scaled_box_w = box_w;
        slot->scaled_box_h = box_h;
        slot->scaled_w = dw;
        slot->scaled_h = dh;
    }
    if (!slot->scaled) return false;
    int dx = x + (box_w - dw) / 2;
    int dy = y + (box_h - dh) / 2;
    XPutImage(a->dpy, draw_target(a), a->gc, slot->scaled, 0, 0, dx, dy, (unsigned)dw, (unsigned)dh);
    return true;
}


static artwork_mode_t default_artwork_mode(const app_t *a) {
    if (a->series_episode_mode) return ART_LANDSCAPE;
    if (a->content_kind == CONTENT_VOD || a->content_kind == CONTENT_SERIES) return ART_PORTRAIT;
    return ART_LANDSCAPE;
}

static artwork_mode_t detect_artwork_mode(app_t *a) {
    int portrait = 0, landscape = 0, square = 0, sampled = 0;
    size_t limit = a->filtered_len < 24u ? a->filtered_len : 24u;
    for (size_t i = 0; i < limit; ++i) {
        size_t chidx = a->filtered[i];
        if (chidx >= ACTIVE_CHANNELS(a).len) continue;
        vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[chidx];
        vip_error_t error = {0};
        char *path = vip_thumbnail_cache_path(a->cache_dir, ch->provider_id, ch->id, &error);
        image_slot_t *slot = path ? image_cache_slot_get(a, path) : NULL;
        free(path);
        if (!slot || !slot->image || slot->image->height <= 0) continue;
        double ratio = (double)slot->image->width / (double)slot->image->height;
        if (ratio < 0.88) ++portrait;
        else if (ratio > 1.18) ++landscape;
        else ++square;
        ++sampled;
    }
    artwork_mode_t fallback = default_artwork_mode(a);
    if (sampled < 3) return fallback;
    if (portrait >= landscape && portrait >= square) return ART_PORTRAIT;
    if (landscape >= portrait && landscape >= square) return ART_LANDSCAPE;
    return ART_SQUARE;
}

static bool details_panel_active(const app_t *a) {
    return a && a->width >= 1180 && a->login_mode == LOGIN_XTREAM &&
           !a->series_episode_mode &&
           (a->content_kind == CONTENT_VOD || a->content_kind == CONTENT_SERIES);
}

static void details_panel_geometry(const app_t *a, int *x, int *y, int *w, int *h) {
    int panel_w = DETAILS_PANEL_W;
    if (a->width < 1300) panel_w = 360;
    *x = a->width - panel_w - 16;
    *y = TOPBAR_H + 18;
    *w = panel_w;
    *h = a->height - *y - 18;
    if (*h < 120) *h = 120;
}

/* Choose card geometry from artwork shape instead of forcing posters and
 * channel logos into the same aspect ratio. */
static card_layout_t browse_layout(app_t *a) {
    card_layout_t layout = {0};
    layout.mode = detect_artwork_mode(a);
    if (layout.mode == ART_PORTRAIT) {
        layout.card_w = 196;
        layout.art_h = 294;
    } else if (layout.mode == ART_SQUARE) {
        layout.card_w = 224;
        layout.art_h = 224;
    } else {
        layout.card_w = 320;
        layout.art_h = 180;
    }
    layout.card_h = layout.art_h + 48;
    int content_x = SIDEBAR_W + 20;
    int avail_w = a->width - content_x - 18;
    if (details_panel_active(a)) {
        int px, py, pw, ph;
        details_panel_geometry(a, &px, &py, &pw, &ph);
        (void)py; (void)ph;
        avail_w = px - content_x - 12;
    }
    layout.cols = (avail_w + GRID_GAP) / (layout.card_w + GRID_GAP);
    if (layout.cols < 1) layout.cols = 1;
    layout.row_step = layout.card_h + GRID_GAP;
    return layout;
}

static bool contains_ascii_case(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return true;
    if (!haystack) return false;
    size_t nn = strlen(needle);
    for (const char *h = haystack; *h; ++h) {
        size_t i = 0;
        while (i < nn && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == nn) return true;
    }
    return false;
}

static void rebuild_filter(app_t *a) {
    pthread_mutex_lock(&a->data_mutex);
    size_t need = ACTIVE_CHANNELS(a).len;
    if (need > a->filtered_cap) {
        size_t cap = need ? need : 1;
        size_t *p = realloc(a->filtered, cap * sizeof(*p));
        if (p) { a->filtered = p; a->filtered_cap = cap; }
    }
    a->filtered_len = 0;
    const char *cat = NULL;
    if (a->selected_category >= 0 && (size_t)a->selected_category < ACTIVE_CATEGORIES(a).len)
        cat = ACTIVE_CATEGORIES(a).items[a->selected_category].id;
    if (a->filtered) {
        for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i) {
            vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[i];
            if (cat && (!ch->category_id || strcmp(ch->category_id, cat) != 0)) continue;
            if (a->favorites_only && (!a->favorite_flags || !a->favorite_flags[i])) continue;
            if (!contains_ascii_case(ch->name, a->search)) continue;
            a->filtered[a->filtered_len++] = i;
        }
    }
    pthread_mutex_unlock(&a->data_mutex);
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    if (a->thumbs) vip_thumbnail_scheduler_cancel_pending(a->thumbs);
}

static void recalc_category_counts(app_t *a) {
    free(a->category_counts); a->category_counts = NULL;
    if (ACTIVE_CATEGORIES(a).len == 0) return;
    a->category_counts = calloc(ACTIVE_CATEGORIES(a).len, sizeof(*a->category_counts));
    if (!a->category_counts) return;
    for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i) {
        const char *cid = ACTIVE_CHANNELS(a).items[i].category_id;
        if (!cid) continue;
        for (size_t j = 0; j < ACTIVE_CATEGORIES(a).len; ++j) {
            if (strcmp(cid, ACTIVE_CATEGORIES(a).items[j].id) == 0) { ++a->category_counts[j]; break; }
        }
    }
}

static size_t favorite_count(app_t *a) {
    size_t n = 0;
    if (!a->favorite_flags) return 0;
    for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i) if (a->favorite_flags[i]) ++n;
    return n;
}

static void load_media_state(app_t *a) {
    free(a->favorite_flags);
    free(a->progress_flags);
    free(a->series_watched);
    free(a->series_total);
    a->favorite_flags = NULL;
    a->progress_flags = NULL;
    a->series_watched = NULL;
    a->series_total = NULL;
    size_t count = ACTIVE_CHANNELS(a).len;
    if (count == 0u) return;
    a->favorite_flags = calloc(count, sizeof(*a->favorite_flags));
    a->progress_flags = calloc(count, sizeof(*a->progress_flags));
    if (a->content_kind == CONTENT_SERIES && !a->series_episode_mode) {
        a->series_watched = calloc(count, sizeof(*a->series_watched));
        a->series_total = calloc(count, sizeof(*a->series_total));
    }
    if (!a->db || !ACTIVE_CHANNELS(a).items[0].provider_id) return;
    vip_error_t error = {0};
    if (a->favorite_flags)
        (void)vip_database_load_favorite_flags(a->db, ACTIVE_CHANNELS(a).items[0].provider_id,
                                               &ACTIVE_CHANNELS(a), a->favorite_flags, count, &error);
    if (a->progress_flags && (a->content_kind == CONTENT_VOD || a->series_episode_mode)) {
        vip_error_clear(&error);
        (void)vip_database_load_progress(a->db, ACTIVE_CHANNELS(a).items[0].provider_id,
                                         &ACTIVE_CHANNELS(a), a->progress_flags, count, &error);
    }
    if (a->series_watched && a->series_total) {
        for (size_t i = 0; i < count; ++i) {
            vip_series_progress_t sp = {0};
            vip_error_clear(&error);
            if (vip_database_get_series_progress(a->db, ACTIVE_CHANNELS(a).items[i].provider_id,
                                                 ACTIVE_CHANNELS(a).items[i].id, &sp, &error) == VIP_OK) {
                a->series_watched[i] = sp.watched_count;
                a->series_total[i] = sp.total_count;
            }
            vip_series_progress_clear(&sp);
        }
    }
}

static void toggle_favorite(app_t *a, size_t channel_index) {
    if (channel_index >= ACTIVE_CHANNELS(a).len || !a->favorite_flags) return;
    bool value = !a->favorite_flags[channel_index];
    a->favorite_flags[channel_index] = value;
    if (a->db) {
        vip_error_t error = {0};
        vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[channel_index];
        (void)vip_database_set_favorite(a->db, ch->provider_id, ch->id, value, &error);
    }
    snprintf(a->toast, sizeof(a->toast), "%s",
             value ? "Adicionado aos favoritos" : "Removido dos favoritos");
    a->toast_until_ms = monotonic_ms() + 1600;
    if (a->favorites_only) rebuild_filter(a);
}

static const char *content_label(content_kind_t kind) {
    switch (kind) {
        case CONTENT_VOD: return "Filmes";
        case CONTENT_SERIES: return "Séries";
        case CONTENT_LIVE:
        default: return "TV";
    }
}

static const char *content_plural(app_t *a) {
    if (a->series_episode_mode) return "episódios";
    switch (a->content_kind) {
        case CONTENT_VOD: return "filmes";
        case CONTENT_SERIES: return "séries";
        case CONTENT_LIVE:
        default: return "canais";
    }
}

static const char *all_content_label(app_t *a) {
    if (a->series_episode_mode) return "Todos os episódios";
    switch (a->content_kind) {
        case CONTENT_VOD: return "Todos os filmes";
        case CONTENT_SERIES: return "Todas as séries";
        case CONTENT_LIVE:
        default: return "Todos os canais";
    }
}

static void switch_content(app_t *a, content_kind_t kind) {
    if (!a || kind < CONTENT_LIVE || kind > CONTENT_SERIES) return;
    if (atomic_load(&a->series_running)) return;
    a->series_episode_mode = false;
    clear_details_view(a);
    a->content_kind = kind;
    a->favorites_only = false;
    a->selected_category = -1;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    free(a->favorite_flags);
    a->favorite_flags = NULL;
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}

static void return_from_episode_list(app_t *a) {
    if (!a || !a->series_episode_mode) return;
    a->series_episode_mode = false;
    clear_details_view(a);
    a->selected_category = -1;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}

static void thumbnail_ready(const vip_thumbnail_request_t *request,
                            vip_status_t status,
                            const char *path,
                            const vip_error_t *error,
                            void *userdata) {
    app_t *a = userdata;
    (void)error;
    if (status == VIP_OK && path && a->db) {
        vip_error_t db_error = {0};
        vip_thumbnail_source_t src = request->logo_url && request->logo_url[0]
                                         ? VIP_THUMB_SERVER_LOGO : VIP_THUMB_CAPTURED_FRAME;
        int64_t now = (int64_t)time(NULL);
        (void)vip_database_set_thumbnail(a->db, request->provider_id, request->channel_id,
                                         path, src, now, now, &db_error);
    }
    atomic_store(&a->thumbs_dirty, true);
}

static void enqueue_thumbnail(app_t *a, const vip_channel_t *ch, int64_t priority) {
    if (!a->thumbs || !ch || !ch->provider_id || !ch->id || !ch->stream_url) return;
    if (strncmp(ch->stream_url, "series://", 9u) == 0 && (!ch->logo_url || !ch->logo_url[0])) return;
    vip_error_t error = {0};
    char *path = vip_thumbnail_cache_path(a->cache_dir, ch->provider_id, ch->id, &error);
    if (path) {
        struct stat st;
        bool exists = stat(path, &st) == 0 && st.st_size > 0;
        free(path);
        if (exists) return;
    }
    vip_thumbnail_request_t req = {
        .provider_id = ch->provider_id,
        .channel_id = ch->id,
        .logo_url = ch->logo_url,
        .stream_url = ch->stream_url,
        .priority = priority,
    };
    (void)vip_thumbnail_scheduler_enqueue(a->thumbs, &req, &error);
}

static size_t thumbnail_worker_count(void) {
    const char *override = getenv("VIPTV_THUMB_WORKERS");
    if (override && override[0]) {
        char *end = NULL;
        errno = 0;
        unsigned long parsed = strtoul(override, &end, 10);
        if (errno == 0 && end != override && *end == '\0' &&
            parsed >= 1ul && parsed <= 64ul)
            return (size_t)parsed;
    }

    long online = sysconf(_SC_NPROCESSORS_ONLN);
    size_t workers = online > 0 ? (size_t)online : 8u;
    if (workers < 8u) workers = 8u;
    if (workers > 16u) workers = 16u;
    return workers;
}

static size_t prefetch_thumbnail_list(app_t *a,
                                      vip_channel_list_t *channels,
                                      size_t *cursor,
                                      size_t budget,
                                      int64_t priority) {
    if (!a || !channels || !cursor || channels->len == 0u || budget == 0u) return 0u;
    size_t limit = budget < channels->len ? budget : channels->len;
    for (size_t i = 0; i < limit; ++i) {
        size_t index = *cursor % channels->len;
        *cursor = (index + 1u) % channels->len;
        enqueue_thumbnail(a, &channels->items[index], priority);
    }
    return limit;
}

/*
 * Mantém o cache "aquecendo" em segundo plano. Cards visíveis e o painel de
 * detalhes continuam vencendo a fila por usarem prioridades muito maiores.
 * Quando uma imagem falha, a próxima volta pelo catálogo a coloca na fila
 * novamente; portanto o carregamento não morre silenciosamente.
 */
static void prefetch_thumbnail_batch(app_t *a) {
    if (!a || !a->thumbs || a->screen == SCREEN_LOGIN || !a->active_profile_id[0]) return;

    int64_t now = monotonic_ms();
    if (now < a->thumb_prefetch_next_ms) return;
    a->thumb_prefetch_next_ms = now + THUMB_PREFETCH_INTERVAL_MS;

    bool episode_source = a->series_episode_mode && a->episode_channels.len > 0u;
    size_t sources = episode_source ? 1u : 0u;
    for (int k = 0; k < 3; ++k) {
        if (a->catalogs[k].loaded && a->catalogs[k].channels.len > 0u) ++sources;
    }
    if (sources == 0u) return;

    size_t budget = THUMB_PREFETCH_BATCH;
    for (int k = 0; k < 3 && budget > 0u; ++k) {
        catalog_t *catalog = &a->catalogs[k];
        if (!catalog->loaded || catalog->channels.len == 0u) continue;

        size_t quota = (budget + sources - 1u) / sources;
        int64_t priority = THUMB_BACKGROUND_PRIORITY - (int64_t)k * 100LL;
        size_t used = prefetch_thumbnail_list(a, &catalog->channels,
                                              &a->thumb_prefetch_cursor[k],
                                              quota, priority);
        budget -= used;
        --sources;
    }

    if (episode_source && budget > 0u) {
        (void)prefetch_thumbnail_list(a, &a->episode_channels,
                                      &a->episode_prefetch_cursor,
                                      budget, THUMB_BACKGROUND_PRIORITY + 500LL);
    }
}


static vip_status_t login_one_server(const char *server,
                                     const char *username,
                                     const char *password,
                                     vip_credentials_t *credentials,
                                     vip_category_list_t *cats,
                                     vip_channel_list_t *channels,
                                     catalog_t *vod,
                                     catalog_t *series,
                                     vip_error_t *error) {
    vip_xtream_client_t *client = NULL;
    vip_status_t st = vip_credentials_init(credentials, server, username, password, error);
    if (st == VIP_OK) st = vip_xtream_client_create(&client, credentials, error);
    if (st == VIP_OK) st = vip_xtream_authenticate(client, error);
    if (st == VIP_OK) st = vip_xtream_live_categories(client, cats, error);
    if (st == VIP_OK) st = vip_xtream_live_streams(client, channels, error);

    if (st == VIP_OK && vod) {
        vip_error_t optional_error = {0};
        vip_category_list_init(&vod->categories);
        vip_channel_list_init(&vod->channels);
        vip_status_t vst = vip_xtream_vod_categories(client, &vod->categories, &optional_error);
        if (vst == VIP_OK) vst = vip_xtream_vod_streams(client, &vod->channels, &optional_error);
        if (vst == VIP_OK) {
            vod->loaded = true;
        } else {
            fprintf(stderr, "[catalog] filmes indisponíveis: %s\n", optional_error.message);
            vip_category_list_clear(&vod->categories);
            vip_channel_list_clear(&vod->channels);
            vod->loaded = false;
        }
    }
    if (st == VIP_OK && series) {
        vip_error_t optional_error = {0};
        vip_category_list_init(&series->categories);
        vip_channel_list_init(&series->channels);
        vip_status_t sst = vip_xtream_series_categories(client, &series->categories, &optional_error);
        if (sst == VIP_OK) sst = vip_xtream_series(client, &series->channels, &optional_error);
        if (sst == VIP_OK) {
            series->loaded = true;
        } else {
            fprintf(stderr, "[catalog] séries indisponíveis: %s\n", optional_error.message);
            vip_category_list_clear(&series->categories);
            vip_channel_list_clear(&series->channels);
            series->loaded = false;
        }
    }
    if (client) vip_xtream_client_destroy(client);
    return st;
}

static void remap_catalog_provider_id(vip_category_list_t *cats,
                                      vip_channel_list_t *channels,
                                      const char provider_id[17]) {
    if (!provider_id || !provider_id[0]) return;
    for (size_t i = 0; i < cats->len; ++i) {
        if (cats->items[i].provider_id) memcpy(cats->items[i].provider_id, provider_id, 17u);
    }
    for (size_t i = 0; i < channels->len; ++i) {
        if (channels->items[i].provider_id) memcpy(channels->items[i].provider_id, provider_id, 17u);
    }
}

/* Network authentication/catalog fetch runs off-thread; Xlib must not be
 * called from this worker.  Results are transferred back through app state. */
static void *login_worker(void *userdata) {
    login_job_t *job = userdata;
    app_t *a = job->app;
    vip_error_t error = {0};
    vip_credentials_t credentials = {0};
    vip_category_list_t cats; vip_category_list_init(&cats);
    vip_channel_list_t channels; vip_channel_list_init(&channels);
    catalog_t vod = {0};
    catalog_t series = {0};
    bool success = false;
    bool used_alternate = false;
    char provider_id[17] = {0};
    vip_status_t st = VIP_ERR_INVALID_ARGUMENT;

    if (job->mode == LOGIN_M3U) {
        fprintf(stderr, "[login] carregando playlist M3U\n");
        st = vip_m3u_load(job->server, &cats, &channels, provider_id, &error);
    } else {
        fprintf(stderr, "[login] conectando ao servidor primário\n");
        st = login_one_server(job->server, job->username, job->password,
                              &credentials, &cats, &channels, &vod, &series, &error);
        if (st != VIP_OK && job->server_alt && job->server_alt[0]) {
            fprintf(stderr, "[login] primário falhou; tentando servidor alternativo\n");
            pthread_mutex_lock(&a->data_mutex);
            snprintf(a->status, sizeof(a->status), "Primário indisponível; tentando servidor alternativo...");
            pthread_mutex_unlock(&a->data_mutex);
            vip_category_list_clear(&cats); vip_category_list_init(&cats);
            vip_channel_list_clear(&channels); vip_channel_list_init(&channels);
            vip_category_list_clear(&vod.categories); vip_channel_list_clear(&vod.channels); memset(&vod, 0, sizeof(vod));
            vip_category_list_clear(&series.categories); vip_channel_list_clear(&series.channels); memset(&series, 0, sizeof(series));
            vip_credentials_clear(&credentials);
            vip_error_clear(&error);
            st = login_one_server(job->server_alt, job->username, job->password,
                                  &credentials, &cats, &channels, &vod, &series, &error);
            used_alternate = st == VIP_OK;
            if (used_alternate) {
                vip_credentials_t identity = {0};
                vip_error_t identity_error = {0};
                if (vip_credentials_init(&identity, job->server, job->username, job->password, &identity_error) == VIP_OK) {
                    snprintf(credentials.provider_id, sizeof(credentials.provider_id), "%s", identity.provider_id);
                    remap_catalog_provider_id(&cats, &channels, identity.provider_id);
                    if (vod.loaded) remap_catalog_provider_id(&vod.categories, &vod.channels, identity.provider_id);
                    if (series.loaded) remap_catalog_provider_id(&series.categories, &series.channels, identity.provider_id);
                }
                vip_credentials_clear(&identity);
            }
        }
        if (st == VIP_OK) snprintf(provider_id, sizeof(provider_id), "%s", credentials.provider_id);
    }

    if (st == VIP_OK) {
        if (a->db) (void)vip_database_replace_catalog(a->db, provider_id, &cats, &channels, &error);
        pthread_mutex_lock(&a->data_mutex);
        vip_category_list_clear(&a->catalogs[CONTENT_LIVE].categories);
        vip_channel_list_clear(&a->catalogs[CONTENT_LIVE].channels);
        a->catalogs[CONTENT_LIVE].categories = cats; memset(&cats, 0, sizeof(cats));
        a->catalogs[CONTENT_LIVE].channels = channels; memset(&channels, 0, sizeof(channels));
        a->catalogs[CONTENT_LIVE].loaded = true;
        vip_category_list_clear(&a->catalogs[CONTENT_VOD].categories);
        vip_channel_list_clear(&a->catalogs[CONTENT_VOD].channels);
        vip_category_list_clear(&a->catalogs[CONTENT_SERIES].categories);
        vip_channel_list_clear(&a->catalogs[CONTENT_SERIES].channels);
        if (job->mode == LOGIN_XTREAM) {
            a->catalogs[CONTENT_VOD] = vod; memset(&vod, 0, sizeof(vod));
            a->catalogs[CONTENT_SERIES] = series; memset(&series, 0, sizeof(series));
        } else {
            memset(&a->catalogs[CONTENT_VOD], 0, sizeof(a->catalogs[CONTENT_VOD]));
            memset(&a->catalogs[CONTENT_SERIES], 0, sizeof(a->catalogs[CONTENT_SERIES]));
            vip_category_list_init(&a->catalogs[CONTENT_VOD].categories);
            vip_channel_list_init(&a->catalogs[CONTENT_VOD].channels);
            vip_category_list_init(&a->catalogs[CONTENT_SERIES].categories);
            vip_channel_list_init(&a->catalogs[CONTENT_SERIES].channels);
        }
        a->active_server_alt = used_alternate;
        a->content_kind = CONTENT_LIVE;
        a->series_episode_mode = false;
        snprintf(a->active_profile_id, sizeof(a->active_profile_id), "%s", provider_id);
        if (job->mode == LOGIN_XTREAM) {
            fprintf(stderr, "[catalog] filmes: %zu itens/%zu categorias; séries: %zu itens/%zu categorias\n",
                    a->catalogs[CONTENT_VOD].channels.len, a->catalogs[CONTENT_VOD].categories.len,
                    a->catalogs[CONTENT_SERIES].channels.len, a->catalogs[CONTENT_SERIES].categories.len);
        }
        snprintf(a->status, sizeof(a->status), "%zu canais em %zu categorias%s",
                 a->catalogs[CONTENT_LIVE].channels.len, a->catalogs[CONTENT_LIVE].categories.len,
                 used_alternate ? " (servidor alternativo)" : "");
        pthread_mutex_unlock(&a->data_mutex);
        success = true;
    } else {
        fprintf(stderr, "[login] falhou: %s\n", error.message);
        pthread_mutex_lock(&a->data_mutex);
        snprintf(a->status, sizeof(a->status), "%s", error.message[0] ? error.message : "falha ao conectar");
        pthread_mutex_unlock(&a->data_mutex);
    }

    vip_category_list_clear(&cats);
    vip_channel_list_clear(&channels);
    vip_category_list_clear(&vod.categories); vip_channel_list_clear(&vod.channels);
    vip_category_list_clear(&series.categories); vip_channel_list_clear(&series.channels);
    vip_credentials_clear(&credentials);
    if (job->password) { volatile char *p = job->password; size_t n = strlen(job->password); while (n--) *p++ = 0; }
    free(job->server); free(job->server_alt); free(job->username); free(job->password); free(job->profile_name); free(job);
    atomic_store(&a->login_success, success);
    atomic_store(&a->login_running, false);
    atomic_store(&a->login_done, true);
    return NULL;
}

static void start_login(app_t *a) {
    if (atomic_load(&a->login_running)) return;
    if (!a->server[0]) {
        snprintf(a->status, sizeof(a->status), a->login_mode == LOGIN_M3U
                 ? "Informe uma URL ou caminho de playlist M3U" : "Preencha servidor, usuário e senha");
        return;
    }
    if (a->login_mode == LOGIN_XTREAM && (!a->username[0] || !a->password[0])) {
        snprintf(a->status, sizeof(a->status), "Preencha servidor, usuário e senha");
        return;
    }
    login_job_t *job = calloc(1, sizeof(*job));
    if (!job) { snprintf(a->status, sizeof(a->status), "Sem memória"); return; }
    job->app = a;
    job->mode = a->login_mode;
    job->server = vip_strdup(a->server);
    job->server_alt = vip_strdup(a->server_alt);
    job->username = vip_strdup(a->username);
    job->password = vip_strdup(a->password);
    job->profile_name = vip_strdup(a->profile_name);
    if (!job->server || !job->server_alt || !job->username || !job->password || !job->profile_name) {
        free(job->server); free(job->server_alt); free(job->username); free(job->password); free(job->profile_name); free(job);
        snprintf(a->status, sizeof(a->status), "Sem memória"); return;
    }
    snprintf(a->status, sizeof(a->status), a->login_mode == LOGIN_M3U ? "Carregando M3U..." : "Conectando...");
    atomic_store(&a->login_done, false);
    atomic_store(&a->login_success, false);
    atomic_store(&a->login_running, true);
    if (pthread_create(&a->login_thread, NULL, login_worker, job) != 0) {
        atomic_store(&a->login_running, false);
        free(job->server); free(job->server_alt); free(job->username); free(job->password); free(job->profile_name); free(job);
        snprintf(a->status, sizeof(a->status), "Falha ao iniciar conexão");
        return;
    }
    a->login_thread_started = true;
}

static void *series_worker(void *userdata) {
    series_job_t *job = userdata;
    app_t *a = job->app;
    vip_credentials_t credentials = {0};
    vip_xtream_client_t *client = NULL;
    vip_category_list_t seasons; vip_category_list_init(&seasons);
    vip_channel_list_t episodes; vip_channel_list_init(&episodes);
    vip_error_t error = {0};

    vip_status_t st = vip_credentials_init(&credentials, job->server, job->username, job->password, &error);
    if (st == VIP_OK) st = vip_xtream_client_create(&client, &credentials, &error);
    if (st == VIP_OK) st = vip_xtream_series_episodes(client, job->series_id, &seasons, &episodes, &error);

    if (st == VIP_OK) {
        pthread_mutex_lock(&a->data_mutex);
        vip_category_list_clear(&a->episode_categories);
        vip_channel_list_clear(&a->episode_channels);
        a->episode_categories = seasons; memset(&seasons, 0, sizeof(seasons));
        a->episode_channels = episodes; memset(&episodes, 0, sizeof(episodes));
        snprintf(a->series_title, sizeof(a->series_title), "%s", job->title ? job->title : "Série");
        snprintf(a->status, sizeof(a->status), "%zu episódios", a->episode_channels.len);
        pthread_mutex_unlock(&a->data_mutex);
        atomic_store(&a->series_success, true);
    } else {
        pthread_mutex_lock(&a->data_mutex);
        snprintf(a->status, sizeof(a->status), "Falha ao carregar episódios: %s",
                 error.message[0] ? error.message : "erro desconhecido");
        pthread_mutex_unlock(&a->data_mutex);
        atomic_store(&a->series_success, false);
    }

    if (client) vip_xtream_client_destroy(client);
    vip_credentials_clear(&credentials);
    vip_category_list_clear(&seasons);
    vip_channel_list_clear(&episodes);
    if (job->password) {
        volatile char *wipe = job->password;
        size_t n = strlen(job->password);
        while (n-- > 0u) *wipe++ = 0;
    }
    free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job);
    atomic_store(&a->series_running, false);
    atomic_store(&a->series_done, true);
    return NULL;
}

static void start_series_load(app_t *a, size_t channel_index) {
    if (!a || a->content_kind != CONTENT_SERIES || a->series_episode_mode ||
        atomic_load(&a->series_running) || channel_index >= ACTIVE_CHANNELS(a).len) return;
    vip_channel_t *series = &ACTIVE_CHANNELS(a).items[channel_index];
    if (!series->id || strncmp(series->id, "series:", 7u) != 0) return;
    snprintf(a->series_parent_id, sizeof(a->series_parent_id), "%s", series->id);
    const char *server = a->active_server_alt && a->server_alt[0] ? a->server_alt : a->server;
    series_job_t *job = calloc(1, sizeof(*job));
    if (!job) return;
    job->app = a;
    job->server = vip_strdup(server);
    job->username = vip_strdup(a->username);
    job->password = vip_strdup(a->password);
    job->series_id = vip_strdup(series->id);
    job->title = vip_strdup(series->name);
    if (!job->server || !job->username || !job->password || !job->series_id || !job->title) {
        free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job);
        return;
    }
    pthread_mutex_lock(&a->data_mutex);
    snprintf(a->status, sizeof(a->status), "Carregando episódios de %s...", series->name);
    pthread_mutex_unlock(&a->data_mutex);
    atomic_store(&a->series_done, false);
    atomic_store(&a->series_success, false);
    atomic_store(&a->series_running, true);
    if (pthread_create(&a->series_thread, NULL, series_worker, job) != 0) {
        atomic_store(&a->series_running, false);
        free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job);
        return;
    }
    a->series_thread_started = true;
}

static bool metadata_has_content(const vip_media_metadata_t *metadata) {
    return metadata &&
           ((metadata->plot && metadata->plot[0]) ||
            (metadata->cover_url && metadata->cover_url[0]) ||
            (metadata->backdrop_url && metadata->backdrop_url[0]) ||
            (metadata->genre && metadata->genre[0]) ||
            (metadata->release_date && metadata->release_date[0]) ||
            (metadata->rating && metadata->rating[0]) ||
            (metadata->duration && metadata->duration[0]) ||
            (metadata->cast && metadata->cast[0]) ||
            (metadata->director && metadata->director[0]));
}

static void details_job_free(details_job_t *job) {
    if (!job) return;
    if (job->password) {
        volatile char *wipe = job->password;
        size_t n = strlen(job->password);
        while (n-- > 0u) *wipe++ = 0;
    }
    free(job->server);
    free(job->username);
    free(job->password);
    free(job->provider_id);
    free(job->media_id);
    free(job->title);
    free(job);
}

static void *details_worker(void *userdata) {
    details_job_t *job = userdata;
    app_t *a = job->app;
    vip_media_metadata_t metadata; vip_media_metadata_init(&metadata);
    vip_media_metadata_t cached; vip_media_metadata_init(&cached);
    vip_error_t error = {0};
    bool cache_found = false;
    int64_t cache_updated = 0;
    bool used_cache = false;
    vip_status_t st = VIP_ERR_NETWORK;

    if (a->db) {
        vip_error_t cache_error = {0};
        if (vip_database_get_media_metadata(a->db, job->provider_id, job->media_id,
                                            &cached, &cache_updated, &cache_found,
                                            &cache_error) == VIP_OK && cache_found && metadata_has_content(&cached)) {
            int64_t age = (int64_t)time(NULL) - cache_updated;
            if (cache_updated > 0 && age >= 0 && age < 7LL * 24LL * 60LL * 60LL) {
                metadata = cached;
                memset(&cached, 0, sizeof(cached));
                st = VIP_OK;
                used_cache = true;
            }
        }
    }

    if (st != VIP_OK) {
        vip_credentials_t credentials = {0};
        vip_xtream_client_t *client = NULL;
        st = vip_credentials_init(&credentials, job->server, job->username, job->password, &error);
        if (st == VIP_OK) st = vip_xtream_client_create(&client, &credentials, &error);
        if (st == VIP_OK) {
            if (job->kind == CONTENT_VOD)
                st = vip_xtream_vod_info(client, job->media_id, &metadata, &error);
            else
                st = vip_xtream_series_metadata(client, job->media_id, &metadata, &error);
        }
        if (client) vip_xtream_client_destroy(client);
        vip_credentials_clear(&credentials);

        if (st == VIP_OK && metadata_has_content(&metadata) && a->db) {
            vip_error_t db_error = {0};
            (void)vip_database_set_media_metadata(a->db, job->provider_id, job->media_id,
                                                  &metadata, &db_error);
        } else if (st != VIP_OK && cache_found && metadata_has_content(&cached)) {
            vip_media_metadata_clear(&metadata);
            metadata = cached;
            memset(&cached, 0, sizeof(cached));
            st = VIP_OK;
            used_cache = true;
        }
    }

    pthread_mutex_lock(&a->data_mutex);
    bool still_current = strcmp(a->details_media_id, job->media_id) == 0;
    if (still_current) {
        vip_media_metadata_clear(&a->details_metadata);
        if (st == VIP_OK) {
            a->details_metadata = metadata;
            memset(&metadata, 0, sizeof(metadata));
            snprintf(a->details_status, sizeof(a->details_status), "%s",
                     used_cache ? "Detalhes carregados do cache" : "Detalhes carregados");
        } else {
            char bounded_error[220];
            bounded_text(bounded_error, sizeof(bounded_error),
                         error.message[0] ? error.message : "provider não retornou metadados", 90);
            snprintf(a->details_status, sizeof(a->details_status), "Sem detalhes: %.220s", bounded_error);
        }
    }
    pthread_mutex_unlock(&a->data_mutex);

    vip_media_metadata_clear(&metadata);
    vip_media_metadata_clear(&cached);
    atomic_store(&a->details_success, still_current && st == VIP_OK);
    atomic_store(&a->details_running, false);
    atomic_store(&a->details_done, true);
    details_job_free(job);
    return NULL;
}

static void clear_details_view(app_t *a) {
    if (!a) return;
    pthread_mutex_lock(&a->data_mutex);
    vip_media_metadata_clear(&a->details_metadata);
    a->details_media_id[0] = '\0';
    a->details_title[0] = '\0';
    a->details_status[0] = '\0';
    pthread_mutex_unlock(&a->data_mutex);
}

static void start_details_load(app_t *a, size_t channel_index) {
    if (!a || atomic_load(&a->details_running) || a->login_mode != LOGIN_XTREAM ||
        a->series_episode_mode || (a->content_kind != CONTENT_VOD && a->content_kind != CONTENT_SERIES) ||
        channel_index >= ACTIVE_CHANNELS(a).len) return;
    vip_channel_t *media = &ACTIVE_CHANNELS(a).items[channel_index];
    if (!media->id || !media->provider_id || !media->id[0]) return;
    const char *server = a->active_server_alt && a->server_alt[0] ? a->server_alt : a->server;
    if (!server[0] || !a->username[0] || !a->password[0]) return;

    details_job_t *job = calloc(1, sizeof(*job));
    if (!job) return;
    job->app = a;
    job->kind = a->content_kind;
    job->server = vip_strdup(server);
    job->username = vip_strdup(a->username);
    job->password = vip_strdup(a->password);
    job->provider_id = vip_strdup(media->provider_id);
    job->media_id = vip_strdup(media->id);
    job->title = vip_strdup(media->name);
    if (!job->server || !job->username || !job->password || !job->provider_id || !job->media_id || !job->title) {
        details_job_free(job);
        return;
    }

    pthread_mutex_lock(&a->data_mutex);
    vip_media_metadata_clear(&a->details_metadata);
    snprintf(a->details_media_id, sizeof(a->details_media_id), "%s", media->id);
    snprintf(a->details_title, sizeof(a->details_title), "%s", media->name);
    snprintf(a->details_status, sizeof(a->details_status), "Carregando detalhes...");
    pthread_mutex_unlock(&a->data_mutex);
    atomic_store(&a->details_done, false);
    atomic_store(&a->details_success, false);
    atomic_store(&a->details_running, true);
    if (pthread_create(&a->details_thread, NULL, details_worker, job) != 0) {
        atomic_store(&a->details_running, false);
        pthread_mutex_lock(&a->data_mutex);
        snprintf(a->details_status, sizeof(a->details_status), "Falha ao iniciar carregamento de detalhes");
        pthread_mutex_unlock(&a->data_mutex);
        details_job_free(job);
        return;
    }
    a->details_thread_started = true;
}

static void maybe_start_details_load(app_t *a) {
    if (!details_panel_active(a) || a->screen != SCREEN_BROWSE || atomic_load(&a->details_running) ||
        a->filtered_len == 0u) return;
    if (a->focused_filtered >= a->filtered_len) a->focused_filtered = a->filtered_len - 1u;
    size_t channel_index = a->filtered[a->focused_filtered];
    if (channel_index >= ACTIVE_CHANNELS(a).len) return;
    const char *id = ACTIVE_CHANNELS(a).items[channel_index].id;
    if (!id || !id[0]) return;
    pthread_mutex_lock(&a->data_mutex);
    bool same = strcmp(a->details_media_id, id) == 0;
    pthread_mutex_unlock(&a->data_mutex);
    if (!same) start_details_load(a, channel_index);
}

static void activate_item(app_t *a, size_t channel_index) {
    if (a->content_kind == CONTENT_SERIES && !a->series_episode_mode) {
        start_series_load(a, channel_index);
    } else {
        enter_player(a, channel_index);
    }
}

static void request_paste(app_t *a, Atom selection) {
    if (a->input_focus == 0) return;
    a->paste_target = a->input_focus;
    XConvertSelection(a->dpy, selection, a->utf8, a->paste_property, a->win, CurrentTime);
}

static char *active_input(app_t *a, size_t *cap) {
    switch (a->input_focus) {
        case INPUT_SERVER: *cap = sizeof(a->server); return a->server;
        case INPUT_SERVER_ALT: *cap = sizeof(a->server_alt); return a->server_alt;
        case INPUT_USERNAME: *cap = sizeof(a->username); return a->username;
        case INPUT_PASSWORD: *cap = sizeof(a->password); return a->password;
        case INPUT_SEARCH: *cap = sizeof(a->search); return a->search;
        case INPUT_PROFILE_NAME: *cap = sizeof(a->profile_name); return a->profile_name;
        default: *cap = 0; return NULL;
    }
}

static void append_input(app_t *a, const char *text, size_t n) {
    size_t cap = 0; char *dst = active_input(a, &cap);
    if (!dst || cap == 0 || !text) return;
    size_t cur = strlen(dst);
    if (n > cap - 1 - cur) n = cap - 1 - cur;
    memcpy(dst + cur, text, n); dst[cur+n] = '\0';
    if (a->input_focus == INPUT_SEARCH) rebuild_filter(a);
}

static void backspace_input(app_t *a) {
    size_t cap = 0; char *dst = active_input(a, &cap); (void)cap;
    if (!dst) return;
    size_t n = strlen(dst);
    if (n == 0) return;
    do { --n; } while (n > 0 && (((unsigned char)dst[n] & 0xc0u) == 0x80u));
    dst[n] = '\0';
    if (a->input_focus == INPUT_SEARCH) rebuild_filter(a);
}


static bool executable_in_path(const char *name) {
    const char *path = getenv("PATH");
    if (!name || !name[0] || !path) return false;
    char *copy = vip_strdup(path);
    if (!copy) return false;
    bool found = false;
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        char full[1024];
        int n = snprintf(full, sizeof(full), "%s/%s", dir[0] ? dir : ".", name);
        if (n > 0 && (size_t)n < sizeof(full) && access(full, X_OK) == 0) { found = true; break; }
    }
    free(copy);
    return found;
}

/* Password persistence is delegated to Secret Service via secret-tool.
 * SQLite stores only non-secret profile fields. */
static bool keyring_store_password(const char *profile_id, const char *password) {
    if (!profile_id || !profile_id[0] || !password || !password[0] || !executable_in_path("secret-tool")) return false;
    int inpipe[2];
    if (pipe(inpipe) != 0) return false;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(inpipe[0], STDIN_FILENO);
        close(inpipe[0]); close(inpipe[1]);
        execlp("secret-tool", "secret-tool", "store", "--label=Visual IPTV", "application", "visual-iptv", "profile", profile_id, (char *)NULL);
        _exit(127);
    }
    close(inpipe[0]);
    if (pid < 0) { close(inpipe[1]); return false; }
    size_t len = strlen(password);
    (void)write(inpipe[1], password, len);
    (void)write(inpipe[1], "\n", 1u);
    close(inpipe[1]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool keyring_lookup_password(const char *profile_id, char *out, size_t cap) {
    if (!out || cap == 0u) return false;
    out[0] = '\0';
    if (!profile_id || !profile_id[0] || !executable_in_path("secret-tool")) return false;
    int outpipe[2];
    if (pipe(outpipe) != 0) return false;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(outpipe[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        close(outpipe[0]); close(outpipe[1]);
        execlp("secret-tool", "secret-tool", "lookup", "application", "visual-iptv", "profile", profile_id, (char *)NULL);
        _exit(127);
    }
    close(outpipe[1]);
    if (pid < 0) { close(outpipe[0]); return false; }
    ssize_t n = read(outpipe[0], out, cap - 1u);
    close(outpipe[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (n <= 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) { out[0] = '\0'; return false; }
    out[n] = '\0';
    while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) out[--n] = '\0';
    return n > 0;
}

static void refresh_profiles(app_t *a) {
    vip_profile_list_clear(&a->profiles);
    vip_profile_list_init(&a->profiles);
    if (!a->db) return;
    vip_error_t error = {0};
    if (vip_database_list_profiles(a->db, &a->profiles, &error) != VIP_OK)
        fprintf(stderr, "[profiles] %s\n", error.message);
    if (a->profile_scroll < 0) a->profile_scroll = 0;
    if ((size_t)a->profile_scroll > a->profiles.len) a->profile_scroll = (int)a->profiles.len;
}

static void save_active_profile(app_t *a) {
    if (!a->db || !a->active_profile_id[0] || !a->server[0]) return;
    char generated_name[160];
    const char *name = a->profile_name;
    if (!name[0]) {
        if (a->login_mode == LOGIN_XTREAM && a->username[0])
            snprintf(generated_name, sizeof(generated_name), "%.127s", a->username);
        else snprintf(generated_name, sizeof(generated_name), "Lista M3U");
        name = generated_name;
        snprintf(a->profile_name, sizeof(a->profile_name), "%.127s", name);
    }
    vip_profile_t profile = {
        .profile_id = a->active_profile_id,
        .name = (char *)name,
        .type = a->login_mode == LOGIN_M3U ? VIP_PROFILE_M3U : VIP_PROFILE_XTREAM,
        .server = a->server,
        .server_alt = a->server_alt,
        .username = a->username,
    };
    vip_error_t error = {0};
    if (vip_database_save_profile(a->db, &profile, &error) != VIP_OK)
        fprintf(stderr, "[profiles] %s\n", error.message);
    else if (a->login_mode == LOGIN_XTREAM && a->password[0] && !keyring_store_password(a->active_profile_id, a->password))
        fprintf(stderr, "[profiles] senha não persistida: Secret Service/secret-tool indisponível\n");
    refresh_profiles(a);
}

static void load_profile_into_form(app_t *a, size_t index) {
    if (!a || index >= a->profiles.len) return;
    vip_profile_t *p = &a->profiles.items[index];
    a->login_mode = p->type == VIP_PROFILE_M3U ? LOGIN_M3U : LOGIN_XTREAM;
    snprintf(a->profile_name, sizeof(a->profile_name), "%s", p->name ? p->name : "");
    snprintf(a->server, sizeof(a->server), "%s", p->server ? p->server : "");
    snprintf(a->server_alt, sizeof(a->server_alt), "%s", p->server_alt ? p->server_alt : "");
    snprintf(a->username, sizeof(a->username), "%s", p->username ? p->username : "");
    a->password[0] = '\0';
    if (a->login_mode == LOGIN_XTREAM) (void)keyring_lookup_password(p->profile_id, a->password, sizeof(a->password));
    a->input_focus = a->login_mode == LOGIN_XTREAM && !a->password[0] ? INPUT_PASSWORD : INPUT_SERVER;
    snprintf(a->status, sizeof(a->status), a->login_mode == LOGIN_XTREAM && !a->password[0]
             ? "Perfil carregado; informe a senha" : "Perfil carregado; pressione Conectar");
}

static bool current_item_has_progress(app_t *a) {
    return a && !a->player_item_live && a->current_channel < ACTIVE_CHANNELS(a).len && a->db;
}

static void update_series_progress(app_t *a, const char *last_episode_id) {
    if (!a->series_episode_mode || !a->series_parent_id[0] || !a->db || ACTIVE_CHANNELS(a).len == 0u) return;
    int watched = 0;
    if (a->progress_flags) {
        for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i) if (a->progress_flags[i].completed) ++watched;
    }
    vip_channel_t *first = &ACTIVE_CHANNELS(a).items[0];
    vip_error_t error = {0};
    (void)vip_database_set_series_progress(a->db, first->provider_id, a->series_parent_id,
                                           last_episode_id, watched, (int)ACTIVE_CHANNELS(a).len, &error);
}

/* Throttle routine writes during playback, but force persistence on pause,
 * seek/exit and other lifecycle boundaries. */
static void save_current_progress(app_t *a, bool force) {
    if (!current_item_has_progress(a) || !a->player) return;
    int64_t now = monotonic_ms();
    vip_mpv_player_snapshot_t snap = {0};
    vip_mpv_player_snapshot(a->player, &snap);
    if (snap.natural_end && a->progress_flags && a->current_channel < ACTIVE_CHANNELS(a).len &&
        a->progress_flags[a->current_channel].completed) return;
    if (!force && !snap.natural_end && now - a->player_last_progress_save_ms < 5000) return;
    if (snap.position_seconds < 0.0 || (snap.duration_seconds <= 1.0 && !snap.natural_end)) return;
    bool completed = snap.natural_end ||
                     (snap.duration_seconds > 0.0 &&
                      (snap.position_seconds / snap.duration_seconds >= 0.95 ||
                       (snap.duration_seconds > 300.0 && snap.duration_seconds - snap.position_seconds <= 60.0)));
    vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[a->current_channel];
    vip_error_t error = {0};
    if (vip_database_set_progress(a->db, ch->provider_id, ch->id,
                                  snap.position_seconds, snap.duration_seconds, completed, &error) == VIP_OK) {
        if (a->progress_flags && a->current_channel < ACTIVE_CHANNELS(a).len) {
            a->progress_flags[a->current_channel].position_seconds = snap.position_seconds;
            a->progress_flags[a->current_channel].duration_seconds = snap.duration_seconds;
            a->progress_flags[a->current_channel].completed = completed;
            a->progress_flags[a->current_channel].updated_at = (int64_t)time(NULL);
        }
        if (a->series_episode_mode) update_series_progress(a, ch->id);
        a->player_last_progress_save_ms = now;
    }
}

static bool player_hud_visible(app_t *a) {
    return !a->fullscreen || a->timeline_dragging || monotonic_ms() < a->player_hud_until_ms;
}

static void show_player_hud(app_t *a) {
    a->player_hud_until_ms = monotonic_ms() + 3000;
    if (a->screen == SCREEN_PLAYER) layout_video_window(a);
}

/* Keep keyboard focus on the application even though mpv owns a child
 * rendering window inside the visual player region. */
static void focus_player_input(app_t *a) {
    if (!a || !a->dpy || !a->win || a->screen != SCREEN_PLAYER) return;
    XSetInputFocus(a->dpy, a->win, RevertToParent, CurrentTime);
    XFlush(a->dpy);
}

/* Recover only focus transitions into the embedded player subtree.  A real
 * application switch (for example Alt+Tab) must be left to the window manager. */
static void recover_player_focus_if_needed(app_t *a, const XFocusChangeEvent *focus_event) {
    if (!a || !focus_event || a->screen != SCREEN_PLAYER) return;
    /* NotifyInferior means focus moved from the top-level UI into one of its
       descendants (for this screen, normally the reparented mpv subtree).
       Do not recover for nonlinear/ancestor transitions: those are used by
       Alt-Tab and other legitimate focus changes to another application. */
    if (focus_event->detail == NotifyInferior) {
        if (getenv("VIPTV_MPV_DEBUG"))
            fprintf(stderr,"[mpv-debug] foco entrou na subárvore do player; devolvendo a ui\n");
        focus_player_input(a);
    }
}

static void format_clock(double seconds, char out[32]) {
    if (seconds < 0.0) seconds = 0.0;
    long s = (long)(seconds + 0.5);
    long h = s / 3600; long m = (s % 3600) / 60; long sec = s % 60;
    if (h > 0) snprintf(out, 32, "%ld:%02ld:%02ld", h, m, sec);
    else snprintf(out, 32, "%02ld:%02ld", m, sec);
}

static void timeline_geometry(app_t *a, int *x, int *y, int *w, int *h) {
    int left = 230;
    int right = 170;
    *x = left;
    *w = a->width - left - right;
    if (*w < 120) *w = 120;
    *y = a->height - PLAYER_CONTROLS_H + 30;
    *h = 14;
}

static void set_fullscreen(app_t *a, bool enable) {
    Atom wm_state = XInternAtom(a->dpy, "_NET_WM_STATE", False);
    Atom fs = XInternAtom(a->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    XEvent e; memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage; e.xclient.window = a->win; e.xclient.message_type = wm_state;
    e.xclient.format = 32; e.xclient.data.l[0] = enable ? 1 : 0; e.xclient.data.l[1] = (long)fs;
    XSendEvent(a->dpy, DefaultRootWindow(a->dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(a->dpy);
    a->fullscreen = enable;
}

/* Enter playback without destroying the mpv process: loadfile is sent over
 * IPC and optional resume position is applied after the file is loaded. */
static void enter_player(app_t *a, size_t channel_index) {
    if (channel_index >= ACTIVE_CHANNELS(a).len || !a->player) return;
    if (a->screen == SCREEN_PLAYER) save_current_progress(a, true);
    a->current_channel = channel_index;
    a->player_item_live = a->content_kind == CONTENT_LIVE && !a->series_episode_mode;
    a->screen = SCREEN_PLAYER;
    if (!a->fullscreen) set_fullscreen(a, true);
    snprintf(a->player_status, sizeof(a->player_status), "Abrindo stream...");
    a->player_open_ms = monotonic_ms();
    a->player_last_progress_save_ms = a->player_open_ms;
    a->player_hud_until_ms = a->player_open_ms + 3000;
    a->player_alt_attempted = false;
    a->timeline_dragging = false;
    /* O player não pausa mais o pipeline de thumbnails: o cache continua
       sendo preenchido mesmo durante a reprodução. */
    set_video_visible(a, true);
    focus_player_input(a);
    /* This child is only a graphics container. mpv creates its own native
       X11/GL window, which the player backend reparents into this container. */
    XSync(a->dpy, False);
    if (getenv("VIPTV_MPV_DEBUG")) {
        XWindowAttributes wa;
        if (XGetWindowAttributes(a->dpy, a->video_win, &wa))
            fprintf(stderr, "[mpv-debug] container-window mapped=%s size=%dx%d\n",
                    wa.map_state == IsViewable ? "yes" : "no", wa.width, wa.height);
    }
    vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[channel_index];
    double resume = 0.0;
    if (!a->player_item_live && a->db) {
        vip_watch_progress_t saved = {0};
        vip_error_t db_error = {0};
        if (vip_database_get_progress(a->db, ch->provider_id, ch->id, &saved, &db_error) == VIP_OK &&
            !saved.completed && saved.position_seconds > 5.0 &&
            (saved.duration_seconds <= 0.0 || saved.position_seconds < saved.duration_seconds - 20.0))
            resume = saved.position_seconds;
    }
    vip_error_t error = {0};
    vip_status_t st = resume > 0.0
        ? vip_mpv_player_load_at(a->player, ch->stream_url, resume, &error)
        : vip_mpv_player_load(a->player, ch->stream_url, &error);
    if (st != VIP_OK) snprintf(a->player_status, sizeof(a->player_status), "%s", error.message);
    fprintf(stderr, "[player] %s%s\n", ch->name, resume > 0.0 ? " (retomado)" : "");
}

static void leave_player(app_t *a) {
    save_current_progress(a, true);
    if (a->player) vip_mpv_player_stop(a->player);
    set_video_visible(a, false);
    if (a->thumbs) vip_thumbnail_scheduler_set_paused(a->thumbs, false);
    if (a->fullscreen) set_fullscreen(a, false);
    a->screen = SCREEN_BROWSE;
    a->input_focus = INPUT_SEARCH;
    a->timeline_dragging = false;
    load_media_state(a);
    rebuild_filter(a);
}

static size_t normalized_server_prefix(const char *server, char *out, size_t cap) {
    if (!server || !server[0] || !out || cap < 2u) return 0;
    while (isspace((unsigned char)*server)) ++server;
    size_t n = strlen(server);
    while (n > 0u && isspace((unsigned char)server[n-1u])) --n;
    while (n > 0u && server[n-1u] == '/') --n;
    if (n + 2u > cap) return 0;
    memcpy(out, server, n);
    out[n++] = '/'; out[n] = '\0';
    return n;
}

/* Failover keeps the Xtream path/query intact and swaps only the normalized
 * server prefix, so the same media identifier is requested from the mirror. */
static char *alternate_stream_url(app_t *a, const char *url) {
    if (!a || !url || !url[0] || !a->server_alt[0] || a->active_server_alt) return NULL;
    char primary[512], alternate[512];
    size_t pn = normalized_server_prefix(a->server, primary, sizeof(primary));
    size_t an = normalized_server_prefix(a->server_alt, alternate, sizeof(alternate));
    if (pn == 0u || an == 0u || strncmp(url, primary, pn) != 0) return NULL;
    const char *suffix = url + pn;
    size_t need = an + strlen(suffix) + 1u;
    char *out = malloc(need);
    if (!out) return NULL;
    snprintf(out, need, "%s%s", alternate, suffix);
    return out;
}

static void maybe_failover_player(app_t *a) {
    if (!a || a->screen != SCREEN_PLAYER || !a->player || a->player_alt_attempted) return;
    if (vip_mpv_player_state(a->player) != VIP_PLAYER_ERROR) return;
    if (a->current_channel >= ACTIVE_CHANNELS(a).len) return;
    char *url = alternate_stream_url(a, ACTIVE_CHANNELS(a).items[a->current_channel].stream_url);
    a->player_alt_attempted = true;
    if (!url) return;
    fprintf(stderr, "[player] stream primário falhou; tentando servidor alternativo\n");
    a->player_open_ms = monotonic_ms();
    vip_error_t error = {0};
    if (vip_mpv_player_load(a->player, url, &error) != VIP_OK)
        snprintf(a->player_status, sizeof(a->player_status), "%s", error.message);
    free(url);
}

static void draw_input(app_t *a, int x, int y, int w, int h, const char *value,
                       const char *placeholder, int focus_id, bool password) {
    unsigned long bg = a->colors.panel2;
    fill_rect(a, x, y, (unsigned)w, (unsigned)h, bg);
    stroke_rect(a, x, y, (unsigned)w, (unsigned)h,
                a->input_focus == focus_id ? a->colors.accent : a->colors.border);
    const char *text = value && value[0] ? value : placeholder;
    char masked[256];
    if (password && value && value[0]) {
        size_t n = strlen(value); if (n > sizeof(masked)-1) n = sizeof(masked)-1;
        memset(masked, '*', n); masked[n] = '\0'; text = masked;
    }
    draw_text(a, x + 12, y + h/2 + 6, text, value && value[0] ? a->colors.text : a->colors.muted);
}

static void draw_login(app_t *a) {
    fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);
    int w = a->width > 1120 ? 1080 : a->width - 40;
    if (w < 720) w = 720;
    int h = 620;
    int x = (a->width - w) / 2, y = (a->height - h) / 2;
    if (y < 18) y = 18;
    fill_rect(a, x, y, (unsigned)w, (unsigned)h, a->colors.panel);
    stroke_rect(a, x, y, (unsigned)w, (unsigned)h, a->colors.border);
    draw_text(a, x + 34, y + 42, "Visual IPTV", a->colors.text);
    draw_text(a, x + 34, y + 66, "mpv integrado | Xtream e M3U | perfis locais", a->colors.muted);

    int form_x = x + 34, form_w = (w * 58) / 100 - 50;
    int list_x = x + (w * 60) / 100, list_w = w - (list_x - x) - 34;
    int mode_y = y + 88;
    int mode_w = (form_w - 10) / 2;
    for (int i = 0; i < 2; ++i) {
        bool selected = (int)a->login_mode == i;
        int bx = form_x + i * (mode_w + 10);
        fill_rect(a, bx, mode_y, (unsigned)mode_w, 42, selected ? a->colors.accent2 : a->colors.panel2);
        stroke_rect(a, bx, mode_y, (unsigned)mode_w, 42, selected ? a->colors.accent : a->colors.border);
        draw_centered(a, bx, mode_y + 27, mode_w, i == LOGIN_XTREAM ? "Xtream" : "M3U", selected ? a->colors.text : a->colors.muted);
    }

    draw_input(a, form_x, y+142, form_w, 44, a->profile_name, "Nome da lista (opcional)", INPUT_PROFILE_NAME, false);
    if (a->login_mode == LOGIN_XTREAM) {
        draw_input(a, form_x, y+196, form_w, 44, a->server, "Servidor primário (https://...)", INPUT_SERVER, false);
        draw_input(a, form_x, y+250, form_w, 44, a->server_alt, "Servidor alternativo (opcional)", INPUT_SERVER_ALT, false);
        draw_input(a, form_x, y+304, form_w, 44, a->username, "Usuário", INPUT_USERNAME, false);
        draw_input(a, form_x, y+358, form_w, 44, a->password, "Senha", INPUT_PASSWORD, true);
    } else {
        draw_input(a, form_x, y+196, form_w, 44, a->server, "URL ou caminho de playlist .m3u/.m3u8", INPUT_SERVER, false);
        draw_text(a, form_x, y+266, "M3U remoto (HTTP/HTTPS) ou arquivo local.", a->colors.muted);
        draw_text(a, form_x, y+292, "A playlist é carregada diretamente, sem emulador.", a->colors.muted);
    }
    bool ready = a->server[0] && !atomic_load(&a->login_running) &&
                 (a->login_mode == LOGIN_M3U || (a->username[0] && a->password[0]));
    int connect_y = y + 430;
    fill_rect(a, form_x, connect_y, (unsigned)form_w, 50, ready ? a->colors.accent2 : a->colors.panel2);
    stroke_rect(a, form_x, connect_y, (unsigned)form_w, 50, ready ? a->colors.accent : a->colors.border);
    draw_centered(a, form_x, connect_y+31, form_w, atomic_load(&a->login_running) ? "Conectando..." : "Conectar",
                  ready ? a->colors.text : a->colors.muted);

    draw_text(a, list_x, y+106, "Listas salvas", a->colors.text);
    int row_y = y + 132;
    int visible = 7;
    for (int r = 0; r < visible; ++r) {
        int idx = a->profile_scroll + r;
        if (idx < 0 || (size_t)idx >= a->profiles.len) break;
        vip_profile_t *p = &a->profiles.items[idx];
        fill_rect(a, list_x, row_y, (unsigned)list_w, 52, a->colors.panel2);
        stroke_rect(a, list_x, row_y, (unsigned)list_w, 52, a->colors.border);
        char label[220];
        snprintf(label, sizeof(label), "%s  [%s]", p->name ? p->name : "Lista", p->type == VIP_PROFILE_M3U ? "M3U" : "Xtream");
        bounded_text(label, sizeof(label), label, 44);
        draw_text(a, list_x+12, row_y+23, label, a->colors.text);
        char sub[220]; bounded_text(sub, sizeof(sub), p->server ? p->server : "", 48);
        draw_text(a, list_x+12, row_y+43, sub, a->colors.muted);
        row_y += 60;
    }
    if (a->profiles.len == 0u) draw_text(a, list_x, y+154, "Nenhuma lista salva ainda.", a->colors.muted);

    char status_copy[512];
    pthread_mutex_lock(&a->data_mutex);
    snprintf(status_copy, sizeof(status_copy), "%s", a->status);
    pthread_mutex_unlock(&a->data_mutex);
    draw_text(a, form_x, y+h-42, status_copy,
              (strstr(status_copy, "falha") || strstr(status_copy, "Erro")) ? a->colors.danger : a->colors.muted);
}

static int draw_wrapped_text(app_t *a, int x, int y, int width,
                             const char *text, int max_lines,
                             unsigned long color) {
    if (!text || !text[0] || max_lines <= 0) return y;
    char copy[3072];
    snprintf(copy, sizeof(copy), "%s", text);
    for (char *p = copy; *p; ++p) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
    char line[768] = {0};
    char *save = NULL;
    int lines = 0;
    for (char *word = strtok_r(copy, " ", &save); word && lines < max_lines;
         word = strtok_r(NULL, " ", &save)) {
        char candidate[768];
        /* Build the candidate explicitly so very long provider text is safely
         * truncated without triggering format-truncation warnings. */
        size_t candidate_len = strlen(line);
        if (candidate_len >= sizeof(candidate)) candidate_len = sizeof(candidate) - 1u;
        memcpy(candidate, line, candidate_len);
        if (candidate_len > 0u && candidate_len + 1u < sizeof(candidate))
            candidate[candidate_len++] = ' ';
        size_t room = sizeof(candidate) - candidate_len - 1u;
        size_t word_len = strlen(word);
        if (word_len > room) word_len = room;
        memcpy(candidate + candidate_len, word, word_len);
        candidate[candidate_len + word_len] = '\0';
        if (line[0] && text_width(a, candidate) > width) {
            draw_text(a, x, y, line, color);
            y += 20;
            ++lines;
            if (lines >= max_lines) break;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
    }
    if (line[0] && lines < max_lines) {
        draw_text(a, x, y, line, color);
        y += 20;
    }
    return y;
}

static void detail_art_id(char *out, size_t cap, const char *media_id) {
    snprintf(out, cap, "detail-art:%s", media_id ? media_id : "unknown");
}

static void enqueue_detail_artwork(app_t *a, const vip_channel_t *ch,
                                   const char *art_url, int64_t priority) {
    if (!a || !a->thumbs || !ch || !ch->provider_id || !ch->id || !ch->stream_url ||
        !art_url || !art_url[0]) return;
    char id[256];
    detail_art_id(id, sizeof(id), ch->id);
    vip_thumbnail_request_t req = {
        .provider_id = ch->provider_id,
        .channel_id = id,
        .logo_url = (char *)art_url,
        .stream_url = ch->stream_url,
        .priority = priority,
    };
    vip_error_t error = {0};
    (void)vip_thumbnail_scheduler_enqueue(a->thumbs, &req, &error);
}

static void draw_details_panel(app_t *a) {
    if (!details_panel_active(a) || a->filtered_len == 0u) return;
    if (a->focused_filtered >= a->filtered_len) a->focused_filtered = a->filtered_len - 1u;
    size_t chidx = a->filtered[a->focused_filtered];
    if (chidx >= ACTIVE_CHANNELS(a).len) return;
    vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[chidx];

    int px, py, pw, ph;
    details_panel_geometry(a, &px, &py, &pw, &ph);
    fill_rect(a, px, py, (unsigned)pw, (unsigned)ph, a->colors.panel);
    stroke_rect(a, px, py, (unsigned)pw, (unsigned)ph, a->colors.border);

    char loaded_id[128], status[256], plot[3072], cover[1024], backdrop[1024];
    char genre[256], release_date[128], rating[64], duration[128], cast[768], director[512];
    pthread_mutex_lock(&a->data_mutex);
    snprintf(loaded_id, sizeof(loaded_id), "%s", a->details_media_id);
    snprintf(status, sizeof(status), "%s", a->details_status);
    snprintf(plot, sizeof(plot), "%s", a->details_metadata.plot ? a->details_metadata.plot : "");
    snprintf(cover, sizeof(cover), "%s", a->details_metadata.cover_url ? a->details_metadata.cover_url : "");
    snprintf(backdrop, sizeof(backdrop), "%s", a->details_metadata.backdrop_url ? a->details_metadata.backdrop_url : "");
    snprintf(genre, sizeof(genre), "%s", a->details_metadata.genre ? a->details_metadata.genre : "");
    snprintf(release_date, sizeof(release_date), "%s", a->details_metadata.release_date ? a->details_metadata.release_date : "");
    snprintf(rating, sizeof(rating), "%s", a->details_metadata.rating ? a->details_metadata.rating : "");
    snprintf(duration, sizeof(duration), "%s", a->details_metadata.duration ? a->details_metadata.duration : "");
    snprintf(cast, sizeof(cast), "%s", a->details_metadata.cast ? a->details_metadata.cast : "");
    snprintf(director, sizeof(director), "%s", a->details_metadata.director ? a->details_metadata.director : "");
    pthread_mutex_unlock(&a->data_mutex);

    char title[256]; bounded_text(title, sizeof(title), ch->name, 44);
    draw_text(a, px + 16, py + 30, title, a->colors.text);
    bool favorite = a->favorite_flags && a->favorite_flags[chidx];
    int fav_w = 92, fav_h = 32, fav_x = px + pw - fav_w - 14, fav_y = py + 10;
    fill_rect(a, fav_x, fav_y, (unsigned)fav_w, (unsigned)fav_h,
              favorite ? a->colors.accent2 : a->colors.panel2);
    stroke_rect(a, fav_x, fav_y, (unsigned)fav_w, (unsigned)fav_h,
                favorite ? a->colors.accent : a->colors.border);
    draw_centered(a, fav_x, fav_y + 21, fav_w, favorite ? "SALVO" : "FAVORITAR",
                  favorite ? a->colors.text : a->colors.muted);

    bool current = ch->id && strcmp(loaded_id, ch->id) == 0;
    int art_x = px + 16, art_y = py + 50, art_w = pw - 32, art_h = 170;
    fill_rect(a, art_x, art_y, (unsigned)art_w, (unsigned)art_h, a->colors.black);
    bool art_ok = false;
    const char *art_url = backdrop[0] ? backdrop : cover;
    if (current && art_url[0]) {
        char art_id[256]; detail_art_id(art_id, sizeof(art_id), ch->id);
        vip_error_t art_error = {0};
        char *path = vip_thumbnail_cache_path(a->cache_dir, ch->provider_id, art_id, &art_error);
        art_ok = path && draw_cached_image_contain(a, path, art_x, art_y, art_w, art_h);
        free(path);
        if (!art_ok) enqueue_detail_artwork(a, ch, art_url, 2000000LL);
    }
    if (!art_ok) draw_centered(a, art_x, art_y + art_h / 2 + 5, art_w,
                               current ? "carregando banner..." : "carregando detalhes...", a->colors.muted);
    stroke_rect(a, art_x, art_y, (unsigned)art_w, (unsigned)art_h, a->colors.border);

    int y = art_y + art_h + 24;
    if (!current) {
        draw_text(a, px + 16, y, "Carregando detalhes da obra...", a->colors.muted);
        return;
    }

    char facts[640] = {0};
    size_t used = 0u;
    const char *values[4] = {release_date, genre, rating, duration};
    const char *labels[4] = {"Data", "Gênero", "Nota", "Duração"};
    for (size_t i = 0; i < 4u; ++i) {
        if (!values[i][0]) continue;
        int n = snprintf(facts + used, sizeof(facts) - used, "%s%s: %s",
                         used ? " | " : "", labels[i], values[i]);
        if (n < 0 || (size_t)n >= sizeof(facts) - used) break;
        used += (size_t)n;
    }
    if (facts[0]) {
        y = draw_wrapped_text(a, px + 16, y, pw - 32, facts, 2, a->colors.muted);
        y += 6;
    }
    draw_text(a, px + 16, y, "Sinopse", a->colors.text);
    y += 22;
    if (plot[0]) y = draw_wrapped_text(a, px + 16, y, pw - 32, plot, 8, a->colors.muted);
    else {
        draw_text(a, px + 16, y, "O provider não enviou sinopse para este item.", a->colors.muted);
        y += 20;
    }
    y += 8;
    if (director[0] && y < py + ph - 72) {
        char line[600]; snprintf(line, sizeof(line), "Direção: %s", director);
        y = draw_wrapped_text(a, px + 16, y, pw - 32, line, 2, a->colors.muted);
    }
    if (cast[0] && y < py + ph - 48) {
        char line[880]; snprintf(line, sizeof(line), "Elenco: %s", cast);
        (void)draw_wrapped_text(a, px + 16, y, pw - 32, line, 2, a->colors.muted);
    }
    if (!plot[0] && status[0])
        draw_text(a, px + 16, py + ph - 18, status, a->colors.muted);
}

static void draw_toast(app_t *a) {
    if (!a || !a->toast[0] || monotonic_ms() >= a->toast_until_ms) return;
    int w = text_width(a, a->toast) + 38;
    if (w < 220) w = 220;
    if (w > a->width - 40) w = a->width - 40;
    int x = (a->width - w) / 2;
    int y = TOPBAR_H + 10;
    fill_rect(a, x, y, (unsigned)w, 40, a->colors.panel2);
    stroke_rect(a, x, y, (unsigned)w, 40, a->colors.accent);
    draw_centered(a, x, y + 26, w, a->toast, a->colors.text);
}

static int category_visible_rows(app_t *a) { int n = (a->height - TOPBAR_H - 50) / 40; return n > 1 ? n : 1; }

static void draw_browse(app_t *a) {
    fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);
    fill_rect(a, 0, 0, (unsigned)a->width, TOPBAR_H, a->colors.panel);
    fill_rect(a, 0, TOPBAR_H, SIDEBAR_W, (unsigned)(a->height-TOPBAR_H), a->colors.panel2);

    const int tab_y = 12, tab_h = 46;
    const int tab_x[3] = {8, 88, 174};
    const int tab_w[3] = {74, 80, 88};
    for (int k = 0; k < 3; ++k) {
        bool selected = (int)a->content_kind == k;
        fill_rect(a, tab_x[k], tab_y, (unsigned)tab_w[k], (unsigned)tab_h,
                  selected ? a->colors.accent2 : a->colors.panel2);
        stroke_rect(a, tab_x[k], tab_y, (unsigned)tab_w[k], (unsigned)tab_h,
                    selected ? a->colors.accent : a->colors.border);
        draw_centered(a, tab_x[k], 42, tab_w[k], content_label((content_kind_t)k),
                      selected ? a->colors.text : a->colors.muted);
    }

    int list_w = 94, fav_w = 174;
    int list_x = a->width - list_w - 18;
    int fav_x = list_x - fav_w - 10;
    int search_w = fav_x - (SIDEBAR_W + 18) - 10;
    if (search_w < 180) search_w = 180;
    char search_hint[96]; snprintf(search_hint, sizeof(search_hint), "Buscar %s...", content_plural(a));
    draw_input(a, SIDEBAR_W+18, 12, search_w, 46, a->search, search_hint, INPUT_SEARCH, false);
    fill_rect(a, fav_x, 12, (unsigned)fav_w, 46, a->favorites_only ? a->colors.accent2 : a->colors.panel2);
    stroke_rect(a, fav_x, 12, (unsigned)fav_w, 46, a->favorites_only ? a->colors.accent : a->colors.border);
    char fav_label[128]; snprintf(fav_label, sizeof(fav_label), "* Favoritos (%zu)", favorite_count(a));
    draw_centered(a, fav_x, 42, fav_w, fav_label, a->favorites_only ? a->colors.text : a->colors.muted);
    fill_rect(a, list_x, 12, (unsigned)list_w, 46, a->colors.panel2);
    stroke_rect(a, list_x, 12, (unsigned)list_w, 46, a->colors.border);
    draw_centered(a, list_x, 42, list_w, "Listas", a->colors.muted);

    int y = TOPBAR_H + 12;
    bool all_sel = a->selected_category < 0;
    fill_rect(a, 8, y, SIDEBAR_W-16, 36, all_sel ? a->colors.accent2 : a->colors.panel2);
    char all_label[128]; snprintf(all_label, sizeof(all_label), "%s (%zu)", all_content_label(a), ACTIVE_CHANNELS(a).len);
    draw_text(a, 18, y+24, all_label, all_sel ? a->colors.text : a->colors.muted);
    y += 42;
    int rows = category_visible_rows(a) - 1;
    for (int r = 0; r < rows; ++r) {
        int idx = a->category_scroll + r;
        if (idx < 0 || (size_t)idx >= ACTIVE_CATEGORIES(a).len) break;
        bool selected = a->selected_category == idx;
        fill_rect(a, 8, y, SIDEBAR_W-16, 36, selected ? a->colors.accent2 : a->colors.panel2);
        char full_label[512]; char label[256]; size_t count = a->category_counts ? a->category_counts[idx] : 0;
        snprintf(full_label, sizeof(full_label), "%s (%zu)", ACTIVE_CATEGORIES(a).items[idx].name, count);
        bounded_text(label, sizeof(label), full_label, 34);
        draw_text(a, 18, y+24, label, selected ? a->colors.text : a->colors.muted);
        y += 42;
    }

    card_layout_t layout = browse_layout(a);
    int content_x = SIDEBAR_W + 20;
    int content_y = TOPBAR_H + 18;
    int avail_w = a->width - content_x - 18;
    int first_row = a->grid_scroll / layout.row_step;
    int y_offset = -(a->grid_scroll % layout.row_step);
    int visible_rows = (a->height - content_y) / layout.row_step + 3;

    if (a->filtered_len == 0) {
        char empty[160]; snprintf(empty, sizeof(empty), "Nenhum %s nesta seleção", content_plural(a));
        draw_text(a, content_x, content_y+32, empty, a->colors.muted);
        return;
    }

    for (int rr = 0; rr < visible_rows; ++rr) {
        int row = first_row + rr;
        int cy = content_y + y_offset + rr * layout.row_step;
        if (cy > a->height || cy + layout.card_h < TOPBAR_H) continue;
        for (int col = 0; col < layout.cols; ++col) {
            size_t fidx = (size_t)row * (size_t)layout.cols + (size_t)col;
            if (fidx >= a->filtered_len) break;
            size_t chidx = a->filtered[fidx];
            vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[chidx];
            int cx = content_x + col * (layout.card_w + GRID_GAP);
            bool focused = fidx == a->focused_filtered;
            if (focused) {
                stroke_rect(a, cx-3, cy-3, (unsigned)(layout.card_w+6), (unsigned)(layout.card_h+6), a->colors.accent);
                stroke_rect(a, cx-2, cy-2, (unsigned)(layout.card_w+4), (unsigned)(layout.card_h+4), a->colors.accent);
            }
            fill_rect(a, cx, cy, (unsigned)layout.card_w, (unsigned)layout.art_h, a->colors.black);
            vip_error_t error = {0};
            char *path = vip_thumbnail_cache_path(a->cache_dir, ch->provider_id, ch->id, &error);
            bool image_ok = path && draw_cached_image_contain(a, path, cx, cy, layout.card_w, layout.art_h);
            if (!image_ok) {
                draw_centered(a, cx, cy + layout.art_h/2 + 5, layout.card_w, "carregando imagem...", a->colors.muted);
                int distance = rr >= 0 ? rr : -rr;
                int64_t priority = 1000000LL - (int64_t)distance * 1000LL - col;
                enqueue_thumbnail(a, ch, priority);
            }
            free(path);
            stroke_rect(a, cx, cy, (unsigned)layout.card_w, (unsigned)layout.art_h, a->colors.border);
            bool favorite = a->favorite_flags && a->favorite_flags[chidx];
            int card_fav_w = 58, card_fav_h = 28, card_fav_x = cx + layout.card_w - card_fav_w - 6, card_fav_y = cy + 6;
            fill_rect(a, card_fav_x, card_fav_y, (unsigned)card_fav_w, (unsigned)card_fav_h,
                      favorite ? a->colors.accent2 : a->colors.panel2);
            stroke_rect(a, card_fav_x, card_fav_y, (unsigned)card_fav_w, (unsigned)card_fav_h,
                        favorite ? a->colors.accent : a->colors.border);
            draw_centered(a, card_fav_x, card_fav_y + 19, card_fav_w, favorite ? "SALVO" : "FAV",
                          favorite ? a->colors.text : a->colors.muted);
            if (a->progress_flags && (a->content_kind == CONTENT_VOD || a->series_episode_mode)) {
                vip_watch_progress_t *pr = &a->progress_flags[chidx];
                if (pr->duration_seconds > 1.0 || pr->completed) {
                    double ratio = pr->completed ? 1.0 : pr->position_seconds / pr->duration_seconds;
                    if (ratio < 0.0) ratio = 0.0;
                    if (ratio > 1.0) ratio = 1.0;
                    int pw = (int)((double)layout.card_w * ratio);
                    fill_rect(a, cx, cy + layout.art_h - 5, (unsigned)layout.card_w, 5, a->colors.panel2);
                    if (pw > 0) fill_rect(a, cx, cy + layout.art_h - 5, (unsigned)pw, 5, a->colors.accent);
                    if (pr->completed) {
                        int badge_w = 92;
                        fill_rect(a, cx + 6, cy + 6, (unsigned)badge_w, 26, a->colors.accent2);
                        stroke_rect(a, cx + 6, cy + 6, (unsigned)badge_w, 26, a->colors.accent);
                        draw_centered(a, cx + 6, cy + 24, badge_w, "ASSISTIDO", a->colors.text);
                    } else if (pr->duration_seconds > 1.0 && pr->position_seconds > 3.0) {
                        int percent = (int)(ratio * 100.0 + 0.5);
                        char badge[32]; snprintf(badge, sizeof(badge), "%d%%", percent);
                        fill_rect(a, cx + 6, cy + 6, 54, 26, a->colors.panel2);
                        stroke_rect(a, cx + 6, cy + 6, 54, 26, a->colors.accent);
                        draw_centered(a, cx + 6, cy + 24, 54, badge, a->colors.text);
                    }
                }
            }
            char title[160]; bounded_text(title, sizeof(title), ch->name, layout.mode == ART_PORTRAIT ? 28 : 42);
            draw_text(a, cx, cy + layout.art_h + 24, title, a->colors.text);
            if (a->content_kind == CONTENT_SERIES && !a->series_episode_mode &&
                a->series_watched && a->series_total && a->series_total[chidx] > 0) {
                char progress[80];
                snprintf(progress, sizeof(progress), "%d/%d episódios", a->series_watched[chidx], a->series_total[chidx]);
                draw_text(a, cx, cy + layout.art_h + 43, progress, a->colors.muted);
            }
        }
    }

    draw_details_panel(a);

    if (atomic_load(&a->series_running)) {
        char loading[512];
        pthread_mutex_lock(&a->data_mutex); snprintf(loading, sizeof(loading), "%s", a->status); pthread_mutex_unlock(&a->data_mutex);
        int box_w = avail_w > 680 ? 680 : avail_w; int box_x = content_x + (avail_w - box_w) / 2; int box_y = content_y + 20;
        fill_rect(a, box_x, box_y, (unsigned)box_w, 56, a->colors.panel2);
        stroke_rect(a, box_x, box_y, (unsigned)box_w, 56, a->colors.accent);
        draw_centered(a, box_x, box_y + 35, box_w, loading, a->colors.text);
    }
    draw_toast(a);
}

static void layout_video_window(app_t *a) {
    if (!a || !a->dpy || !a->video_win || a->width <= 0 || a->height <= 0) return;
    bool hud = player_hud_visible(a);
    int top = a->fullscreen ? 0 : PLAYER_HEADER_H;
    int bottom = hud ? PLAYER_CONTROLS_H : 0;
    int video_h = a->height - top - bottom;
    if (video_h < 1) video_h = 1;
    XMoveResizeWindow(a->dpy, a->video_win, 0, top, (unsigned)a->width, (unsigned)video_h);
    if (a->player_input_win) {
        XMoveResizeWindow(a->dpy, a->player_input_win, 0, top, (unsigned)a->width, (unsigned)video_h);
        if (a->video_mapped) XRaiseWindow(a->dpy, a->player_input_win);
    }
}

static void set_video_visible(app_t *a, bool visible) {
    if (!a || !a->dpy || !a->video_win || a->video_mapped == visible) return;
    if (visible) {
        layout_video_window(a);
        XMapRaised(a->dpy, a->video_win);
        if (a->player_input_win) XMapRaised(a->dpy, a->player_input_win);
    } else {
        if (a->player_input_win) XUnmapWindow(a->dpy, a->player_input_win);
        XUnmapWindow(a->dpy, a->video_win);
    }
    a->video_mapped = visible;
    XFlush(a->dpy);
}

/* video_win contains mpv pixels; player_input_win is an InputOnly sibling
 * stacked above it so mouse motion/clicks still reach the Visual IPTV HUD. */
static void sync_video_window(app_t *a) {
    if (!a || a->screen != SCREEN_PLAYER || !a->player) {
        set_video_visible(a, false);
        return;
    }
    vip_player_state_t state = vip_mpv_player_state(a->player);
    bool visible = state == VIP_PLAYER_OPENING || state == VIP_PLAYER_BUFFERING ||
                   state == VIP_PLAYER_PLAYING || state == VIP_PLAYER_PAUSED ||
                   state == VIP_PLAYER_RECONNECTING;
    set_video_visible(a, visible);
    if (visible) layout_video_window(a);
}

static void draw_player(app_t *a) {
    fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.black);
    vip_mpv_player_snapshot_t snap = {0};
    if (a->player) vip_mpv_player_snapshot(a->player, &snap);
    vip_player_state_t player_state = a->player ? snap.state : VIP_PLAYER_ERROR;
    const char *state_text = a->player ? vip_mpv_player_state_name(player_state) : a->player_status;
    bool hud = player_hud_visible(a);

    if (!a->fullscreen) {
        fill_rect(a, 0, 0, (unsigned)a->width, PLAYER_HEADER_H, a->colors.panel);
        fill_rect(a, 12, 10, 132, 44, a->colors.panel2);
        stroke_rect(a, 12, 10, 132, 44, a->colors.border);
        draw_text(a, 28, 39, "< Voltar", a->colors.text);
        if (a->current_channel < ACTIVE_CHANNELS(a).len)
            draw_text(a, 168, 39, ACTIVE_CHANNELS(a).items[a->current_channel].name, a->colors.text);
    }

    if (hud) {
        int y = a->height - PLAYER_CONTROLS_H;
        fill_rect(a, 0, y, (unsigned)a->width, PLAYER_CONTROLS_H, a->colors.panel);
        fill_rect(a, 16, y+18, 52, 46, a->colors.panel2);
        stroke_rect(a, 16, y+18, 52, 46, a->colors.border);
        draw_centered(a, 16, y+47, 52, snap.paused ? ">" : "||", a->colors.text);
        fill_rect(a, 76, y+18, 82, 46, a->colors.panel2);
        stroke_rect(a, 76, y+18, 82, 46, a->colors.border);
        draw_centered(a, 76, y+47, 82, "< Voltar", a->colors.text);
        if (a->player_item_live) {
            draw_text(a, 176, y+48, "AO VIVO", a->colors.text);
            draw_text(a, 258, y+48, "<- -> troca canal", a->colors.muted);
        } else {
            int tx,ty,tw,th; timeline_geometry(a,&tx,&ty,&tw,&th);
            fill_rect(a, tx, ty, (unsigned)tw, (unsigned)th, a->colors.panel2);
            double ratio = snap.duration_seconds > 0.0 ? snap.position_seconds / snap.duration_seconds : 0.0;
            if (ratio < 0.0) ratio = 0.0;
            if (ratio > 1.0) ratio = 1.0;
            int fill = (int)((double)tw * ratio);
            if (fill > 0) fill_rect(a, tx, ty, (unsigned)fill, (unsigned)th, a->colors.accent);
            stroke_rect(a, tx, ty, (unsigned)tw, (unsigned)th, a->colors.border);
            char pos[32], dur[32]; format_clock(snap.position_seconds,pos); format_clock(snap.duration_seconds,dur);
            draw_text(a, 166, y+45, pos, a->colors.text);
            draw_text(a, a->width-150, y+45, dur, a->colors.text);
            draw_text(a, tx, y+70, "Clique/arraste para buscar | <- -> 10s", a->colors.muted);
        }
        int sw = text_width(a, state_text);
        draw_text(a, a->width - sw - 18, y+72, state_text,
                  player_state == VIP_PLAYER_ERROR ? a->colors.danger : a->colors.muted);
    }

    if (player_state == VIP_PLAYER_ERROR) {
        const char *err = a->player ? vip_mpv_player_last_error(a->player) : a->player_status;
        char bounded[220]; bounded_text(bounded, sizeof(bounded), err && err[0] ? err : "Falha desconhecida no stream", 90);
        draw_centered(a, 0, a->height / 2 - 12, a->width, "Erro ao reproduzir", a->colors.danger);
        draw_centered(a, 20, a->height / 2 + 22, a->width - 40, bounded, a->colors.muted);
        draw_centered(a, 0, a->height / 2 + 56, a->width, "Pressione Esc para voltar", a->colors.muted);
    } else if (!a->video_mapped) {
        draw_centered(a, 0, a->height / 2, a->width, state_text, a->colors.muted);
    }
}

static bool ensure_backbuffer(app_t *a) {
    if (a->width <= 0 || a->height <= 0) return false;
    if (a->backbuffer && a->backbuffer_w == a->width && a->backbuffer_h == a->height) return true;
    if (a->backbuffer) {
        XFreePixmap(a->dpy, a->backbuffer);
        a->backbuffer = 0;
    }
    a->backbuffer = XCreatePixmap(a->dpy, a->win, (unsigned)a->width, (unsigned)a->height, (unsigned)a->depth);
    if (!a->backbuffer) return false;
    a->backbuffer_w = a->width;
    a->backbuffer_h = a->height;
    return true;
}

static void redraw(app_t *a) {
    bool buffered = ensure_backbuffer(a);
    a->draw = buffered ? a->backbuffer : a->win;
    if (a->screen == SCREEN_LOGIN) draw_login(a);
    else if (a->screen == SCREEN_BROWSE) draw_browse(a);
    else draw_player(a);
    if (buffered) {
        XCopyArea(a->dpy, a->backbuffer, a->win, a->gc, 0, 0,
                  (unsigned)a->width, (unsigned)a->height, 0, 0);
    }
    a->draw = a->win;
    XFlush(a->dpy);
}

static void choose_category(app_t *a, int index) {
    a->selected_category = index;
    rebuild_filter(a);
}

static void handle_browse_click(app_t *a, int x, int y) {
    const int tab_x[3] = {8, 88, 174}; const int tab_w[3] = {74, 80, 88};
    if (y >= 12 && y < 58 && x < SIDEBAR_W) {
        for (int k = 0; k < 3; ++k) if (point_in(x, y, tab_x[k], 12, tab_w[k], 46)) { switch_content(a, (content_kind_t)k); return; }
    }
    int list_w=94, fav_w=174, list_x=a->width-list_w-18, fav_x=list_x-fav_w-10;
    int search_w=fav_x-(SIDEBAR_W+18)-10; if (search_w<180) search_w=180;
    if (point_in(x,y,SIDEBAR_W+18,12,search_w,46)) { a->input_focus=INPUT_SEARCH; return; }
    if (point_in(x,y,fav_x,12,fav_w,46)) { a->favorites_only=!a->favorites_only; rebuild_filter(a); return; }
    if (point_in(x,y,list_x,12,list_w,46)) {
        if (a->thumbs) vip_thumbnail_scheduler_cancel_pending(a->thumbs);
        clear_details_view(a);
        refresh_profiles(a); a->screen=SCREEN_LOGIN; a->input_focus=INPUT_SERVER;
        snprintf(a->status,sizeof(a->status),"Escolha uma lista salva ou conecte outra"); return;
    }
    if (x < SIDEBAR_W && y >= TOPBAR_H) {
        int local=y-(TOPBAR_H+12); if (local>=0 && local<36) { choose_category(a,-1); return; }
        local-=42; if (local>=0) { int row=local/42; if (local%42<36) { int idx=a->category_scroll+row; if (idx>=0 && (size_t)idx<ACTIVE_CATEGORIES(a).len) choose_category(a,idx); } }
        return;
    }
    if (details_panel_active(a)) {
        int px, py, pw, ph;
        details_panel_geometry(a, &px, &py, &pw, &ph);
        if (point_in(x, y, px, py, pw, ph)) {
            if (a->filtered_len > 0u) {
                if (a->focused_filtered >= a->filtered_len) a->focused_filtered = a->filtered_len - 1u;
                size_t chidx = a->filtered[a->focused_filtered];
                int panel_fav_w = 92, panel_fav_h = 32;
                int panel_fav_x = px + pw - panel_fav_w - 14, panel_fav_y = py + 10;
                if (point_in(x, y, panel_fav_x, panel_fav_y, panel_fav_w, panel_fav_h)) toggle_favorite(a, chidx);
            }
            return;
        }
    }
    card_layout_t layout=browse_layout(a);
    int content_x=SIDEBAR_W+20, content_y=TOPBAR_H+18;
    if (x<content_x || y<content_y) return;
    int relx=x-content_x, rely=y-content_y+a->grid_scroll;
    int col=relx/(layout.card_w+GRID_GAP), row=rely/layout.row_step;
    if (col<0 || col>=layout.cols || relx%(layout.card_w+GRID_GAP)>=layout.card_w || rely%layout.row_step>=layout.card_h) return;
    size_t fidx=(size_t)row*(size_t)layout.cols+(size_t)col;
    if (fidx<a->filtered_len) {
        a->focused_filtered=fidx; size_t chidx=a->filtered[fidx];
        int cx=content_x+col*(layout.card_w+GRID_GAP); int cy=content_y-a->grid_scroll+row*layout.row_step;
        if (point_in(x,y,cx+layout.card_w-66,cy+2,64,38)) toggle_favorite(a,chidx);
        else activate_item(a,chidx);
    }
}

static void handle_click(app_t *a, int x, int y) {
    if (a->screen == SCREEN_LOGIN) {
        int w=a->width>1120?1080:a->width-40; if(w<720)w=720; int h=620,px=(a->width-w)/2,py=(a->height-h)/2; if(py<18)py=18;
        int form_x=px+34, form_w=(w*58)/100-50, list_x=px+(w*60)/100, list_w=w-(list_x-px)-34;
        int mode_w=(form_w-10)/2;
        if (point_in(x,y,form_x,py+88,mode_w,42)) { a->login_mode=LOGIN_XTREAM; a->input_focus=INPUT_SERVER; return; }
        if (point_in(x,y,form_x+mode_w+10,py+88,mode_w,42)) { a->login_mode=LOGIN_M3U; a->input_focus=INPUT_SERVER; return; }
        if (point_in(x,y,form_x,py+142,form_w,44)) a->input_focus=INPUT_PROFILE_NAME;
        else if (point_in(x,y,form_x,py+196,form_w,44)) a->input_focus=INPUT_SERVER;
        else if (a->login_mode==LOGIN_XTREAM && point_in(x,y,form_x,py+250,form_w,44)) a->input_focus=INPUT_SERVER_ALT;
        else if (a->login_mode==LOGIN_XTREAM && point_in(x,y,form_x,py+304,form_w,44)) a->input_focus=INPUT_USERNAME;
        else if (a->login_mode==LOGIN_XTREAM && point_in(x,y,form_x,py+358,form_w,44)) a->input_focus=INPUT_PASSWORD;
        else if (point_in(x,y,form_x,py+430,form_w,50)) start_login(a);
        else {
            int row_y=py+132;
            for (int r=0;r<7;++r) {
                int idx=a->profile_scroll+r;
                if ((size_t)idx>=a->profiles.len) break;
                if (point_in(x,y,list_x,row_y,list_w,52)) { load_profile_into_form(a,(size_t)idx); return; }
                row_y+=60;
            }
            a->input_focus=0;
        }
    } else if (a->screen == SCREEN_BROWSE) handle_browse_click(a,x,y);
    else {
        show_player_hud(a);
        if (!a->fullscreen && point_in(x,y,12,10,132,44)) { leave_player(a); return; }
        int cy=a->height-PLAYER_CONTROLS_H;
        if (point_in(x,y,16,cy+18,52,46) && a->player) {
            vip_mpv_player_set_paused(a->player,!vip_mpv_player_is_paused(a->player)); save_current_progress(a,true); return;
        }
        if (point_in(x,y,76,cy+18,82,46)) { leave_player(a); return; }
        if (!a->player_item_live && a->player) {
            int tx,ty,tw,th; timeline_geometry(a,&tx,&ty,&tw,&th);
            if (point_in(x,y,tx,ty,tw,th+12)) {
                vip_mpv_player_snapshot_t snap={0}; vip_mpv_player_snapshot(a->player,&snap);
                if (snap.duration_seconds>0.0) {
                    double pos=((double)(x-tx)/(double)tw)*snap.duration_seconds; vip_error_t err={0};
                    (void)vip_mpv_player_seek(a->player,pos,&err); a->timeline_dragging=true;
                }
            }
        }
    }
}

static void handle_wheel(app_t *a, int x, int y, int direction) {
    if (a->screen == SCREEN_LOGIN) {
        int max=(int)a->profiles.len-7; if(max<0)max=0; a->profile_scroll+=direction; if(a->profile_scroll<0)a->profile_scroll=0; if(a->profile_scroll>max)a->profile_scroll=max; return;
    }
    if (a->screen != SCREEN_BROWSE) return;
    if (x < SIDEBAR_W) {
        int maxscroll=(int)ACTIVE_CATEGORIES(a).len-(category_visible_rows(a)-1); if(maxscroll<0)maxscroll=0;
        a->category_scroll+=direction*3; if(a->category_scroll<0)a->category_scroll=0; if(a->category_scroll>maxscroll)a->category_scroll=maxscroll;
    } else {
        card_layout_t layout=browse_layout(a); int rows=(int)((a->filtered_len+(size_t)layout.cols-1u)/(size_t)layout.cols);
        int content_h=rows*layout.row_step; int maxscroll=content_h-(a->height-TOPBAR_H-18); if(maxscroll<0)maxscroll=0;
        a->grid_scroll+=direction*(layout.mode==ART_PORTRAIT?220:180); if(a->grid_scroll<0)a->grid_scroll=0; if(a->grid_scroll>maxscroll)a->grid_scroll=maxscroll;
        if (a->thumbs) vip_thumbnail_scheduler_cancel_pending(a->thumbs);
    }
    (void)y;
}

static int browse_columns(app_t *a) {
    return browse_layout(a).cols;
}

static void ensure_grid_focus_visible(app_t *a) {
    if (a->filtered_len==0) { a->grid_scroll=0; return; }
    if (a->focused_filtered>=a->filtered_len) a->focused_filtered=a->filtered_len-1u;
    card_layout_t layout=browse_layout(a); int row=(int)(a->focused_filtered/(size_t)layout.cols); int row_top=row*layout.row_step;
    int content_y=TOPBAR_H+18; int viewport_h=a->height-content_y; if(viewport_h<layout.card_h)viewport_h=layout.card_h;
    if(row_top<a->grid_scroll)a->grid_scroll=row_top;
    else if(row_top+layout.card_h>a->grid_scroll+viewport_h)a->grid_scroll=row_top+layout.card_h-viewport_h;
    if(a->grid_scroll<0)a->grid_scroll=0;
}

static void move_grid_focus(app_t *a, int dx, int dy) {
    if (a->filtered_len == 0) return;
    int cols = browse_columns(a);
    long current = (long)a->focused_filtered;
    long next = current + (long)dx + (long)dy * (long)cols;
    if (next < 0) next = 0;
    if ((size_t)next >= a->filtered_len) next = (long)a->filtered_len - 1;
    a->focused_filtered = (size_t)next;
    ensure_grid_focus_visible(a);
}

static void switch_relative_channel(app_t *a, int delta) {
    if (a->filtered_len == 0) return;
    size_t pos = 0;
    bool found = false;
    for (size_t i = 0; i < a->filtered_len; ++i) {
        if (a->filtered[i] == a->current_channel) { pos = i; found = true; break; }
    }
    if (!found) return;
    long next = (long)pos + delta;
    if (next < 0) next = (long)a->filtered_len - 1;
    if ((size_t)next >= a->filtered_len) next = 0;
    enter_player(a, a->filtered[(size_t)next]);
}

static void handle_key(app_t *a, XKeyEvent *kev) {
    KeySym sym=NoSymbol; char buf[64]; int n=XLookupString(kev,buf,sizeof(buf),&sym,NULL);
    bool ctrl=(kev->state&ControlMask)!=0, shift=(kev->state&ShiftMask)!=0;
    if (sym==XK_F11) { set_fullscreen(a,!a->fullscreen); show_player_hud(a); return; }
    if (a->screen==SCREEN_PLAYER) {
        show_player_hud(a);
        if (sym==XK_Escape || sym==XK_BackSpace) { leave_player(a); return; }
        if (sym==XK_space && a->player) { vip_mpv_player_set_paused(a->player,!vip_mpv_player_is_paused(a->player)); save_current_progress(a,true); return; }
        if (sym==XK_Left) { if(a->player_item_live)switch_relative_channel(a,-1); else if(a->player){vip_error_t e={0};(void)vip_mpv_player_seek_relative(a->player,-10.0,&e);} return; }
        if (sym==XK_Right) { if(a->player_item_live)switch_relative_channel(a,1); else if(a->player){vip_error_t e={0};(void)vip_mpv_player_seek_relative(a->player,10.0,&e);} return; }
        if ((sym==XK_Up || sym==XK_Down) && a->player) { vip_mpv_player_snapshot_t sn={0}; vip_mpv_player_snapshot(a->player,&sn); vip_error_t e={0}; double v=sn.volume+(sym==XK_Up?5.0:-5.0); if(v<0)v=0;if(v>100)v=100;(void)vip_mpv_player_set_volume(a->player,v,&e); return; }
        return;
    }
    if (a->screen==SCREEN_BROWSE) {
        if(sym==XK_1){switch_content(a,CONTENT_LIVE);return;} if(sym==XK_2){switch_content(a,CONTENT_VOD);return;} if(sym==XK_3){switch_content(a,CONTENT_SERIES);return;}
        if(sym==XK_l||sym==XK_L){if(a->thumbs)vip_thumbnail_scheduler_cancel_pending(a->thumbs);refresh_profiles(a);a->screen=SCREEN_LOGIN;a->input_focus=INPUT_SERVER;return;}
        if(sym==XK_Left){move_grid_focus(a,-1,0);return;} if(sym==XK_Right){move_grid_focus(a,1,0);return;} if(sym==XK_Up){move_grid_focus(a,0,-1);return;} if(sym==XK_Down){move_grid_focus(a,0,1);return;}
        if((sym==XK_Return||sym==XK_KP_Enter)&&a->filtered_len>0){if(a->focused_filtered>=a->filtered_len)a->focused_filtered=a->filtered_len-1u;activate_item(a,a->filtered[a->focused_filtered]);return;}
        if((sym==XK_f||sym==XK_F)&&!ctrl&&a->filtered_len>0){if(a->focused_filtered>=a->filtered_len)a->focused_filtered=a->filtered_len-1u;toggle_favorite(a,a->filtered[a->focused_filtered]);return;}
    }
    if(sym==XK_Escape){if(a->screen==SCREEN_BROWSE&&a->series_episode_mode){return_from_episode_list(a);return;}if(a->screen==SCREEN_BROWSE&&a->search[0]){a->search[0]='\0';rebuild_filter(a);}return;}
    if(ctrl&&(sym==XK_v||sym==XK_V)){request_paste(a,a->clipboard);return;} if(shift&&sym==XK_Insert){request_paste(a,XA_PRIMARY);return;}
    if(sym==XK_Tab){
        if(a->screen==SCREEN_LOGIN){
            if(a->login_mode==LOGIN_M3U)a->input_focus=a->input_focus==INPUT_PROFILE_NAME?INPUT_SERVER:INPUT_PROFILE_NAME;
            else a->input_focus=a->input_focus==INPUT_PROFILE_NAME?INPUT_SERVER:a->input_focus==INPUT_SERVER?INPUT_SERVER_ALT:a->input_focus==INPUT_SERVER_ALT?INPUT_USERNAME:a->input_focus==INPUT_USERNAME?INPUT_PASSWORD:INPUT_PROFILE_NAME;
        } else a->input_focus=INPUT_SEARCH; return;
    }
    if(sym==XK_Return||sym==XK_KP_Enter){if(a->screen==SCREEN_LOGIN)start_login(a);return;}
    if(sym==XK_BackSpace){backspace_input(a);return;} if(n>0&&!ctrl&&(unsigned char)buf[0]>=0x20u)append_input(a,buf,(size_t)n);
}

static void handle_selection(app_t *a, XSelectionEvent *sel) {
    if (sel->property == None) return;
    Atom type; int format; unsigned long nitems, after; unsigned char *data = NULL;
    if (XGetWindowProperty(a->dpy, a->win, sel->property, 0, 8192, True, AnyPropertyType,
                           &type, &format, &nitems, &after, &data) == Success && data) {
        if (format == 8) append_input(a, (const char *)data, nitems);
        XFree(data);
    }
}

/* Single-threaded X11 event dispatch.  Background jobs communicate by
 * state/flags and are observed from the main loop rather than calling Xlib. */
static void process_event(app_t *a, XEvent *e) {
    switch(e->type){
        case Expose: break;
        case ConfigureNotify:
            /* Only the top-level application window owns the global layout size.
               video_win also selects StructureNotifyMask and emits ConfigureNotify
               events (including its initial 1px geometry). Treating those as main
               window resizes collapses the player container to 1px high. */
            if (e->xconfigure.window == a->win) {
                a->width = e->xconfigure.width;
                a->height = e->xconfigure.height;
                if (a->screen == SCREEN_PLAYER) layout_video_window(a);
            }
            break;
        case ClientMessage: if((Atom)e->xclient.data.l[0]==a->wm_delete)a->quit=true;break;
        case MotionNotify:
            if(a->screen==SCREEN_PLAYER){
                show_player_hud(a);
                if(getenv("VIPTV_MPV_DEBUG"))
                    fprintf(stderr,"[mpv-debug] input MotionNotify window=%lu\n",(unsigned long)e->xmotion.window);
                if(a->timeline_dragging&&!a->player_item_live&&a->player&&e->xmotion.window==a->win){int tx,ty,tw,th;timeline_geometry(a,&tx,&ty,&tw,&th);vip_mpv_player_snapshot_t sn={0};vip_mpv_player_snapshot(a->player,&sn);if(sn.duration_seconds>0&&e->xmotion.x>=tx&&e->xmotion.x<=tx+tw){double pos=((double)(e->xmotion.x-tx)/(double)tw)*sn.duration_seconds;vip_error_t er={0};(void)vip_mpv_player_seek(a->player,pos,&er);}}
            }break;
        case ButtonRelease: if(e->xbutton.button==Button1){a->mouse_down=false;if(a->screen==SCREEN_PLAYER){a->timeline_dragging=false;save_current_progress(a,true);show_player_hud(a);}}break;
        case ButtonPress:
            a->mouse_down=true;
            if(a->screen==SCREEN_PLAYER&&(e->xbutton.window==a->video_win||e->xbutton.window==a->player_input_win)){
                show_player_hud(a);
                focus_player_input(a);
                if(getenv("VIPTV_MPV_DEBUG"))
                    fprintf(stderr,"[mpv-debug] input ButtonPress window=%lu button=%u\n",(unsigned long)e->xbutton.window,e->xbutton.button);
                break;
            }
            if(e->xbutton.button==Button1)handle_click(a,e->xbutton.x,e->xbutton.y);
            else if(e->xbutton.button==Button4)handle_wheel(a,e->xbutton.x,e->xbutton.y,-1);
            else if(e->xbutton.button==Button5)handle_wheel(a,e->xbutton.x,e->xbutton.y,1);
            else if(e->xbutton.button==Button2)request_paste(a,XA_PRIMARY);
            break;
        case KeyPress:
            if(a->screen==SCREEN_PLAYER&&getenv("VIPTV_MPV_DEBUG"))
                fprintf(stderr,"[mpv-debug] input KeyPress window=%lu keycode=%u\n",(unsigned long)e->xkey.window,e->xkey.keycode);
            handle_key(a,&e->xkey);break;
        case FocusIn:
        case FocusOut:
            if(a->screen==SCREEN_PLAYER&&getenv("VIPTV_MPV_DEBUG")) {
                Window focus=None; int revert=0; XGetInputFocus(a->dpy,&focus,&revert);
                fprintf(stderr,"[mpv-debug] input %s event-window=%lu current-focus=%lu\n",
                        e->type==FocusIn?"FocusIn":"FocusOut",
                        (unsigned long)e->xfocus.window,(unsigned long)focus);
            }
            if(e->type==FocusOut&&a->screen==SCREEN_PLAYER) recover_player_focus_if_needed(a,&e->xfocus);
            break;
        case SelectionNotify: handle_selection(a,&e->xselection);break;
        default:break;
    }
}

static bool init_x11(app_t *a, vip_error_t *error) {
    XInitThreads();
    a->dpy = XOpenDisplay(NULL);
    if (!a->dpy) { vip_error_set(error, VIP_ERR_IO, "não foi possível abrir DISPLAY X11"); return false; }
    a->screen_num = DefaultScreen(a->dpy);
    a->visual = DefaultVisual(a->dpy, a->screen_num);
    a->depth = DefaultDepth(a->dpy, a->screen_num);
    a->cmap = DefaultColormap(a->dpy, a->screen_num);
    a->width=DEFAULT_W; a->height=DEFAULT_H;
    a->win = XCreateSimpleWindow(a->dpy, RootWindow(a->dpy,a->screen_num), 20,20,
                                 (unsigned)a->width,(unsigned)a->height,0,
                                 BlackPixel(a->dpy,a->screen_num),BlackPixel(a->dpy,a->screen_num));
    XStoreName(a->dpy,a->win,APP_TITLE);
    XSelectInput(a->dpy,a->win,ExposureMask|KeyPressMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|StructureNotifyMask|FocusChangeMask);
    a->wm_delete=XInternAtom(a->dpy,"WM_DELETE_WINDOW",False);
    XSetWMProtocols(a->dpy,a->win,&a->wm_delete,1);
    a->clipboard=XInternAtom(a->dpy,"CLIPBOARD",False);
    a->utf8=XInternAtom(a->dpy,"UTF8_STRING",False);
    a->paste_property=XInternAtom(a->dpy,"VISUAL_IPTV_PASTE",False);
    a->gc=XCreateGC(a->dpy,a->win,0,NULL);
    XSetGraphicsExposures(a->dpy, a->gc, False);
    a->draw=a->win;
    a->font=XLoadQueryFont(a->dpy,"-misc-fixed-medium-r-normal--15-*-*-*-*-*-iso8859-1");
    if (!a->font) a->font=XLoadQueryFont(a->dpy,"9x15");
    if (!a->font) a->font=XLoadQueryFont(a->dpy,"fixed");
    if (a->font) XSetFont(a->dpy,a->gc,a->font->fid);
    a->video_win = XCreateSimpleWindow(a->dpy, a->win, 0, 0, 1, 1, 0,
                                       BlackPixel(a->dpy,a->screen_num),
                                       BlackPixel(a->dpy,a->screen_num));
    XSelectInput(a->dpy, a->video_win, KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
    XSetWindowAttributes input_attrs; memset(&input_attrs,0,sizeof(input_attrs));
    input_attrs.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    a->player_input_win = XCreateWindow(a->dpy, a->win, 0, 0, 1, 1, 0, 0, InputOnly,
                                        CopyFromParent, CWEventMask, &input_attrs);
    if (!a->player_input_win) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível criar overlay de entrada do player");
        return false;
    }
    init_palette(a);
    XMapWindow(a->dpy,a->win);
    XFlush(a->dpy);
    return true;
}

static bool pulse_runtime_available(void) {
    const char *server = getenv("PULSE_SERVER");
    if (server && server[0]) return true;
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !runtime[0]) return false;
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/pulse/native", runtime);
    return n > 0 && (size_t)n < sizeof(path) && access(path, F_OK) == 0;
}

static void init_runtime(app_t *a) {
    init_paths(a);
    vip_error_t error={0};
    if (vip_database_open(&a->db,a->db_path,&error)!=VIP_OK) {
        fprintf(stderr,"[db] %s\n",error.message); a->db=NULL;
    } else refresh_profiles(a);
    vip_ffmpeg_decoder_config_t dc={.ffmpeg_path="ffmpeg",.timeout_ms=10000,.candidate_frames=3,.output_width=320,.output_height=180};
    if (vip_ffmpeg_decoder_create(&a->decoder,&dc,&error)==VIP_OK &&
        vip_thumbnail_capture_context_init(&a->capture_context,a->decoder,a->cache_dir,82,&error)==VIP_OK) {
        size_t thumb_workers = thumbnail_worker_count();
        fprintf(stderr, "[thumbs] %zu workers; prefetch contínuo habilitado\n", thumb_workers);
        if (vip_thumbnail_scheduler_create(&a->thumbs,thumb_workers,vip_thumbnail_capture_with_decoder,&a->capture_context,
                                           thumbnail_ready,a,&error)!=VIP_OK)
            fprintf(stderr,"[thumbs] %s\n",error.message);
    } else fprintf(stderr,"[decoder] %s\n",error.message);
    bool audio_requested = getenv("VIPTV_NO_AUDIO") == NULL && getenv("VIPTV_TEST_NO_AUDIO") == NULL;
    bool audio = audio_requested && pulse_runtime_available();
    if (audio_requested && !audio)
        fprintf(stderr, "[audio] Pulse/PipeWire-Pulse não detectado; reprodução continuará sem áudio\n");
    vip_mpv_player_config_t pc={.mpv_path="mpv",.window_id=(unsigned long)a->video_win,
                                .audio=audio,.startup_grace_ms=300};
    if (vip_mpv_player_create(&a->player,&pc,&error)!=VIP_OK)
        fprintf(stderr,"[player] %s\n",error.message);
}

static void destroy_app(app_t *a) {
    if (a->login_thread_started) pthread_join(a->login_thread,NULL);
    if (a->series_thread_started) pthread_join(a->series_thread,NULL);
    if (a->details_thread_started) pthread_join(a->details_thread,NULL);
    if (a->player) vip_mpv_player_destroy(a->player);
    if (a->thumbs) vip_thumbnail_scheduler_destroy(a->thumbs);
    vip_thumbnail_capture_context_clear(&a->capture_context);
    if (a->decoder) vip_thumbnail_decoder_destroy(a->decoder);
    if (a->db) vip_database_close(a->db);
    for (size_t i=0;i<CACHE_SLOTS;++i) image_slot_clear(&a->image_cache[i]);
    free(a->filtered); free(a->category_counts); free(a->favorite_flags); free(a->progress_flags);
    free(a->series_watched); free(a->series_total);
    vip_media_metadata_clear(&a->details_metadata);
    vip_profile_list_clear(&a->profiles);
    for (int i = 0; i < 3; ++i) {
        vip_category_list_clear(&a->catalogs[i].categories);
        vip_channel_list_clear(&a->catalogs[i].channels);
    }
    vip_category_list_clear(&a->episode_categories);
    vip_channel_list_clear(&a->episode_channels);
    pthread_mutex_destroy(&a->data_mutex);
    if (a->dpy) {
        if (a->font) XFreeFont(a->dpy,a->font);
        if (a->backbuffer) XFreePixmap(a->dpy,a->backbuffer);
        if (a->gc) XFreeGC(a->dpy,a->gc);
        if (a->player_input_win) XDestroyWindow(a->dpy,a->player_input_win);
        if (a->video_win) XDestroyWindow(a->dpy,a->video_win);
        if (a->win) XDestroyWindow(a->dpy,a->win);
        XCloseDisplay(a->dpy);
    }
    curl_global_cleanup();
}

static void handle_async(app_t *a) {
    if(atomic_exchange(&a->login_done,false)){
        if(a->login_thread_started){pthread_join(a->login_thread,NULL);a->login_thread_started=false;}
        if(atomic_load(&a->login_success)){
            save_active_profile(a); recalc_category_counts(a); load_media_state(a); a->favorites_only=false;a->selected_category=-1;a->category_scroll=0;a->search[0]='\0';
            rebuild_filter(a);a->screen=SCREEN_BROWSE;a->input_focus=INPUT_SEARCH;
            fprintf(stderr,"[catalog] %zu canais, %zu categorias\n",ACTIVE_CHANNELS(a).len,ACTIVE_CATEGORIES(a).len);
            if(a->test_series){switch_content(a,CONTENT_SERIES);if(ACTIVE_CHANNELS(a).len>0)start_series_load(a,0);}
            else if(a->test_autoplay&&ACTIVE_CHANNELS(a).len>0)enter_player(a,0);
        }
    }
    if(atomic_exchange(&a->series_done,false)){
        if(a->series_thread_started){pthread_join(a->series_thread,NULL);a->series_thread_started=false;}
        if(atomic_load(&a->series_success)&&a->content_kind==CONTENT_SERIES){
            clear_details_view(a);
            a->series_episode_mode=true;a->favorites_only=false;a->selected_category=-1;a->category_scroll=0;a->grid_scroll=0;a->focused_filtered=0;a->search[0]='\0';
            recalc_category_counts(a);load_media_state(a);rebuild_filter(a);
            fprintf(stderr,"[series] %zu episódios carregados\n",ACTIVE_CHANNELS(a).len);
        }
    }
    if(atomic_exchange(&a->details_done,false)){
        if(a->details_thread_started){pthread_join(a->details_thread,NULL);a->details_thread_started=false;}
    }
    (void)atomic_exchange(&a->thumbs_dirty,false);
}

static void init_test_env(app_t *a) {
    const char *server=getenv("VIPTV_TEST_SERVER"),*alt=getenv("VIPTV_TEST_ALT_SERVER"),
               *user=getenv("VIPTV_TEST_USERNAME"),*pass=getenv("VIPTV_TEST_PASSWORD");
    if (server&&user&&pass) {
        fprintf(stderr, "[test] autoconnect habilitado\n");
        snprintf(a->server,sizeof(a->server),"%s",server);
        if (alt) snprintf(a->server_alt,sizeof(a->server_alt),"%s",alt);
        snprintf(a->username,sizeof(a->username),"%s",user);
        snprintf(a->password,sizeof(a->password),"%s",pass);
        a->test_autoplay=getenv("VIPTV_TEST_AUTOPLAY")!=NULL;
        a->test_series=getenv("VIPTV_TEST_SERIES")!=NULL;
        const char *back_ms=getenv("VIPTV_TEST_AUTOBACK_MS");
        if (back_ms) a->test_autoback_delay_ms=(int)strtol(back_ms,NULL,10);
        const char *exit_ms=getenv("VIPTV_TEST_EXIT_MS");
        if (exit_ms) a->test_exit_at_ms=monotonic_ms()+strtoll(exit_ms,NULL,10);
        start_login(a);
    }
}

int vip_x11_app_run(void) {
    setlocale(LC_ALL, "");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    app_t a; memset(&a,0,sizeof(a));
    pthread_mutex_init(&a.data_mutex,NULL);
    for (int i = 0; i < 3; ++i) {
        vip_category_list_init(&a.catalogs[i].categories);
        vip_channel_list_init(&a.catalogs[i].channels);
    }
    vip_category_list_init(&a.episode_categories);
    vip_channel_list_init(&a.episode_channels);
    vip_profile_list_init(&a.profiles);
    atomic_init(&a.login_running,false); atomic_init(&a.login_done,false); atomic_init(&a.login_success,false);
    atomic_init(&a.series_running,false); atomic_init(&a.series_done,false); atomic_init(&a.series_success,false);
    atomic_init(&a.details_running,false); atomic_init(&a.details_done,false); atomic_init(&a.details_success,false);
    atomic_init(&a.thumbs_dirty,false);
    vip_media_metadata_init(&a.details_metadata);
    a.content_kind=CONTENT_LIVE; a.login_mode=LOGIN_XTREAM;
    a.screen=SCREEN_LOGIN; a.input_focus=INPUT_SERVER; a.selected_category=-1;
    snprintf(a.status,sizeof(a.status),"Cole as credenciais Xtream e conecte");
    vip_error_t error={0};
    if (!init_x11(&a,&error)) { fprintf(stderr,"ERRO: %s\n",error.message); destroy_app(&a); return 1; }
    init_runtime(&a);
    init_test_env(&a);

    int xfd=ConnectionNumber(a.dpy);
    int64_t next_draw=0;
    while (!a.quit) {
        while (XPending(a.dpy)) { XEvent e; XNextEvent(a.dpy,&e); process_event(&a,&e); }
        handle_async(&a);
        maybe_start_details_load(&a);
        prefetch_thumbnail_batch(&a);
        maybe_failover_player(&a);
        sync_video_window(&a);
        int64_t now=monotonic_ms();
        if (a.screen==SCREEN_PLAYER) save_current_progress(&a,false);
        if (a.screen==SCREEN_PLAYER && a.test_autoback_delay_ms>0 && !a.test_autoback_done &&
            now-a.player_open_ms >= a.test_autoback_delay_ms) {
            leave_player(&a);
            a.test_autoback_done=true;
            fprintf(stderr,"[test] voltar do player OK\n");
        }
        if (a.test_exit_at_ms>0 && now>=a.test_exit_at_ms) a.quit=true;
        if (now>=next_draw) { redraw(&a); next_draw=now+(a.screen==SCREEN_PLAYER?33:100); }
        fd_set rfds; FD_ZERO(&rfds); FD_SET(xfd,&rfds);
        struct timeval tv={.tv_sec=0,.tv_usec=16000};
        (void)select(xfd+1,&rfds,NULL,NULL,&tv);
    }
    destroy_app(&a);
    return 0;
}
