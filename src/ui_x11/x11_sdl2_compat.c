/* SPDX-License-Identifier: MIT */
/*
 * Cross-platform Xlib subset implementation backed by SDL2 + Cairo image
 * surfaces.  Allows the native C17 desktop UI (Hub, IPTV, Pluto TV) to run
 * unmodified on Windows (Win32 + SDL2) and Linux SDL2.
 */
#include <X11/XF86keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#ifdef _WIN32
#include <SDL2/SDL_syswm.h>
#include <windows.h>
#endif
#include <cairo/cairo.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DRAWABLES 32
#define MAX_ATOMS 64
#define EVENT_QUEUE_CAP 256

typedef struct {
    Drawable id;
    bool in_use;
    bool is_pixmap;
    bool is_toplevel;
    bool is_input_only;
    Window parent;
    int x;
    int y;
    int width;
    int height;
    int map_state;
    uint32_t *pixels;
    bool dirty;
#ifdef _WIN32
    HWND child_hwnd;
#endif
} sdl_drawable_t;

struct _XGC {
    unsigned long foreground;
    Font font;
    bool clip_enabled;
    XRectangle clip_rect;
};

struct _XDisplay {
    SDL_Window *sdl_win;
    SDL_Renderer *sdl_renderer;
    SDL_Texture *sdl_texture;
    int tex_w;
    int tex_h;
    Visual visual;
    Drawable toplevel_id;
    Drawable next_id;
    sdl_drawable_t drawables[MAX_DRAWABLES];
    char *atom_names[MAX_ATOMS];
    size_t atom_count;
    Atom wm_delete_atom;
    Atom net_wm_state_atom;
    Atom net_wm_fullscreen_atom;
    Atom paste_property_atom;
    char *paste_text;
    bool is_fullscreen;
    XEvent queue[EVENT_QUEUE_CAP];
    size_t queue_head;
    size_t queue_len;
    int mouse_x;
    int mouse_y;
};

static sdl_drawable_t *find_drawable(Display *dpy, Drawable id) {
    if (!dpy || id == None)
        return NULL;
    for (size_t i = 0; i < MAX_DRAWABLES; ++i) {
        if (dpy->drawables[i].in_use && dpy->drawables[i].id == id)
            return &dpy->drawables[i];
    }
    return NULL;
}

static sdl_drawable_t *alloc_drawable(Display *dpy) {
    if (!dpy)
        return NULL;
    for (size_t i = 0; i < MAX_DRAWABLES; ++i) {
        if (!dpy->drawables[i].in_use) {
            memset(&dpy->drawables[i], 0, sizeof(dpy->drawables[i]));
            dpy->drawables[i].in_use = true;
            dpy->drawables[i].id = dpy->next_id++;
            return &dpy->drawables[i];
        }
    }
    return NULL;
}

static bool ensure_drawable_pixels(sdl_drawable_t *drw, int width, int height) {
    if (!drw || drw->is_input_only || width <= 0 || height <= 0)
        return false;
    if (drw->pixels && drw->width == width && drw->height == height)
        return true;
    uint32_t *next = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if (!next)
        return false;
    if (drw->pixels) {
        int copy_w = drw->width < width ? drw->width : width;
        int copy_h = drw->height < height ? drw->height : height;
        for (int y = 0; y < copy_h; ++y) {
            memcpy(next + (size_t)y * (size_t)width, drw->pixels + (size_t)y * (size_t)drw->width,
                   (size_t)copy_w * sizeof(uint32_t));
        }
        free(drw->pixels);
    }
    drw->pixels = next;
    drw->width = width;
    drw->height = height;
    drw->dirty = true;
    return true;
}

static void enqueue_event(Display *dpy, const XEvent *ev) {
    if (!dpy || !ev || dpy->queue_len >= EVENT_QUEUE_CAP)
        return;
    size_t idx = (dpy->queue_head + dpy->queue_len) % EVENT_QUEUE_CAP;
    dpy->queue[idx] = *ev;
    ++dpy->queue_len;
}

static unsigned int sdl_mods_to_x11(Uint16 mod) {
    unsigned int state = 0;
    if (mod & KMOD_SHIFT)
        state |= ShiftMask;
    if (mod & KMOD_CAPS)
        state |= LockMask;
    if (mod & KMOD_CTRL)
        state |= ControlMask;
    if (mod & KMOD_ALT)
        state |= Mod1Mask;
    return state;
}

static KeySym sdl_keycode_to_keysym(SDL_Keycode sym, Uint16 mod) {
    switch (sym) {
    case SDLK_BACKSPACE:
        return XK_BackSpace;
    case SDLK_TAB:
        return XK_Tab;
    case SDLK_RETURN:
        return XK_Return;
    case SDLK_KP_ENTER:
        return XK_KP_Enter;
    case SDLK_ESCAPE:
        return XK_Escape;
    case SDLK_SPACE:
        return XK_space;
    case SDLK_INSERT:
        return XK_Insert;
    case SDLK_LEFT:
        return XK_Left;
    case SDLK_UP:
        return XK_Up;
    case SDLK_RIGHT:
        return XK_Right;
    case SDLK_DOWN:
        return XK_Down;
    case SDLK_SELECT:
        return XK_Select;
    case SDLK_AC_BACK:
        return XF86XK_Back;
    case SDLK_F2:
        return XK_F2;
    case SDLK_F11:
        return XK_F11;
    default:
        break;
    }
    if (sym >= 'a' && sym <= 'z') {
        bool upper = ((mod & KMOD_SHIFT) != 0) ^ ((mod & KMOD_CAPS) != 0);
        return (KeySym)(upper ? (sym - 'a' + 'A') : sym);
    }
    if (sym >= 0x20 && sym <= 0x7e)
        return (KeySym)sym;
    return NoSymbol;
}

