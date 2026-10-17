/* SPDX-License-Identifier: MIT */
#include "x11_app_internal.h"

/* Handle the alloc color operation. */
static unsigned long alloc_color(app_t *a, const char *hex) {
    XColor exact = {0}, screen = {0};
    if (XAllocNamedColor(a->dpy, a->cmap, hex, &screen, &exact))
        return screen.pixel;
    return BlackPixel(a->dpy, a->screen_num);
}

/* Initialize palette. */
void init_palette(app_t *a) {
    a->colors.bg = alloc_color(a, "#090B11");
    a->colors.panel = alloc_color(a, "#111722");
    a->colors.panel2 = alloc_color(a, "#171C28");
    a->colors.hover = alloc_color(a, "#202839");
    a->colors.border = alloc_color(a, "#343A48");
    a->colors.text = alloc_color(a, "#F6F7FB");
    a->colors.muted = alloc_color(a, "#AAB2C4");
    a->colors.accent = alloc_color(a, "#FF5F2E");
    a->colors.accent2 = alloc_color(a, "#572517");
    a->colors.danger = alloc_color(a, "#FF7185");
    a->colors.black = alloc_color(a, "#04060A");
}

/* Draw target. */
static Drawable draw_target(app_t *a) {
    return a->draw ? a->draw : a->win;
}
/* Set fg. */
static void set_fg(app_t *a, unsigned long color) {
    XSetForeground(a->dpy, a->gc, color);
}
/* Fill rect. */
static void fill_rect(app_t *a, int x, int y, unsigned w, unsigned h, unsigned long color) {
    set_fg(a, color);
    XFillRectangle(a->dpy, draw_target(a), a->gc, x, y, w, h);
}
/* Stroke rect. */
static void stroke_rect(app_t *a, int x, int y, unsigned w, unsigned h, unsigned long color) {
    set_fg(a, color);
    XDrawRectangle(a->dpy, draw_target(a), a->gc, x, y, w, h);
}

/* Fill round rect. */
static void fill_round_rect(app_t *a, int x, int y, int w, int h, int r, unsigned long color) {
    if (w <= 0 || h <= 0)
        return;
    if (r < 0)
        r = 0;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    if (r == 0) {
        fill_rect(a, x, y, (unsigned)w, (unsigned)h, color);
        return;
    }
    set_fg(a, color);
    XFillRectangle(a->dpy, draw_target(a), a->gc, x + r, y, (unsigned)(w - 2 * r), (unsigned)h);
    XFillRectangle(a->dpy, draw_target(a), a->gc, x, y + r, (unsigned)w, (unsigned)(h - 2 * r));
    XFillArc(a->dpy, draw_target(a), a->gc, x, y, (unsigned)(2 * r), (unsigned)(2 * r), 90 * 64, 90 * 64);
    XFillArc(a->dpy, draw_target(a), a->gc, x + w - 2 * r, y, (unsigned)(2 * r), (unsigned)(2 * r), 0,
             90 * 64);
    XFillArc(a->dpy, draw_target(a), a->gc, x, y + h - 2 * r, (unsigned)(2 * r), (unsigned)(2 * r), 180 * 64,
             90 * 64);
    XFillArc(a->dpy, draw_target(a), a->gc, x + w - 2 * r, y + h - 2 * r, (unsigned)(2 * r),
             (unsigned)(2 * r), 270 * 64, 90 * 64);
}

/* Stroke round rect. */
static void stroke_round_rect(app_t *a, int x, int y, int w, int h, int r, unsigned long color) {
    if (w <= 0 || h <= 0)
        return;
    if (r < 0)
        r = 0;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    if (r == 0) {
        stroke_rect(a, x, y, (unsigned)w, (unsigned)h, color);
        return;
    }
    set_fg(a, color);
    XDrawLine(a->dpy, draw_target(a), a->gc, x + r, y, x + w - r, y);
    XDrawLine(a->dpy, draw_target(a), a->gc, x + r, y + h, x + w - r, y + h);
    XDrawLine(a->dpy, draw_target(a), a->gc, x, y + r, x, y + h - r);
    XDrawLine(a->dpy, draw_target(a), a->gc, x + w, y + r, x + w, y + h - r);
    XDrawArc(a->dpy, draw_target(a), a->gc, x, y, (unsigned)(2 * r), (unsigned)(2 * r), 90 * 64, 90 * 64);
    XDrawArc(a->dpy, draw_target(a), a->gc, x + w - 2 * r, y, (unsigned)(2 * r), (unsigned)(2 * r), 0,
             90 * 64);
    XDrawArc(a->dpy, draw_target(a), a->gc, x, y + h - 2 * r, (unsigned)(2 * r), (unsigned)(2 * r), 180 * 64,
             90 * 64);
    XDrawArc(a->dpy, draw_target(a), a->gc, x + w - 2 * r, y + h - 2 * r, (unsigned)(2 * r),
             (unsigned)(2 * r), 270 * 64, 90 * 64);
}

/* Draw surface. */
static void draw_surface(app_t *a, int x, int y, int w, int h, int r, bool focused) {
    fill_round_rect(a, x + 3, y + 5, w, h, r, a->colors.black);
    fill_round_rect(a, x, y, w, h, r, focused ? a->colors.accent2 : a->colors.panel2);
    stroke_round_rect(a, x, y, w, h, r, focused ? a->colors.accent : a->colors.border);
}

/* Handle the utf8 to latin1 operation. */
static uint32_t decode_utf8_codepoint(const unsigned char *p, size_t remaining, size_t *advance) {
    *advance = 1u;
    if (*p < 0x80u)
        return *p;
    if (remaining >= 2u && (*p & 0xe0u) == 0xc0u && (p[1] & 0xc0u) == 0x80u) {
        *advance = 2u;
        return ((uint32_t)(p[0] & 0x1fu) << 6) | (uint32_t)(p[1] & 0x3fu);
    }
    if (remaining >= 3u && (*p & 0xf0u) == 0xe0u && (p[1] & 0xc0u) == 0x80u &&
        (p[2] & 0xc0u) == 0x80u) {
        *advance = 3u;
        return ((uint32_t)(p[0] & 0x0fu) << 12) | ((uint32_t)(p[1] & 0x3fu) << 6) |
               (uint32_t)(p[2] & 0x3fu);
    }
    if (remaining >= 4u && (*p & 0xf8u) == 0xf0u && (p[1] & 0xc0u) == 0x80u &&
        (p[2] & 0xc0u) == 0x80u && (p[3] & 0xc0u) == 0x80u) {
        *advance = 4u;
        return ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3fu) << 12) |
               ((uint32_t)(p[2] & 0x3fu) << 6) | (uint32_t)(p[3] & 0x3fu);
    }
    return (uint32_t)'?';
}

static char latin1_char_for_codepoint(uint32_t cp) {
    if (cp <= 0xffu)
        return (char)cp;
    if (cp == 0x2018u || cp == 0x2019u)
        return '\'';
    if (cp == 0x201cu || cp == 0x201du)
        return '"';
    if (cp == 0x2013u || cp == 0x2014u)
        return '-';
    return '?';
}

static size_t utf8_to_latin1(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0u)
        return 0u;
    if (!src)
        src = "";

    size_t out = 0u;
    const unsigned char *p = (const unsigned char *)src;
    const unsigned char *end = p + strlen(src);
    while (p < end && out + 1u < cap) {
        size_t advance = 1u;
        uint32_t cp = decode_utf8_codepoint(p, (size_t)(end - p), &advance);
        dst[out++] = latin1_char_for_codepoint(cp);
        p += advance;
    }
    dst[out] = '\0';
    return out;
}

/* Handle the text width operation. */
static int text_width(app_t *a, const char *text) {
    if (!text)
        return 0;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    if (a->font)
        return XTextWidth(a->font, latin, (int)len);
    return (int)len * 8;
}

/* Draw text. */
static void draw_text(app_t *a, int x, int y, const char *text, unsigned long color) {
    if (!text)
        return;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    if (a->font)
        XSetFont(a->dpy, a->gc, a->font->fid);
    set_fg(a, color);
    XDrawString(a->dpy, draw_target(a), a->gc, x, y, latin, (int)len);
}

/* Draw centered. */
static void draw_centered(app_t *a, int x, int y, int w, const char *text, unsigned long color) {
    int tw = text_width(a, text);
    draw_text(a, x + (w - tw) / 2, y, text, color);
}

/* Handle the text width font operation. */
static int text_width_font(app_t *a, XFontStruct *font, const char *text) {
    if (!text)
        return 0;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    XFontStruct *f = font ? font : a->font;
    return f ? XTextWidth(f, latin, (int)len) : (int)len * 8;
}

/* Draw text font. */
static void draw_text_font(app_t *a, XFontStruct *font, int x, int y, const char *text, unsigned long color) {
    if (!text)
        return;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    XFontStruct *f = font ? font : a->font;
    if (f)
        XSetFont(a->dpy, a->gc, f->fid);
    set_fg(a, color);
    XDrawString(a->dpy, draw_target(a), a->gc, x, y, latin, (int)len);
}

/* Draw centered font. */
static void draw_centered_font(app_t *a, XFontStruct *font, int x, int y, int w, const char *text,
                               unsigned long color) {
    int tw = text_width_font(a, font, text);
    draw_text_font(a, font, x + (w - tw) / 2, y, text, color);
}



/* Handle the mkdir one operation. */

/* Handle the pixel from rgb operation. */
static unsigned long pixel_from_rgb(app_t *a, uint8_t r, uint8_t g, uint8_t b) {
    unsigned long rm = a->visual->red_mask, gm = a->visual->green_mask, bm = a->visual->blue_mask;
    unsigned rs = 0, gs = 0, bs = 0;
    while (rs < 32u && ((rm >> rs) & 1u) == 0u)
        ++rs;
    while (gs < 32u && ((gm >> gs) & 1u) == 0u)
        ++gs;
    while (bs < 32u && ((bm >> bs) & 1u) == 0u)
        ++bs;
    unsigned long rmax = rm >> rs, gmax = gm >> gs, bmax = bm >> bs;
    unsigned long rv = ((unsigned long)r * rmax + 127u) / 255u;
    unsigned long gv = ((unsigned long)g * gmax + 127u) / 255u;
    unsigned long bv = ((unsigned long)b * bmax + 127u) / 255u;
    return ((rv << rs) & rm) | ((gv << gs) & gm) | ((bv << bs) & bm);
}

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf env;
} jpeg_err_t;
/* Transfer control to the guarded JPEG error path after a libjpeg failure. */
static void jpeg_fail(j_common_ptr cinfo) {
    jpeg_err_t *e = (jpeg_err_t *)cinfo->err;
    longjmp(e->env, 1);
}

