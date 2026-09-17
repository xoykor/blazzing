/* SPDX-License-Identifier: MIT */
/*
 * Startup hub window used to choose the native IPTV or Pluto TV experience.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/hub.h"
#include "visual_iptv/ui_render.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Run the requested state in the x11 app. */
int vip_x11_app_run(void);

typedef enum { HUB_ACTION_NONE = 0, HUB_ACTION_IPTV, HUB_ACTION_PLUTO, HUB_ACTION_QUIT } hub_action_t;

typedef struct {
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    XFontStruct *font;
    XFontStruct *title_font;
    XFontStruct *heading_font;
    vip_ui_renderer_t renderer;
    Atom wm_delete;
    int width;
    int height;
    int selected;
    unsigned long bg;
    unsigned long panel;
    unsigned long panel2;
    unsigned long border;
    unsigned long text;
    unsigned long muted;
    unsigned long accent;
    unsigned long accent2;
    unsigned long shadow;
} hub_window_t;

/* Resolve a named X11 color for the startup hub. */
static unsigned long hub_color(hub_window_t *h, const char *name) {
    XColor exact = {0};
    XColor screen = {0};
    if (XAllocNamedColor(h->dpy, DefaultColormap(h->dpy, h->screen), name, &screen, &exact))
        return screen.pixel;
    return BlackPixel(h->dpy, h->screen);
}

/* Initialize colors in the startup hub. */
static void hub_init_colors(hub_window_t *h) {
    h->bg = hub_color(h, "#070A12");
    h->panel = hub_color(h, "#0E1420");
    h->panel2 = hub_color(h, "#151E2D");
    h->border = hub_color(h, "#2B3950");
    h->text = hub_color(h, "#F6F8FC");
    h->muted = hub_color(h, "#91A0B7");
    h->accent = hub_color(h, "#62A9FF");
    h->accent2 = hub_color(h, "#183E6B");
    h->shadow = hub_color(h, "#030509");
}

/* Fill the requested state in the startup hub. */
static void hub_fill(hub_window_t *h, int x, int y, int w, int height, unsigned long color) {
    XSetForeground(h->dpy, h->gc, color);
    XFillRectangle(h->dpy, h->win, h->gc, x, y, (unsigned)w, (unsigned)height);
}

/* Fill the requested state in the hub round. */
static void hub_round_fill(hub_window_t *h, int x, int y, int w, int height, int r, unsigned long color) {
    if (w <= 0 || height <= 0)
        return;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > height)
        r = height / 2;
    XSetForeground(h->dpy, h->gc, color);
    XFillRectangle(h->dpy, h->win, h->gc, x + r, y, (unsigned)(w - 2 * r), (unsigned)height);
    XFillRectangle(h->dpy, h->win, h->gc, x, y + r, (unsigned)w, (unsigned)(height - 2 * r));
    XFillArc(h->dpy, h->win, h->gc, x, y, (unsigned)(2 * r), (unsigned)(2 * r), 90 * 64, 90 * 64);
    XFillArc(h->dpy, h->win, h->gc, x + w - 2 * r, y, (unsigned)(2 * r), (unsigned)(2 * r), 0, 90 * 64);
    XFillArc(h->dpy, h->win, h->gc, x, y + height - 2 * r, (unsigned)(2 * r), (unsigned)(2 * r), 180 * 64,
             90 * 64);
    XFillArc(h->dpy, h->win, h->gc, x + w - 2 * r, y + height - 2 * r, (unsigned)(2 * r), (unsigned)(2 * r),
             270 * 64, 90 * 64);
}

/* Stroke the requested state in the hub round. */
static void hub_round_stroke(hub_window_t *h, int x, int y, int w, int height, int r, unsigned long color) {
    XSetForeground(h->dpy, h->gc, color);
    XDrawLine(h->dpy, h->win, h->gc, x + r, y, x + w - r, y);
    XDrawLine(h->dpy, h->win, h->gc, x + r, y + height, x + w - r, y + height);
    XDrawLine(h->dpy, h->win, h->gc, x, y + r, x, y + height - r);
    XDrawLine(h->dpy, h->win, h->gc, x + w, y + r, x + w, y + height - r);
    XDrawArc(h->dpy, h->win, h->gc, x, y, (unsigned)(2 * r), (unsigned)(2 * r), 90 * 64, 90 * 64);
    XDrawArc(h->dpy, h->win, h->gc, x + w - 2 * r, y, (unsigned)(2 * r), (unsigned)(2 * r), 0, 90 * 64);
    XDrawArc(h->dpy, h->win, h->gc, x, y + height - 2 * r, (unsigned)(2 * r), (unsigned)(2 * r), 180 * 64,
             90 * 64);
    XDrawArc(h->dpy, h->win, h->gc, x + w - 2 * r, y + height - 2 * r, (unsigned)(2 * r), (unsigned)(2 * r),
             270 * 64, 90 * 64);
}

