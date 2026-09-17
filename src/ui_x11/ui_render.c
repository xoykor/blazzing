/* SPDX-License-Identifier: MIT */
#include "visual_iptv/ui_render.h"

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>
#include <stddef.h>
#include <string.h>

static cairo_surface_t *surface_of(vip_ui_renderer_t *renderer) {
    return renderer ? (cairo_surface_t *)renderer->surface : NULL;
}

static cairo_t *context_of(vip_ui_renderer_t *renderer) {
    return renderer ? (cairo_t *)renderer->context : NULL;
}

static PangoLayout *layout_of(vip_ui_renderer_t *renderer) {
    return renderer ? (PangoLayout *)renderer->layout : NULL;
}

static double clamp_alpha(double alpha) {
    if (alpha < 0.0) return 0.0;
    if (alpha > 1.0) return 1.0;
    return alpha;
}

static void set_rgb(cairo_t *cr, uint32_t rgb, double alpha) {
    double r = (double)((rgb >> 16) & 0xffu) / 255.0;
    double g = (double)((rgb >> 8) & 0xffu) / 255.0;
    double b = (double)(rgb & 0xffu) / 255.0;
    cairo_set_source_rgba(cr, r, g, b, clamp_alpha(alpha));
}

static void rounded_path(cairo_t *cr, double x, double y,
                         double w, double h, double radius) {
    if (w <= 0.0 || h <= 0.0) return;
    double r = radius;
    if (r < 0.0) r = 0.0;
    if (r > w * 0.5) r = w * 0.5;
    if (r > h * 0.5) r = h * 0.5;
    const double pi = 3.14159265358979323846;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -pi * 0.5, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0, pi * 0.5);
    cairo_arc(cr, x + r, y + h - r, r, pi * 0.5, pi);
    cairo_arc(cr, x + r, y + r, r, pi, pi * 1.5);
    cairo_close_path(cr);
}

static void sync_external_draw(vip_ui_renderer_t *renderer) {
    cairo_surface_t *surface = surface_of(renderer);
    if (surface) cairo_surface_mark_dirty(surface);
}

static PangoFontDescription *set_layout_font(vip_ui_renderer_t *renderer,
                                              const char *text,
                                              const char *font) {
    PangoLayout *layout = layout_of(renderer);
    if (!layout) return NULL;
    PangoFontDescription *desc = pango_font_description_from_string(font && font[0] ? font : "Sans 11");
    if (!desc) return NULL;
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text ? text : "", -1);
    return desc;
}

bool vip_ui_renderer_begin(vip_ui_renderer_t *renderer,
                           Display *display,
                           Drawable drawable,
                           Visual *visual,
                           int width,
                           int height) {
    if (!renderer || !display || !drawable || !visual || width <= 0 || height <= 0)
        return false;
    memset(renderer, 0, sizeof(*renderer));
    cairo_surface_t *surface = cairo_xlib_surface_create(display, drawable, visual, width, height);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) cairo_surface_destroy(surface);
        return false;
    }
    cairo_t *cr = cairo_create(surface);
    if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        if (cr) cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return false;
    }
    PangoLayout *layout = pango_cairo_create_layout(cr);
    if (!layout) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        return false;
    }
    renderer->display = display;
    renderer->drawable = drawable;
    renderer->visual = visual;
    renderer->width = width;
    renderer->height = height;
    renderer->surface = surface;
    renderer->context = cr;
    renderer->layout = layout;
    renderer->active = true;
    return true;
}

void vip_ui_renderer_flush(vip_ui_renderer_t *renderer) {
    if (!renderer || !renderer->active) return;
    cairo_surface_flush(surface_of(renderer));
}

void vip_ui_renderer_end(vip_ui_renderer_t *renderer) {
    if (!renderer) return;
    if (renderer->layout) g_object_unref(layout_of(renderer));
    if (renderer->context) cairo_destroy(context_of(renderer));
    if (renderer->surface) cairo_surface_destroy(surface_of(renderer));
    memset(renderer, 0, sizeof(*renderer));
}