/* Load jpeg ximage. */
static XImage *load_jpeg_ximage(app_t *a, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    struct jpeg_decompress_struct cinfo;
    jpeg_err_t jerr;
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
    unsigned w = cinfo.output_width, h = cinfo.output_height;
    if (w == 0 || h == 0 || w > 2048u || h > 2048u) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    XImage *img = XCreateImage(a->dpy, a->visual, (unsigned)a->depth, ZPixmap, 0, NULL, w, h, 32, 0);
    if (!img) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    size_t bytes = (size_t)img->bytes_per_line * h;
    img->data = calloc(1, bytes);
    if (!img->data) {
        XDestroyImage(img);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    uint8_t *row = malloc((size_t)w * 3u);
    if (!row) {
        XDestroyImage(img);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return NULL;
    }
    while (cinfo.output_scanline < h) {
        JSAMPROW rp = row;
        JDIMENSION y = cinfo.output_scanline;
        (void)jpeg_read_scanlines(&cinfo, &rp, 1);
        for (unsigned x = 0; x < w; ++x) {
            XPutPixel(img, (int)x, (int)y,
                      pixel_from_rgb(a, row[x * 3u], row[x * 3u + 1u], row[x * 3u + 2u]));
        }
    }
    free(row);
    (void)jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return img;
}

/* Clear owned state from the requested state in the image slot. */
static void image_slot_clear(image_slot_t *slot) {
    if (!slot)
        return;
    free(slot->path);
    if (slot->image)
        XDestroyImage(slot->image);
    if (slot->scaled)
        XDestroyImage(slot->scaled);
    memset(slot, 0, sizeof(*slot));
}

/* Return the requested state from the image cache slot. */
static image_slot_t *image_cache_slot_get(app_t *a, const char *path) {
    struct stat st;
    if (!path || stat(path, &st) != 0 || st.st_size <= 0)
        return NULL;
    ++a->image_age;
    image_slot_t *victim = &a->image_cache[0];
    for (size_t i = 0; i < CACHE_SLOTS; ++i) {
        image_slot_t *slot = &a->image_cache[i];
        if (slot->path && strcmp(slot->path, path) == 0 && slot->mtime == st.st_mtime) {
            slot->age = a->image_age;
            return slot;
        }
        if (!slot->path || slot->age < victim->age)
            victim = slot;
    }
    XImage *img = load_jpeg_ximage(a, path);
    if (!img) {
        /* The file exists but is not a decodable JPEG. Remove it so the
           enqueue path below can fetch it again instead of displaying an
           eternal "carregando imagem...". */
        (void)remove(path);
        return NULL;
    }
    image_slot_clear(victim);
    victim->path = vip_strdup(path);
    victim->mtime = st.st_mtime;
    victim->image = img;
    victim->age = a->image_age;
    return victim;
}

/* Scale ximage. */
static XImage *scale_ximage(app_t *a, const XImage *src, int width, int height) {
    if (!src || width < 1 || height < 1)
        return NULL;
    XImage *dst = XCreateImage(a->dpy, a->visual, (unsigned)a->depth, ZPixmap, 0, NULL, (unsigned)width,
                               (unsigned)height, 32, 0);
    if (!dst)
        return NULL;
    size_t bytes = (size_t)dst->bytes_per_line * (size_t)height;
    dst->data = calloc(1, bytes);
    if (!dst->data) {
        XDestroyImage(dst);
        return NULL;
    }
    for (int y = 0; y < height; ++y) {
        int sy = (int)((int64_t)y * src->height / height);
        if (sy >= src->height)
            sy = src->height - 1;
        for (int x = 0; x < width; ++x) {
            int sx = (int)((int64_t)x * src->width / width);
            if (sx >= src->width)
                sx = src->width - 1;
            XPutPixel(dst, x, y, XGetPixel((XImage *)src, sx, sy));
        }
    }
    return dst;
}

/* Draw cached image contain. */
static bool contained_image_size(const XImage *image, int box_w, int box_h, int *dw, int *dh) {
    if (!image || box_w < 1 || box_h < 1 || image->width < 1 || image->height < 1)
        return false;
    double sx = (double)box_w / (double)image->width;
    double sy = (double)box_h / (double)image->height;
    double scale = sx < sy ? sx : sy;
    if (scale <= 0.0)
        return false;
    *dw = (int)((double)image->width * scale + 0.5);
    *dh = (int)((double)image->height * scale + 0.5);
    if (*dw < 1)
        *dw = 1;
    if (*dh < 1)
        *dh = 1;
    if (*dw > box_w)
        *dw = box_w;
    if (*dh > box_h)
        *dh = box_h;
    return true;
}

static bool ensure_contained_scaled_image(app_t *a, image_slot_t *slot, int box_w, int box_h, int dw, int dh) {
    bool unchanged = slot->scaled && slot->scaled_box_w == box_w && slot->scaled_box_h == box_h &&
                     slot->scaled_w == dw && slot->scaled_h == dh;
    if (unchanged)
        return true;
    if (slot->scaled) {
        XDestroyImage(slot->scaled);
        slot->scaled = NULL;
    }
    slot->scaled = scale_ximage(a, slot->image, dw, dh);
    slot->scaled_box_w = box_w;
    slot->scaled_box_h = box_h;
    slot->scaled_w = dw;
    slot->scaled_h = dh;
    return slot->scaled != NULL;
}

static bool draw_cached_image_contain(app_t *a, const char *path, int x, int y, int box_w, int box_h) {
    image_slot_t *slot = image_cache_slot_get(a, path);
    int dw = 0, dh = 0;
    if (!slot || !contained_image_size(slot->image, box_w, box_h, &dw, &dh))
        return false;
    if (!ensure_contained_scaled_image(a, slot, box_w, box_h, dw, dh))
        return false;
    int dx = x + (box_w - dw) / 2;
    int dy = y + (box_h - dh) / 2;
    XPutImage(a->dpy, draw_target(a), a->gc, slot->scaled, 0, 0, dx, dy, (unsigned)dw, (unsigned)dh);
    return true;
}

/* Handle the default artwork mode operation. */
static artwork_mode_t default_artwork_mode(const app_t *a) {
    if (a->series_season_select)
        return ART_PORTRAIT;
    if (a->series_episode_mode)
        return ART_LANDSCAPE;
    if (a->content_kind == CONTENT_VOD || a->content_kind == CONTENT_SERIES)
        return ART_PORTRAIT;
    return ART_LANDSCAPE;
}

/* Handle the detect artwork mode operation. */
static void count_artwork_ratio(double ratio, int *portrait, int *landscape, int *square) {
    if (ratio < 0.88)
        ++*portrait;
    else if (ratio > 1.18)
        ++*landscape;
    else
        ++*square;
}

static bool sample_artwork_ratio(app_t *a, size_t chidx, double *ratio) {
    if (chidx >= ACTIVE_CHANNELS(a).len)
        return false;
    vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[chidx];
    vip_error_t error = {0};
    char *path = ch->logo_url && ch->logo_url[0]
                     ? vip_thumbnail_artwork_cache_path(a->cache_dir, ch->provider_id, ch->id,
                                                        ch->logo_url, &error)
                     : vip_thumbnail_cache_path(a->cache_dir, ch->provider_id, ch->id, &error);
    image_slot_t *slot = path ? image_cache_slot_get(a, path) : NULL;
    free(path);
    if (!slot || !slot->image || slot->image->height <= 0)
        return false;
    *ratio = (double)slot->image->width / (double)slot->image->height;
    return true;
}

static artwork_mode_t dominant_artwork_mode(int portrait, int landscape, int square,
                                           artwork_mode_t fallback, int sampled) {
    if (sampled < 3)
        return fallback;
    if (portrait >= landscape && portrait >= square)
        return ART_PORTRAIT;
    if (landscape >= portrait && landscape >= square)
        return ART_LANDSCAPE;
    return ART_SQUARE;
}

static artwork_mode_t detect_artwork_mode(app_t *a) {
    int portrait = 0, landscape = 0, square = 0, sampled = 0;
    size_t limit = a->filtered_len < 24u ? a->filtered_len : 24u;
    for (size_t i = 0; i < limit; ++i) {
        double ratio = 0.0;
        if (!sample_artwork_ratio(a, a->filtered[i], &ratio))
            continue;
        count_artwork_ratio(ratio, &portrait, &landscape, &square);
        ++sampled;
    }
    return dominant_artwork_mode(portrait, landscape, square, default_artwork_mode(a), sampled);
}

/* Handle the details panel active operation. */
bool details_panel_active(const app_t *a) {
    return a && a->width >= 1180 && a->login_mode == LOGIN_XTREAM && !a->series_episode_mode &&
           (a->content_kind == CONTENT_VOD || a->content_kind == CONTENT_SERIES);
}

/* Handle the details panel geometry operation. */
void details_panel_geometry(const app_t *a, int *x, int *y, int *w, int *h) {
    int panel_w = DETAILS_PANEL_W;
    if (a->width < 1300)
        panel_w = 360;
    *x = a->width - panel_w - 16;
    *y = TOPBAR_H + 18;
    *w = panel_w;
    *h = a->height - *y - 18;
    if (*h < 120)
        *h = 120;
}

/* Choose card geometry from artwork shape instead of forcing posters and
 * channel logos into the same aspect ratio. */
card_layout_t browse_layout(app_t *a) {
    card_layout_t layout = {0};
    layout.mode = detect_artwork_mode(a);
    int ideal_w = 196, min_w = 168, max_w = 224;
    if (layout.mode == ART_SQUARE) {
        ideal_w = 224;
        min_w = 184;
        max_w = 260;
    } else if (layout.mode == ART_LANDSCAPE) {
        ideal_w = 320;
        min_w = 260;
        max_w = 380;
    }

    int content_x = SIDEBAR_W + 20;
    int avail_w = a->width - content_x - 18;
    if (details_panel_active(a)) {
        int px, py, pw, ph;
        details_panel_geometry(a, &px, &py, &pw, &ph);
        (void)py;
        (void)ph;
        avail_w = px - content_x - 12;
    }
    if (avail_w < min_w)
        avail_w = min_w;
    layout.cols = (avail_w + GRID_GAP) / (ideal_w + GRID_GAP);
    if (layout.cols < 1)
        layout.cols = 1;
    int fitted = (avail_w - (layout.cols - 1) * GRID_GAP) / layout.cols;
    while (fitted > max_w && layout.cols < 16) {
        ++layout.cols;
        fitted = (avail_w - (layout.cols - 1) * GRID_GAP) / layout.cols;
    }
    while (fitted < min_w && layout.cols > 1) {
        --layout.cols;
        fitted = (avail_w - (layout.cols - 1) * GRID_GAP) / layout.cols;
    }
    if (fitted < min_w)
        fitted = min_w;
    if (fitted > max_w)
        fitted = max_w;
    layout.card_w = fitted;
    if (layout.mode == ART_PORTRAIT)
        layout.art_h = (layout.card_w * 3) / 2;
    else if (layout.mode == ART_SQUARE)
        layout.art_h = layout.card_w;
    else
        layout.art_h = (layout.card_w * 9) / 16;
    layout.card_h = layout.art_h + 66;
    layout.row_step = layout.card_h + GRID_GAP;
    return layout;
}

/* Handle the step browse animations operation. */
static bool step_grid_scroll_animation(app_t *a, int64_t now, bool *scroll_changed) {
    if (!a->grid_scroll_animating)
        return false;
    if (a->grid_scroll_last_ms <= 0)
        a->grid_scroll_last_ms = now;
    int64_t elapsed = now - a->grid_scroll_last_ms;
    if (elapsed < 1)
        elapsed = 1;
    if (elapsed > 32)
        elapsed = 32;
    a->grid_scroll_last_ms = now;

    int diff = a->grid_scroll_target - a->grid_scroll;
    if (diff == 0) {
        a->grid_scroll_animating = false;
        return false;
    }
    int step = (int)((int64_t)diff * elapsed / 85LL);
    if (step == 0)
        step = diff > 0 ? 1 : -1;
    if ((diff > 0 && step > diff) || (diff < 0 && step < diff))
        step = diff;
    a->grid_scroll += step;
    *scroll_changed = true;
    a->grid_scroll_animating = a->grid_scroll != a->grid_scroll_target;
    return a->grid_scroll_animating;
}

static bool step_card_hover_animation(app_t *a, int64_t now) {
    if (!a->hovered_card_valid)
        return false;
    bool active = vip_ui_motion_step(&a->hover_motion, now, 140);
    if (a->hover_motion.value <= 0.0f && a->hover_motion.target <= 0.0f)
        a->hovered_card_valid = false;
    return active;
}

static bool step_control_hover_animation(app_t *a, int64_t now) {
    return a->hovered_control != HOVER_NONE && vip_ui_motion_step(&a->control_motion, now, 120);
}

bool step_browse_animations(app_t *a, int64_t now) {
    if (!a || a->screen != SCREEN_BROWSE)
        return false;

    bool scroll_changed = false;
    bool active = step_grid_scroll_animation(a, now, &scroll_changed);
    if (scroll_changed && a->mouse_inside)
        update_browse_hover(a, a->mouse_x, a->mouse_y);
    if (step_card_hover_animation(a, now))
        active = true;
    if (step_control_hover_animation(a, now))
        active = true;
    a->ui_motion_active = active;
    return active;
}

/* Handle the contains ascii case operation. */

/* Draw up to two title lines so long work names stay identifiable. */
static size_t card_title_breakpoint(app_t *a, const char *text, const char *font, int width) {
    size_t len = strlen(text);
    size_t best = 0u;
    char first[256];
    for (size_t i = 0u; i < len && i < sizeof(first) - 1u; ++i) {
        if (text[i] != ' ')
            continue;
        size_t n = i;
        while (n > 0u && text[n - 1u] == ' ')
            --n;
        if (n == 0u || n >= sizeof(first))
            continue;
        memcpy(first, text, n);
        first[n] = '\0';
        if (vip_ui_render_text_width(&a->renderer, first, font) > width)
            break;
        best = i + 1u;
    }
    return best;
}

static void draw_split_card_title(app_t *a, int x, int y, int width, const char *text,
                                  const char *font, size_t split) {
    char first[256];
    size_t first_len = split;
    while (first_len > 0u && text[first_len - 1u] == ' ')
        --first_len;
    if (first_len >= sizeof(first))
        first_len = sizeof(first) - 1u;
    memcpy(first, text, first_len);
    first[first_len] = '\0';

    const char *second = text + split;
    while (*second == ' ')
        ++second;
    vip_ui_render_text(&a->renderer, x, y, width, first, font, 0xF6F7FBu, 1.0, false);
    if (*second)
        vip_ui_render_text(&a->renderer, x, y + 16, width, second, font, 0xF6F7FBu, 1.0, false);
}

static void draw_card_title(app_t *a, int x, int y, int width, const char *text, bool active) {
    if (!a || !text || !text[0] || width <= 0)
        return;
    if (!a->renderer.active) {
        char bounded[256];
        bounded_text(bounded, sizeof(bounded), text, 92);
        draw_text_font(a, active ? a->font_heading : a->font, x, y + 14, bounded, a->colors.text);
        return;
    }

    const char *font = active ? "Sans SemiBold 10" : "Sans 10";
    if (vip_ui_render_text_width(&a->renderer, text, font) <= width) {
        vip_ui_render_text(&a->renderer, x, y, width, text, font, 0xF6F7FBu, 1.0, false);
        return;
    }

    size_t split = card_title_breakpoint(a, text, font, width);
    if (split == 0u) {
        vip_ui_render_text(&a->renderer, x, y, width, text, font, 0xF6F7FBu, 1.0, false);
        return;
    }
    draw_split_card_title(a, x, y, width, text, font, split);
}

/* Format the compact season/episode label shown under episode cards. */
static int xtream_episode_season(const vip_channel_t *ch) {
    if (!ch->category_id || !ch->category_id[0])
        return 0;
    char *end = NULL;
    long parsed = strtol(ch->category_id, &end, 10);
    if (end == ch->category_id || !end || *end != '\0' || parsed < 0 || parsed > INT_MAX)
        return 0;
    return (int)parsed;
}

static bool episode_numbers(const app_t *a, const vip_channel_t *ch, int *season, int *episode) {
    *season = 0;
    *episode = 0;
    if (a->login_mode == LOGIN_M3U) {
        char series_name[256];
        return vip_m3u_parse_episode_label(ch->name, series_name, sizeof(series_name), season, episode);
    }
    *season = xtream_episode_season(ch);
    *episode = ch->position;
    return true;
}

static bool episode_card_meta(const app_t *a, const vip_channel_t *ch, char *buffer, size_t cap) {
    if (!a || !ch || !buffer || cap == 0u || !a->series_episode_mode || a->series_season_select)
        return false;

    int season = 0, episode = 0;
    if (!episode_numbers(a, ch, &season, &episode) || episode <= 0)
        return false;
    if (season > 0)
        snprintf(buffer, cap, "T%d · E%d", season, episode);
    else
        snprintf(buffer, cap, "E%d", episode);
    return true;
}

/* Ensure the filtered-index buffer can hold all active channels. */


static void draw_pairing_qr(app_t *a, int x, int y, int size) {
    if (!a || !a->pairing_qr || size <= 0)
        return;
    const int quiet = 4;
    int modules = a->pairing_qr->width + quiet * 2;
    int cell = size / modules;
    if (cell < 1)
        cell = 1;
    int actual = modules * cell;
    int ox = x + (size - actual) / 2;
    int oy = y + (size - actual) / 2;

    if (a->renderer.active)
        vip_ui_render_round_rect(&a->renderer, ox, oy, actual, actual, 8, 0xFFFFFFu, 1.0);
    else
        fill_rect(a, ox, oy, (unsigned)actual, (unsigned)actual, WhitePixel(a->dpy, a->screen_num));

    for (int row = 0; row < a->pairing_qr->width; ++row) {
        for (int col = 0; col < a->pairing_qr->width; ++col) {
            if ((a->pairing_qr->data[row * a->pairing_qr->width + col] & 1u) == 0u)
                continue;
            int rx = ox + (col + quiet) * cell;
            int ry = oy + (row + quiet) * cell;
            if (a->renderer.active)
                vip_ui_render_round_rect(&a->renderer, rx, ry, cell, cell, 0, 0x000000u, 1.0);
            else
                fill_rect(a, rx, ry, (unsigned)cell, (unsigned)cell, BlackPixel(a->dpy, a->screen_num));
        }
    }
}



/* Select season. */

/* Draw input. */
static const char *input_display_text(const char *value, const char *placeholder, bool search_focused,
                                      bool password, char *masked, size_t masked_cap) {
    const char *text = value && value[0] ? value : (search_focused ? "Digite para buscar..." : placeholder);
    if (!password || !value || !value[0])
        return text;
    size_t n = strlen(value);
    if (n >= masked_cap)
        n = masked_cap - 1u;
    memset(masked, '*', n);
    masked[n] = '\0';
    return masked;
}

static void draw_search_caret(app_t *a, int x, int y, int w, int h, const char *value) {
    const char *caret_text = value && value[0] ? value : "";
    int measured = a->renderer.active ? vip_ui_render_text_width(&a->renderer, caret_text, "Sans 10")
                                      : text_width(a, caret_text);
    int caret_x = x + 16 + measured;
    if (caret_x < x + 16)
        caret_x = x + 16;
    if (caret_x > x + w - 18)
        caret_x = x + w - 18;
    set_fg(a, a->colors.accent);
    XDrawLine(a->dpy, draw_target(a), a->gc, caret_x, y + 11, caret_x, y + h - 11);
}

static void draw_input(app_t *a, int x, int y, int w, int h, const char *value, const char *placeholder,
                       int focus_id, bool password) {
    bool focused = a->input_focus == focus_id;
    bool search_focused = focused && focus_id == INPUT_SEARCH;
    fill_round_rect(a, x + 2, y + 3, w, h, 12, a->colors.black);
    fill_round_rect(a, x, y, w, h, 12, search_focused ? a->colors.accent2 : a->colors.panel2);
    stroke_round_rect(a, x, y, w, h, 12, focused ? a->colors.accent : a->colors.border);
    if (focused) {
        stroke_round_rect(a, x + 2, y + 2, w - 4, h - 4, 10,
                          search_focused ? a->colors.accent : a->colors.accent2);
        if (search_focused)
            fill_round_rect(a, x + 5, y + 8, 4, h - 16, 2, a->colors.accent);
    }

    char masked[256];
    const char *text = input_display_text(value, placeholder, search_focused, password, masked, sizeof(masked));
    bool bright = search_focused || (value && value[0]);
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, x + 16, y + (h - 16) / 2, w - 32, text, "Sans 10",
                           bright ? 0xF6F7FBu : 0xAAB2C4u, 1.0, false);
    else
        draw_text(a, x + 16, y + h / 2 + 6, text, bright ? a->colors.text : a->colors.muted);
    if (search_focused)
        draw_search_caret(a, x, y, w, h, value);
}

