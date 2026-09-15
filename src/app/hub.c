/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/hub.h"
#include "visual_iptv/core.h"
#include "visual_iptv/streaming_services.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int vip_x11_app_run(void);

typedef enum {
    HUB_ACTION_NONE = 0,
    HUB_ACTION_IPTV,
    HUB_ACTION_PLUTO,
    HUB_ACTION_QUIT
} hub_action_t;

typedef struct {
    Display *dpy;
    int screen;
    Window win;
    GC gc;
    XFontStruct *font;
    Atom wm_delete;
    int width;
    int height;
    int selected;
    char status[256];
    unsigned long bg;
    unsigned long panel;
    unsigned long panel2;
    unsigned long border;
    unsigned long text;
    unsigned long muted;
    unsigned long accent;
    unsigned long accent2;
} hub_window_t;

static unsigned long hub_color(hub_window_t *h, const char *name) {
    XColor exact = {0};
    XColor screen = {0};
    if (XAllocNamedColor(h->dpy, DefaultColormap(h->dpy, h->screen), name, &screen, &exact))
        return screen.pixel;
    return BlackPixel(h->dpy, h->screen);
}

static void hub_init_colors(hub_window_t *h) {
    h->bg = hub_color(h, "#111416");
    h->panel = hub_color(h, "#202428");
    h->panel2 = hub_color(h, "#171a1d");
    h->border = hub_color(h, "#394047");
    h->text = hub_color(h, "#f1f3f5");
    h->muted = hub_color(h, "#9aa0a6");
    h->accent = hub_color(h, "#4ba3ff");
    h->accent2 = hub_color(h, "#275d84");
}

static void hub_fill(hub_window_t *h, int x, int y, int w, int height, unsigned long color) {
    XSetForeground(h->dpy, h->gc, color);
    XFillRectangle(h->dpy, h->win, h->gc, x, y, (unsigned)w, (unsigned)height);
}

static void hub_stroke(hub_window_t *h, int x, int y, int w, int height, unsigned long color) {
    XSetForeground(h->dpy, h->gc, color);
    XDrawRectangle(h->dpy, h->win, h->gc, x, y, (unsigned)w, (unsigned)height);
}

static int hub_text_width(hub_window_t *h, const char *text) {
    if (!text) return 0;
    if (h->font) return XTextWidth(h->font, text, (int)strlen(text));
    return (int)strlen(text) * 8;
}

static void hub_text(hub_window_t *h, int x, int y, const char *text, unsigned long color) {
    if (!text) return;
    XSetForeground(h->dpy, h->gc, color);
    XDrawString(h->dpy, h->win, h->gc, x, y, text, (int)strlen(text));
}

static void hub_center(hub_window_t *h, int x, int y, int w, const char *text, unsigned long color) {
    int tw = hub_text_width(h, text);
    hub_text(h, x + (w - tw) / 2, y, text, color);
}

static void card_geometry(const hub_window_t *h, int index, int *x, int *y, int *w, int *height) {
    const int gap = 18;
    const int cols = 3;
    int margin = h->width > 1100 ? 74 : 34;
    int available = h->width - margin * 2 - gap * (cols - 1);
    int card_w = available / cols;
    if (card_w < 240) card_w = 240;
    int row = index / cols;
    int col = index % cols;
    *x = margin + col * (card_w + gap);
    *y = 176 + row * 194;
    *w = card_w;
    *height = 166;
}

static void draw_service_card(hub_window_t *h,
                              int index,
                              const char *name,
                              const char *subtitle,
                              const char *mode) {
    int x = 0, y = 0, w = 0, height = 0;
    card_geometry(h, index, &x, &y, &w, &height);
    bool selected = h->selected == index;
    hub_fill(h, x, y, w, height, selected ? h->accent2 : h->panel2);
    hub_stroke(h, x, y, w, height, selected ? h->accent : h->border);
    if (selected) hub_stroke(h, x + 2, y + 2, w - 4, height - 4, h->accent);
    hub_text(h, x + 18, y + 36, name, h->text);
    hub_text(h, x + 18, y + 70, subtitle, selected ? h->text : h->muted);
    hub_text(h, x + 18, y + height - 22, mode, selected ? h->text : h->muted);
}

static void hub_draw(hub_window_t *h) {
    hub_fill(h, 0, 0, h->width, h->height, h->bg);
    hub_fill(h, 0, 0, h->width, 92, h->panel);
    hub_text(h, 54, 42, "Blazzing", h->text);
    hub_text(h, 54, 67, "Streaming hub", h->muted);
    hub_text(h, 54, 132, "Escolha uma fonte", h->text);
    hub_text(h, 54, 154, "IPTV e Pluto rodam nativamente. Servicos com DRM abrem no ambiente web oficial.", h->muted);

    draw_service_card(h, 0, "IPTV / Listas", "Xtream, M3U e perfis salvos", "NATIVO  |  MPV");
    draw_service_card(h, 1, "Pluto TV", "TV gratis, sem login", "NATIVO  |  MPV");
    draw_service_card(h, 2, "Prime Video", "Conta Amazon no site oficial", "WEB  |  DRM OFICIAL");
    draw_service_card(h, 3, "Max", "Conta Max no site oficial", "WEB  |  DRM OFICIAL");
    draw_service_card(h, 4, "Globoplay", "Conta Globo no site oficial", "WEB  |  DRM OFICIAL");

    int footer_y = h->height - 70;
    hub_fill(h, 0, footer_y, h->width, 70, h->panel);
    hub_text(h, 54, footer_y + 28, "Setas: navegar   Enter: abrir   Esc: sair", h->muted);
    if (h->status[0]) hub_text(h, 54, footer_y + 52, h->status, h->text);
    XFlush(h->dpy);
}

