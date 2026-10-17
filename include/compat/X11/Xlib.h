/* SPDX-License-Identifier: MIT */
/*
 * Cross-platform Xlib subset header backed by SDL2 + Cairo image surfaces.
 * Used on Windows (_WIN32) and when VIPTV_USE_SDL2 is enabled.
 */
#ifndef VIP_COMPAT_X11_XLIB_H
#define VIP_COMPAT_X11_XLIB_H

#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int Bool;
typedef int Status;
typedef unsigned long XID;
typedef XID Window;
typedef XID Drawable;
typedef XID Pixmap;
typedef XID Colormap;
typedef XID Font;
typedef unsigned long Atom;
typedef unsigned long KeySym;
typedef unsigned long Time;

#ifndef True
#define True 1
#endif
#ifndef False
#define False 0
#endif
#ifndef None
#define None 0UL
#endif
#ifndef CurrentTime
#define CurrentTime 0UL
#endif
#ifndef NoSymbol
#define NoSymbol 0UL
#endif
#ifndef Success
#define Success 0
#endif
#ifndef BadWindow
#define BadWindow 3
#endif
#ifndef AnyPropertyType
#define AnyPropertyType 0UL
#endif
#ifndef PropModeReplace
#define PropModeReplace 0
#endif
#ifndef ZPixmap
#define ZPixmap 2
#endif
#ifndef Unsorted
#define Unsorted 0
#endif
#ifndef RevertToParent
#define RevertToParent 2
#endif
#ifndef NotifyInferior
#define NotifyInferior 2
#endif
#ifndef IsUnmapped
#define IsUnmapped 0
#endif
#ifndef IsUnviewable
#define IsUnviewable 1
#endif
#ifndef IsViewable
#define IsViewable 2
#endif
#ifndef InputOutput
#define InputOutput 1
#endif
#ifndef InputOnly
#define InputOnly 2
#endif
#ifndef CopyFromParent
#define CopyFromParent 0L
#endif

#define CWBackPixel (1L << 1)
#define CWBorderPixel (1L << 3)
#define CWOverrideRedirect (1L << 9)
#define CWEventMask (1L << 11)

/* Event types */
#define KeyPress 2
#define KeyRelease 3
#define ButtonPress 4
#define ButtonRelease 5
#define MotionNotify 6
#define EnterNotify 7
#define LeaveNotify 8
#define FocusIn 9
#define FocusOut 10
#define Expose 12
#define UnmapNotify 18
#define MapNotify 19
#define ConfigureNotify 22
#define PropertyNotify 28
#define SelectionClear 29
#define SelectionRequest 30
#define SelectionNotify 31
#define ClientMessage 33

/* Event masks */
#define NoEventMask 0L
#define KeyPressMask (1L << 0)
#define KeyReleaseMask (1L << 1)
#define ButtonPressMask (1L << 2)
#define ButtonReleaseMask (1L << 3)
#define EnterWindowMask (1L << 4)
#define LeaveWindowMask (1L << 5)
#define PointerMotionMask (1L << 6)
#define ExposureMask (1L << 15)
#define StructureNotifyMask (1L << 17)
#define SubstructureNotifyMask (1L << 19)
#define SubstructureRedirectMask (1L << 20)
#define FocusChangeMask (1L << 21)
#define PropertyChangeMask (1L << 22)

/* Key/Button masks and buttons */
enum {
    ShiftMask = (1u << 0),
    LockMask = (1u << 1),
    ControlMask = (1u << 2),
    Mod1Mask = (1u << 3),
    Button1 = 1u,
    Button2 = 2u,
    Button3 = 3u,
    Button4 = 4u,
    Button5 = 5u
};

typedef struct _XDisplay Display;
typedef struct _XGC *GC;

typedef struct {
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
} Visual;

typedef struct {
    unsigned long pixel;
    unsigned short red, green, blue;
    char flags;
    char pad;
} XColor;

