/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/hub.h"
#include "visual_iptv/core.h"
#include "visual_iptv/decoder.h"
#include "visual_iptv/player_mpv.h"
#include "visual_iptv/provider_pluto.h"
#include "visual_iptv/thumbnails.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <curl/curl.h>
#include <jpeglib.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PLUTO_DEFAULT_W 1500
#define PLUTO_DEFAULT_H 880
#define PLUTO_HEADER_H 68
#define PLUTO_PLAYER_FOOTER_H 72
#define PLUTO_CARD_W 280
#define PLUTO_ART_H 158
#define PLUTO_CARD_H 198
#define PLUTO_GAP 18
#define PLUTO_IMAGE_CACHE 72

typedef struct {
    char *path;
    time_t mtime;
    XImage *image;
    XImage *scaled;
    int box_w;
    int box_h;
    int scaled_w;
    int scaled_h;
    uint64_t age;
} pluto_image_slot_t;

typedef struct {
    Display *dpy;
    int screen;
    Window win;
    Window video_win;
    GC gc;
    XFontStruct *font;
    Atom wm_delete;
    Visual *visual;
    int depth;
    Colormap cmap;
    int width;
    int height;
    bool quit;
    bool playing;
    size_t selected;
    int scroll;
    char status[320];

    unsigned long bg;
    unsigned long panel;
    unsigned long panel2;
    unsigned long border;
    unsigned long text;
    unsigned long muted;
    unsigned long accent;
    unsigned long accent2;
    unsigned long black;
    unsigned long danger;

    vip_category_list_t categories;
    vip_channel_list_t channels;
    vip_mpv_player_t *player;
    vip_thumbnail_decoder_t *decoder;
    vip_thumbnail_capture_context_t capture_context;
    vip_thumbnail_scheduler_t *thumbs;
    atomic_bool thumbs_dirty;
    char cache_dir[1024];
    pluto_image_slot_t image_cache[PLUTO_IMAGE_CACHE];
    uint64_t image_age;
} pluto_app_t;

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf env;
} pluto_jpeg_error_t;

static void jpeg_fail(j_common_ptr cinfo) {
    pluto_jpeg_error_t *error = (pluto_jpeg_error_t *)cinfo->err;
    longjmp(error->env, 1);
}

static unsigned long alloc_color(pluto_app_t *a, const char *name) {
    XColor exact = {0};
    XColor screen = {0};
    if (XAllocNamedColor(a->dpy, a->cmap, name, &screen, &exact)) return screen.pixel;
    return BlackPixel(a->dpy, a->screen);
}

static void init_colors(pluto_app_t *a) {
    a->bg = alloc_color(a, "#111416");
    a->panel = alloc_color(a, "#202428");
    a->panel2 = alloc_color(a, "#171a1d");
    a->border = alloc_color(a, "#394047");
    a->text = alloc_color(a, "#f1f3f5");
    a->muted = alloc_color(a, "#9aa0a6");
    a->accent = alloc_color(a, "#4ba3ff");
    a->accent2 = alloc_color(a, "#275d84");
    a->black = BlackPixel(a->dpy, a->screen);
    a->danger = alloc_color(a, "#ff6b6b");
}

static void fill_rect(pluto_app_t *a, int x, int y, int w, int h, unsigned long color) {
    if (w <= 0 || h <= 0) return;
    XSetForeground(a->dpy, a->gc, color);
    XFillRectangle(a->dpy, a->win, a->gc, x, y, (unsigned)w, (unsigned)h);
}

static void stroke_rect(pluto_app_t *a, int x, int y, int w, int h, unsigned long color) {
    if (w <= 0 || h <= 0) return;
    XSetForeground(a->dpy, a->gc, color);
    XDrawRectangle(a->dpy, a->win, a->gc, x, y, (unsigned)w, (unsigned)h);
}

static int text_width(pluto_app_t *a, const char *text) {
    if (!text) return 0;
    if (a->font) return XTextWidth(a->font, text, (int)strlen(text));
    return (int)strlen(text) * 8;
}