static void pump_sdl_events(Display *dpy) {
    if (!dpy)
        return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT: {
            XEvent xev;
            memset(&xev, 0, sizeof(xev));
            xev.type = ClientMessage;
            xev.xclient.display = dpy;
            xev.xclient.window = dpy->toplevel_id;
            xev.xclient.format = 32;
            xev.xclient.data.l[0] = (long)dpy->wm_delete_atom;
            enqueue_event(dpy, &xev);
            break;
        }
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                XEvent xev;
                memset(&xev, 0, sizeof(xev));
                xev.type = ClientMessage;
                xev.xclient.display = dpy;
                xev.xclient.window = dpy->toplevel_id;
                xev.xclient.format = 32;
                xev.xclient.data.l[0] = (long)dpy->wm_delete_atom;
                enqueue_event(dpy, &xev);
            } else if (ev.window.event == SDL_WINDOWEVENT_EXPOSED ||
                       ev.window.event == SDL_WINDOWEVENT_SHOWN) {
                sdl_drawable_t *top = find_drawable(dpy, dpy->toplevel_id);
                if (top)
                    top->dirty = true;
                XEvent xev;
                memset(&xev, 0, sizeof(xev));
                xev.type = Expose;
                xev.xexpose.display = dpy;
                xev.xexpose.window = dpy->toplevel_id;
                xev.xexpose.width = top ? top->width : 0;
                xev.xexpose.height = top ? top->height : 0;
                xev.xexpose.count = 0;
                enqueue_event(dpy, &xev);
            } else if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                       ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                int w = ev.window.data1;
                int h = ev.window.data2;
                sdl_drawable_t *top = find_drawable(dpy, dpy->toplevel_id);
                if (top && w > 0 && h > 0)
                    (void)ensure_drawable_pixels(top, w, h);
                XEvent xev;
                memset(&xev, 0, sizeof(xev));
                xev.type = ConfigureNotify;
                xev.xconfigure.display = dpy;
                xev.xconfigure.window = dpy->toplevel_id;
                xev.xconfigure.width = w;
                xev.xconfigure.height = h;
                enqueue_event(dpy, &xev);
            } else if (ev.window.event == SDL_WINDOWEVENT_LEAVE) {
                XEvent xev;
                memset(&xev, 0, sizeof(xev));
                xev.type = LeaveNotify;
                xev.xmotion.display = dpy;
                xev.xmotion.window = dpy->toplevel_id;
                enqueue_event(dpy, &xev);
            }
            break;
        case SDL_MOUSEMOTION: {
            dpy->mouse_x = ev.motion.x;
            dpy->mouse_y = ev.motion.y;
            XEvent xev;
            memset(&xev, 0, sizeof(xev));
            xev.type = MotionNotify;
            xev.xmotion.display = dpy;
            xev.xmotion.window = dpy->toplevel_id;
            xev.xmotion.x = ev.motion.x;
            xev.xmotion.y = ev.motion.y;
            enqueue_event(dpy, &xev);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            dpy->mouse_x = ev.button.x;
            dpy->mouse_y = ev.button.y;
            unsigned int btn = Button1;
            if (ev.button.button == SDL_BUTTON_MIDDLE)
                btn = Button2;
            else if (ev.button.button == SDL_BUTTON_RIGHT)
                btn = Button3;
            else if (ev.button.button != SDL_BUTTON_LEFT)
                break;
            XEvent xev;
            memset(&xev, 0, sizeof(xev));
            xev.type = ev.type == SDL_MOUSEBUTTONDOWN ? ButtonPress : ButtonRelease;
            xev.xbutton.display = dpy;
            xev.xbutton.window = dpy->toplevel_id;
            xev.xbutton.x = ev.button.x;
            xev.xbutton.y = ev.button.y;
            xev.xbutton.button = btn;
            enqueue_event(dpy, &xev);
            break;
        }
        case SDL_MOUSEWHEEL: {
            if (ev.wheel.y == 0)
                break;
            XEvent xev;
            memset(&xev, 0, sizeof(xev));
            xev.type = ButtonPress;
            xev.xbutton.display = dpy;
            xev.xbutton.window = dpy->toplevel_id;
            xev.xbutton.x = dpy->mouse_x;
            xev.xbutton.y = dpy->mouse_y;
            xev.xbutton.button = ev.wheel.y > 0 ? Button4 : Button5;
            enqueue_event(dpy, &xev);
            break;
        }
        case SDL_KEYDOWN: {
            Uint16 mod = ev.key.keysym.mod;
            SDL_Keycode k = ev.key.keysym.sym;
            KeySym sym = sdl_keycode_to_keysym(k, mod);
            bool ctrl = (mod & KMOD_CTRL) != 0;
            bool is_special = (k == SDLK_BACKSPACE || k == SDLK_TAB || k == SDLK_RETURN ||
                               k == SDLK_KP_ENTER || k == SDLK_ESCAPE || k == SDLK_SPACE ||
                               k == SDLK_INSERT || k == SDLK_LEFT || k == SDLK_UP || k == SDLK_RIGHT ||
                               k == SDLK_DOWN || k == SDLK_SELECT || k == SDLK_AC_BACK || k == SDLK_F2 ||
                               k == SDLK_F11);
            if (!is_special && !ctrl)
                break;
            XEvent xev;
            memset(&xev, 0, sizeof(xev));
            xev.type = KeyPress;
            xev.xkey.display = dpy;
            xev.xkey.window = dpy->toplevel_id;
            xev.xkey.state = sdl_mods_to_x11(mod);
            xev.xkey.keycode = (unsigned int)ev.key.keysym.scancode;
            xev.xkey.keysym = sym;
            if (k == SDLK_SPACE && !ctrl) {
                xev.xkey.text[0] = ' ';
                xev.xkey.text[1] = '\0';
            }
            enqueue_event(dpy, &xev);
            break;
        }
        case SDL_TEXTINPUT: {
            if (!ev.text.text[0])
                break;
            if (ev.text.text[0] == ' ' && ev.text.text[1] == '\0')
                break;
            XEvent xev;
            memset(&xev, 0, sizeof(xev));
            xev.type = KeyPress;
            xev.xkey.display = dpy;
            xev.xkey.window = dpy->toplevel_id;
            xev.xkey.state = sdl_mods_to_x11(SDL_GetModState());
            xev.xkey.keysym = (ev.text.text[1] == '\0' && (unsigned char)ev.text.text[0] >= 0x20u &&
                               (unsigned char)ev.text.text[0] <= 0x7eu)
                                  ? (KeySym)(unsigned char)ev.text.text[0]
                                  : NoSymbol;
            snprintf(xev.xkey.text, sizeof(xev.xkey.text), "%s", ev.text.text);
            enqueue_event(dpy, &xev);
            break;
        }
        default:
            break;
        }
    }
}