static bool point_in(int px, int py, int x, int y, int w, int height) {
    return px >= x && px < x + w && py >= y && py < y + height;
}

static hub_action_t activate_selected(hub_window_t *h) {
    if (h->selected == 0) return HUB_ACTION_IPTV;
    if (h->selected == 1) return HUB_ACTION_PLUTO;

    vip_streaming_service_id_t service_id = VIP_SERVICE_PRIME_VIDEO;
    if (h->selected == 3) service_id = VIP_SERVICE_MAX;
    else if (h->selected == 4) service_id = VIP_SERVICE_GLOBOPLAY;

    vip_error_t error = {0};
    vip_status_t st = vip_streaming_service_open(service_id, NULL, &error);
    if (st == VIP_OK) {
        const vip_streaming_service_t *service = vip_streaming_service_get(service_id);
        snprintf(h->status, sizeof(h->status), "%s aberto no navegador.", service ? service->name : "Servico");
    } else {
        snprintf(h->status, sizeof(h->status), "Falha ao abrir: %.200s", error.message);
    }
    return HUB_ACTION_NONE;
}

static hub_action_t hub_window_run(void) {
    hub_window_t h;
    memset(&h, 0, sizeof(h));
    h.width = 1280;
    h.height = 650;
    h.selected = 0;

    h.dpy = XOpenDisplay(NULL);
    if (!h.dpy) {
        fprintf(stderr, "[hub] nao foi possivel abrir DISPLAY X11\n");
        return HUB_ACTION_QUIT;
    }
    h.screen = DefaultScreen(h.dpy);
    h.win = XCreateSimpleWindow(h.dpy, RootWindow(h.dpy, h.screen), 40, 40,
                                (unsigned)h.width, (unsigned)h.height, 0,
                                BlackPixel(h.dpy, h.screen), BlackPixel(h.dpy, h.screen));
    XStoreName(h.dpy, h.win, "Blazzing - Streaming Hub");
    XSelectInput(h.dpy, h.win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
    h.wm_delete = XInternAtom(h.dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(h.dpy, h.win, &h.wm_delete, 1);
    h.gc = XCreateGC(h.dpy, h.win, 0, NULL);
    h.font = XLoadQueryFont(h.dpy, "-misc-fixed-medium-r-normal--15-*-*-*-*-*-iso8859-1");
    if (!h.font) h.font = XLoadQueryFont(h.dpy, "9x15");
    if (!h.font) h.font = XLoadQueryFont(h.dpy, "fixed");
    if (h.font) XSetFont(h.dpy, h.gc, h.font->fid);
    hub_init_colors(&h);
    XMapWindow(h.dpy, h.win);

    hub_action_t action = HUB_ACTION_NONE;
    bool done = false;
    while (!done) {
        XEvent event;
        XNextEvent(h.dpy, &event);
        switch (event.type) {
            case Expose:
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
            case ButtonPress:
                if (event.xbutton.button == Button1) {
                    for (int i = 0; i < 5; ++i) {
                        int x = 0, y = 0, w = 0, height = 0;
                        card_geometry(&h, i, &x, &y, &w, &height);
                        if (point_in(event.xbutton.x, event.xbutton.y, x, y, w, height)) {
                            h.selected = i;
                            action = activate_selected(&h);
                            if (action != HUB_ACTION_NONE) done = true;
                            hub_draw(&h);
                            break;
                        }
                    }
                }
                break;
            case KeyPress: {
                KeySym sym = XLookupKeysym(&event.xkey, 0);
                if (sym == XK_Escape) {
                    action = HUB_ACTION_QUIT;
                    done = true;
                } else if (sym == XK_Left) {
                    if (h.selected > 0) --h.selected;
                    hub_draw(&h);
                } else if (sym == XK_Right) {
                    if (h.selected < 4) ++h.selected;
                    hub_draw(&h);
                } else if (sym == XK_Up) {
                    if (h.selected >= 3) h.selected -= 3;
                    hub_draw(&h);
                } else if (sym == XK_Down) {
                    if (h.selected <= 1) h.selected += 3;
                    else if (h.selected == 2) h.selected = 4;
                    hub_draw(&h);
                } else if (sym == XK_Return || sym == XK_KP_Enter) {
                    action = activate_selected(&h);
                    if (action != HUB_ACTION_NONE) done = true;
                    hub_draw(&h);
                }
                break;
            }
            default:
                break;
        }
    }

    if (h.font) XFreeFont(h.dpy, h.font);
    if (h.gc) XFreeGC(h.dpy, h.gc);
    XDestroyWindow(h.dpy, h.win);
    XCloseDisplay(h.dpy);
    return action;
}

int vip_hub_run(void) {
    for (;;) {
        hub_action_t action = hub_window_run();
        if (action == HUB_ACTION_QUIT) return 0;
        if (action == HUB_ACTION_IPTV) {
            int rc = vip_x11_app_run();
            if (rc != 0) return rc;
        } else if (action == HUB_ACTION_PLUTO) {
            int rc = vip_pluto_app_run();
            if (rc != 0) fprintf(stderr, "[hub] Pluto TV encerrou com codigo %d\n", rc);
        }
    }
}
