/* SPDX-License-Identifier: MIT */
#ifndef VIP_X11_KEYRING_H
#define VIP_X11_KEYRING_H

#include <stdbool.h>
#include <stddef.h>

/* Store password in the Secret Service via secret-tool. */
bool keyring_store_password(const char *profile_id, const char *password);

/* Look up password from the Secret Service via secret-tool. */
bool keyring_lookup_password(const char *profile_id, char *out, size_t cap);

#endif /* VIP_X11_KEYRING_H */