void vip_ui_render_linear_gradient(vip_ui_renderer_t *renderer,
                                   int x, int y, int w, int h,
                                   uint32_t top_rgb,
                                   uint32_t bottom_rgb) {
    if (!renderer || !renderer->active || w <= 0 || h <= 0) return;
    sync_external_draw(renderer);
    cairo_t *cr = context_of(renderer);
    cairo_pattern_t *gradient = cairo_pattern_create_linear(0.0, (double)y, 0.0, (double)(y + h));
    double tr = (double)((top_rgb >> 16) & 0xffu) / 255.0;
    double tg = (double)((top_rgb >> 8) & 0xffu) / 255.0;
    double tb = (double)(top_rgb & 0xffu) / 255.0;
    double br = (double)((bottom_rgb >> 16) & 0xffu) / 255.0;
    double bg = (double)((bottom_rgb >> 8) & 0xffu) / 255.0;
    double bb = (double)(bottom_rgb & 0xffu) / 255.0;
    cairo_pattern_add_color_stop_rgb(gradient, 0.0, tr, tg, tb);
    cairo_pattern_add_color_stop_rgb(gradient, 1.0, br, bg, bb);
    cairo_rectangle(cr, x, y, w, h);
    cairo_set_source(cr, gradient);
    cairo_fill(cr);
    cairo_pattern_destroy(gradient);
    cairo_surface_flush(surface_of(renderer));
}

void vip_ui_render_round_rect(vip_ui_renderer_t *renderer,
                              int x, int y, int w, int h, int radius,
                              uint32_t rgb, double alpha) {
    if (!renderer || !renderer->active || w <= 0 || h <= 0) return;
    sync_external_draw(renderer);
    cairo_t *cr = context_of(renderer);
    rounded_path(cr, x, y, w, h, radius);
    set_rgb(cr, rgb, alpha);
    cairo_fill(cr);
    cairo_surface_flush(surface_of(renderer));
}

void vip_ui_render_round_stroke(vip_ui_renderer_t *renderer,
                                int x, int y, int w, int h, int radius,
                                uint32_t rgb, double alpha, double line_width) {
    if (!renderer || !renderer->active || w <= 0 || h <= 0 || line_width <= 0.0) return;
    sync_external_draw(renderer);
    cairo_t *cr = context_of(renderer);
    rounded_path(cr, x + line_width * 0.5, y + line_width * 0.5,
                 w - line_width, h - line_width, radius);
    set_rgb(cr, rgb, alpha);
    cairo_set_line_width(cr, line_width);
    cairo_stroke(cr);
    cairo_surface_flush(surface_of(renderer));
}

void vip_ui_render_text(vip_ui_renderer_t *renderer,
                        int x, int y, int width,
                        const char *text,
                        const char *font,
                        uint32_t rgb,
                        double alpha,
                        bool centered) {
    if (!renderer || !renderer->active || !text || !text[0] || width <= 0) return;
    sync_external_draw(renderer);
    cairo_t *cr = context_of(renderer);
    PangoLayout *layout = layout_of(renderer);
    PangoFontDescription *desc = set_layout_font(renderer, text, font);
    if (!desc) return;
    pango_layout_set_width(layout, width * PANGO_SCALE);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    pango_layout_set_alignment(layout, centered ? PANGO_ALIGN_CENTER : PANGO_ALIGN_LEFT);
    set_rgb(cr, rgb, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    cairo_surface_flush(surface_of(renderer));
    pango_font_description_free(desc);
}

int vip_ui_render_text_width(vip_ui_renderer_t *renderer,
                             const char *text,
                             const char *font) {
    if (!renderer || !renderer->active || !text || !text[0]) return 0;
    PangoLayout *layout = layout_of(renderer);
    PangoFontDescription *desc = set_layout_font(renderer, text, font);
    if (!desc) return 0;
    pango_layout_set_width(layout, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    pango_layout_set_single_paragraph_mode(layout, TRUE);
    int width = 0;
    int height = 0;
    pango_layout_get_pixel_size(layout, &width, &height);
    (void)height;
    pango_font_description_free(desc);
    return width;
}
