/* SPDX-License-Identifier: MIT */
#ifndef VIP_COMPAT_X11_XUTIL_H
#define VIP_COMPAT_X11_XUTIL_H

#include <X11/Xlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int dummy;
} XComposeStatus;

int XLookupString(XKeyEvent *event_struct, char *buffer_return, int bytes_buffer, KeySym *keysym_return,
                  XComposeStatus *status_in_out);

#ifdef __cplusplus
}
#endif

#endif