/* Draw login. */
static void draw_login(app_t *a) {
    if (a->renderer.active)
        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, a->height, 0x090B11u, 0x111722u);
    else
        fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);
    int w = a->width > 1120 ? 1080 : a->width - 40;
    if (w < 720)
        w = 720;
    int h = 620;
    int x = (a->width - w) / 2, y = (a->height - h) / 2;
    if (y < 18)
        y = 18;
    int panel_h = h;
    if (y + panel_h > a->height - 12 && a->height - y - 12 >= 500)
        panel_h = a->height - y - 12;
    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, x + 8, y + 10, w, panel_h, 26, 0x000000u, 0.58);
        vip_ui_render_round_rect(&a->renderer, x, y, w, panel_h, 26, 0x111722u, 0.97);
        vip_ui_render_round_stroke(&a->renderer, x, y, w, panel_h, 26, 0x343A48u, 1.0, 1.0);
    } else {
        fill_round_rect(a, x + 8, y + 10, w, panel_h, 24, a->colors.black);
        fill_round_rect(a, x, y, w, panel_h, 24, a->colors.panel);
        stroke_round_rect(a, x, y, w, panel_h, 24, a->colors.border);
    }

    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, x + 30, y + 25, 42, 42, 13, 0xFF5F2Eu, 1.0);
        vip_ui_render_text(&a->renderer, x + 30, y + 35, 42, "B", "Sans Bold 13", 0x090B11u, 1.0, true);
    } else {
        fill_round_rect(a, x + 30, y + 25, 42, 42, 13, a->colors.accent);
        draw_centered_font(a, a->font_heading, x + 30, y + 53, 42, "B", a->colors.bg);
    }
    if (a->renderer.active) {
        vip_ui_render_text(&a->renderer, x + 86, y + 27, 360, "Blazzing", "Sans Bold 22", 0xF6F7FBu, 1.0,
                           false);
        vip_ui_render_text(&a->renderer, x + 86, y + 55, 420, "IPTV para Linux",
                           "Sans 10", 0xAAB2C4u, 1.0, false);
    } else {
        draw_text_font(a, a->font_title, x + 86, y + 49, "Blazzing", a->colors.text);
        draw_text(a, x + 86, y + 69, "Streaming, listas e biblioteca em um só lugar", a->colors.muted);
    }

    int form_x = x + 34, form_w = (w * 58) / 100 - 50;
    int list_x = x + (w * 60) / 100, list_w = w - (list_x - x) - 34;
    int mode_y = y + 88;
    int mode_w = (form_w - 10) / 2;
    for (int i = 0; i < 2; ++i) {
        bool selected = (int)a->login_mode == i;
        bool mode_focused = a->input_focus == INPUT_MODE;
        int bx = form_x + i * (mode_w + 10);
        if (a->renderer.active) {
            vip_ui_render_round_rect(&a->renderer, bx, mode_y, mode_w, 42, 12,
                                     selected ? 0x572517u : 0x171C28u, 1.0);
            vip_ui_render_round_stroke(&a->renderer, bx, mode_y, mode_w, 42, 12,
                                       mode_focused && selected ? 0xFFFFFFu
                                                                : (selected ? 0xFF5F2Eu : 0x343A48u),
                                       1.0, mode_focused && selected ? 2.5 : (selected ? 1.5 : 1.0));
            vip_ui_render_text(&a->renderer, bx, mode_y + 12, mode_w, i == LOGIN_XTREAM ? "Xtream" : "M3U",
                               selected ? "Sans Bold 10" : "Sans 10", selected ? 0xF6F7FBu : 0xAAB2C4u, 1.0,
                               true);
        } else {
            fill_round_rect(a, bx, mode_y, mode_w, 42, 12, selected ? a->colors.accent2 : a->colors.panel2);
            stroke_round_rect(a, bx, mode_y, mode_w, 42, 12,
                              mode_focused && selected ? a->colors.text
                                                       : (selected ? a->colors.accent : a->colors.border));
            draw_centered(a, bx, mode_y + 27, mode_w, i == LOGIN_XTREAM ? "Xtream" : "M3U",
                          selected ? a->colors.text : a->colors.muted);
        }
    }

    draw_input(a, form_x, y + 142, form_w, 44, a->profile_name, "Nome da lista (opcional)",
               INPUT_PROFILE_NAME, false);
    if (a->login_mode == LOGIN_XTREAM) {
        draw_input(a, form_x, y + 196, form_w, 44, a->server, "Servidor primário (https://...)", INPUT_SERVER,
                   false);
        draw_input(a, form_x, y + 250, form_w, 44, a->server_alt, "Servidor alternativo (opcional)",
                   INPUT_SERVER_ALT, false);
        draw_input(a, form_x, y + 304, form_w, 44, a->username, "Usuário", INPUT_USERNAME, false);
        draw_input(a, form_x, y + 358, form_w, 44, a->password, "Senha", INPUT_PASSWORD, true);
    } else {
        draw_input(a, form_x, y + 196, form_w, 44, a->server, "URL ou caminho de playlist .m3u/.m3u8",
                   INPUT_SERVER, false);
        if (a->renderer.active) {
            vip_ui_render_text(&a->renderer, form_x, y + 252, form_w,
                               "M3U remoto (HTTP/HTTPS) ou arquivo local.", "Sans 9", 0xAAB2C4u, 1.0, false);
            vip_ui_render_text(&a->renderer, form_x, y + 278, form_w,
                               "A playlist é processada diretamente pelo Blazzing.", "Sans 9", 0xAAB2C4u, 1.0,
                               false);
        } else {
            draw_text(a, form_x, y + 266, "M3U remoto (HTTP/HTTPS) ou arquivo local.", a->colors.muted);
            draw_text(a, form_x, y + 292, "A playlist é processada diretamente pelo Blazzing.",
                      a->colors.muted);
        }

        int phone_y = y + 314;
        bool waiting_phone = a->pairing_relay != NULL;
        bool phone_focused = a->input_focus == INPUT_PHONE;
        const char *phone_label = waiting_phone ? "Aguardando celular..."
                                  : (a->pairing_retry_ready
                                         ? "Tentar novamente (QR)"
                                         : "Adicionar pelo celular");
        if (a->renderer.active) {
            vip_ui_render_round_rect(&a->renderer, form_x, phone_y, form_w, 46, 12,
                                     waiting_phone ? 0x572517u : 0x171C28u, 1.0);
            vip_ui_render_round_stroke(&a->renderer, form_x, phone_y, form_w, 46, 12,
                                       phone_focused ? 0xFFFFFFu
                                                     : (waiting_phone ? 0xFF5F2Eu : 0x343A48u),
                                       1.0, phone_focused ? 2.5 : 1.0);
            vip_ui_render_text(&a->renderer, form_x, phone_y + 13, form_w,
                               phone_label,
                               waiting_phone ? "Sans Bold 10" : "Sans 10",
                               waiting_phone ? 0xF6F7FBu : 0xAAB2C4u, 1.0, true);
            if (waiting_phone)
                vip_ui_render_text(&a->renderer, form_x, phone_y + 57, form_w,
                                   "Pareamento HTTPS pela Internet", "Sans 8",
                                   0xAAB2C4u, 1.0, false);
        } else {
            fill_round_rect(a, form_x, phone_y, form_w, 46, 12,
                            waiting_phone ? a->colors.accent2 : a->colors.panel2);
            stroke_round_rect(a, form_x, phone_y, form_w, 46, 12,
                              phone_focused ? a->colors.text
                                            : (waiting_phone ? a->colors.accent : a->colors.border));
            draw_centered(a, form_x, phone_y + 29, form_w,
                          phone_label,
                          waiting_phone ? a->colors.text : a->colors.muted);
            if (waiting_phone)
                draw_text(a, form_x, phone_y + 69,
                          "Pareamento HTTPS pela Internet", a->colors.muted);
        }
    }
    bool ready = a->server[0] && !atomic_load(&a->login_running) &&
                 (a->login_mode == LOGIN_M3U || (a->username[0] && a->password[0]));
    int connect_y = y + 430;
    bool connect_focused = a->input_focus == INPUT_CONNECT;
    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, form_x + 3, connect_y + 5, form_w, 50, 15, 0x000000u, 0.48);
        vip_ui_render_round_rect(&a->renderer, form_x, connect_y, form_w, 50, 15,
                                 ready ? 0xFF5F2Eu : 0x171C28u, 1.0);
        vip_ui_render_round_stroke(&a->renderer, form_x, connect_y, form_w, 50, 15,
                                   connect_focused ? 0xFFFFFFu
                                                   : (ready ? 0xFF8A45u : 0x343A48u),
                                   1.0, connect_focused ? 2.5 : 1.0);
        vip_ui_render_text(&a->renderer, form_x, connect_y + 15, form_w,
                           atomic_load(&a->login_running) ? "Conectando..." : "Conectar", "Sans Bold 11",
                           ready ? 0x090B11u : 0xAAB2C4u, 1.0, true);
    } else {
        fill_round_rect(a, form_x + 2, connect_y + 4, form_w, 50, 14, a->colors.black);
        fill_round_rect(a, form_x, connect_y, form_w, 50, 14, ready ? a->colors.accent : a->colors.panel2);
        stroke_round_rect(a, form_x, connect_y, form_w, 50, 14,
                          connect_focused ? a->colors.text
                                          : (ready ? a->colors.accent : a->colors.border));
        draw_centered_font(a, a->font_heading, form_x, connect_y + 32, form_w,
                           atomic_load(&a->login_running) ? "Conectando..." : "Conectar",
                           ready ? a->colors.bg : a->colors.muted);
    }

    const char *side_title = a->pairing_relay ? "Adicionar pelo celular" : "Suas listas";
    const char *side_subtitle =
        a->pairing_relay ? "Escaneie o QR Code em qualquer conexão"
                         : "Acesso rápido aos perfis salvos";
    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, list_x - 14, y + 88, list_w + 28, 438, 18,
                                 0x111A28u, 0.98);
        vip_ui_render_round_stroke(&a->renderer, list_x - 14, y + 88, list_w + 28, 438, 18,
                                   0x343A48u, 1.0, 1.0);
        vip_ui_render_text(&a->renderer, list_x, y + 102, list_w, side_title,
                           "Sans Bold 12", 0xF6F7FBu, 1.0, false);
        vip_ui_render_text(&a->renderer, list_x, y + 126, list_w, side_subtitle,
                           "Sans 9", 0xAAB2C4u, 1.0, false);
    } else {
        fill_round_rect(a, list_x - 14, y + 88, list_w + 28, 438, 18, a->colors.panel2);
        stroke_round_rect(a, list_x - 14, y + 88, list_w + 28, 438, 18, a->colors.border);
        draw_text_font(a, a->font_heading, list_x, y + 116, side_title, a->colors.text);
        draw_text(a, list_x, y + 137, side_subtitle, a->colors.muted);
    }

    if (a->pairing_relay && a->pairing_qr) {
        int qr_size = list_w < 290 ? list_w - 20 : 270;
        if (qr_size < 120)
            qr_size = 120;
        int qr_x = list_x + (list_w - qr_size) / 2;
        int qr_y = y + 158;
        draw_pairing_qr(a, qr_x, qr_y, qr_size);

        char session_hint[96];
        snprintf(session_hint, sizeof(session_hint), "Sessão %.8s • expira em 45 s",
                 vip_pairing_relay_session_id(a->pairing_relay));
        if (a->renderer.active) {
            vip_ui_render_text(&a->renderer, list_x, qr_y + qr_size + 16, list_w,
                               "Via Internet • nenhuma porta local necessária",
                               "Sans 8", 0xAAB2C4u, 1.0, true);
            vip_ui_render_text(&a->renderer, list_x, qr_y + qr_size + 38, list_w,
                               session_hint, "Sans 8", 0xAAB2C4u, 1.0, true);
            vip_ui_render_text(&a->renderer, list_x, qr_y + qr_size + 60, list_w,
                               "Back/Esc cancela o pareamento",
                               "Sans 8", 0xAAB2C4u, 1.0, true);
        } else {
            draw_centered(a, list_x, qr_y + qr_size + 30, list_w,
                          "Via Internet • nenhuma porta local necessária", a->colors.muted);
            draw_centered(a, list_x, qr_y + qr_size + 52, list_w,
                          session_hint, a->colors.muted);
            draw_centered(a, list_x, qr_y + qr_size + 74, list_w,
                          "Back/Esc cancela o pareamento", a->colors.muted);
        }
    }

    int row_y = y + 154;
    int visible = 6;
    for (int r = 0; !a->pairing_relay && r < visible; ++r) {
        int idx = a->profile_scroll + r;
        if (idx < 0 || (size_t)idx >= a->profiles.len)
            break;
        vip_profile_t *p = &a->profiles.items[idx];
        bool profile_focused = a->input_focus == INPUT_SAVED_PROFILE && a->profile_focus == idx;
        if (a->renderer.active) {
            vip_ui_render_round_rect(&a->renderer, list_x, row_y, list_w, 52, 12,
                                     profile_focused ? 0x572517u : 0x171C28u, 1.0);
            vip_ui_render_round_stroke(&a->renderer, list_x, row_y, list_w, 52, 12,
                                       profile_focused ? 0xFFFFFFu : 0x343A48u,
                                       1.0, profile_focused ? 2.5 : 1.0);
        } else {
            draw_surface(a, list_x, row_y, list_w, 52, 12, profile_focused);
            if (profile_focused)
                stroke_round_rect(a, list_x - 2, row_y - 2, list_w + 4, 56, 13, a->colors.text);
        }
        char label[220];
        snprintf(label, sizeof(label), "%s  ·  %s", p->name ? p->name : "Lista",
                 p->type == VIP_PROFILE_M3U ? "M3U" : "Xtream");
        bounded_text(label, sizeof(label), label, 44);
        char sub[220];
        bounded_text(sub, sizeof(sub), p->server ? p->server : "", 46);
        if (a->renderer.active) {
            vip_ui_render_text(&a->renderer, list_x + 13, row_y + 8, list_w - 26, label,
                               profile_focused ? "Sans Bold 9" : "Sans SemiBold 9",
                               0xF6F7FBu, 1.0, false);
            vip_ui_render_text(&a->renderer, list_x + 13, row_y + 29, list_w - 26, sub, "Sans 8", 0xAAB2C4u,
                               1.0, false);
        } else {
            draw_text(a, list_x + 13, row_y + 22, label,
                      profile_focused ? a->colors.text : a->colors.text);
            draw_text_font(a, a->font_small, list_x + 13, row_y + 42, sub, a->colors.muted);
        }
        row_y += 60;
    }
    if (!a->pairing_relay && a->profiles.len == 0u) {
        if (a->renderer.active)
            vip_ui_render_text(&a->renderer, list_x, y + 168, list_w, "Nenhuma lista salva ainda.", "Sans 9",
                               0xAAB2C4u, 1.0, false);
        else
            draw_text(a, list_x, y + 182, "Nenhuma lista salva ainda.", a->colors.muted);
    }

    char status_copy[512];
    pthread_mutex_lock(&a->data_mutex);
    snprintf(status_copy, sizeof(status_copy), "%s", a->status);
    pthread_mutex_unlock(&a->data_mutex);
    bool status_error = strstr(status_copy, "falha") != NULL ||
                        strstr(status_copy, "Falha") != NULL ||
                        strstr(status_copy, "Erro") != NULL ||
                        strstr(status_copy, "erro") != NULL ||
                        strstr(status_copy, "HTTP") != NULL ||
                        strstr(status_copy, "indisponível") != NULL ||
                        strstr(status_copy, "expirou") != NULL ||
                        strstr(status_copy, "nenhum stream") != NULL;
    int status_y = connect_y + 60;
    if (status_y > a->height - 28)
        status_y = a->height - 28;
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, form_x, status_y, form_w, status_copy, "Sans 8",
                           status_error ? 0xFF7185u : 0xAAB2C4u, 1.0, false);
    else
        draw_text(a, form_x, status_y + 12, status_copy, status_error ? a->colors.danger : a->colors.muted);

    const char *credit = "by Xoykor";
    if (a->renderer.active) {
        int credit_w = vip_ui_render_text_width(&a->renderer, credit, "Sans 8");
        vip_ui_render_text(&a->renderer, a->width - credit_w - 18, a->height - 28, credit_w,
                           credit, "Sans 8", 0x66758Bu, 0.92, false);
    } else {
        int credit_w = text_width(a, credit);
        draw_text_font(a, a->font_small, a->width - credit_w - 18, a->height - 16,
                       credit, a->colors.muted);
    }
}

