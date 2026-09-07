/* SPDX-License-Identifier: MIT */
/*
 * Core ownership and validation helpers shared by every module.
 *
 * This file deliberately stays dependency-light: provider, database, UI and
 * playback layers can all depend on it without creating circular dependencies.
 */
#include "visual_iptv/core.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *vip_strdup(const char *text) {
    if (!text) return NULL;
    size_t n = strlen(text) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, text, n);
    return copy;
}

char *vip_strdup_nullable(const char *text) {
    if (!text || text[0] == '\0') return NULL;
    return vip_strdup(text);
}

void vip_error_clear(vip_error_t *error) {
    if (!error) return;
    error->code = VIP_OK;
    error->message[0] = '\0';
}

void vip_error_set(vip_error_t *error, vip_status_t code, const char *fmt, ...) {
    if (!error) return;
    error->code = code;
    if (!fmt) {
        error->message[0] = '\0';
        return;
    }
    va_list ap;
    va_start(ap, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    vsnprintf(error->message, sizeof(error->message), fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    va_end(ap);
}

static uint64_t fnv1a64_update(uint64_t h, const char *text) {
    for (const unsigned char *p = (const unsigned char *)text; p && *p; ++p) {
        h ^= *p;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

/* Provider IDs intentionally identify an account, not merely a host, so two
 * users on the same Xtream server keep separate favorites/progress/cache. */
static uint64_t fnv1a64_account(const char *server, const char *username) {
    uint64_t h = UINT64_C(14695981039346656037);
    h = fnv1a64_update(h, server);
    h ^= UINT64_C(0xff);
    h *= UINT64_C(1099511628211);
    h = fnv1a64_update(h, username);
    return h;
}

/* Keep exactly one trailing slash because Xtream endpoint builders append
 * relative paths directly to this normalized base URL. */
static char *normalize_server(const char *server, vip_error_t *error) {
    if (!server) {
        vip_error_set(error, VIP_ERR_INVALID_URL, "servidor ausente");
        return NULL;
    }
    while (isspace((unsigned char)*server)) ++server;
    size_t len = strlen(server);
    while (len > 0 && isspace((unsigned char)server[len - 1])) --len;
    if (len < 8 || (strncmp(server, "http://", 7) != 0 && strncmp(server, "https://", 8) != 0)) {
        vip_error_set(error, VIP_ERR_INVALID_URL, "o servidor deve começar com http:// ou https://");
        return NULL;
    }
    size_t scheme = strncmp(server, "https://", 8) == 0 ? 8 : 7;
    if (len <= scheme || server[scheme] == '/') {
        vip_error_set(error, VIP_ERR_INVALID_URL, "URL de servidor inválida");
        return NULL;
    }
    while (len > scheme && server[len - 1] == '/') --len;
    char *out = malloc(len + 2);
    if (!out) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL do servidor");
        return NULL;
    }
    memcpy(out, server, len);
    out[len] = '/';
    out[len + 1] = '\0';
    return out;
}

vip_status_t vip_credentials_init(vip_credentials_t *out,
                                  const char *server,
                                  const char *username,
                                  const char *password,
                                  vip_error_t *error) {
    if (!out || !username || !password) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "credenciais inválidas");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->server = normalize_server(server, error);
    if (!out->server) return error ? error->code : VIP_ERR_INVALID_URL;
    out->username = vip_strdup(username);
    out->password = vip_strdup(password);
    if (!out->username || !out->password) {
        vip_credentials_clear(out);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para credenciais");
        return VIP_ERR_NOMEM;
    }
    snprintf(out->provider_id, sizeof(out->provider_id), "%016llx",
             (unsigned long long)fnv1a64_account(out->server, out->username));
    vip_error_clear(error);
    return VIP_OK;
}

void vip_credentials_clear(vip_credentials_t *credentials) {
    if (!credentials) return;
    free(credentials->server);
    free(credentials->username);
    if (credentials->password) {
        volatile char *p = credentials->password;
        size_t n = strlen(credentials->password);
        while (n--) *p++ = 0;
    }
    free(credentials->password);
    memset(credentials, 0, sizeof(*credentials));
}

void vip_category_list_init(vip_category_list_t *list) {
    if (list) memset(list, 0, sizeof(*list));
}

static void category_clear(vip_category_t *category) {
    if (!category) return;
    free(category->provider_id);
    free(category->id);
    free(category->name);
    memset(category, 0, sizeof(*category));
}

void vip_category_list_clear(vip_category_list_t *list) {
    if (!list) return;
    for (size_t i = 0; i < list->len; ++i) category_clear(&list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static vip_status_t reserve_categories(vip_category_list_t *list, size_t need, vip_error_t *error) {
    if (need <= list->cap) return VIP_OK;
    size_t cap = list->cap ? list->cap * 2 : 32;
    while (cap < need) cap *= 2;
    vip_category_t *items = realloc(list->items, cap * sizeof(*items));
    if (!items) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para categorias");
        return VIP_ERR_NOMEM;
    }
    list->items = items;
    list->cap = cap;
    return VIP_OK;
}

vip_status_t vip_category_list_push(vip_category_list_t *list,
                                    const vip_category_t *category,
                                    vip_error_t *error) {
    if (!list || !category || !category->id || !category->name) return VIP_ERR_INVALID_ARGUMENT;
    vip_status_t st = reserve_categories(list, list->len + 1, error);
    if (st != VIP_OK) return st;
    vip_category_t copy = {
        .provider_id = vip_strdup_nullable(category->provider_id),
        .id = vip_strdup(category->id),
        .name = vip_strdup(category->name),
        .position = category->position,
    };
    if (!copy.id || !copy.name || (category->provider_id && !copy.provider_id)) {
        category_clear(&copy);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para categoria");
        return VIP_ERR_NOMEM;
    }
    list->items[list->len++] = copy;
    return VIP_OK;
}

void vip_channel_list_init(vip_channel_list_t *list) {
    if (list) memset(list, 0, sizeof(*list));
}

static void channel_clear(vip_channel_t *channel) {
    if (!channel) return;
    free(channel->provider_id);
    free(channel->id);
    free(channel->category_id);
    free(channel->name);
    free(channel->logo_url);
    free(channel->stream_url);
    free(channel->epg_channel_id);
    memset(channel, 0, sizeof(*channel));
}

void vip_channel_list_clear(vip_channel_list_t *list) {
    if (!list) return;
    for (size_t i = 0; i < list->len; ++i) channel_clear(&list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static vip_status_t reserve_channels(vip_channel_list_t *list, size_t need, vip_error_t *error) {
    if (need <= list->cap) return VIP_OK;
    size_t cap = list->cap ? list->cap * 2 : 128;
    while (cap < need) cap *= 2;
    vip_channel_t *items = realloc(list->items, cap * sizeof(*items));
    if (!items) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para canais");
        return VIP_ERR_NOMEM;
    }
    list->items = items;
    list->cap = cap;
    return VIP_OK;
}

vip_status_t vip_channel_list_push(vip_channel_list_t *list,
                                   const vip_channel_t *channel,
                                   vip_error_t *error) {
    if (!list || !channel || !channel->id || !channel->name || !channel->stream_url)
        return VIP_ERR_INVALID_ARGUMENT;
    vip_status_t st = reserve_channels(list, list->len + 1, error);
    if (st != VIP_OK) return st;
    vip_channel_t copy = {
        .provider_id = vip_strdup_nullable(channel->provider_id),
        .id = vip_strdup(channel->id),
        .category_id = vip_strdup_nullable(channel->category_id),
        .name = vip_strdup(channel->name),
        .logo_url = vip_strdup_nullable(channel->logo_url),
        .stream_url = vip_strdup(channel->stream_url),
        .epg_channel_id = vip_strdup_nullable(channel->epg_channel_id),
        .position = channel->position,
    };
    if (!copy.id || !copy.name || !copy.stream_url || (channel->provider_id && !copy.provider_id)) {
        channel_clear(&copy);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para canal");
        return VIP_ERR_NOMEM;
    }
    list->items[list->len++] = copy;
    return VIP_OK;
}

void vip_media_metadata_init(vip_media_metadata_t *metadata) {
    if (metadata) memset(metadata, 0, sizeof(*metadata));
}

void vip_media_metadata_clear(vip_media_metadata_t *metadata) {
    if (!metadata) return;
    free(metadata->plot);
    free(metadata->cover_url);
    free(metadata->backdrop_url);
    free(metadata->genre);
    free(metadata->release_date);
    free(metadata->rating);
    free(metadata->duration);
    free(metadata->cast);
    free(metadata->director);
    free(metadata->youtube_trailer);
    memset(metadata, 0, sizeof(*metadata));
}

vip_status_t vip_media_metadata_copy(vip_media_metadata_t *dst,
                                     const vip_media_metadata_t *src,
                                     vip_error_t *error) {
    if (!dst || !src) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "metadados inválidos");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    vip_media_metadata_t copy = {
        .plot = vip_strdup_nullable(src->plot),
        .cover_url = vip_strdup_nullable(src->cover_url),
        .backdrop_url = vip_strdup_nullable(src->backdrop_url),
        .genre = vip_strdup_nullable(src->genre),
        .release_date = vip_strdup_nullable(src->release_date),
        .rating = vip_strdup_nullable(src->rating),
        .duration = vip_strdup_nullable(src->duration),
        .cast = vip_strdup_nullable(src->cast),
        .director = vip_strdup_nullable(src->director),
        .youtube_trailer = vip_strdup_nullable(src->youtube_trailer),
    };
    if ((src->plot && src->plot[0] && !copy.plot) ||
        (src->cover_url && src->cover_url[0] && !copy.cover_url) ||
        (src->backdrop_url && src->backdrop_url[0] && !copy.backdrop_url) ||
        (src->genre && src->genre[0] && !copy.genre) ||
        (src->release_date && src->release_date[0] && !copy.release_date) ||
        (src->rating && src->rating[0] && !copy.rating) ||
        (src->duration && src->duration[0] && !copy.duration) ||
        (src->cast && src->cast[0] && !copy.cast) ||
        (src->director && src->director[0] && !copy.director) ||
        (src->youtube_trailer && src->youtube_trailer[0] && !copy.youtube_trailer)) {
        vip_media_metadata_clear(&copy);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para metadados");
        return VIP_ERR_NOMEM;
    }
    vip_media_metadata_clear(dst);
    *dst = copy;
    vip_error_clear(error);
    return VIP_OK;
}