/* Handle the hub text font operation. */
static void hub_text_font(hub_window_t *h, XFontStruct *font, int x, int y, const char *text,
                          unsigned long color) {
    if (!text)
        return;
    XFontStruct *f = font ? font : h->font;
    if (f)
        XSetFont(h->dpy, h->gc, f->fid);
    XSetForeground(h->dpy, h->gc, color);
    XDrawString(h->dpy, h->win, h->gc, x, y, text, (int)strlen(text));
}

/* Handle the hub text operation. */
static void hub_text(hub_window_t *h, int x, int y, const char *text, unsigned long color) {
    hub_text_font(h, h->font, x, y, text, color);
}

/* Handle the card geometry operation. */
static void card_geometry(const hub_window_t *h, int index, int *x, int *y, int *w, int *height) {
    const int gap = 24;
    int margin = h->width > 1000 ? 86 : 34;
    int card_w = (h->width - margin * 2 - gap) / 2;
    if (card_w < 280)
        card_w = 280;
    if (card_w > 520)
        card_w = 520;
    int total_w = card_w * 2 + gap;
    int start_x = (h->width - total_w) / 2;
    *x = start_x + index * (card_w + gap);
    *y = 218;
    *w = card_w;
    *height = 238;
}

/* Draw service card. */
static void draw_service_card(hub_window_t *h, int index, const char *monogram, const char *name,
                              const char *subtitle, const char *mode) {
    int x = 0, y = 0, w = 0, height = 0;
    card_geometry(h, index, &x, &y, &w, &height);
    bool selected = h->selected == index;

    if (h->renderer.active) {
        vip_ui_render_round_rect(&h->renderer, x + 6, y + 9, w, height, 22, 0x000000u, 0.46);
        vip_ui_render_round_rect(&h->renderer, x, y, w, height, 22, selected ? 0x173B67u : 0x151E2Du, 0.99);
        vip_ui_render_round_stroke(&h->renderer, x, y, w, height, 22, selected ? 0x62A9FFu : 0x2B3950u, 1.0,
                                   selected ? 1.8 : 1.0);
        if (selected)
            vip_ui_render_round_stroke(&h->renderer, x + 3, y + 3, w - 6, height - 6, 19, 0x8BC1FFu, 0.55,
                                       1.0);

        vip_ui_render_round_rect(&h->renderer, x + 22, y + 22, 48, 48, 14, selected ? 0x62A9FFu : 0x0E1420u,
                                 1.0);
        vip_ui_render_text(&h->renderer, x + 22, y + 35, 48, monogram, "Sans Bold 12",
                           selected ? 0x050811u : 0xF6F8FCu, 1.0, true);
        vip_ui_render_text(&h->renderer, x + 88, y + 29, w - 116, name, "Sans Bold 13", 0xF6F8FCu, 1.0,
                           false);
        vip_ui_render_text(&h->renderer, x + 22, y + 92, w - 44, subtitle, "Sans 9",
                           selected ? 0xDDE9F8u : 0x91A0B7u, 1.0, false);

        vip_ui_render_round_rect(&h->renderer, x + 22, y + height - 58, w - 44, 36, 11,
                                 selected ? 0x0E1A2Au : 0x090E17u, 0.96);
        vip_ui_render_text(&h->renderer, x + 36, y + height - 48, w - 190, mode, "Sans SemiBold 8",
                           selected ? 0xDDE9F8u : 0x91A0B7u, 1.0, false);
        if (selected) {
            vip_ui_render_round_rect(&h->renderer, x + w - 128, y + height - 53, 92, 26, 9, 0x62A9FFu, 1.0);
            vip_ui_render_text(&h->renderer, x + w - 128, y + height - 46, 92, "ABRIR  >", "Sans Bold 8",
                               0x050811u, 1.0, true);
        }
        return;
    }

    hub_round_fill(h, x + 5, y + 7, w, height, 22, h->shadow);
    hub_round_fill(h, x, y, w, height, 22, selected ? h->accent2 : h->panel2);
    hub_round_stroke(h, x, y, w, height, 22, selected ? h->accent : h->border);
    if (selected)
        hub_round_stroke(h, x + 2, y + 2, w - 4, height - 4, 20, h->accent);
    hub_round_fill(h, x + 22, y + 22, 48, 48, 14, selected ? h->accent : h->panel);
    hub_text_font(h, h->heading_font, x + 38, y + 55, monogram, selected ? h->bg : h->text);
    hub_text_font(h, h->heading_font, x + 88, y + 51, name, h->text);
    hub_text(h, x + 22, y + 104, subtitle, selected ? h->text : h->muted);
    hub_round_fill(h, x + 22, y + height - 54, w - 44, 34, 11, selected ? h->panel : h->bg);
    hub_text(h, x + 36, y + height - 31, mode, selected ? h->text : h->muted);
}