/* Draw wrapped text. */
static void normalize_wrapped_text(char *text) {
    for (char *p = text; *p; ++p)
        if (*p == '\n' || *p == '\r' || *p == '\t')
            *p = ' ';
}

static void build_wrapped_candidate(char *candidate, size_t cap, const char *line, const char *word) {
    size_t used = strlen(line);
    if (used >= cap)
        used = cap - 1u;
    memcpy(candidate, line, used);
    if (used > 0u && used + 1u < cap)
        candidate[used++] = ' ';
    size_t room = cap - used - 1u;
    size_t word_len = strlen(word);
    if (word_len > room)
        word_len = room;
    memcpy(candidate + used, word, word_len);
    candidate[used + word_len] = '\0';
}

static int draw_wrapped_text(app_t *a, int x, int y, int width, const char *text, int max_lines,
                             unsigned long color) {
    if (!text || !text[0] || max_lines <= 0)
        return y;

    char copy[3072];
    snprintf(copy, sizeof(copy), "%s", text);
    normalize_wrapped_text(copy);

    char line[768] = {0};
    char *save = NULL;
    int lines = 0;
    for (char *word = strtok_r(copy, " ", &save); word && lines < max_lines;
         word = strtok_r(NULL, " ", &save)) {
        char candidate[768];
        build_wrapped_candidate(candidate, sizeof(candidate), line, word);
        if (line[0] && text_width(a, candidate) > width) {
            draw_text(a, x, y, line, color);
            y += 20;
            if (++lines >= max_lines)
                break;
            snprintf(line, sizeof(line), "%s", word);
            continue;
        }
        snprintf(line, sizeof(line), "%s", candidate);
    }
    if (line[0] && lines < max_lines) {
        draw_text(a, x, y, line, color);
        y += 20;
    }
    return y;
}