Status XInitThreads(void) {
    return 1;
}

Display *XOpenDisplay(const char *display_name) {
    (void)display_name;
    SDL_SetMainReady();
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "[sdl2] SDL_InitSubSystem(SDL_INIT_VIDEO) falhou: %s\n", SDL_GetError());
            return NULL;
        }
    }
    Display *dpy = calloc(1, sizeof(*dpy));
    if (!dpy)
        return NULL;
    dpy->visual.red_mask = 0x00FF0000UL;
    dpy->visual.green_mask = 0x0000FF00UL;
    dpy->visual.blue_mask = 0x000000FFUL;
    dpy->next_id = 1;
    sdl_drawable_t *root = alloc_drawable(dpy);
    if (root) {
        root->width = 1920;
        root->height = 1080;
    }
    dpy->wm_delete_atom = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    dpy->net_wm_state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);
    dpy->net_wm_fullscreen_atom = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    dpy->paste_property_atom = XInternAtom(dpy, "VISUAL_IPTV_PASTE", False);
    SDL_StartTextInput();
    return dpy;
}

int XCloseDisplay(Display *dpy) {
    if (!dpy)
        return 0;
    for (size_t i = 0; i < MAX_DRAWABLES; ++i) {
        if (dpy->drawables[i].in_use) {
#ifdef _WIN32
            if (dpy->drawables[i].child_hwnd)
                DestroyWindow(dpy->drawables[i].child_hwnd);
#endif
            free(dpy->drawables[i].pixels);
        }
    }
    for (size_t i = 0; i < dpy->atom_count; ++i)
        free(dpy->atom_names[i]);
    free(dpy->paste_text);
    if (dpy->sdl_texture)
        SDL_DestroyTexture(dpy->sdl_texture);
    if (dpy->sdl_renderer)
        SDL_DestroyRenderer(dpy->sdl_renderer);
    if (dpy->sdl_win)
        SDL_DestroyWindow(dpy->sdl_win);
    free(dpy);
    return 0;
}

int DefaultScreen(Display *dpy) {
    (void)dpy;
    return 0;
}

Window RootWindow(Display *dpy, int screen_number) {
    (void)dpy;
    (void)screen_number;
    return 1UL;
}

Visual *DefaultVisual(Display *dpy, int screen_number) {
    (void)screen_number;
    return dpy ? &dpy->visual : NULL;
}

int DefaultDepth(Display *dpy, int screen_number) {
    (void)dpy;
    (void)screen_number;
    return 24;
}

Colormap DefaultColormap(Display *dpy, int screen_number) {
    (void)dpy;
    (void)screen_number;
    return 1UL;
}

unsigned long BlackPixel(Display *dpy, int screen_number) {
    (void)dpy;
    (void)screen_number;
    return 0x000000UL;
}

unsigned long WhitePixel(Display *dpy, int screen_number) {
    (void)dpy;
    (void)screen_number;
    return 0xFFFFFFUL;
}

int DisplayWidth(Display *dpy, int screen_number) {
    (void)dpy;
    (void)screen_number;
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0)
        return dm.w;
    return 1920;
}

int DisplayHeight(Display *dpy, int screen_number) {
    (void)dpy;
    (void)screen_number;
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.h > 0)
        return dm.h;
    return 1080;
}

int ConnectionNumber(Display *dpy) {
    (void)dpy;
    return -1;
}

Window XCreateSimpleWindow(Display *dpy, Window parent, int x, int y, unsigned int width,
                           unsigned int height, unsigned int border_width, unsigned long border,
                           unsigned long background) {
    (void)border_width;
    (void)border;
    (void)background;
    if (!dpy)
        return None;
    sdl_drawable_t *drw = alloc_drawable(dpy);
    if (!drw)
        return None;
    drw->parent = parent;
    drw->x = x;
    drw->y = y;
    drw->width = (int)width;
    drw->height = (int)height;
    drw->map_state = IsUnmapped;

    if (parent == RootWindow(dpy, 0) && !dpy->sdl_win) {
        drw->is_toplevel = true;
        dpy->toplevel_id = drw->id;
        dpy->sdl_win = SDL_CreateWindow("Blazzing", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        (int)width, (int)height,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!dpy->sdl_win) {
            drw->in_use = false;
            return None;
        }
        dpy->sdl_renderer = SDL_CreateRenderer(dpy->sdl_win, -1, SDL_RENDERER_ACCELERATED);
        if (!dpy->sdl_renderer)
            dpy->sdl_renderer = SDL_CreateRenderer(dpy->sdl_win, -1, SDL_RENDERER_SOFTWARE);
        (void)ensure_drawable_pixels(drw, (int)width, (int)height);
#ifdef _WIN32
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        if (SDL_GetWindowWMInfo(dpy->sdl_win, &wmInfo)) {
            HWND hwnd = wmInfo.info.win.window;
            LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
            SetWindowLongPtrA(hwnd, GWL_STYLE, style | WS_CLIPCHILDREN);
        }
#endif
    } else {
#ifdef _WIN32
        if (dpy->sdl_win) {
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            if (SDL_GetWindowWMInfo(dpy->sdl_win, &wmInfo)) {
                HWND parent_hwnd = wmInfo.info.win.window;
                HWND child = CreateWindowExA(0, "STATIC", "",
                                             WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, x, y,
                                             (int)width, (int)height, parent_hwnd, NULL,
                                             GetModuleHandleA(NULL), NULL);
                if (child) {
                    drw->child_hwnd = child;
                    drw->id = (Drawable)(uintptr_t)child;
                }
            }
        }
#endif
    }
    return drw->id;
}