/* Draw the requested state in the startup hub. */
static void hub_draw(hub_window_t *h) {
    bool modern = vip_ui_renderer_begin(&h->renderer, h->dpy, h->win, DefaultVisual(h->dpy, h->screen),
                                        h->width, h->height);
    if (modern) {
        vip_ui_render_linear_gradient(&h->renderer, 0, 0, h->width, h->height, 0x050811u, 0x090F1Au);
        vip_ui_render_round_rect(&h->renderer, 0, 0, h->width, 124, 0, 0x0E1420u, 0.96);
        vip_ui_render_round_rect(&h->renderer, 0, 0, h->width, 5, 0, 0x62A9FFu, 1.0);
        vip_ui_render_round_rect(&h->renderer, 52, 35, 48, 48, 15, 0x62A9FFu, 1.0);
        vip_ui_render_text(&h->renderer, 52, 48, 48, "B", "Sans Bold 13", 0x050811u, 1.0, true);
        vip_ui_render_text(&h->renderer, 116, 37, 280, "Blazzing", "Sans Bold 18", 0xF6F8FCu, 1.0, false);
        vip_ui_render_text(&h->renderer, 116, 68, h->width - 168,
                           "IPTV e Pluto TV, sem atalhos para serviços externos", "Sans 9", 0x91A0B7u, 1.0,
                           false);

        vip_ui_render_text(&h->renderer, 54, 151, h->width - 108, "Escolha uma fonte", "Sans Bold 14",
                           0xF6F8FCu, 1.0, false);
        vip_ui_render_text(
            &h->renderer, 54, 179, h->width - 108,
            "O Blazzing mantém apenas reprodução que funciona de forma nativa dentro do aplicativo.",
            "Sans 9", 0x91A0B7u, 1.0, false);
    } else {
        hub_fill(h, 0, 0, h->width, h->height, h->bg);
        hub_fill(h, 0, 0, h->width, 5, h->accent);
        hub_fill(h, 0, 5, h->width, 118, h->panel);
        hub_round_fill(h, 52, 35, 48, 48, 15, h->accent);
        hub_text_font(h, h->heading_font, 68, 68, "B", h->bg);
        hub_text_font(h, h->title_font, 116, 59, "Blazzing", h->text);
        hub_text(h, 116, 83, "IPTV e Pluto TV, sem atalhos para serviços externos", h->muted);
        hub_text_font(h, h->heading_font, 54, 164, "Escolha uma fonte", h->text);
        hub_text(h, 54, 188,
                 "O Blazzing mantem apenas reproducao que funciona de forma nativa dentro do aplicativo.",
                 h->muted);
    }

    draw_service_card(h, 0, "I", "IPTV / Listas", "Xtream, M3U e perfis salvos", "NATIVO  ·  MPV");
    draw_service_card(h, 1, "P", "Pluto TV", "TV gratuita integrada ao Blazzing", "NATIVO  ·  MPV");

    int footer_y = h->height - 68;
    if (h->renderer.active) {
        vip_ui_render_round_rect(&h->renderer, 0, footer_y, h->width, 68, 0, 0x0E1420u, 0.96);
        vip_ui_render_round_rect(&h->renderer, 0, footer_y, h->width, 1, 0, 0x2B3950u, 1.0);
        vip_ui_render_text(&h->renderer, 54, footer_y + 27, h->width - 108,
                           "Setas: navegar    Enter: abrir    Esc: sair", "Sans 8", 0x91A0B7u, 1.0, false);
        vip_ui_renderer_end(&h->renderer);
    } else {
        hub_fill(h, 0, footer_y, h->width, 68, h->panel);
        hub_fill(h, 0, footer_y, h->width, 1, h->border);
        hub_text(h, 54, footer_y + 40, "Setas: navegar   Enter: abrir   Esc: sair", h->muted);
    }
    XFlush(h->dpy);
}

/* Return whether the supplied point lies inside the rectangle. */
static bool point_in(int px, int py, int x, int y, int w, int height) {
    return px >= x && px < x + w && py >= y && py < y + height;
}

/* Handle the card at operation. */
static int card_at(const hub_window_t *h, int px, int py) {
    for (int i = 0; i < 2; ++i) {
        int x = 0, y = 0, w = 0, height = 0;
        card_geometry(h, i, &x, &y, &w, &height);
        if (point_in(px, py, x, y, w, height))
            return i;
    }
    return -1;
}

/* Handle the activate selected operation. */
static hub_action_t activate_selected(const hub_window_t *h) {
    return h->selected == 0 ? HUB_ACTION_IPTV : HUB_ACTION_PLUTO;
}