/* Handle the detail art id operation. */
static void detail_art_id(char *out, size_t cap, const char *media_id) {
    snprintf(out, cap, "detail-art:%s", media_id ? media_id : "unknown");
}

/* Handle the enqueue detail artwork operation. */
static void enqueue_detail_artwork(app_t *a, const vip_channel_t *ch, const char *art_url, int64_t priority) {
    if (!a || !a->thumbs || !ch || !ch->provider_id || !ch->id || !ch->stream_url || !art_url || !art_url[0])
        return;
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

/* Draw details panel. */
typedef struct {
    char loaded_id[128];
    char status[256];
    char plot[3072];
    char cover[1024];
    char backdrop[1024];
    char genre[256];
    char release_date[128];
    char rating[64];
    char duration[128];
    char cast[768];
    char director[512];
} details_view_data_t;

static void read_details_view_data(app_t *a, details_view_data_t *data) {
    pthread_mutex_lock(&a->data_mutex);
    snprintf(data->loaded_id, sizeof(data->loaded_id), "%s", a->details_media_id);
    snprintf(data->status, sizeof(data->status), "%s", a->details_status);
    snprintf(data->plot, sizeof(data->plot), "%s", a->details_metadata.plot ? a->details_metadata.plot : "");
    snprintf(data->cover, sizeof(data->cover), "%s",
             a->details_metadata.cover_url ? a->details_metadata.cover_url : "");
    snprintf(data->backdrop, sizeof(data->backdrop), "%s",
             a->details_metadata.backdrop_url ? a->details_metadata.backdrop_url : "");
    snprintf(data->genre, sizeof(data->genre), "%s",
             a->details_metadata.genre ? a->details_metadata.genre : "");
    snprintf(data->release_date, sizeof(data->release_date), "%s",
             a->details_metadata.release_date ? a->details_metadata.release_date : "");
    snprintf(data->rating, sizeof(data->rating), "%s",
             a->details_metadata.rating ? a->details_metadata.rating : "");
    snprintf(data->duration, sizeof(data->duration), "%s",
             a->details_metadata.duration ? a->details_metadata.duration : "");
    snprintf(data->cast, sizeof(data->cast), "%s", a->details_metadata.cast ? a->details_metadata.cast : "");
    snprintf(data->director, sizeof(data->director), "%s",
             a->details_metadata.director ? a->details_metadata.director : "");
    pthread_mutex_unlock(&a->data_mutex);
}

static void draw_details_surface(app_t *a, int px, int py, int pw, int ph) {
    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, px + 5, py + 8, pw, ph, 20, 0x000000u, 0.55);
        vip_ui_render_round_rect(&a->renderer, px, py, pw, ph, 20, 0x111722u, 0.97);
        vip_ui_render_round_stroke(&a->renderer, px, py, pw, ph, 20, 0x343A48u, 1.0, 1.0);
        return;
    }
    fill_round_rect(a, px + 4, py + 6, pw, ph, 18, a->colors.black);
    fill_round_rect(a, px, py, pw, ph, 18, a->colors.panel);
    stroke_round_rect(a, px, py, pw, ph, 18, a->colors.border);
}

static void draw_details_header(app_t *a, const vip_channel_t *ch, size_t chidx, int px, int py, int pw) {
    char title[256];
    bounded_text(title, sizeof(title), ch->name, 72);
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, px + 16, py + 13, pw - 132, title, "Sans Bold 12", 0xF6F7FBu, 1.0,
                           false);
    else
        draw_text_font(a, a->font_heading, px + 16, py + 31, title, a->colors.text);

    bool favorite = a->favorite_flags && a->favorite_flags[chidx];
    int fav_w = 92, fav_h = 32, fav_x = px + pw - fav_w - 14, fav_y = py + 10;
    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, fav_x, fav_y, fav_w, fav_h, 12,
                                 favorite ? 0x572517u : 0x171C28u, 1.0);
        vip_ui_render_round_stroke(&a->renderer, fav_x, fav_y, fav_w, fav_h, 12,
                                   favorite ? 0xFF5F2Eu : 0x343A48u, 1.0, 1.0);
        vip_ui_render_text(&a->renderer, fav_x, fav_y + 9, fav_w, favorite ? "SALVO" : "FAVORITAR",
                           favorite ? "Sans Bold 8" : "Sans 8", favorite ? 0xF6F7FBu : 0xAAB2C4u, 1.0, true);
        return;
    }
    fill_round_rect(a, fav_x, fav_y, fav_w, fav_h, 12, favorite ? a->colors.accent2 : a->colors.panel2);
    stroke_round_rect(a, fav_x, fav_y, fav_w, fav_h, 12, favorite ? a->colors.accent : a->colors.border);
    draw_centered(a, fav_x, fav_y + 21, fav_w, favorite ? "SALVO" : "FAVORITAR",
                  favorite ? a->colors.text : a->colors.muted);
}

static int draw_details_art(app_t *a, const vip_channel_t *ch, const details_view_data_t *data,
                            bool current, int px, int py, int pw) {
    int art_x = px + 16, art_y = py + 50, art_w = pw - 32, art_h = 170;
    fill_rect(a, art_x, art_y, (unsigned)art_w, (unsigned)art_h, a->colors.black);

    bool art_ok = false;
    const char *art_url = data->backdrop[0] ? data->backdrop : data->cover;
    if (current && art_url[0]) {
        char art_id[256];
        detail_art_id(art_id, sizeof(art_id), ch->id);
        vip_error_t art_error = {0};
        char *path = vip_thumbnail_artwork_cache_path(a->cache_dir, ch->provider_id, art_id,
                                                      art_url, &art_error);
        art_ok = path && draw_cached_image_contain(a, path, art_x, art_y, art_w, art_h);
        free(path);
        if (!art_ok)
            enqueue_detail_artwork(a, ch, art_url, 2000000LL);
    }
    if (!art_ok)
        draw_centered(a, art_x, art_y + art_h / 2 + 5, art_w,
                      current ? "carregando banner..." : "carregando detalhes...", a->colors.muted);
    stroke_rect(a, art_x, art_y, (unsigned)art_w, (unsigned)art_h, a->colors.border);
    return art_y + art_h + 24;
}

static void build_details_facts(const details_view_data_t *data, char *facts, size_t cap) {
    size_t used = 0u;
    const char *values[4] = {data->release_date, data->genre, data->rating, data->duration};
    const char *labels[4] = {"Data", "Gênero", "Nota", "Duração"};
    for (size_t i = 0; i < 4u; ++i) {
        if (!values[i][0])
            continue;
        int n = snprintf(facts + used, cap - used, "%s%s: %s", used ? " | " : "", labels[i], values[i]);
        if (n < 0 || (size_t)n >= cap - used)
            break;
        used += (size_t)n;
    }
}

static void draw_details_text(app_t *a, const details_view_data_t *data, int px, int py, int pw, int ph, int y) {
    char facts[640] = {0};
    build_details_facts(data, facts, sizeof(facts));
    if (facts[0]) {
        y = draw_wrapped_text(a, px + 16, y, pw - 32, facts, 2, a->colors.muted);
        y += 6;
    }
    draw_text(a, px + 16, y, "Sinopse", a->colors.text);
    y += 22;
    if (data->plot[0])
        y = draw_wrapped_text(a, px + 16, y, pw - 32, data->plot, 8, a->colors.muted);
    else {
        draw_text(a, px + 16, y, "O provider não enviou sinopse para este item.", a->colors.muted);
        y += 20;
    }
    y += 8;
    if (data->director[0] && y < py + ph - 72) {
        char line[600];
        snprintf(line, sizeof(line), "Direção: %s", data->director);
        y = draw_wrapped_text(a, px + 16, y, pw - 32, line, 2, a->colors.muted);
    }
    if (data->cast[0] && y < py + ph - 48) {
        char line[880];
        snprintf(line, sizeof(line), "Elenco: %s", data->cast);
        (void)draw_wrapped_text(a, px + 16, y, pw - 32, line, 2, a->colors.muted);
    }
    if (!data->plot[0] && data->status[0])
        draw_text(a, px + 16, py + ph - 18, data->status, a->colors.muted);
}

static void draw_details_panel(app_t *a) {
    if (!details_panel_active(a) || a->filtered_len == 0u)
        return;
    if (a->focused_filtered >= a->filtered_len)
        a->focused_filtered = a->filtered_len - 1u;
    size_t chidx = a->filtered[a->focused_filtered];
    if (chidx >= ACTIVE_CHANNELS(a).len)
        return;
    vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[chidx];

    int px, py, pw, ph;
    details_panel_geometry(a, &px, &py, &pw, &ph);
    draw_details_surface(a, px, py, pw, ph);

    details_view_data_t data = {0};
    read_details_view_data(a, &data);
    draw_details_header(a, ch, chidx, px, py, pw);
    bool current = ch->id && strcmp(data.loaded_id, ch->id) == 0;
    int y = draw_details_art(a, ch, &data, current, px, py, pw);
    if (!current) {
        draw_text(a, px + 16, y, "Carregando detalhes da obra...", a->colors.muted);
        return;
    }
    draw_details_text(a, &data, px, py, pw, ph, y);
}

/* Draw toast. */
static void draw_toast(app_t *a) {
    if (!a || !a->toast[0] || monotonic_ms() >= a->toast_until_ms)
        return;
    int w = text_width(a, a->toast) + 44;
    if (w < 220)
        w = 220;
    if (w > a->width - 40)
        w = a->width - 40;
    int x = (a->width - w) / 2;
    int y = TOPBAR_H + 10;
    fill_round_rect(a, x + 3, y + 4, w, 42, 14, a->colors.black);
    fill_round_rect(a, x, y, w, 42, 14, a->colors.panel2);
    stroke_round_rect(a, x, y, w, 42, 14, a->colors.accent);
    draw_centered(a, x, y + 27, w, a->toast, a->colors.text);
}

/* Handle the browse sidebar category y operation. */
int browse_sidebar_category_y(const app_t *a) {
    return TOPBAR_H + 12 + (a && a->series_episode_mode ? 48 : 0);
}

/* Handle the category visible rows operation. */
int category_visible_rows(app_t *a) {
    int top = browse_sidebar_category_y(a);
    int n = (a->height - top - 8) / 40;
    return n > 1 ? n : 1;
}

/* Handle the browse back label operation. */
static const char *browse_back_label(const app_t *a) {
    if (!a || !a->series_episode_mode)
        return "Voltar";
    return a->series_season_select ? "< Séries" : "< Temporadas";
}

/* Return whether text contains a case-insensitive standalone ASCII token. */
static bool contains_ascii_token_case(const char *text, const char *token) {
    if (!text || !token || !token[0])
        return false;
    size_t token_len = strlen(token);
    for (const char *p = text; *p; ++p) {
        size_t remaining = strlen(p);
        if (remaining < token_len)
            break;
        if (strncasecmp(p, token, token_len) != 0)
            continue;
        unsigned char before = p == text ? 0u : (unsigned char)p[-1];
        unsigned char after = (unsigned char)p[token_len];
        if ((p == text || !isalnum(before)) && (!after || !isalnum(after)))
            return true;
    }
    return false;
}

/* Keep language/audio variants visible even when a long title is ellipsized. */
static const char *media_variant_label(const char *name) {
    if (!name || !name[0])
        return NULL;
    if (contains_ascii_token_case(name, "dual"))
        return "DUAL";
    if (contains_ascii_token_case(name, "legendado") || contains_ascii_token_case(name, "legendada") ||
        contains_ascii_token_case(name, "leg") || contains_ascii_token_case(name, "sub") ||
        contains_ascii_token_case(name, "subbed"))
        return "LEG";
    if (contains_ascii_token_case(name, "dublado") || contains_ascii_token_case(name, "dublada") ||
        contains_ascii_token_case(name, "dub"))
        return "DUB";
    return NULL;
}