Window XCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                     unsigned int border_width, int depth, unsigned int c_class, Visual *visual,
                     unsigned long valuemask, XSetWindowAttributes *attributes) {
    (void)border_width;
    (void)depth;
    (void)visual;
    (void)valuemask;
    (void)attributes;
    if (!dpy)
        return None;
    sdl_drawable_t *drw = alloc_drawable(dpy);
    if (!drw)
        return None;
    drw->parent = parent;
    drw->x = x;
    drw->y = y;
    drw->width = (int)width;
    drw->height = (int)height;
    drw->is_input_only = (c_class == InputOnly);
    drw->map_state = IsUnmapped;
    return drw->id;
}

int XDestroyWindow(Display *dpy, Window w) {
    sdl_drawable_t *drw = find_drawable(dpy, w);
    if (!drw)
        return 0;
#ifdef _WIN32
    if (drw->child_hwnd) {
        DestroyWindow(drw->child_hwnd);
        drw->child_hwnd = NULL;
    }
#endif
    free(drw->pixels);
    drw->pixels = NULL;
    drw->in_use = false;
    return 1;
}

int XMapWindow(Display *dpy, Window w) {
    sdl_drawable_t *drw = find_drawable(dpy, w);
    if (!drw)
        return 0;
    drw->map_state = IsViewable;
    if (drw->is_toplevel && dpy->sdl_win) {
        SDL_ShowWindow(dpy->sdl_win);
        XEvent xev;
        memset(&xev, 0, sizeof(xev));
        xev.type = Expose;
        xev.xexpose.display = dpy;
        xev.xexpose.window = w;
        xev.xexpose.width = drw->width;
        xev.xexpose.height = drw->height;
        enqueue_event(dpy, &xev);
    }
#ifdef _WIN32
    if (drw->child_hwnd)
        ShowWindow(drw->child_hwnd, SW_SHOW);
#endif
    return 1;
}

int XMapRaised(Display *dpy, Window w) {
    int rc = XMapWindow(dpy, w);
#ifdef _WIN32
    sdl_drawable_t *drw = find_drawable(dpy, w);
    if (drw && drw->child_hwnd)
        SetWindowPos(drw->child_hwnd, HWND_TOP, drw->x, drw->y, drw->width, drw->height, SWP_SHOWWINDOW);
#endif
    return rc;
}

int XUnmapWindow(Display *dpy, Window w) {
    sdl_drawable_t *drw = find_drawable(dpy, w);
    if (!drw)
        return 0;
    drw->map_state = IsUnmapped;
#ifdef _WIN32
    if (drw->child_hwnd)
        ShowWindow(drw->child_hwnd, SW_HIDE);
#endif
    return 1;
}

int XRaiseWindow(Display *dpy, Window w) {
    sdl_drawable_t *drw = find_drawable(dpy, w);
    if (!drw)
        return 0;
    if (drw->is_toplevel && dpy->sdl_win)
        SDL_RaiseWindow(dpy->sdl_win);
#ifdef _WIN32
    if (drw->child_hwnd)
        SetWindowPos(drw->child_hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#endif
    return 1;
}

int XMoveResizeWindow(Display *dpy, Window w, int x, int y, unsigned int width, unsigned int height) {
    sdl_drawable_t *drw = find_drawable(dpy, w);
    if (!drw)
        return 0;
    drw->x = x;
    drw->y = y;
    if (drw->is_toplevel && dpy->sdl_win) {
        SDL_SetWindowPosition(dpy->sdl_win, x, y);
        SDL_SetWindowSize(dpy->sdl_win, (int)width, (int)height);
        (void)ensure_drawable_pixels(drw, (int)width, (int)height);
    } else {
        drw->width = (int)width;
        drw->height = (int)height;
#ifdef _WIN32
        if (drw->child_hwnd)
            SetWindowPos(drw->child_hwnd, HWND_TOP, x, y, (int)width, (int)height, SWP_NOACTIVATE);
#endif
    }
    return 1;
}

int XStoreName(Display *dpy, Window w, const char *window_name) {
    sdl_drawable_t *drw = find_drawable(dpy, w);
    if (drw && drw->is_toplevel && dpy->sdl_win && window_name)
        SDL_SetWindowTitle(dpy->sdl_win, window_name);
    return 1;
}

int XSelectInput(Display *dpy, Window w, long event_mask) {
    (void)dpy;
    (void)w;
    (void)event_mask;
    return 1;
}

Status XGetWindowAttributes(Display *dpy, Window w, XWindowAttributes *window_attributes_return) {
    if (!dpy || !window_attributes_return)
        return 0;
    memset(window_attributes_return, 0, sizeof(*window_attributes_return));
    sdl_drawable_t *drw = find_drawable(dpy, w);
    if (!drw)
        return 0;
    if (drw->is_toplevel && dpy->sdl_win) {
        SDL_GetWindowPosition(dpy->sdl_win, &drw->x, &drw->y);
        SDL_GetWindowSize(dpy->sdl_win, &drw->width, &drw->height);
    }
    window_attributes_return->x = drw->x;
    window_attributes_return->y = drw->y;
    window_attributes_return->width = drw->width;
    window_attributes_return->height = drw->height;
    window_attributes_return->map_state = drw->map_state;
    return 1;
}

Bool XTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y,
                           int *dest_x_return, int *dest_y_return, Window *child_return) {
    (void)dest_w;
    if (child_return)
        *child_return = None;
    sdl_drawable_t *drw = find_drawable(dpy, src_w);
    if (!drw)
        return False;
    if (drw->is_toplevel && dpy->sdl_win)
        SDL_GetWindowPosition(dpy->sdl_win, &drw->x, &drw->y);
    if (dest_x_return)
        *dest_x_return = drw->x + src_x;
    if (dest_y_return)
        *dest_y_return = drw->y + src_y;
    return True;
}

