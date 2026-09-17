/* SPDX-License-Identifier: MIT */
/*
 * Process entry point that hands control to the graphical hub.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#include "visual_iptv/hub.h"

/* Run this executable's main entry point. */
int main(void) {
    return vip_hub_run();
}