/* Handle the content label operation. */
static const char *content_label(content_kind_t kind) {
    switch (kind) {
    case CONTENT_VOD:
        return "Filmes";
    case CONTENT_SERIES:
        return "Séries";
    case CONTENT_LIVE:
    default:
        return "TV";
    }
}

/* Handle the content plural operation. */
static const char *content_plural(app_t *a) {
    if (a->series_season_select)
        return "temporadas";
    if (a->series_episode_mode)
        return "episódios";
    switch (a->content_kind) {
    case CONTENT_VOD:
        return "filmes";
    case CONTENT_SERIES:
        return "séries";
    case CONTENT_LIVE:
    default:
        return "canais";
    }
}

/* Handle the all content label operation. */
static const char *all_content_label(app_t *a) {
    if (a->series_season_select)
        return "Todas as temporadas";
    if (a->series_episode_mode)
        return "Todos os episódios";
    switch (a->content_kind) {
    case CONTENT_VOD:
        return "Todos os filmes";
    case CONTENT_SERIES:
        return "Todas as séries";
    case CONTENT_LIVE:
    default:
        return "Todos os canais";
    }
}

/* Draw the browse screen's fixed background surfaces. */
static void draw_browse_background(app_t *a) {
    if (a->renderer.active) {
        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, a->height, 0x090B11u, 0x0D1119u);
        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, TOPBAR_H, 0x171C28u, 0x111722u);
        vip_ui_render_linear_gradient(&a->renderer, 0, TOPBAR_H, SIDEBAR_W, a->height - TOPBAR_H, 0x171C28u,
                                      0x111722u);
    } else {
        fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);
        fill_rect(a, 0, 0, (unsigned)a->width, TOPBAR_H, a->colors.panel);
        fill_rect(a, 0, TOPBAR_H, SIDEBAR_W, (unsigned)(a->height - TOPBAR_H), a->colors.panel2);
    }
    fill_rect(a, SIDEBAR_W - 1, TOPBAR_H, 1, (unsigned)(a->height - TOPBAR_H), a->colors.border);
}

typedef struct {
    unsigned long fill;
    unsigned long border;
    unsigned long text;
} browse_control_style_t;

/* Selected, hovered and keyboard-focused controls share the same visual
 * priority. Centralizing it keeps every concrete control focused on layout. */
static browse_control_style_t browse_control_style(const app_t *a, bool selected, bool hovered, bool focused) {
    browse_control_style_t style = {
        .fill = a->colors.panel2,
        .border = a->colors.border,
        .text = a->colors.muted,
    };

    if (selected) {
        style.fill = a->colors.accent2;
        style.border = a->colors.accent;
        style.text = a->colors.text;
    } else if (hovered || focused) {
        style.fill = a->colors.hover;
        style.border = hovered ? a->colors.accent : a->colors.border;
        style.text = a->colors.text;
    }
    if (focused)
        style.border = a->colors.text;
    return style;
}

/* Draw one content-kind tab. */
static void draw_browse_tab(app_t *a, int kind, int x, int width) {
    const int tab_y = 12, tab_h = 46;
    bool selected = (int)a->content_kind == kind;
    bool focused = a->browse_focus == BROWSE_FOCUS_TOP && a->browse_top_focus == kind;
    bool hovered = a->hovered_control == HOVER_TAB_BASE + kind;
    browse_control_style_t style = browse_control_style(a, selected, hovered, focused);

    fill_round_rect(a, x, tab_y, width, tab_h, 13, style.fill);
    stroke_round_rect(a, x, tab_y, width, tab_h, 13, style.border);

    if (hovered && !selected) {
        float hover_t = vip_ui_ease_out_cubic(a->control_motion.value);
        int line_w = (int)((float)(width - 24) * hover_t + 0.5f);
        if (line_w > 0)
            fill_round_rect(a, x + (width - line_w) / 2, tab_y + tab_h - 4, line_w, 3, 1,
                            a->colors.accent);
    }

    const char *font = (selected || focused) ? "Sans Bold 10" : "Sans 10";
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, x, tab_y + 14, width, content_label((content_kind_t)kind), font,
                           style.text == a->colors.text ? 0xF6F7FBu : 0xAAB2C4u, 1.0, true);
    else
        draw_centered(a, x, 42, width, content_label((content_kind_t)kind), style.text);
}

/* Draw the TV/Filmes/Séries tabs. */
static void draw_browse_tabs(app_t *a) {
    const int tab_x[3] = {8, 88, 174};
    const int tab_w[3] = {74, 80, 88};
    for (int kind = 0; kind < 3; ++kind)
        draw_browse_tab(a, kind, tab_x[kind], tab_w[kind]);
}

/* Draw the browse search field. */
static void draw_browse_search(app_t *a, int search_w) {
    char search_hint[96];
    snprintf(search_hint, sizeof(search_hint), "Buscar %s...", content_plural(a));
    draw_input(a, SIDEBAR_W + 18, 12, search_w, 46, a->search, search_hint, INPUT_SEARCH, false);
    if (a->hovered_control == HOVER_SEARCH && a->input_focus != INPUT_SEARCH)
        stroke_round_rect(a, SIDEBAR_W + 18, 12, search_w, 46, 13, a->colors.accent);
}

/* Draw the favorites toggle and its count. */
static void draw_browse_favorites(app_t *a, int x, int width) {
    bool focused = a->browse_focus == BROWSE_FOCUS_TOP &&
                   a->browse_top_focus == BROWSE_TOP_FAVORITES;
    bool hovered = a->hovered_control == HOVER_FAVORITES;
    browse_control_style_t style = browse_control_style(a, a->favorites_only, hovered, focused);

    fill_round_rect(a, x, 12, width, 46, 13, style.fill);
    stroke_round_rect(a, x, 12, width, 46, 13, style.border);

    char label[128];
    snprintf(label, sizeof(label), "* Favoritos (%zu)", favorite_count(a));
    const char *font = (a->favorites_only || focused) ? "Sans Bold 10" : "Sans 10";
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, x, 27, width, label, font,
                           style.text == a->colors.text ? 0xF6F7FBu : 0xAAB2C4u, 1.0, true);
    else
        draw_centered(a, x, 42, width, label, style.text);
}

/* Draw the list selector button. */
static void draw_browse_lists(app_t *a, int x, int width) {
    bool focused = a->browse_focus == BROWSE_FOCUS_TOP &&
                   a->browse_top_focus == BROWSE_TOP_LISTS;
    bool hovered = a->hovered_control == HOVER_LISTS;
    browse_control_style_t style = browse_control_style(a, false, hovered, focused);

    fill_round_rect(a, x, 12, width, 46, 13, style.fill);
    stroke_round_rect(a, x, 12, width, 46, 13, style.border);
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, x, 27, width, "Listas", "Sans 10",
                           style.text == a->colors.text ? 0xF6F7FBu : 0xAAB2C4u, 1.0, true);
    else
        draw_centered(a, x, 42, width, "Listas", style.text);
}

/* Draw search, favorites and list controls from left to right. */
static void draw_browse_toolbar(app_t *a) {
    const int list_w = 94, fav_w = 174;
    int list_x = a->width - list_w - 18;
    int fav_x = list_x - fav_w - 10;
    int search_w = fav_x - (SIDEBAR_W + 18) - 10;
    if (search_w < 180)
        search_w = 180;

    draw_browse_search(a, search_w);
    draw_browse_favorites(a, fav_x, fav_w);
    draw_browse_lists(a, list_x, list_w);
}

/* Draw the series navigation row when browsing seasons or episodes. */
static int draw_browse_back_row(app_t *a, int y) {
    if (!a->series_episode_mode)
        return y;

    bool focused = a->browse_focus == BROWSE_FOCUS_SIDEBAR && a->browse_sidebar_focus == -2;
    bool hovered = a->hovered_control == HOVER_BACK;
    fill_round_rect(a, 8, y, SIDEBAR_W - 16, 38, 11,
                    (hovered || focused) ? a->colors.accent2 : a->colors.panel);
    stroke_round_rect(a, 8, y, SIDEBAR_W - 16, 38, 11,
                      focused ? a->colors.text : a->colors.accent);
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, 18, y + 10, SIDEBAR_W - 36, browse_back_label(a), "Sans Bold 10",
                           0xF6F7FBu, 1.0, false);
    else
        draw_text_font(a, a->font_heading, 18, y + 26, browse_back_label(a), a->colors.text);
    return y + 48;
}

/* Draw a selectable sidebar category row. */
static void draw_browse_category_choice(app_t *a, int y, const char *label, bool selected, bool focused,
                                        bool hovered) {
    browse_control_style_t style = browse_control_style(a, selected, hovered, focused);
    fill_round_rect(a, 8, y, SIDEBAR_W - 16, 36, 11, style.fill);
    if (focused)
        stroke_round_rect(a, 8, y, SIDEBAR_W - 16, 36, 11, style.border);

    const char *font = (selected || focused) ? "Sans Bold 9" : "Sans 9";
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, 18, y + 9, SIDEBAR_W - 36, label, font,
                           style.text == a->colors.text ? 0xF6F7FBu : 0xAAB2C4u, 1.0, false);
    else
        draw_text(a, 18, y + 24, label, style.text);
}

/* Draw the synthetic "all content" category row. */
static int draw_browse_all_row(app_t *a, int y) {
    char label[128];
    snprintf(label, sizeof(label), "%s (%zu)", all_content_label(a), ACTIVE_CHANNELS(a).len);
    bool focused = a->browse_focus == BROWSE_FOCUS_SIDEBAR && a->browse_sidebar_focus == -1;
    draw_browse_category_choice(a, y, label, a->selected_category < 0, focused,
                                a->hovered_control == HOVER_CATEGORY_ALL);
    return y + 42;
}

/* Draw one real provider category row. */
static void draw_browse_category_row(app_t *a, int idx, int y) {
    char full_label[512];
    char label[256];
    size_t count = a->category_counts ? a->category_counts[idx] : 0;
    snprintf(full_label, sizeof(full_label), "%s (%zu)", ACTIVE_CATEGORIES(a).items[idx].name, count);
    bounded_text(label, sizeof(label), full_label, 34);

    bool focused = a->browse_focus == BROWSE_FOCUS_SIDEBAR &&
                   a->browse_sidebar_focus == idx;
    draw_browse_category_choice(a, y, label, a->selected_category == idx, focused,
                                a->hovered_control == HOVER_CATEGORY_BASE + idx);
}

/* Draw the optional back row plus the visible category rows. */
static void draw_browse_sidebar(app_t *a) {
    int y = draw_browse_back_row(a, TOPBAR_H + 12);
    y = draw_browse_all_row(a, y);

    int rows = category_visible_rows(a) - 1;
    for (int row = 0; row < rows; ++row) {
        int idx = a->category_scroll + row;
        if (idx < 0 || (size_t)idx >= ACTIVE_CATEGORIES(a).len)
            break;
        draw_browse_category_row(a, idx, y);
        y += 42;
    }
}

/* Draw the card body and active/hover chrome before artwork content. */
static void draw_browse_card_shell(app_t *a, const card_layout_t *layout, int cx, int cy,
                                   bool active_card, float hover_eased) {
    if (a->renderer.active && active_card) {
        vip_ui_render_round_rect(&a->renderer, cx + 5, cy + 8 + (int)(3.0f * hover_eased),
                                 layout->card_w, layout->card_h, 18, 0x000000u, 0.62);
        vip_ui_render_round_stroke(&a->renderer, cx - 3, cy - 3, layout->card_w + 6,
                                   layout->card_h + 6, 19, 0xFF5F2Eu, 0.80, 2.0);
    } else {
        fill_round_rect(a, cx + 4, cy + 6 + (int)(3.0f * hover_eased),
                        layout->card_w, layout->card_h, 16, a->colors.black);
    }

    fill_round_rect(a, cx, cy, layout->card_w, layout->card_h, 16, a->colors.panel2);
    if (active_card) {
        stroke_round_rect(a, cx - 3, cy - 3, layout->card_w + 6,
                          layout->card_h + 6, 18, a->colors.accent);
        stroke_round_rect(a, cx - 1, cy - 1, layout->card_w + 2,
                          layout->card_h + 2, 19, a->colors.text);
    }
    fill_round_rect(a, cx, cy, layout->card_w, layout->art_h, 18, a->colors.black);
}