typedef struct {
    Font fid;
    int ascent;
    int descent;
} XFontStruct;

typedef struct {
    short x, y;
    unsigned short width, height;
} XRectangle;

typedef struct {
    int x, y;
    int width, height;
    int map_state;
} XWindowAttributes;

typedef struct {
    long event_mask;
    Bool override_redirect;
    unsigned long background_pixel;
    unsigned long border_pixel;
} XSetWindowAttributes;

typedef struct {
    int dummy;
} XGCValues;

typedef struct _XImage {
    int width, height;
    int xoffset;
    int format;
    char *data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
} XImage;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int x, y;
    int width, height;
    int count;
} XExposeEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window event;
    Window window;
    int x, y;
    int width, height;
    int border_width;
    Window above;
    Bool override_redirect;
} XConfigureEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Atom message_type;
    int format;
    union {
        char b[20];
        short s[10];
        long l[5];
    } data;
} XClientMessageEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Atom atom;
    Time time;
    int state;
} XPropertyEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    char is_hint;
    Bool same_screen;
} XMotionEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int button;
    Bool same_screen;
} XButtonEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    Window root;
    Window subwindow;
    Time time;
    int x, y;
    int x_root, y_root;
    unsigned int state;
    unsigned int keycode;
    Bool same_screen;
    KeySym keysym;
    char text[32];
} XKeyEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int mode;
    int detail;
} XFocusChangeEvent;

typedef struct {
    int type;
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window requestor;
    Atom selection;
    Atom target;
    Atom property;
    Time time;
} XSelectionEvent;

typedef union _XEvent {
    int type;
    XExposeEvent xexpose;
    XConfigureEvent xconfigure;
    XClientMessageEvent xclient;
    XPropertyEvent xproperty;
    XMotionEvent xmotion;
    XButtonEvent xbutton;
    XKeyEvent xkey;
    XFocusChangeEvent xfocus;
    XSelectionEvent xselection;
    long pad[24];
} XEvent;

typedef int (*XErrorHandler)(Display *, void *);

Status XInitThreads(void);
Display *XOpenDisplay(const char *display_name);
int XCloseDisplay(Display *dpy);
int DefaultScreen(Display *dpy);
Window RootWindow(Display *dpy, int screen_number);
Visual *DefaultVisual(Display *dpy, int screen_number);
int DefaultDepth(Display *dpy, int screen_number);
Colormap DefaultColormap(Display *dpy, int screen_number);
unsigned long BlackPixel(Display *dpy, int screen_number);
unsigned long WhitePixel(Display *dpy, int screen_number);
int DisplayWidth(Display *dpy, int screen_number);
int DisplayHeight(Display *dpy, int screen_number);
int ConnectionNumber(Display *dpy);

Window XCreateSimpleWindow(Display *dpy, Window parent, int x, int y, unsigned int width,
                           unsigned int height, unsigned int border_width, unsigned long border,
                           unsigned long background);
Window XCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                     unsigned int border_width, int depth, unsigned int c_class, Visual *visual,
                     unsigned long valuemask, XSetWindowAttributes *attributes);
int XDestroyWindow(Display *dpy, Window w);
int XMapWindow(Display *dpy, Window w);
int XMapRaised(Display *dpy, Window w);
int XUnmapWindow(Display *dpy, Window w);
int XRaiseWindow(Display *dpy, Window w);
int XMoveResizeWindow(Display *dpy, Window w, int x, int y, unsigned int width, unsigned int height);
int XStoreName(Display *dpy, Window w, const char *window_name);
int XSelectInput(Display *dpy, Window w, long event_mask);
Status XGetWindowAttributes(Display *dpy, Window w, XWindowAttributes *window_attributes_return);
Bool XTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y,
                           int *dest_x_return, int *dest_y_return, Window *child_return);