static void draw_text(pluto_app_t *a, int x, int y, const char *text, unsigned long color) {
    if (!text) return;
    XSetForeground(a->dpy, a->gc, color);
    XDrawString(a->dpy, a->win, a->gc, x, y, text, (int)strlen(text));
}

static void draw_center(pluto_app_t *a, int x, int y, int w, const char *text, unsigned long color) {
    int tw = text_width(a, text);
    draw_text(a, x + (w - tw) / 2, y, text, color);
}

static void bounded_text(char *dst, size_t cap, const char *src, size_t max_bytes) {
    if (!dst || cap == 0u) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n > max_bytes) n = max_bytes;
    if (n >= cap) n = cap - 1u;
    memcpy(dst, src, n);
    dst[n] = '\0';
    if (strlen(src) > n && cap >= 4u) {
        if (n > cap - 4u) n = cap - 4u;
        memcpy(dst + n, "...", 4u);
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

static void init_cache_path(pluto_app_t *a) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(a->cache_dir, sizeof(a->cache_dir), "%s/.cache/visual-iptv-x11/thumbnails", home);
    (void)mkdir_parents(a->cache_dir);
}

static unsigned long pixel_from_rgb(pluto_app_t *a, uint8_t r, uint8_t g, uint8_t b) {
    unsigned long rm = a->visual->red_mask;
    unsigned long gm = a->visual->green_mask;
    unsigned long bm = a->visual->blue_mask;
    unsigned rs = 0u, gs = 0u, bs = 0u;
    while (rs < 32u && ((rm >> rs) & 1u) == 0u) ++rs;
    while (gs < 32u && ((gm >> gs) & 1u) == 0u) ++gs;
    while (bs < 32u && ((bm >> bs) & 1u) == 0u) ++bs;
    unsigned long rmax = rm >> rs;
    unsigned long gmax = gm >> gs;
    unsigned long bmax = bm >> bs;
    unsigned long rv = ((unsigned long)r * rmax + 127u) / 255u;
    unsigned long gv = ((unsigned long)g * gmax + 127u) / 255u;
    unsigned long bv = ((unsigned long)b * bmax + 127u) / 255u;
    return ((rv << rs) & rm) | ((gv << gs) & gm) | ((bv << bs) & bm);
}