/* Load cached artwork or enqueue the same thumbnail request as before. */
static void draw_browse_card_image(app_t *a, vip_channel_t *channel, const card_layout_t *layout,
                                   int cx, int cy, int row_offset, int col, bool active_card) {
    vip_error_t error = {0};
    char *path = channel->logo_url && channel->logo_url[0]
                     ? vip_thumbnail_artwork_cache_path(a->cache_dir, channel->provider_id,
                                                        channel->id, channel->logo_url, &error)
                     : vip_thumbnail_cache_path(a->cache_dir, channel->provider_id, channel->id,
                                                &error);
    bool image_ok = path &&
                    draw_cached_image_contain(a, path, cx, cy, layout->card_w, layout->art_h);
    bool can_load_image =
        (channel->logo_url && channel->logo_url[0]) ||
        strncmp(channel->stream_url ? channel->stream_url : "", "series://", 9u) != 0;

    if (!image_ok) {
        const char *placeholder = can_load_image ? "carregando imagem..." : "sem capa";
        if (a->renderer.active)
            vip_ui_render_text(&a->renderer, cx + 8, cy + layout->art_h / 2 - 7,
                               layout->card_w - 16, placeholder, "Sans 9", 0xAAB2C4u,
                               1.0, true);
        else
            draw_centered(a, cx, cy + layout->art_h / 2 + 5, layout->card_w,
                          placeholder, a->colors.muted);

        if (can_load_image) {
            int distance = row_offset >= 0 ? row_offset : -row_offset;
            int64_t priority = 1000000LL - (int64_t)distance * 1000LL - col;
            enqueue_thumbnail(a, channel, priority);
        }
    }
    free(path);

    stroke_round_rect(a, cx, cy, layout->card_w, layout->art_h, 18,
                      active_card ? a->colors.accent : a->colors.border);
}

/* Draw the hover-only action over the artwork. */
static void draw_browse_card_open_action(app_t *a, const card_layout_t *layout, int cx, int cy,
                                         bool hovered, float hover_eased) {
    if (!hovered || hover_eased <= 0.30f)
        return;

    const int open_w = 78, open_h = 30;
    int open_x = cx + 8;
    int open_y = cy + layout->art_h - open_h - 8;
    fill_round_rect(a, open_x, open_y, open_w, open_h, 11, a->colors.accent2);
    stroke_round_rect(a, open_x, open_y, open_w, open_h, 11, a->colors.accent);
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, open_x, open_y + 7, open_w, "ABRIR",
                           "Sans Bold 8", 0xF6F7FBu, 1.0, true);
    else
        draw_centered_font(a, a->font_small, open_x, open_y + 20, open_w, "ABRIR",
                           a->colors.text);
}

/* Draw the favorite badge and return its bottom edge for stacked badges. */
static int draw_browse_card_favorite_badge(app_t *a, size_t chidx, const card_layout_t *layout,
                                           int cx, int cy) {
    bool favorite = a->favorite_flags && a->favorite_flags[chidx];
    const int width = 58, height = 28;
    int x = cx + layout->card_w - width - 6;
    int y = cy + 6;

    fill_round_rect(a, x, y, width, height, 10,
                    favorite ? a->colors.accent2 : a->colors.panel);
    stroke_round_rect(a, x, y, width, height, 10,
                      favorite ? a->colors.accent : a->colors.border);
    draw_centered(a, x, y + 19, width, favorite ? "SALVO" : "FAV",
                  favorite ? a->colors.text : a->colors.muted);
    return y + height;
}

/* Draw the optional DUAL/LEG variant badge below the favorite badge. */
static void draw_browse_card_variant_badge(app_t *a, const vip_channel_t *channel,
                                           const card_layout_t *layout, int cx, int favorite_bottom) {
    const char *label = media_variant_label(channel->name);
    if (!label)
        return;

    int width = strcmp(label, "DUAL") == 0 ? 62 : 50;
    int x = cx + layout->card_w - width - 6;
    int y = favorite_bottom + 6;
    fill_round_rect(a, x, y, width, 24, 9, a->colors.panel);
    stroke_round_rect(a, x, y, width, 24, 9, a->colors.accent);
    draw_centered_font(a, a->font_small, x, y + 17, width, label, a->colors.text);
}

/* Draw artwork, card chrome and artwork-local overlays. */
static void draw_browse_card_artwork(app_t *a, vip_channel_t *channel, size_t chidx,
                                     const card_layout_t *layout, int cx, int cy, int row_offset,
                                     int col, bool active_card, bool hovered, float hover_eased) {
    draw_browse_card_shell(a, layout, cx, cy, active_card, hover_eased);
    draw_browse_card_image(a, channel, layout, cx, cy, row_offset, col, active_card);
    draw_browse_card_open_action(a, layout, cx, cy, hovered, hover_eased);
    int favorite_bottom = draw_browse_card_favorite_badge(a, chidx, layout, cx, cy);
    draw_browse_card_variant_badge(a, channel, layout, cx, favorite_bottom);
}

/* Convert persisted progress to the clamped ratio used by the progress bar. */
static double browse_progress_ratio(const vip_watch_progress_t *progress) {
    if (progress->completed)
        return 1.0;

    double ratio = progress->position_seconds / progress->duration_seconds;
    if (ratio < 0.0)
        return 0.0;
    if (ratio > 1.0)
        return 1.0;
    return ratio;
}

/* Draw the completion/percentage badge that sits over card artwork. */
static void draw_browse_progress_badge(app_t *a, const vip_watch_progress_t *progress,
                                       double ratio, int cx, int cy) {
    if (progress->completed) {
        const int badge_w = 92;
        fill_rect(a, cx + 6, cy + 6, (unsigned)badge_w, 26, a->colors.accent2);
        stroke_rect(a, cx + 6, cy + 6, (unsigned)badge_w, 26, a->colors.accent);
        draw_centered(a, cx + 6, cy + 24, badge_w, "ASSISTIDO", a->colors.text);
        return;
    }
    if (progress->duration_seconds <= 1.0 || progress->position_seconds <= 3.0)
        return;

    char badge[32];
    int percent = (int)(ratio * 100.0 + 0.5);
    snprintf(badge, sizeof(badge), "%d%%", percent);
    fill_rect(a, cx + 6, cy + 6, 54, 26, a->colors.panel2);
    stroke_rect(a, cx + 6, cy + 6, 54, 26, a->colors.accent);
    draw_centered(a, cx + 6, cy + 24, 54, badge, a->colors.text);
}

/* Draw persisted playback progress without coupling it to artwork loading. */
static void draw_browse_card_progress(app_t *a, size_t chidx, const card_layout_t *layout, int cx, int cy) {
    if (!a->progress_flags)
        return;
    if (a->content_kind != CONTENT_VOD &&
        (!a->series_episode_mode || a->series_season_select))
        return;

    vip_watch_progress_t *progress = &a->progress_flags[chidx];
    if (progress->duration_seconds <= 1.0 && !progress->completed)
        return;

    double ratio = browse_progress_ratio(progress);
    int progress_w = (int)((double)layout->card_w * ratio);
    fill_rect(a, cx, cy + layout->art_h - 5, (unsigned)layout->card_w, 5, a->colors.panel2);
    if (progress_w > 0)
        fill_rect(a, cx, cy + layout->art_h - 5, (unsigned)progress_w, 5, a->colors.accent);

    draw_browse_progress_badge(a, progress, ratio, cx, cy);
}

/* Count episodes belonging to the currently represented season. */
static size_t browse_season_episode_count(const app_t *a, const vip_channel_t *channel) {
    size_t count = 0u;
    for (size_t i = 0; i < a->episode_channels.len; ++i) {
        const char *category_id = a->episode_channels.items[i].category_id;
        if (category_id && channel->category_id &&
            strcmp(category_id, channel->category_id) == 0)
            ++count;
    }
    return count;
}

/* Produce the secondary card line for the current series browsing mode. */
static bool browse_card_metadata_text(app_t *a, vip_channel_t *channel, size_t chidx,
                                      char *text, size_t text_size) {
    if (a->series_season_select) {
        size_t count = browse_season_episode_count(a, channel);
        snprintf(text, text_size, "%zu episódio%s", count, count == 1u ? "" : "s");
        return true;
    }
    if (a->series_episode_mode)
        return episode_card_meta(a, channel, text, text_size);

    if (a->content_kind != CONTENT_SERIES || !a->series_watched ||
        !a->series_total || a->series_total[chidx] <= 0)
        return false;

    snprintf(text, text_size, "%d/%d episódios", a->series_watched[chidx],
             a->series_total[chidx]);
    return true;
}

/* Draw the secondary line under a card title for the active browse mode. */
static void draw_browse_card_metadata(app_t *a, vip_channel_t *channel, size_t chidx,
                                      const card_layout_t *layout, int cx, int cy) {
    char text[80];
    if (!browse_card_metadata_text(a, channel, chidx, text, sizeof(text)))
        return;

    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, cx + 9, cy + layout->art_h + 43,
                           layout->card_w - 18, text, "Sans 8", 0xAAB2C4u, 1.0, false);
    else
        draw_text_font(a, a->font_small, cx + 8, cy + layout->art_h + 56, text,
                       a->colors.muted);
}

/* Draw one visible card. All per-card state is derived here so the grid loop
 * only decides which cards are visible and where they belong. */
static void draw_browse_card(app_t *a, const card_layout_t *layout, int content_x, int row_y, int rr, int col,
                             size_t fidx) {
    size_t chidx = a->filtered[fidx];
    vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[chidx];
    int cx = content_x + col * (layout->card_w + GRID_GAP);
    bool focused = a->browse_focus == BROWSE_FOCUS_GRID && fidx == a->focused_filtered;
    bool hovered = a->hovered_card_valid && fidx == a->hovered_filtered && a->hover_motion.value > 0.001f;
    float hover_eased = hovered ? vip_ui_ease_out_cubic(a->hover_motion.value) : 0.0f;
    int cy = row_y - (int)(8.0f * hover_eased + 0.5f);
    bool active_card = focused || hovered;

    draw_browse_card_artwork(a, ch, chidx, layout, cx, cy, rr, col, active_card, hovered, hover_eased);
    draw_browse_card_progress(a, chidx, layout, cx, cy);

    char grouped_title[256];
    const char *display_title = m3u_series_display_name(a, ch, grouped_title, sizeof(grouped_title));
    draw_card_title(a, cx + 9, cy + layout->art_h + 9, layout->card_w - 18, display_title, active_card);
    draw_browse_card_metadata(a, ch, chidx, layout, cx, cy);
}

/* Draw the visible grid and keep the X11 clip limited to the content area. */
static void draw_browse_grid(app_t *a, const card_layout_t *layout, int content_x, int content_y) {
    int grid_right = a->width - 18;
    if (details_panel_active(a)) {
        int px, py, pw, ph;
        details_panel_geometry(a, &px, &py, &pw, &ph);
        (void)py;
        (void)pw;
        (void)ph;
        grid_right = px - 12;
    }

    if (grid_right > content_x && a->height > content_y) {
        XRectangle grid_clip = {
            .x = (short)content_x,
            .y = (short)content_y,
            .width = (unsigned short)(grid_right - content_x),
            .height = (unsigned short)(a->height - content_y),
        };
        XSetClipRectangles(a->dpy, a->gc, 0, 0, &grid_clip, 1, Unsorted);
    }

    int first_row = a->grid_scroll / layout->row_step;
    int y_offset = -(a->grid_scroll % layout->row_step);
    int visible_rows = (a->height - content_y) / layout->row_step + 3;
    for (int rr = 0; rr < visible_rows; ++rr) {
        int row = first_row + rr;
        int row_y = content_y + y_offset + rr * layout->row_step;
        if (row_y > a->height || row_y + layout->card_h < content_y)
            continue;

        for (int col = 0; col < layout->cols; ++col) {
            size_t fidx = (size_t)row * (size_t)layout->cols + (size_t)col;
            if (fidx >= a->filtered_len)
                break;
            draw_browse_card(a, layout, content_x, row_y, rr, col, fidx);
        }
    }

    XSetClipMask(a->dpy, a->gc, None);
}