int XSetInputFocus(Display *dpy, Window focus, int revert_to, Time time) {
    (void)focus;
    (void)revert_to;
    (void)time;
    if (dpy && dpy->sdl_win)
        SDL_RaiseWindow(dpy->sdl_win);
    return 1;
}

int XGetInputFocus(Display *dpy, Window *focus_return, int *revert_to_return) {
    if (focus_return)
        *focus_return = dpy ? dpy->toplevel_id : None;
    if (revert_to_return)
        *revert_to_return = RevertToParent;
    return 1;
}

Atom XInternAtom(Display *dpy, const char *atom_name, Bool only_if_exists) {
    if (!dpy || !atom_name)
        return None;
    for (size_t i = 0; i < dpy->atom_count; ++i) {
        if (strcmp(dpy->atom_names[i], atom_name) == 0)
            return (Atom)(100 + i);
    }
    if (only_if_exists || dpy->atom_count >= MAX_ATOMS)
        return None;
    size_t len = strlen(atom_name);
    char *copy = malloc(len + 1u);
    if (!copy)
        return None;
    memcpy(copy, atom_name, len + 1u);
    dpy->atom_names[dpy->atom_count] = copy;
    return (Atom)(100 + dpy->atom_count++);
}

Status XSetWMProtocols(Display *dpy, Window w, Atom *protocols, int count) {
    (void)w;
    if (!dpy || !protocols || count <= 0)
        return 0;
    dpy->wm_delete_atom = protocols[0];
    return 1;
}

int XChangeProperty(Display *dpy, Window w, Atom property, Atom type, int format, int mode,
                    const unsigned char *data, int nelements) {
    (void)dpy;
    (void)w;
    (void)property;
    (void)type;
    (void)format;
    (void)mode;
    (void)data;
    (void)nelements;
    return 1;
}

int XGetWindowProperty(Display *dpy, Window w, Atom property, long long_offset, long long_length,
                       Bool delete_prop, Atom req_type, Atom *actual_type_return,
                       int *actual_format_return, unsigned long *nitems_return,
                       unsigned long *bytes_after_return, unsigned char **prop_return) {
    (void)w;
    (void)long_offset;
    (void)long_length;
    (void)delete_prop;
    (void)req_type;
    if (actual_type_return)
        *actual_type_return = None;
    if (actual_format_return)
        *actual_format_return = 0;
    if (nitems_return)
        *nitems_return = 0;
    if (bytes_after_return)
        *bytes_after_return = 0;
    if (prop_return)
        *prop_return = NULL;
    if (!dpy || !prop_return)
        return BadWindow;

    if (property == dpy->net_wm_state_atom) {
        if (dpy->is_fullscreen) {
            Atom *atom_out = malloc(sizeof(Atom));
            if (!atom_out)
                return BadWindow;
            *atom_out = dpy->net_wm_fullscreen_atom;
            if (actual_type_return)
                *actual_type_return = XA_ATOM;
            if (actual_format_return)
                *actual_format_return = 32;
            if (nitems_return)
                *nitems_return = 1;
            *prop_return = (unsigned char *)atom_out;
        }
        return Success;
    }

    if (property == dpy->paste_property_atom && dpy->paste_text) {
        size_t len = strlen(dpy->paste_text);
        unsigned char *buf = malloc(len + 1u);
        if (!buf)
            return BadWindow;
        memcpy(buf, dpy->paste_text, len + 1u);
        if (actual_type_return)
            *actual_type_return = XA_STRING;
        if (actual_format_return)
            *actual_format_return = 8;
        if (nitems_return)
            *nitems_return = (unsigned long)len;
        *prop_return = buf;
        return Success;
    }

    return Success;
}

int XConvertSelection(Display *dpy, Atom selection, Atom target, Atom property, Window requestor,
                      Time time) {
    if (!dpy)
        return 0;
    free(dpy->paste_text);
    dpy->paste_text = NULL;
    if (SDL_HasClipboardText()) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            size_t len = strlen(clip);
            dpy->paste_text = malloc(len + 1u);
            if (dpy->paste_text)
                memcpy(dpy->paste_text, clip, len + 1u);
            SDL_free(clip);
        }
    }
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = SelectionNotify;
    xev.xselection.display = dpy;
    xev.xselection.requestor = requestor;
    xev.xselection.selection = selection;
    xev.xselection.target = target;
    xev.xselection.property = dpy->paste_text ? property : None;
    xev.xselection.time = time;
    enqueue_event(dpy, &xev);
    return 1;
}

