#!/usr/bin/env python3
from pathlib import Path

path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

def rep(old, new, label):
    global s
    if old not in s:
        raise SystemExit(f'missing anchor: {label}')
    s = s.replace(old, new, 1)

rep('''    bool quit;\n    bool fullscreen;\n    bool mouse_down;\n''', '''    bool quit;\n    bool fullscreen;\n    bool fullscreen_requested;\n    bool fullscreen_fallback;\n    int fullscreen_attempts;\n    int64_t fullscreen_retry_at_ms;\n    bool windowed_geometry_valid;\n    int windowed_x;\n    int windowed_y;\n    int windowed_w;\n    int windowed_h;\n    bool mouse_down;\n''', 'app fullscreen state')

start = s.index('static void set_fullscreen(app_t *a, bool enable) {')
end = s.index('\n\n/* Enter playback without destroying', start)
new_block = r'''typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
} motif_wm_hints_t;

#define MWM_HINTS_DECORATIONS (1UL << 1)

static bool wm_reports_fullscreen(app_t *a) {
    if (!a || !a->dpy || !a->win) return false;
    Atom wm_state = XInternAtom(a->dpy, "_NET_WM_STATE", False);
    Atom fs = XInternAtom(a->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long nitems = 0, bytes_after = 0;
    unsigned char *data = NULL;
    bool found = false;
    if (XGetWindowProperty(a->dpy, a->win, wm_state, 0, 64, False, XA_ATOM,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &data) == Success && data && actual_format == 32) {
        Atom *atoms = (Atom *)data;
        for (unsigned long i = 0; i < nitems; ++i) {
            if (atoms[i] == fs) { found = true; break; }
        }
    }
    if (data) XFree(data);
    return found;
}

static void set_window_decorations(app_t *a, bool enabled) {
    if (!a || !a->dpy || !a->win) return;
    Atom motif = XInternAtom(a->dpy, "_MOTIF_WM_HINTS", False);
    motif_wm_hints_t hints = {0};
    hints.flags = MWM_HINTS_DECORATIONS;
    hints.decorations = enabled ? 1UL : 0UL;
    XChangeProperty(a->dpy, a->win, motif, motif, 32, PropModeReplace,
                    (unsigned char *)&hints, 5);
}

static void remember_windowed_geometry(app_t *a) {
    if (!a || !a->dpy || !a->win || a->windowed_geometry_valid) return;
    XWindowAttributes wa;
    if (!XGetWindowAttributes(a->dpy, a->win, &wa)) return;
    Window child = None;
    int root_x = 0, root_y = 0;
    if (!XTranslateCoordinates(a->dpy, a->win, RootWindow(a->dpy, a->screen_num),
                               0, 0, &root_x, &root_y, &child)) {
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
    if (!a || !a->dpy || !a->win) return;
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
    if (!a || !a->dpy || !a->win) return;
    set_window_decorations(a, false);
    int sw = DisplayWidth(a->dpy, a->screen_num);
    int sh = DisplayHeight(a->dpy, a->screen_num);
    if (sw < 1) sw = a->width;
    if (sh < 1) sh = a->height;
    XMoveResizeWindow(a->dpy, a->win, 0, 0, (unsigned)sw, (unsigned)sh);
    XRaiseWindow(a->dpy, a->win);
    XSync(a->dpy, False);
    a->fullscreen_fallback = true;
    a->fullscreen = true;
    layout_video_window(a);
}

static void set_fullscreen(app_t *a, bool enable) {
    if (!a || !a->dpy || !a->win) return;
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
            XMoveResizeWindow(a->dpy, a->win,
                              a->windowed_x, a->windowed_y,
                              (unsigned)a->windowed_w, (unsigned)a->windowed_h);
        }
        XFlush(a->dpy);
        a->windowed_geometry_valid = false;
    }
}

static void maybe_enforce_fullscreen(app_t *a) {
    if (!a || !a->fullscreen_requested || a->fullscreen ||
        monotonic_ms() < a->fullscreen_retry_at_ms) return;
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
'''
s = s[:start] + new_block + s[end:]

rep('''    if (!a->fullscreen) set_fullscreen(a, true);\n''', '''    if (!a->fullscreen_requested) set_fullscreen(a, true);\n''', 'enter fullscreen request')
rep('''    if (a->fullscreen) set_fullscreen(a, false);\n''', '''    if (a->fullscreen_requested || a->fullscreen) set_fullscreen(a, false);\n''', 'leave fullscreen request')
rep('''    if (sym == XK_F11) {\n        set_fullscreen(a, !a->fullscreen);\n''', '''    if (sym == XK_F11) {\n        set_fullscreen(a, !a->fullscreen_requested);\n''', 'F11 desired state')

rep('''    XSelectInput(a->dpy,a->win,ExposureMask|KeyPressMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|StructureNotifyMask|FocusChangeMask);\n''', '''    XSelectInput(a->dpy,a->win,ExposureMask|KeyPressMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|StructureNotifyMask|FocusChangeMask|PropertyChangeMask);\n''', 'property change mask')

old_prop = '''        case ClientMessage: if((Atom)e->xclient.data.l[0]==a->wm_delete)a->quit=true;break;\n'''
new_prop = '''        case ClientMessage: if((Atom)e->xclient.data.l[0]==a->wm_delete)a->quit=true;break;\n        case PropertyNotify:\n            if (e->xproperty.window == a->win &&\n                e->xproperty.atom == XInternAtom(a->dpy, "_NET_WM_STATE", False)) {\n                bool actual = wm_reports_fullscreen(a);\n                if (actual || !a->fullscreen_fallback) a->fullscreen = actual || a->fullscreen_fallback;\n                if (a->fullscreen) layout_video_window(a);\n            }\n            break;\n'''
rep(old_prop, new_prop, 'fullscreen property tracking')

rep('''        maybe_failover_player(&a);\n        sync_video_window(&a);\n''', '''        maybe_failover_player(&a);\n        maybe_enforce_fullscreen(&a);\n        sync_video_window(&a);\n''', 'main-loop fullscreen enforcement')

path.write_text(s)
print('patched fullscreen reliability')