/* Run the requested state in the hub window. */
static hub_action_t hub_window_run(void) {
    hub_window_t h;
    memset(&h, 0, sizeof(h));
    h.width = 1280;
    h.height = 680;
    h.selected = 0;

    h.dpy = XOpenDisplay(NULL);
    if (!h.dpy) {
        fprintf(stderr, "[hub] nao foi possivel abrir DISPLAY X11\n");
        return HUB_ACTION_QUIT;
    }
    h.screen = DefaultScreen(h.dpy);
    h.win =
        XCreateSimpleWindow(h.dpy, RootWindow(h.dpy, h.screen), 40, 40, (unsigned)h.width, (unsigned)h.height,
                            0, BlackPixel(h.dpy, h.screen), BlackPixel(h.dpy, h.screen));
    XStoreName(h.dpy, h.win, "Blazzing");
    XSelectInput(h.dpy, h.win,
                 ExposureMask | KeyPressMask | ButtonPressMask | PointerMotionMask | StructureNotifyMask);
    h.wm_delete = XInternAtom(h.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(h.dpy, h.win, &h.wm_delete, 1);
    h.gc = XCreateGC(h.dpy, h.win, 0, NULL);
    h.font = XLoadQueryFont(h.dpy, "-misc-fixed-medium-r-normal--15-*-*-*-*-*-iso8859-1");
    if (!h.font)
        h.font = XLoadQueryFont(h.dpy, "9x15");
    if (!h.font)
        h.font = XLoadQueryFont(h.dpy, "fixed");
    h.title_font = XLoadQueryFont(h.dpy, "-*-helvetica-bold-r-normal--24-*-*-*-*-*-iso8859-1");
    h.heading_font = XLoadQueryFont(h.dpy, "-*-helvetica-bold-r-normal--18-*-*-*-*-*-iso8859-1");
    if (h.font)
        XSetFont(h.dpy, h.gc, h.font->fid);
    hub_init_colors(&h);
    XMapWindow(h.dpy, h.win);

    hub_action_t action = HUB_ACTION_NONE;
    bool done = false;
    while (!done) {
        XEvent event;
        XNextEvent(h.dpy, &event);
        switch (event.type) {
        case Expose:
            if (event.xexpose.count == 0)
                hub_draw(&h);
            break;
        case ConfigureNotify:
            if (event.xconfigure.window == h.win) {
                h.width = event.xconfigure.width;
                h.height = event.xconfigure.height;
                hub_draw(&h);
            }
            break;
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == h.wm_delete) {
                action = HUB_ACTION_QUIT;
                done = true;
            }
            break;
        case MotionNotify: {
            int hovered = card_at(&h, event.xmotion.x, event.xmotion.y);
            if (hovered >= 0 && hovered != h.selected) {
                h.selected = hovered;
                hub_draw(&h);
            }
            break;
        }
        case ButtonPress:
            if (event.xbutton.button == Button1) {
                int clicked = card_at(&h, event.xbutton.x, event.xbutton.y);
                if (clicked >= 0) {
                    h.selected = clicked;
                    action = activate_selected(&h);
                    done = true;
                }
            }
            break;
        case KeyPress: {
            KeySym sym = XLookupKeysym(&event.xkey, 0);
            if (sym == XK_Escape) {
                action = HUB_ACTION_QUIT;
                done = true;
            } else if (sym == XK_Left || sym == XK_Up) {
                h.selected = 0;
                hub_draw(&h);
            } else if (sym == XK_Right || sym == XK_Down) {
                h.selected = 1;
                hub_draw(&h);
            } else if (sym == XK_Return || sym == XK_KP_Enter) {
                action = activate_selected(&h);
                done = true;
            }
            break;
        }
        default:
            break;
        }
    }

    if (h.renderer.active)
        vip_ui_renderer_end(&h.renderer);
    if (h.title_font)
        XFreeFont(h.dpy, h.title_font);
    if (h.heading_font)
        XFreeFont(h.dpy, h.heading_font);
    if (h.font)
        XFreeFont(h.dpy, h.font);
    if (h.gc)
        XFreeGC(h.dpy, h.gc);
    XDestroyWindow(h.dpy, h.win);
    XCloseDisplay(h.dpy);
    return action;
}

/* Run the requested state in the startup hub. */
int vip_hub_run(void) {
    for (;;) {
        hub_action_t action = hub_window_run();
        if (action == HUB_ACTION_QUIT)
            return 0;
        if (action == HUB_ACTION_IPTV) {
            int rc = vip_x11_app_run();
            if (rc != 0)
                return rc;
        } else if (action == HUB_ACTION_PLUTO) {
            int rc = vip_pluto_app_run();
            if (rc != 0)
                fprintf(stderr, "[hub] Pluto TV encerrou com codigo %d\n", rc);
        }
    }
}
