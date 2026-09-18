/* SPDX-License-Identifier: MIT */
#ifndef VISUAL_IPTV_PAIRING_RELAY_H
#define VISUAL_IPTV_PAIRING_RELAY_H

#include "visual_iptv/core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct vip_pairing_relay vip_pairing_relay_t;

typedef void (*vip_pairing_submit_fn)(const char *profile_name,
                                      const char *playlist_url,
                                      void *userdata);

/*
 * Create a short-lived pairing session on an HTTPS relay and poll it from a
 * background thread. The relay never receives the AES key used by the phone.
 */
vip_status_t vip_pairing_relay_start(vip_pairing_relay_t **out_relay,
                                     const char *base_url,
                                     vip_pairing_submit_fn on_submit,
                                     void *userdata,
                                     vip_error_t *error);

/* Stop polling, delete the remote session when possible and wipe key material. */
void vip_pairing_relay_stop(vip_pairing_relay_t *relay);

/* Full phone URL. The AES key exists only in its URL fragment after '#'. */
const char *vip_pairing_relay_page_url(const vip_pairing_relay_t *relay);

/* Random 128-bit session identifier encoded as 32 lowercase hex characters. */
const char *vip_pairing_relay_session_id(const vip_pairing_relay_t *relay);

/* True after delivery, expiration or terminal relay shutdown. */
bool vip_pairing_relay_finished(const vip_pairing_relay_t *relay);

/*
 * Decrypt one relay payload. Public for deterministic crypto regression tests;
 * normal application code receives plaintext through the submit callback.
 */
vip_status_t vip_pairing_relay_decrypt_payload(const uint8_t key[32],
                                               const char *payload_json,
                                               char *profile_name,
                                               size_t profile_name_size,
                                               char *playlist_url,
                                               size_t playlist_url_size,
                                               vip_error_t *error);

#endif
