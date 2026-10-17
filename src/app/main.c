/* SPDX-License-Identifier: MIT */
/*
 * Process entry point that hands control to the graphical hub.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#include "visual_iptv/hub.h"
#include <X11/Xlib.h>

/* Run this executable's main entry point. */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    XInitThreads();
    return vip_hub_run();
}
