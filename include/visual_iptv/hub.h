/* SPDX-License-Identifier: MIT */
/*
 * Public API contract for hub.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#ifndef VISUAL_IPTV_HUB_H
#define VISUAL_IPTV_HUB_H

#ifdef __cplusplus
extern "C" {
#endif

/* Implement the vip_hub_run helper. */
int vip_hub_run(void);
/* Implement the vip_pluto_app_run helper. */
int vip_pluto_app_run(void);

#ifdef __cplusplus
}
#endif

#endif