int XSetInputFocus(Display *dpy, Window focus, int revert_to, Time time);
int XGetInputFocus(Display *dpy, Window *focus_return, int *revert_to_return);

Atom XInternAtom(Display *dpy, const char *atom_name, Bool only_if_exists);
Status XSetWMProtocols(Display *dpy, Window w, Atom *protocols, int count);
int XChangeProperty(Display *dpy, Window w, Atom property, Atom type, int format, int mode,
                    const unsigned char *data, int nelements);
int XGetWindowProperty(Display *dpy, Window w, Atom property, long long_offset, long long_length,
                       Bool delete_prop, Atom req_type, Atom *actual_type_return,
                       int *actual_format_return, unsigned long *nitems_return,
                       unsigned long *bytes_after_return, unsigned char **prop_return);
int XConvertSelection(Display *dpy, Atom selection, Atom target, Atom property, Window requestor,
                      Time time);
Status XSendEvent(Display *dpy, Window w, Bool propagate, long event_mask, XEvent *event_send);

int XPending(Display *dpy);
int XNextEvent(Display *dpy, XEvent *event_return);
int XFlush(Display *dpy);
int XSync(Display *dpy, Bool discard);
int XFree(void *data);

Status XAllocNamedColor(Display *dpy, Colormap colormap, const char *color_name,
                        XColor *screen_def_return, XColor *exact_def_return);

GC XCreateGC(Display *dpy, Drawable d, unsigned long valuemask, XGCValues *values);
int XFreeGC(Display *dpy, GC gc);
int XSetForeground(Display *dpy, GC gc, unsigned long foreground);
int XSetFont(Display *dpy, GC gc, Font font);
int XSetGraphicsExposures(Display *dpy, GC gc, Bool graphics_exposures);
int XSetClipRectangles(Display *dpy, GC gc, int clip_x_origin, int clip_y_origin,
                       XRectangle rectangles[], int n, int ordering);
int XSetClipMask(Display *dpy, GC gc, Pixmap pixmap);

XFontStruct *XLoadQueryFont(Display *dpy, const char *name);
int XFreeFont(Display *dpy, XFontStruct *font_struct);
int XTextWidth(XFontStruct *font_struct, const char *string, int count);

int XFillRectangle(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height);
int XDrawRectangle(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height);
int XDrawLine(Display *dpy, Drawable d, GC gc, int x1, int y1, int x2, int y2);
int XFillArc(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2);
int XDrawArc(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int width, unsigned int height,
             int angle1, int angle2);
int XDrawString(Display *dpy, Drawable d, GC gc, int x, int y, const char *string, int length);

Pixmap XCreatePixmap(Display *dpy, Drawable d, unsigned int width, unsigned int height, unsigned int depth);
int XFreePixmap(Display *dpy, Pixmap pixmap);
int XCopyArea(Display *dpy, Drawable src, Drawable dest, GC gc, int src_x, int src_y, unsigned int width,
              unsigned int height, int dest_x, int dest_y);

XImage *XCreateImage(Display *dpy, Visual *visual, unsigned int depth, int format, int offset, char *data,
                     unsigned int width, unsigned int height, int bitmap_pad, int bytes_per_line);
int XDestroyImage(XImage *ximage);
int XPutPixel(XImage *ximage, int x, int y, unsigned long pixel);
unsigned long XGetPixel(XImage *ximage, int x, int y);
int XPutImage(Display *dpy, Drawable d, GC gc, XImage *image, int src_x, int src_y, int dest_x, int dest_y,
              unsigned int width, unsigned int height);

KeySym XLookupKeysym(XKeyEvent *key_event, int index);
const char *XKeysymToString(KeySym keysym);

/* Helper used by vip_ui_renderer_begin when VIPTV_USE_SDL2 is enabled. */
void *vip_sdl2_cairo_surface_create(Display *dpy, Drawable drawable, Visual *visual, int width, int height);
void vip_sdl2_wait_events(Display *dpy, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