static XImage *load_jpeg(pluto_app_t *a, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    struct jpeg_decompress_struct cinfo;
    pluto_jpeg_error_t jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_fail;
    if (setjmp(jerr.env)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    (void)jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    (void)jpeg_start_decompress(&cinfo);
    unsigned width = cinfo.output_width;
    unsigned height = cinfo.output_height;
    if (width == 0u || height == 0u || width > 2048u || height > 2048u) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    XImage *image = XCreateImage(a->dpy, a->visual, (unsigned)a->depth, ZPixmap, 0,
                                 NULL, width, height, 32, 0);
    if (!image) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    size_t image_bytes = (size_t)image->bytes_per_line * (size_t)height;
    image->data = calloc(1u, image_bytes);
    if (!image->data) {
        XDestroyImage(image);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    uint8_t *row = malloc((size_t)width * 3u);
    if (!row) {
        XDestroyImage(image);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    while (cinfo.output_scanline < height) {
        JSAMPROW row_ptr = row;
        JDIMENSION y = cinfo.output_scanline;
        (void)jpeg_read_scanlines(&cinfo, &row_ptr, 1u);
        for (unsigned x = 0u; x < width; ++x) {
            unsigned long pixel = pixel_from_rgb(a, row[x * 3u], row[x * 3u + 1u], row[x * 3u + 2u]);
            XPutPixel(image, (int)x, (int)y, pixel);
        }
    }
    free(row);
    (void)jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return image;
}

static XImage *scale_image(pluto_app_t *a, const XImage *src, int width, int height) {
    if (!src || width <= 0 || height <= 0) return NULL;
    XImage *dst = XCreateImage(a->dpy, a->visual, (unsigned)a->depth, ZPixmap, 0,
                               NULL, (unsigned)width, (unsigned)height, 32, 0);
    if (!dst) return NULL;
    size_t bytes = (size_t)dst->bytes_per_line * (size_t)height;
    dst->data = calloc(1u, bytes);
    if (!dst->data) {
        XDestroyImage(dst);
        return NULL;
    }
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

static void clear_image_slot(pluto_image_slot_t *slot) {
    if (!slot) return;
    free(slot->path);
    if (slot->image) XDestroyImage(slot->image);
    if (slot->scaled) XDestroyImage(slot->scaled);
    memset(slot, 0, sizeof(*slot));
}

static pluto_image_slot_t *image_slot_get(pluto_app_t *a, const char *path) {
    struct stat st;
    if (!path || stat(path, &st) != 0 || st.st_size <= 0) return NULL;
    ++a->image_age;
    pluto_image_slot_t *victim = &a->image_cache[0];
    for (size_t i = 0u; i < PLUTO_IMAGE_CACHE; ++i) {
        pluto_image_slot_t *slot = &a->image_cache[i];
        if (slot->path && strcmp(slot->path, path) == 0 && slot->mtime == st.st_mtime) {
            slot->age = a->image_age;
            return slot;
        }
        if (!slot->path || slot->age < victim->age) victim = slot;
    }
    XImage *image = load_jpeg(a, path);
    if (!image) {
        (void)remove(path);
        return NULL;
    }
    clear_image_slot(victim);
    victim->path = vip_strdup(path);
    victim->mtime = st.st_mtime;
    victim->image = image;
    victim->age = a->image_age;
    return victim;
}

static bool draw_cached_image(pluto_app_t *a, const char *path, int x, int y, int box_w, int box_h) {
    pluto_image_slot_t *slot = image_slot_get(a, path);
    if (!slot || !slot->image || box_w <= 0 || box_h <= 0) return false;
    double sx = (double)box_w / (double)slot->image->width;
    double sy = (double)box_h / (double)slot->image->height;
    double scale = sx < sy ? sx : sy;
    int dw = (int)((double)slot->image->width * scale + 0.5);
    int dh = (int)((double)slot->image->height * scale + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (!slot->scaled || slot->box_w != box_w || slot->box_h != box_h ||
        slot->scaled_w != dw || slot->scaled_h != dh) {
        if (slot->scaled) XDestroyImage(slot->scaled);
        slot->scaled = scale_image(a, slot->image, dw, dh);
        slot->box_w = box_w;
        slot->box_h = box_h;
        slot->scaled_w = dw;
        slot->scaled_h = dh;
    }
    if (!slot->scaled) return false;
    int dx = x + (box_w - dw) / 2;
    int dy = y + (box_h - dh) / 2;
    XPutImage(a->dpy, a->win, a->gc, slot->scaled, 0, 0, dx, dy, (unsigned)dw, (unsigned)dh);
    return true;
}

static void thumbnail_ready(const vip_thumbnail_request_t *request,
                            vip_status_t status,
                            const char *path,
                            const vip_error_t *error,
                            void *userdata) {
    (void)request;
    (void)path;
    pluto_app_t *a = userdata;
    if (status != VIP_OK && error && error->message[0])
        fprintf(stderr, "[pluto/thumb] %s\n", error->message);
    atomic_store(&a->thumbs_dirty, true);
}

static void enqueue_thumbnail(pluto_app_t *a, const vip_channel_t *channel, int64_t priority) {
    if (!a->thumbs || !channel || !channel->provider_id || !channel->id || !channel->stream_url) return;
    vip_error_t error = {0};
    char *path = vip_thumbnail_cache_path(a->cache_dir, channel->provider_id, channel->id, &error);
    if (path) {
        struct stat st;
        bool exists = stat(path, &st) == 0 && st.st_size > 0;
        free(path);
        if (exists) return;
    }
    vip_thumbnail_request_t request = {
        .provider_id = channel->provider_id,
        .channel_id = channel->id,
        .logo_url = channel->logo_url,
        .stream_url = channel->stream_url,
        .priority = priority,
    };
    (void)vip_thumbnail_scheduler_enqueue(a->thumbs, &request, &error);
}

static int grid_columns(const pluto_app_t *a) {
    int available = a->width - 60;
    int cols = (available + PLUTO_GAP) / (PLUTO_CARD_W + PLUTO_GAP);
    return cols > 0 ? cols : 1;
}

static int grid_rows(const pluto_app_t *a) {
    int cols = grid_columns(a);
    return (int)((a->channels.len + (size_t)cols - 1u) / (size_t)cols);
}

static int grid_max_scroll(const pluto_app_t *a) {
    int content_height = grid_rows(a) * (PLUTO_CARD_H + PLUTO_GAP);
    int viewport = a->height - PLUTO_HEADER_H - 24;
    int max_scroll = content_height - viewport;
    return max_scroll > 0 ? max_scroll : 0;
}

static void ensure_selected_visible(pluto_app_t *a) {
    if (a->channels.len == 0u) {
        a->scroll = 0;
        return;
    }
    if (a->selected >= a->channels.len) a->selected = a->channels.len - 1u;
    int cols = grid_columns(a);
    int row = (int)(a->selected / (size_t)cols);
    int row_top = row * (PLUTO_CARD_H + PLUTO_GAP);
    int viewport = a->height - PLUTO_HEADER_H - 24;
    if (row_top < a->scroll) a->scroll = row_top;
    if (row_top + PLUTO_CARD_H > a->scroll + viewport)
        a->scroll = row_top + PLUTO_CARD_H - viewport;
    int max_scroll = grid_max_scroll(a);
    if (a->scroll < 0) a->scroll = 0;
    if (a->scroll > max_scroll) a->scroll = max_scroll;
}

static void layout_video(pluto_app_t *a) {
    if (!a->video_win) return;
    int video_h = a->height - PLUTO_HEADER_H - PLUTO_PLAYER_FOOTER_H;
    if (video_h < 1) video_h = 1;
    XMoveResizeWindow(a->dpy, a->video_win, 0, PLUTO_HEADER_H,
                      (unsigned)a->width, (unsigned)video_h);
}

static void set_video_visible(pluto_app_t *a, bool visible) {
    if (!a->video_win) return;
    if (visible) {
        layout_video(a);
        XMapRaised(a->dpy, a->video_win);
    } else {
        XUnmapWindow(a->dpy, a->video_win);
    }
    XFlush(a->dpy);
}

static void play_selected(pluto_app_t *a) {
    if (!a->player || a->selected >= a->channels.len) return;
    vip_channel_t *channel = &a->channels.items[a->selected];
    vip_error_t error = {0};
    snprintf(a->status, sizeof(a->status), "Abrindo %.220s...", channel->name ? channel->name : "canal");
    a->playing = true;
    set_video_visible(a, true);
    vip_status_t st = vip_mpv_player_load(a->player, channel->stream_url, &error);
    if (st != VIP_OK) {
        snprintf(a->status, sizeof(a->status), "Falha ao reproduzir: %.230s", error.message);
        a->playing = false;
        set_video_visible(a, false);
    }
}

static void stop_playback(pluto_app_t *a) {
    if (a->player) vip_mpv_player_stop(a->player);
    a->playing = false;
    set_video_visible(a, false);
    snprintf(a->status, sizeof(a->status), "%zu canais Pluto TV", a->channels.len);
}

static void switch_channel(pluto_app_t *a, int delta) {
    if (a->channels.len == 0u) return;
    long next = (long)a->selected + (long)delta;
    if (next < 0) next = (long)a->channels.len - 1L;
    if ((size_t)next >= a->channels.len) next = 0L;
    a->selected = (size_t)next;
    play_selected(a);
}

static void draw_header(pluto_app_t *a) {
    fill_rect(a, 0, 0, a->width, PLUTO_HEADER_H, a->panel);
    fill_rect(a, 12, 11, 112, 44, a->panel2);
    stroke_rect(a, 12, 11, 112, 44, a->border);
    draw_center(a, 12, 39, 112, a->playing ? "< Voltar" : "< Hub", a->text);
    draw_text(a, 146, 34, "Pluto TV", a->text);
    if (!a->playing) {
        char count[96];
        snprintf(count, sizeof(count), "%zu canais | sem login", a->channels.len);
        draw_text(a, 146, 54, count, a->muted);
    } else if (a->selected < a->channels.len) {
        char title[220];
        bounded_text(title, sizeof(title), a->channels.items[a->selected].name, 44u);
        draw_text(a, 250, 34, title, a->muted);
    }
}

static void draw_grid(pluto_app_t *a) {
    fill_rect(a, 0, 0, a->width, a->height, a->bg);
    draw_header(a);
    if (a->channels.len == 0u) {
        draw_center(a, 0, a->height / 2, a->width,
                    a->status[0] ? a->status : "Nenhum canal recebido do Pluto TV", a->muted);
        return;
    }

    int cols = grid_columns(a);
    int start_x = 30;
    int start_y = PLUTO_HEADER_H + 18;
    for (size_t i = 0u; i < a->channels.len; ++i) {
        int row = (int)(i / (size_t)cols);
        int col = (int)(i % (size_t)cols);
        int x = start_x + col * (PLUTO_CARD_W + PLUTO_GAP);
        int y = start_y + row * (PLUTO_CARD_H + PLUTO_GAP) - a->scroll;
        if (y > a->height || y + PLUTO_CARD_H < PLUTO_HEADER_H) continue;
        vip_channel_t *channel = &a->channels.items[i];
        bool selected = i == a->selected;
        if (selected) {
            stroke_rect(a, x - 3, y - 3, PLUTO_CARD_W + 6, PLUTO_CARD_H + 6, a->accent);
            stroke_rect(a, x - 2, y - 2, PLUTO_CARD_W + 4, PLUTO_CARD_H + 4, a->accent);
        }
        fill_rect(a, x, y, PLUTO_CARD_W, PLUTO_ART_H, a->black);
        vip_error_t error = {0};
        char *path = vip_thumbnail_cache_path(a->cache_dir, channel->provider_id, channel->id, &error);
        bool image_ok = path && draw_cached_image(a, path, x, y, PLUTO_CARD_W, PLUTO_ART_H);
        free(path);
        if (!image_ok) {
            draw_center(a, x, y + PLUTO_ART_H / 2 + 5, PLUTO_CARD_W, "carregando imagem...", a->muted);
            enqueue_thumbnail(a, channel, 1000000LL - (int64_t)i);
        }
        stroke_rect(a, x, y, PLUTO_CARD_W, PLUTO_ART_H, a->border);
        char title[160];
        bounded_text(title, sizeof(title), channel->name, 38u);
        draw_text(a, x, y + PLUTO_ART_H + 24, title, a->text);
    }

    if (a->status[0]) {
        fill_rect(a, 0, a->height - 32, a->width, 32, a->panel);
        draw_text(a, 30, a->height - 11, a->status, a->muted);
    }
}

static void draw_player(pluto_app_t *a) {
    fill_rect(a, 0, 0, a->width, a->height, a->black);
    draw_header(a);
    int footer_y = a->height - PLUTO_PLAYER_FOOTER_H;
    fill_rect(a, 0, footer_y, a->width, PLUTO_PLAYER_FOOTER_H, a->panel);

    vip_mpv_player_snapshot_t snapshot = {0};
    if (a->player) vip_mpv_player_snapshot(a->player, &snapshot);
    fill_rect(a, 16, footer_y + 14, 54, 44, a->panel2);
    stroke_rect(a, 16, footer_y + 14, 54, 44, a->border);
    draw_center(a, 16, footer_y + 42, 54, snapshot.paused ? ">" : "||", a->text);
    fill_rect(a, 80, footer_y + 14, 88, 44, a->panel2);
    stroke_rect(a, 80, footer_y + 14, 88, 44, a->border);
    draw_center(a, 80, footer_y + 42, 88, "< Voltar", a->text);
    draw_text(a, 190, footer_y + 40, "<- -> troca canal", a->muted);

    const char *state = a->player ? vip_mpv_player_state_name(snapshot.state) : "sem player";
    int tw = text_width(a, state);
    draw_text(a, a->width - tw - 20, footer_y + 42, state,
              snapshot.state == VIP_PLAYER_ERROR ? a->danger : a->muted);
    if (snapshot.state == VIP_PLAYER_ERROR) {
        const char *message = vip_mpv_player_last_error(a->player);
        char bounded[220];
        bounded_text(bounded, sizeof(bounded), message && message[0] ? message : "Falha no stream", 90u);
        draw_center(a, 0, a->height / 2, a->width, bounded, a->danger);
    }
}

static void redraw(pluto_app_t *a) {
    if (a->playing) draw_player(a);
    else draw_grid(a);
    XFlush(a->dpy);
}

static void move_selection(pluto_app_t *a, int dx, int dy) {
    if (a->channels.len == 0u) return;
    int cols = grid_columns(a);
    long next = (long)a->selected + (long)dx + (long)dy * (long)cols;
    if (next < 0) next = 0;
    if ((size_t)next >= a->channels.len) next = (long)a->channels.len - 1L;
    a->selected = (size_t)next;
    ensure_selected_visible(a);
}

static void handle_grid_click(pluto_app_t *a, int px, int py) {
    if (point_in(px, py, 12, 11, 112, 44)) {
        a->quit = true;
        return;
    }
    int cols = grid_columns(a);
    int start_x = 30;
    int start_y = PLUTO_HEADER_H + 18;
    for (size_t i = 0u; i < a->channels.len; ++i) {
        int row = (int)(i / (size_t)cols);
        int col = (int)(i % (size_t)cols);
        int x = start_x + col * (PLUTO_CARD_W + PLUTO_GAP);
        int y = start_y + row * (PLUTO_CARD_H + PLUTO_GAP) - a->scroll;
        if (point_in(px, py, x, y, PLUTO_CARD_W, PLUTO_CARD_H)) {
            a->selected = i;
            play_selected(a);
            return;
        }
    }
}

static void handle_player_click(pluto_app_t *a, int px, int py) {
    if (point_in(px, py, 12, 11, 112, 44)) {
        stop_playback(a);
        return;
    }
    int footer_y = a->height - PLUTO_PLAYER_FOOTER_H;
    if (point_in(px, py, 16, footer_y + 14, 54, 44) && a->player) {
        vip_mpv_player_set_paused(a->player, !vip_mpv_player_is_paused(a->player));
        return;
    }
    if (point_in(px, py, 80, footer_y + 14, 88, 44)) stop_playback(a);
}

static void handle_key(pluto_app_t *a, XKeyEvent *event) {
    KeySym sym = XLookupKeysym(event, 0);
    if (a->playing) {
        if (sym == XK_Escape || sym == XK_BackSpace) stop_playback(a);
        else if (sym == XK_space && a->player)
            vip_mpv_player_set_paused(a->player, !vip_mpv_player_is_paused(a->player));
        else if (sym == XK_Left) switch_channel(a, -1);
        else if (sym == XK_Right) switch_channel(a, 1);
        return;
    }
    if (sym == XK_Escape || sym == XK_BackSpace) a->quit = true;
    else if (sym == XK_Left) move_selection(a, -1, 0);
    else if (sym == XK_Right) move_selection(a, 1, 0);
    else if (sym == XK_Up) move_selection(a, 0, -1);
    else if (sym == XK_Down) move_selection(a, 0, 1);
    else if (sym == XK_Return || sym == XK_KP_Enter) play_selected(a);
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

static bool init_x11(pluto_app_t *a, vip_error_t *error) {
    XInitThreads();
    a->dpy = XOpenDisplay(NULL);
    if (!a->dpy) {
        vip_error_set(error, VIP_ERR_IO, "nao foi possivel abrir DISPLAY X11");
        return false;
    }
    a->screen = DefaultScreen(a->dpy);
    a->visual = DefaultVisual(a->dpy, a->screen);
    a->depth = DefaultDepth(a->dpy, a->screen);
    a->cmap = DefaultColormap(a->dpy, a->screen);
    a->width = PLUTO_DEFAULT_W;
    a->height = PLUTO_DEFAULT_H;
    a->win = XCreateSimpleWindow(a->dpy, RootWindow(a->dpy, a->screen), 30, 30,
                                 (unsigned)a->width, (unsigned)a->height, 0,
                                 BlackPixel(a->dpy, a->screen), BlackPixel(a->dpy, a->screen));
    XStoreName(a->dpy, a->win, "Blazzing - Pluto TV");
    XSelectInput(a->dpy, a->win, ExposureMask | KeyPressMask | ButtonPressMask |
                 StructureNotifyMask);
    a->wm_delete = XInternAtom(a->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(a->dpy, a->win, &a->wm_delete, 1);
    a->gc = XCreateGC(a->dpy, a->win, 0, NULL);
    a->font = XLoadQueryFont(a->dpy, "-misc-fixed-medium-r-normal--15-*-*-*-*-*-iso8859-1");
    if (!a->font) a->font = XLoadQueryFont(a->dpy, "9x15");
    if (!a->font) a->font = XLoadQueryFont(a->dpy, "fixed");
    if (a->font) XSetFont(a->dpy, a->gc, a->font->fid);
    a->video_win = XCreateSimpleWindow(a->dpy, a->win, 0, PLUTO_HEADER_H, 1u, 1u, 0,
                                       BlackPixel(a->dpy, a->screen), BlackPixel(a->dpy, a->screen));
    init_colors(a);
    XMapWindow(a->dpy, a->win);
    XFlush(a->dpy);
    return true;
}

static void draw_loading(pluto_app_t *a, const char *message) {
    fill_rect(a, 0, 0, a->width, a->height, a->bg);
    fill_rect(a, 0, 0, a->width, PLUTO_HEADER_H, a->panel);
    draw_text(a, 30, 40, "Pluto TV", a->text);
    draw_center(a, 0, a->height / 2, a->width, message, a->muted);
    XFlush(a->dpy);
}

static vip_status_t load_catalog(pluto_app_t *a, vip_error_t *error) {
    vip_pluto_client_t *client = NULL;
    vip_status_t st = vip_pluto_client_create(&client, error);
    if (st == VIP_OK) st = vip_pluto_boot(client, error);
    if (st == VIP_OK) st = vip_pluto_live_catalog(client, &a->categories, &a->channels, error);
    if (client) vip_pluto_client_destroy(client);
    return st;
}

static void init_media_runtime(pluto_app_t *a) {
    vip_error_t error = {0};
    vip_ffmpeg_decoder_config_t decoder_config = {
        .ffmpeg_path = "ffmpeg",
        .timeout_ms = 10000,
        .candidate_frames = 3,
        .output_width = 320,
        .output_height = 180,
    };
    if (vip_ffmpeg_decoder_create(&a->decoder, &decoder_config, &error) == VIP_OK &&
        vip_thumbnail_capture_context_init(&a->capture_context, a->decoder, a->cache_dir, 84, &error) == VIP_OK) {
        if (vip_thumbnail_scheduler_create(&a->thumbs, 12u, vip_thumbnail_capture_with_decoder,
                                           &a->capture_context, thumbnail_ready, a, &error) != VIP_OK) {
            fprintf(stderr, "[pluto/thumb] %s\n", error.message);
            a->thumbs = NULL;
        }
    } else {
        fprintf(stderr, "[pluto/decoder] %s\n", error.message);
    }

    vip_mpv_player_config_t player_config = {
        .mpv_path = "mpv",
        .window_id = (unsigned long)a->video_win,
        .audio = pulse_runtime_available(),
        .startup_grace_ms = 300u,
    };
    vip_error_clear(&error);
    if (vip_mpv_player_create(&a->player, &player_config, &error) != VIP_OK) {
        fprintf(stderr, "[pluto/player] %s\n", error.message);
        snprintf(a->status, sizeof(a->status), "Player indisponivel: %.230s", error.message);
        a->player = NULL;
    }
}

static void cleanup(pluto_app_t *a) {
    if (a->player) vip_mpv_player_destroy(a->player);
    if (a->thumbs) vip_thumbnail_scheduler_destroy(a->thumbs);
    vip_thumbnail_capture_context_clear(&a->capture_context);
    if (a->decoder) vip_thumbnail_decoder_destroy(a->decoder);
    for (size_t i = 0u; i < PLUTO_IMAGE_CACHE; ++i) clear_image_slot(&a->image_cache[i]);
    vip_category_list_clear(&a->categories);
    vip_channel_list_clear(&a->channels);
    if (a->dpy) {
        if (a->font) XFreeFont(a->dpy, a->font);
        if (a->gc) XFreeGC(a->dpy, a->gc);
        if (a->video_win) XDestroyWindow(a->dpy, a->video_win);
        if (a->win) XDestroyWindow(a->dpy, a->win);
        XCloseDisplay(a->dpy);
    }
}

int vip_pluto_app_run(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    pluto_app_t a;
    memset(&a, 0, sizeof(a));
    vip_category_list_init(&a.categories);
    vip_channel_list_init(&a.channels);
    atomic_init(&a.thumbs_dirty, false);
    init_cache_path(&a);

    vip_error_t error = {0};
    if (!init_x11(&a, &error)) {
        fprintf(stderr, "[pluto] %s\n", error.message);
        cleanup(&a);
        curl_global_cleanup();
        return 1;
    }
    draw_loading(&a, "Carregando catalogo do Pluto TV...");
    vip_status_t st = load_catalog(&a, &error);
    if (st != VIP_OK) {
        snprintf(a.status, sizeof(a.status), "Falha ao carregar Pluto TV: %.230s", error.message);
        fprintf(stderr, "[pluto] %s\n", a.status);
    } else {
        snprintf(a.status, sizeof(a.status), "%zu canais Pluto TV", a.channels.len);
        fprintf(stderr, "[pluto] %zu canais carregados\n", a.channels.len);
    }
    init_media_runtime(&a);
    redraw(&a);

    while (!a.quit) {
        while (XPending(a.dpy)) {
            XEvent event;
            XNextEvent(a.dpy, &event);
            switch (event.type) {
                case Expose:
                    redraw(&a);
                    break;
                case ConfigureNotify:
                    if (event.xconfigure.window == a.win) {
                        a.width = event.xconfigure.width;
                        a.height = event.xconfigure.height;
                        if (a.playing) layout_video(&a);
                        else ensure_selected_visible(&a);
                        redraw(&a);
                    }
                    break;
                case ClientMessage:
                    if ((Atom)event.xclient.data.l[0] == a.wm_delete) a.quit = true;
                    break;
                case KeyPress:
                    handle_key(&a, &event.xkey);
                    redraw(&a);
                    break;
                case ButtonPress:
                    if (event.xbutton.button == Button1) {
                        if (a.playing) handle_player_click(&a, event.xbutton.x, event.xbutton.y);
                        else handle_grid_click(&a, event.xbutton.x, event.xbutton.y);
                    } else if (!a.playing && (event.xbutton.button == Button4 || event.xbutton.button == Button5)) {
                        int direction = event.xbutton.button == Button4 ? -1 : 1;
                        a.scroll += direction * 180;
                        int max_scroll = grid_max_scroll(&a);
                        if (a.scroll < 0) a.scroll = 0;
                        if (a.scroll > max_scroll) a.scroll = max_scroll;
                    }
                    redraw(&a);
                    break;
                default:
                    break;
            }
        }

        if (atomic_exchange(&a.thumbs_dirty, false)) redraw(&a);
        if (a.playing && a.player) {
            vip_mpv_player_snapshot_t snapshot = {0};
            vip_mpv_player_snapshot(a.player, &snapshot);
            if (snapshot.state == VIP_PLAYER_ERROR) set_video_visible(&a, false);
            else set_video_visible(&a, true);
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 16000000L};
        (void)nanosleep(&pause, NULL);
    }

    cleanup(&a);
    curl_global_cleanup();
    return 0;
}