Status XSendEvent(Display *dpy, Window w, Bool propagate, long event_mask, XEvent *event_send) {
    (void)w;
    (void)propagate;
    (void)event_mask;
    if (!dpy || !event_send)
        return 0;
    if (event_send->type == ClientMessage &&
        event_send->xclient.message_type == dpy->net_wm_state_atom &&
        (Atom)event_send->xclient.data.l[1] == dpy->net_wm_fullscreen_atom) {
        bool enable = event_send->xclient.data.l[0] != 0;
        if (dpy->sdl_win) {
            (void)SDL_SetWindowFullscreen(dpy->sdl_win,
                                          enable ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            dpy->is_fullscreen = enable;
            int nw = 0, nh = 0;
            SDL_GetWindowSize(dpy->sdl_win, &nw, &nh);
            sdl_drawable_t *top = find_drawable(dpy, dpy->toplevel_id);
            if (top && nw > 0 && nh > 0)
                (void)ensure_drawable_pixels(top, nw, nh);
            XEvent cfg;
            memset(&cfg, 0, sizeof(cfg));
            cfg.type = ConfigureNotify;
            cfg.xconfigure.display = dpy;
            cfg.xconfigure.window = dpy->toplevel_id;
            cfg.xconfigure.width = nw;
            cfg.xconfigure.height = nh;
            enqueue_event(dpy, &cfg);

            XEvent prop;
            memset(&prop, 0, sizeof(prop));
            prop.type = PropertyNotify;
            prop.xproperty.display = dpy;
            prop.xproperty.window = dpy->toplevel_id;
            prop.xproperty.atom = dpy->net_wm_state_atom;
            enqueue_event(dpy, &prop);
        }
        return 1;
    }
    enqueue_event(dpy, event_send);
    return 1;
}

int XPending(Display *dpy) {
    if (!dpy)
        return 0;
    pump_sdl_events(dpy);
    return (int)dpy->queue_len;
}

int XNextEvent(Display *dpy, XEvent *event_return) {
    if (!dpy || !event_return)
        return 0;
    while (dpy->queue_len == 0) {
        pump_sdl_events(dpy);
        if (dpy->queue_len == 0)
            SDL_WaitEventTimeout(NULL, 16);
    }
    *event_return = dpy->queue[dpy->queue_head];
    dpy->queue_head = (dpy->queue_head + 1u) % EVENT_QUEUE_CAP;
    --dpy->queue_len;
    return 0;
}

int XFlush(Display *dpy) {
    if (!dpy || !dpy->sdl_renderer)
        return 0;
    sdl_drawable_t *top = find_drawable(dpy, dpy->toplevel_id);
    if (!top || !top->pixels || top->width <= 0 || top->height <= 0 || !top->dirty)
        return 0;
    if (!dpy->sdl_texture || dpy->tex_w != top->width || dpy->tex_h != top->height) {
        if (dpy->sdl_texture)
            SDL_DestroyTexture(dpy->sdl_texture);
        dpy->sdl_texture = SDL_CreateTexture(dpy->sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, top->width, top->height);
        dpy->tex_w = top->width;
        dpy->tex_h = top->height;
    }
    if (dpy->sdl_texture) {
        SDL_UpdateTexture(dpy->sdl_texture, NULL, top->pixels, top->width * (int)sizeof(uint32_t));
        SDL_RenderCopy(dpy->sdl_renderer, dpy->sdl_texture, NULL, NULL);
        SDL_RenderPresent(dpy->sdl_renderer);
    }
    top->dirty = false;
    return 1;
}

int XSync(Display *dpy, Bool discard) {
    (void)discard;
    pump_sdl_events(dpy);
    return XFlush(dpy);
}

int XFree(void *data) {
    free(data);
    return 1;
}

Status XAllocNamedColor(Display *dpy, Colormap colormap, const char *color_name,
                        XColor *screen_def_return, XColor *exact_def_return) {
    (void)dpy;
    (void)colormap;
    if (!color_name || !screen_def_return)
        return 0;
    unsigned int r = 0, g = 0, b = 0;
    if (color_name[0] == '#' && strlen(color_name) == 7 &&
        sscanf(color_name + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
        unsigned long pixel = 0xFF000000UL | ((unsigned long)r << 16) | ((unsigned long)g << 8) |
                              (unsigned long)b;
        screen_def_return->pixel = pixel;
        screen_def_return->red = (unsigned short)(r * 257u);
        screen_def_return->green = (unsigned short)(g * 257u);
        screen_def_return->blue = (unsigned short)(b * 257u);
        if (exact_def_return)
            *exact_def_return = *screen_def_return;
        return 1;
    }
    return 0;
}

GC XCreateGC(Display *dpy, Drawable d, unsigned long valuemask, XGCValues *values) {
    (void)dpy;
    (void)d;
    (void)valuemask;
    (void)values;
    GC gc = calloc(1, sizeof(*gc));
    if (gc)
        gc->foreground = 0xFFFFFFFFUL;
    return gc;
}

int XFreeGC(Display *dpy, GC gc) {
    (void)dpy;
    free(gc);
    return 1;
}

int XSetForeground(Display *dpy, GC gc, unsigned long foreground) {
    (void)dpy;
    if (gc)
        gc->foreground = foreground;
    return 1;
}

int XSetFont(Display *dpy, GC gc, Font font) {
    (void)dpy;
    if (gc)
        gc->font = font;
    return 1;
}

int XSetGraphicsExposures(Display *dpy, GC gc, Bool graphics_exposures) {
    (void)dpy;
    (void)gc;
    (void)graphics_exposures;
    return 1;
}

int XSetClipRectangles(Display *dpy, GC gc, int clip_x_origin, int clip_y_origin,
                       XRectangle rectangles[], int n, int ordering) {
    (void)dpy;
    (void)clip_x_origin;
    (void)clip_y_origin;
    (void)ordering;
    if (!gc)
        return 0;
    if (rectangles && n > 0) {
        gc->clip_enabled = true;
        gc->clip_rect = rectangles[0];
    } else {
        gc->clip_enabled = false;
    }
    return 1;
}

int XSetClipMask(Display *dpy, GC gc, Pixmap pixmap) {
    (void)dpy;
    if (gc && pixmap == None)
        gc->clip_enabled = false;
    return 1;
}

XFontStruct *XLoadQueryFont(Display *dpy, const char *name) {
    (void)dpy;
    (void)name;
    XFontStruct *f = calloc(1, sizeof(*f));
    if (f) {
        f->fid = 1;
        f->ascent = 12;
        f->descent = 4;
    }
    return f;
}

int XFreeFont(Display *dpy, XFontStruct *font_struct) {
    (void)dpy;
    free(font_struct);
    return 1;
}

int XTextWidth(XFontStruct *font_struct, const char *string, int count) {
    (void)font_struct;
    if (!string || count <= 0)
        return 0;
    return count * 8;
}

static void put_pixel_clipped(sdl_drawable_t *drw, GC gc, int x, int y, uint32_t pixel) {
    if (!drw || !drw->pixels || x < 0 || y < 0 || x >= drw->width || y >= drw->height)
        return;
    if (gc && gc->clip_enabled) {
        int cx0 = gc->clip_rect.x;
        int cy0 = gc->clip_rect.y;
        int cx1 = cx0 + (int)gc->clip_rect.width;
        int cy1 = cy0 + (int)gc->clip_rect.height;
        if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1)
            return;
    }
    drw->pixels[(size_t)y * (size_t)drw->width + (size_t)x] = pixel | 0xFF000000u;
}

int XFillRectangle(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height) {
    sdl_drawable_t *drw = find_drawable(dpy, d);
    if (!drw || !drw->pixels || !gc)
        return 0;
    uint32_t color = (uint32_t)gc->foreground | 0xFF000000u;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + (int)width;
    int y1 = y + (int)height;
    if (x1 > drw->width)
        x1 = drw->width;
    if (y1 > drw->height)
        y1 = drw->height;
    if (gc->clip_enabled) {
        int cx0 = gc->clip_rect.x;
        int cy0 = gc->clip_rect.y;
        int cx1 = cx0 + (int)gc->clip_rect.width;
        int cy1 = cy0 + (int)gc->clip_rect.height;
        if (x0 < cx0)
            x0 = cx0;
        if (y0 < cy0)
            y0 = cy0;
        if (x1 > cx1)
            x1 = cx1;
        if (y1 > cy1)
            y1 = cy1;
    }
    for (int py = y0; py < y1; ++py) {
        uint32_t *row = drw->pixels + (size_t)py * (size_t)drw->width;
        for (int px = x0; px < x1; ++px)
            row[px] = color;
    }
    drw->dirty = true;
    return 1;
}

int XDrawRectangle(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height) {
    XDrawLine(dpy, d, gc, x, y, x + (int)width, y);
    XDrawLine(dpy, d, gc, x, y + (int)height, x + (int)width, y + (int)height);
    XDrawLine(dpy, d, gc, x, y, x, y + (int)height);
    XDrawLine(dpy, d, gc, x + (int)width, y, x + (int)width, y + (int)height);
    return 1;
}

int XDrawLine(Display *dpy, Drawable d, GC gc, int x1, int y1, int x2, int y2) {
    sdl_drawable_t *drw = find_drawable(dpy, d);
    if (!drw || !drw->pixels || !gc)
        return 0;
    uint32_t color = (uint32_t)gc->foreground | 0xFF000000u;
    int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_pixel_clipped(drw, gc, x1, y1, color);
        if (x1 == x2 && y1 == y2)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
    drw->dirty = true;
    return 1;
}

int XFillArc(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2) {
    (void)angle1;
    (void)angle2;
    return XFillRectangle(dpy, d, gc, x, y, width, height);
}

int XDrawArc(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2) {
    (void)angle1;
    (void)angle2;
    return XDrawRectangle(dpy, d, gc, x, y, width, height);
}

int XDrawString(Display *dpy, Drawable d, GC gc, int x, int y, const char *string, int length) {
    (void)dpy;
    (void)d;
    (void)gc;
    (void)x;
    (void)y;
    (void)string;
    (void)length;
    return 1;
}

Pixmap XCreatePixmap(Display *dpy, Drawable d, unsigned int width, unsigned int height, unsigned int depth) {
    (void)d;
    (void)depth;
    sdl_drawable_t *drw = alloc_drawable(dpy);
    if (!drw)
        return None;
    drw->is_pixmap = true;
    if (!ensure_drawable_pixels(drw, (int)width, (int)height)) {
        drw->in_use = false;
        return None;
    }
    return drw->id;
}

int XFreePixmap(Display *dpy, Pixmap pixmap) {
    sdl_drawable_t *drw = find_drawable(dpy, pixmap);
    if (!drw || !drw->is_pixmap)
        return 0;
    free(drw->pixels);
    drw->pixels = NULL;
    drw->in_use = false;
    return 1;
}

int XCopyArea(Display *dpy, Drawable src, Drawable dest, GC gc, int src_x, int src_y, unsigned int width,
              unsigned int height, int dest_x, int dest_y) {
    (void)gc;
    sdl_drawable_t *s = find_drawable(dpy, src);
    sdl_drawable_t *d = find_drawable(dpy, dest);
    if (!s || !d || !s->pixels || !d->pixels)
        return 0;
    int w = (int)width;
    int h = (int)height;
    for (int y = 0; y < h; ++y) {
        int sy = src_y + y;
        int dy = dest_y + y;
        if (sy < 0 || sy >= s->height || dy < 0 || dy >= d->height)
            continue;
        int sx0 = src_x < 0 ? 0 : src_x;
        int dx0 = dest_x + (sx0 - src_x);
        int copy_w = w - (sx0 - src_x);
        if (sx0 + copy_w > s->width)
            copy_w = s->width - sx0;
        if (dx0 < 0) {
            sx0 -= dx0;
            copy_w += dx0;
            dx0 = 0;
        }
        if (dx0 + copy_w > d->width)
            copy_w = d->width - dx0;
        if (copy_w > 0) {
            memcpy(d->pixels + (size_t)dy * (size_t)d->width + (size_t)dx0,
                   s->pixels + (size_t)sy * (size_t)s->width + (size_t)sx0,
                   (size_t)copy_w * sizeof(uint32_t));
        }
    }
    d->dirty = true;
    return 1;
}

XImage *XCreateImage(Display *dpy, Visual *visual, unsigned int depth, int format, int offset, char *data,
                     unsigned int width, unsigned int height, int bitmap_pad, int bytes_per_line) {
    (void)dpy;
    (void)visual;
    (void) format;
    (void)offset;
    (void)bitmap_pad;
    if (width == 0 || height == 0)
        return NULL;
    XImage *img = calloc(1, sizeof(*img));
    if (!img)
        return NULL;
    img->width = (int)width;
    img->height = (int)height;
    img->depth = (int)depth;
    img->bits_per_pixel = 32;
    img->bytes_per_line = bytes_per_line > 0 ? bytes_per_line : (int)width * 4;
    img->data = data;
    img->red_mask = 0x00FF0000UL;
    img->green_mask = 0x0000FF00UL;
    img->blue_mask = 0x000000FFUL;
    return img;
}

int XDestroyImage(XImage *ximage) {
    if (!ximage)
        return 0;
    free(ximage->data);
    free(ximage);
    return 1;
}

int XPutPixel(XImage *ximage, int x, int y, unsigned long pixel) {
    if (!ximage || !ximage->data || x < 0 || y < 0 || x >= ximage->width || y >= ximage->height)
        return 0;
    uint32_t *row = (uint32_t *)(void *)(ximage->data + (size_t)y * (size_t)ximage->bytes_per_line);
    row[x] = (uint32_t)pixel | 0xFF000000u;
    return 1;
}

unsigned long XGetPixel(XImage *ximage, int x, int y) {
    if (!ximage || !ximage->data || x < 0 || y < 0 || x >= ximage->width || y >= ximage->height)
        return 0;
    const uint32_t *row =
        (const uint32_t *)(const void *)(ximage->data + (size_t)y * (size_t)ximage->bytes_per_line);
    return (unsigned long)row[x];
}

int XPutImage(Display *dpy, Drawable d, GC gc, XImage *image, int src_x, int src_y, int dest_x, int dest_y,
              unsigned int width, unsigned int height) {
    sdl_drawable_t *drw = find_drawable(dpy, d);
    if (!drw || !drw->pixels || !image || !image->data)
        return 0;
    int w = (int)width;
    int h = (int)height;
    for (int y = 0; y < h; ++y) {
        int sy = src_y + y;
        int dy = dest_y + y;
        if (sy < 0 || sy >= image->height || dy < 0 || dy >= drw->height)
            continue;
        const uint32_t *src_row =
            (const uint32_t *)(const void *)(image->data + (size_t)sy * (size_t)image->bytes_per_line);
        for (int x = 0; x < w; ++x) {
            int sx = src_x + x;
            int dx = dest_x + x;
            if (sx < 0 || sx >= image->width)
                continue;
            put_pixel_clipped(drw, gc, dx, dy, src_row[sx]);
        }
    }
    drw->dirty = true;
    return 1;
}

KeySym XLookupKeysym(XKeyEvent *key_event, int index) {
    (void)index;
    return key_event ? key_event->keysym : NoSymbol;
}

int XLookupString(XKeyEvent *event_struct, char *buffer_return, int bytes_buffer, KeySym *keysym_return,
                  XComposeStatus *status_in_out) {
    (void)status_in_out;
    if (keysym_return)
        *keysym_return = event_struct ? event_struct->keysym : NoSymbol;
    if (!buffer_return || bytes_buffer <= 0 || !event_struct)
        return 0;
    size_t len = strlen(event_struct->text);
    if (len >= (size_t)bytes_buffer)
        len = (size_t)bytes_buffer - 1u;
    memcpy(buffer_return, event_struct->text, len);
    buffer_return[len] = '\0';
    return (int)len;
}

const char *XKeysymToString(KeySym keysym) {
    switch (keysym) {
    case XK_BackSpace:
        return "BackSpace";
    case XK_Tab:
        return "Tab";
    case XK_Return:
        return "Return";
    case XK_Escape:
        return "Escape";
    case XK_space:
        return "space";
    case XK_Left:
        return "Left";
    case XK_Up:
        return "Up";
    case XK_Right:
        return "Right";
    case XK_Down:
        return "Down";
    case XK_F2:
        return "F2";
    case XK_F11:
        return "F11";
    default:
        return "KeySym";
    }
}

void *vip_sdl2_cairo_surface_create(Display *dpy, Drawable drawable, Visual *visual, int width, int height) {
    (void)visual;
    sdl_drawable_t *drw = find_drawable(dpy, drawable);
    if (!drw || !ensure_drawable_pixels(drw, width, height))
        return NULL;
    drw->dirty = true;
    return cairo_image_surface_create_for_data((unsigned char *)drw->pixels, CAIRO_FORMAT_RGB24, width,
                                               height, width * 4);
}

void vip_sdl2_wait_events(Display *dpy, int timeout_ms) {
    if (!dpy)
        return;
    pump_sdl_events(dpy);
    if (dpy->queue_len == 0 && timeout_ms > 0) {
        SDL_WaitEventTimeout(NULL, timeout_ms);
        pump_sdl_events(dpy);
    }
}