/* Draw the non-blocking series loading notice above the grid. */
static void draw_browse_loading(app_t *a, int content_x, int content_y, int avail_w) {
    if (!atomic_load(&a->series_running))
        return;

    char loading[512];
    pthread_mutex_lock(&a->data_mutex);
    snprintf(loading, sizeof(loading), "%s", a->status);
    pthread_mutex_unlock(&a->data_mutex);

    int box_w = avail_w > 680 ? 680 : avail_w;
    int box_x = content_x + (avail_w - box_w) / 2;
    int box_y = content_y + 20;
    fill_round_rect(a, box_x + 3, box_y + 4, box_w, 56, 16, a->colors.black);
    fill_round_rect(a, box_x, box_y, box_w, 56, 16, a->colors.panel2);
    stroke_round_rect(a, box_x, box_y, box_w, 56, 16, a->colors.accent);
    draw_centered(a, box_x, box_y + 35, box_w, loading, a->colors.text);
}

/* Draw browse. */
static void draw_browse(app_t *a) {
    draw_browse_background(a);
    draw_browse_tabs(a);
    draw_browse_toolbar(a);
    draw_browse_sidebar(a);

    card_layout_t layout = browse_layout(a);
    int content_x = SIDEBAR_W + 20;
    int content_y = TOPBAR_H + 18;
    int avail_w = a->width - content_x - 18;

    if (a->filtered_len == 0) {
        char empty[160];
        snprintf(empty, sizeof(empty), "Nenhum %s nesta seleção", content_plural(a));
        draw_text(a, content_x, content_y + 32, empty, a->colors.muted);
        return;
    }

    draw_browse_grid(a, &layout, content_x, content_y);
    draw_details_panel(a);
    draw_browse_loading(a, content_x, content_y, avail_w);
    draw_toast(a);
}



/* Draw player. */
static void draw_player_header(app_t *a) {
    if (a->fullscreen)
        return;
    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, 0, 0, a->width, PLAYER_HEADER_H, 0, 0x111722u, 0.97);
        vip_ui_render_round_rect(&a->renderer, 12, 10, 132, 44, 13, 0x171C28u, 1.0);
        vip_ui_render_round_stroke(&a->renderer, 12, 10, 132, 44, 13, 0x343A48u, 1.0, 1.0);
        vip_ui_render_text(&a->renderer, 28, 24, 104, "<  Voltar", "Sans SemiBold 10", 0xF6F7FBu, 1.0,
                           false);
        if (a->current_channel < ACTIVE_CHANNELS(a).len)
            vip_ui_render_text(&a->renderer, 168, 22, a->width - 190,
                               ACTIVE_CHANNELS(a).items[a->current_channel].name, "Sans Bold 12",
                               0xF6F7FBu, 1.0, false);
        return;
    }
    fill_rect(a, 0, 0, (unsigned)a->width, PLAYER_HEADER_H, a->colors.panel);
    fill_round_rect(a, 12, 10, 132, 44, 13, a->colors.panel2);
    stroke_round_rect(a, 12, 10, 132, 44, 13, a->colors.border);
    draw_text(a, 28, 39, "< Voltar", a->colors.text);
    if (a->current_channel < ACTIVE_CHANNELS(a).len)
        draw_text_font(a, a->font_heading, 168, 39, ACTIVE_CHANNELS(a).items[a->current_channel].name,
                       a->colors.text);
}

static void draw_player_control_buttons(app_t *a, const vip_mpv_player_snapshot_t *snap, int y) {
    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, 10, y + 7, a->width - 20, PLAYER_CONTROLS_H - 12, 18,
                                 0x111722u, 0.96);
        vip_ui_render_round_stroke(&a->renderer, 10, y + 7, a->width - 20, PLAYER_CONTROLS_H - 12, 18,
                                   0x343A48u, 0.95, 1.0);
        vip_ui_render_round_rect(&a->renderer, 16, y + 18, 52, 46, 14, 0x171C28u, 1.0);
        vip_ui_render_round_stroke(&a->renderer, 16, y + 18, 52, 46, 14, 0x343A48u, 1.0, 1.0);
        vip_ui_render_text(&a->renderer, 16, y + 31, 52, snap->paused ? ">" : "||", "Sans Bold 12",
                           0xF6F7FBu, 1.0, true);
        vip_ui_render_round_rect(&a->renderer, 76, y + 18, 82, 46, 14, 0x171C28u, 1.0);
        vip_ui_render_round_stroke(&a->renderer, 76, y + 18, 82, 46, 14, 0x343A48u, 1.0, 1.0);
        vip_ui_render_text(&a->renderer, 76, y + 32, 82, "Voltar", "Sans SemiBold 9", 0xF6F7FBu, 1.0,
                           true);
        return;
    }
    fill_rect(a, 0, y, (unsigned)a->width, PLAYER_CONTROLS_H, a->colors.panel);
    fill_rect(a, 0, y, (unsigned)a->width, 1, a->colors.border);
    fill_round_rect(a, 16, y + 18, 52, 46, 14, a->colors.panel2);
    stroke_round_rect(a, 16, y + 18, 52, 46, 14, a->colors.border);
    draw_centered_font(a, a->font_heading, 16, y + 48, 52, snap->paused ? ">" : "||", a->colors.text);
    fill_round_rect(a, 76, y + 18, 82, 46, 14, a->colors.panel2);
    stroke_round_rect(a, 76, y + 18, 82, 46, 14, a->colors.border);
    draw_centered(a, 76, y + 47, 82, "Voltar", a->colors.text);
}

static void draw_live_player_controls(app_t *a, int y) {
    if (a->renderer.active) {
        vip_ui_render_round_rect(&a->renderer, 176, y + 22, 76, 30, 12, 0xFF7185u, 0.96);
        vip_ui_render_text(&a->renderer, 176, y + 30, 76, "AO VIVO", "Sans Bold 8", 0xF6F7FBu, 1.0, true);
        vip_ui_render_text(&a->renderer, 270, y + 31, 220, "<-  ->  troca canal", "Sans 9", 0xAAB2C4u,
                           1.0, false);
        return;
    }
    fill_round_rect(a, 176, y + 22, 76, 30, 12, a->colors.danger);
    draw_centered(a, 176, y + 43, 76, "AO VIVO", a->colors.text);
    draw_text(a, 270, y + 44, "<- -> troca canal", a->colors.muted);
}

static void draw_vod_timeline(app_t *a, const vip_mpv_player_snapshot_t *snap, int y) {
    int tx, ty, tw, th;
    timeline_geometry(a, &tx, &ty, &tw, &th);
    if (a->renderer.active)
        vip_ui_render_round_rect(&a->renderer, tx, ty, tw, th, th / 2, 0x343A48u, 1.0);
    else
        fill_round_rect(a, tx, ty, tw, th, th / 2, a->colors.panel2);

    double ratio = snap->duration_seconds > 0.0 ? snap->position_seconds / snap->duration_seconds : 0.0;
    if (ratio < 0.0)
        ratio = 0.0;
    if (ratio > 1.0)
        ratio = 1.0;
    int fill = (int)((double)tw * ratio);
    if (fill > 0) {
        if (a->renderer.active)
            vip_ui_render_round_rect(&a->renderer, tx, ty, fill, th, th / 2, 0xFF5F2Eu, 1.0);
        else
            fill_round_rect(a, tx, ty, fill, th, th / 2, a->colors.accent);
    }

    char pos[32], dur[32];
    format_clock(snap->position_seconds, pos);
    format_clock(snap->duration_seconds, dur);
    if (a->renderer.active) {
        vip_ui_render_text(&a->renderer, 166, y + 31, 60, pos, "Sans SemiBold 9", 0xF6F7FBu, 1.0, false);
        vip_ui_render_text(&a->renderer, a->width - 150, y + 31, 132, dur, "Sans SemiBold 9",
                           0xF6F7FBu, 1.0, false);
        vip_ui_render_text(&a->renderer, tx, y + 57, tw, "Clique/arraste para buscar  ·  <- -> 10s",
                           "Sans 8", 0xAAB2C4u, 1.0, false);
        return;
    }
    draw_text(a, 166, y + 45, pos, a->colors.text);
    draw_text(a, a->width - 150, y + 45, dur, a->colors.text);
    draw_text_font(a, a->font_small, tx, y + 70, "Clique/arraste para buscar  ·  <- -> 10s",
                   a->colors.muted);
}

static void draw_player_state_label(app_t *a, const char *state_text, vip_player_state_t state, int y) {
    if (a->renderer.active) {
        int sw = vip_ui_render_text_width(&a->renderer, state_text, "Sans 8");
        vip_ui_render_text(&a->renderer, a->width - sw - 18, y + 58, sw + 2, state_text, "Sans 8",
                           state == VIP_PLAYER_ERROR ? 0xFF7185u : 0xAAB2C4u, 1.0, false);
        return;
    }
    int sw = text_width(a, state_text);
    draw_text_font(a, a->font_small, a->width - sw - 18, y + 72, state_text,
                   state == VIP_PLAYER_ERROR ? a->colors.danger : a->colors.muted);
}

static void draw_player_status_center(app_t *a, vip_player_state_t state, const char *state_text) {
    if (state == VIP_PLAYER_ERROR) {
        const char *err = a->player ? vip_mpv_player_last_error(a->player) : a->player_status;
        char bounded[220];
        bounded_text(bounded, sizeof(bounded), err && err[0] ? err : "Falha desconhecida no stream", 90);
        draw_centered_font(a, a->font_heading, 0, a->height / 2 - 12, a->width, "Não foi possível reproduzir",
                           a->colors.danger);
        draw_centered(a, 20, a->height / 2 + 22, a->width - 40, bounded, a->colors.muted);
        draw_centered(a, 0, a->height / 2 + 56, a->width, "Pressione Esc para voltar", a->colors.muted);
    } else if (!a->video_mapped) {
        draw_centered_font(a, a->font_heading, 0, a->height / 2, a->width, state_text, a->colors.muted);
    }
}

static void draw_player(app_t *a) {
    fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.black);
    vip_mpv_player_snapshot_t snap = {0};
    if (a->player)
        vip_mpv_player_snapshot(a->player, &snap);
    vip_player_state_t state = a->player ? snap.state : VIP_PLAYER_ERROR;
    const char *state_text = a->player ? vip_mpv_player_state_name(state) : a->player_status;

    draw_player_header(a);
    if (player_hud_visible(a)) {
        int y = a->height - PLAYER_CONTROLS_H;
        draw_player_control_buttons(a, &snap, y);
        if (a->player_item_live)
            draw_live_player_controls(a, y);
        else
            draw_vod_timeline(a, &snap, y);
        draw_player_state_label(a, state_text, state, y);
    }
    draw_player_status_center(a, state, state_text);
}

/* Ensure backbuffer. */
static bool ensure_backbuffer(app_t *a) {
    if (a->width <= 0 || a->height <= 0)
        return false;
    if (a->backbuffer && a->backbuffer_w == a->width && a->backbuffer_h == a->height)
        return true;
    if (a->backbuffer) {
        XFreePixmap(a->dpy, a->backbuffer);
        a->backbuffer = 0;
    }
    a->backbuffer =
        XCreatePixmap(a->dpy, a->win, (unsigned)a->width, (unsigned)a->height, (unsigned)a->depth);
    if (!a->backbuffer)
        return false;
    a->backbuffer_w = a->width;
    a->backbuffer_h = a->height;
    return true;
}

/* Handle the redraw operation. */
void redraw(app_t *a) {
    bool buffered = ensure_backbuffer(a);
    a->draw = buffered ? a->backbuffer : a->win;
    (void)vip_ui_renderer_begin(&a->renderer, a->dpy, a->draw, a->visual, a->width, a->height);
    if (a->screen == SCREEN_LOGIN)
        draw_login(a);
    else if (a->screen == SCREEN_BROWSE)
        draw_browse(a);
    else
        draw_player(a);
    vip_ui_renderer_end(&a->renderer);
    if (buffered) {
        XCopyArea(a->dpy, a->backbuffer, a->win, a->gc, 0, 0, (unsigned)a->width, (unsigned)a->height, 0, 0);
    }
    a->draw = a->win;
    XFlush(a->dpy);
}

/* Handle the choose category operation. */

void image_cache_clear(app_t *a) {
    if (!a)
        return;
    for (size_t i = 0; i < CACHE_SLOTS; ++i)
        image_slot_clear(&a->image_cache[i]);
}
