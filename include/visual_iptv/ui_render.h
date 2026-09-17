/* SPDX-License-Identifier: MIT */
/*
 * Public API contract for ui render.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#ifndef VISUAL_IPTV_UI_RENDER_H
#define VISUAL_IPTV_UI_RENDER_H

#include <X11/Xlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    Display *display;
    Drawable drawable;
    Visual *visual;
    int width;
    int height;
    void *surface;
    void *context;
    void *layout;
    bool active;
} vip_ui_renderer_t;

/* Handle the ui renderer begin operation. */
bool vip_ui_renderer_begin(vip_ui_renderer_t *renderer, Display *display, Drawable drawable, Visual *visual,
                           int width, int height);
/* Handle the ui renderer end operation. */
void vip_ui_renderer_end(vip_ui_renderer_t *renderer);
/* Handle the ui renderer flush operation. */
void vip_ui_renderer_flush(vip_ui_renderer_t *renderer);

/* Render linear gradient in the UI. */
void vip_ui_render_linear_gradient(vip_ui_renderer_t *renderer, int x, int y, int w, int h, uint32_t top_rgb,
                                   uint32_t bottom_rgb);
/* Render round rect in the UI. */
void vip_ui_render_round_rect(vip_ui_renderer_t *renderer, int x, int y, int w, int h, int radius,
                              uint32_t rgb, double alpha);
/* Render round stroke in the UI. */
void vip_ui_render_round_stroke(vip_ui_renderer_t *renderer, int x, int y, int w, int h, int radius,
                                uint32_t rgb, double alpha, double line_width);
/* Render text in the UI. */
void vip_ui_render_text(vip_ui_renderer_t *renderer, int x, int y, int width, const char *text,
                        const char *font, uint32_t rgb, double alpha, bool centered);
/* Render text width in the UI. */
int vip_ui_render_text_width(vip_ui_renderer_t *renderer, const char *text, const char *font);

#endif
