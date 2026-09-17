/* SPDX-License-Identifier: MIT */
/*
 * Public API contract for server resolver.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#ifndef VISUAL_IPTV_SERVER_RESOLVER_H
#define VISUAL_IPTV_SERVER_RESOLVER_H

#include "visual_iptv/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *primary;
    char *alternate;
} vip_server_resolution_t;

/* Implement the vip_server_resolution_clear helper. */
void vip_server_resolution_clear(vip_server_resolution_t *resolution);

/* Resolve StreamFire/Spark code 11 credentials into verified Xtream bases.
 * The credentials are POSTed only when the UI explicitly invokes this
 * fallback after an Xtream endpoint failure (currently HTTP 404). */
vip_status_t vip_streamfire_resolve_servers(const char *username, const char *password,
                                            vip_server_resolution_t *out, vip_error_t *error);

/* Pure helpers kept public so the wire-format decoder can be regression-tested
 * without performing network requests. */
vip_status_t vip_streamfire_decode_payload(const char *payload, const char *identity, char **json_out,
                                           vip_error_t *error);
/* Implement the vip_streamfire_collect_bases helper. */
vip_status_t vip_streamfire_collect_bases(const char *json, char ***bases_out, size_t *count_out,
                                          vip_error_t *error);
/* Implement the vip_streamfire_free_bases helper. */
void vip_streamfire_free_bases(char **bases, size_t count);

#ifdef __cplusplus
}
#endif

#endif
