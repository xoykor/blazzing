/* SPDX-License-Identifier: MIT */
/*
 * Application-level thumbnail scheduling policy.
 *
 * The generic scheduler intentionally knows nothing about IPTV semantics.
 * These GNU ld --wrap hooks apply Blazzing's policy without coupling the
 * scheduler to the X11 layer:
 *   - background prefetch only downloads provider artwork;
 *   - stream-frame generation is interactive-only;
 *   - at most two FFmpeg frame captures may run at once;
 *   - viewport changes do not destructively flush pending cache warming.
 */
#include "visual_iptv/thumbnails.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <curl/curl.h>
#include <json-c/json.h>

#define THUMB_INTERACTIVE_PRIORITY INT64_C(500000)
#define THUMB_MAX_FRAME_CAPTURES 2u

static atomic_uint active_frame_captures = 0u;

typedef struct {
    char *data;
    size_t len;
} resolver_buffer_t;

static bool can_resolve_artwork(const vip_thumbnail_request_t *request) {
    return request && request->title && request->title[0] &&
           request->artwork_kind && request->artwork_kind[0] &&
           request->priority >= THUMB_INTERACTIVE_PRIORITY;
}

static const char *artwork_service_base(void) {
    const char *value = getenv("VIPTV_ARTWORK_URL");
    if (value && value[0])
        return value;
    value = getenv("VIPTV_PAIRING_URL");
    if (value && value[0])
        return value;
#ifdef VIPTV_PAIRING_DEFAULT_URL
    return VIPTV_PAIRING_DEFAULT_URL;
#else
    return "https://blazzing-pairing.vsxk.workers.dev";
#endif
}

static size_t resolver_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    resolver_buffer_t *buffer = userdata;
    size_t bytes = size * nmemb;
    if (!buffer || bytes == 0u)
        return bytes;
    if (buffer->len + bytes > 256u * 1024u)
        return 0u;
    char *next = realloc(buffer->data, buffer->len + bytes + 1u);
    if (!next)
        return 0u;
    buffer->data = next;
    memcpy(buffer->data + buffer->len, ptr, bytes);
    buffer->len += bytes;
    buffer->data[buffer->len] = '\0';
    return bytes;
}

static vip_status_t resolve_remote_artwork(const vip_thumbnail_request_t *request, char **url_out,
                                           vip_error_t *error) {
    *url_out = NULL;
    if (!can_resolve_artwork(request)) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "item sem metadados para resolver capa");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    const char *base = artwork_service_base();
    size_t base_len = strlen(base);
    bool slash = base_len > 0u && base[base_len - 1u] == '/';
    const char *route = "api/v1/artwork/resolve";
    size_t endpoint_len = base_len + (slash ? 0u : 1u) + strlen(route) + 1u;
    char *endpoint = malloc(endpoint_len);
    if (!endpoint) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL do resolvedor");
        return VIP_ERR_NOMEM;
    }
    snprintf(endpoint, endpoint_len, "%s%s%s", base, slash ? "" : "/", route);

    json_object *root = json_object_new_object();
    json_object *items = json_object_new_array();
    json_object *item = json_object_new_object();
    if (!root || !items || !item) {
        if (item)
            json_object_put(item);
        if (items)
            json_object_put(items);
        if (root)
            json_object_put(root);
        free(endpoint);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para pedido de capa");
        return VIP_ERR_NOMEM;
    }
    json_object_object_add(item, "id", json_object_new_string("thumb"));
    json_object_object_add(item, "title", json_object_new_string(request->title));
    json_object_object_add(item, "kind", json_object_new_string(request->artwork_kind));
    json_object_array_add(items, item);
    json_object_object_add(root, "items", items);
    const char *payload = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);

    CURL *curl = curl_easy_init();
    resolver_buffer_t body = {0};
    struct curl_slist *headers = NULL;
    if (!curl) {
        json_object_put(root);
        free(endpoint);
        vip_error_set(error, VIP_ERR_NETWORK, "falha ao inicializar resolvedor de capas");
        return VIP_ERR_NETWORK;
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(payload));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 9000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Blazzing/1.3");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, resolver_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    json_object_put(root);
    free(endpoint);

    if (rc != CURLE_OK || http < 200 || http >= 300 || !body.data) {
        free(body.data);
        vip_error_set(error, VIP_ERR_NETWORK, "resolvedor de capas indisponível");
        return VIP_ERR_NETWORK;
    }

    json_object *reply = json_tokener_parse(body.data);
    free(body.data);
    if (!reply) {
        vip_error_set(error, VIP_ERR_MALFORMED, "resposta inválida do resolvedor de capas");
        return VIP_ERR_MALFORMED;
    }

    json_object *rows = NULL;
    const char *resolved = NULL;
    if (json_object_object_get_ex(reply, "items", &rows) &&
        json_object_is_type(rows, json_type_array) &&
        json_object_array_length(rows) > 0u) {
        json_object *row = json_object_array_get_idx(rows, 0u);
        json_object *value = NULL;
        if (row && json_object_object_get_ex(row, "url", &value) &&
            json_object_is_type(value, json_type_string))
            resolved = json_object_get_string(value);
    }

    if (resolved && (!strncmp(resolved, "https://", 8u) || !strncmp(resolved, "http://", 7u)))
        *url_out = vip_strdup(resolved);
    json_object_put(reply);

    if (!*url_out) {
        vip_error_set(error, VIP_ERR_MALFORMED, "capa não encontrada");
        return VIP_ERR_MALFORMED;
    }
    vip_error_clear(error);
    return VIP_OK;
}

/* Handle the real thumbnail scheduler enqueue operation. */
extern vip_status_t __real_vip_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *scheduler,
                                                           const vip_thumbnail_request_t *request,
                                                           vip_error_t *error);
/* Handle the real thumbnail scheduler cancel pending operation. */
extern void __real_vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler);
/* Capture with decoder in the thumbnail subsystem. */
extern vip_status_t __real_vip_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                              char **path_out, vip_error_t *error,
                                                              void *userdata);

/* Return whether artwork. */
static bool has_artwork(const vip_thumbnail_request_t *request) {
    return request && request->logo_url && request->logo_url[0] != '\0';
}

/* Handle the remote stream operation. */
static bool remote_stream(const vip_thumbnail_request_t *request) {
    const char *url = request ? request->stream_url : NULL;
    return url && (!strncmp(url, "http://", 7u) || !strncmp(url, "https://", 8u));
}

/* Capture enabled in the remote. */
static bool remote_capture_enabled(void) {
    const char *value = getenv("VIPTV_ALLOW_REMOTE_THUMB_CAPTURE");
    return value && value[0] && strcmp(value, "0") != 0;
}

/* Handle the frame slot try acquire operation. */
static bool frame_slot_try_acquire(void) {
    unsigned current = atomic_load_explicit(&active_frame_captures, memory_order_relaxed);
    while (current < THUMB_MAX_FRAME_CAPTURES) {
        if (atomic_compare_exchange_weak_explicit(&active_frame_captures, &current, current + 1u,
                                                  memory_order_acquire, memory_order_relaxed))
            return true;
    }
    return false;
}

/* Handle the wrap thumbnail scheduler enqueue operation. */
vip_status_t __wrap_vip_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *scheduler,
                                                    const vip_thumbnail_request_t *request,
                                                    vip_error_t *error) {
    if (!request)
        return VIP_ERR_INVALID_ARGUMENT;

    /* Never let background catalog warming turn into stream opens.  A
       logo-less channel may still request a frame when its card becomes
       visible because viewport priorities are above the interactive cutoff. */
    if (!has_artwork(request) && request->priority < THUMB_INTERACTIVE_PRIORITY) {
        vip_error_clear(error);
        return VIP_OK;
    }
    /* Passing authenticated stream URLs to ffmpeg via argv exposes them to
       local process inspection and slow stream opens can starve artwork jobs.
       Keep remote frame capture disabled unless the user explicitly opts in. */
    if (!has_artwork(request) && remote_stream(request) && !remote_capture_enabled() &&
        !can_resolve_artwork(request)) {
        vip_error_clear(error);
        return VIP_OK;
    }

    return __real_vip_thumbnail_scheduler_enqueue(scheduler, request, error);
}

/* Handle the wrap thumbnail scheduler cancel pending operation. */
void __wrap_vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler) {
    /* Callers use cancellation only at provider/list boundaries. Viewport and
       search changes no longer call this function, so ordinary navigation
       keeps cache warming while a provider switch drops stale queued jobs. */
    __real_vip_thumbnail_scheduler_cancel_pending(scheduler);
}

/* Capture with decoder in the thumbnail subsystem. */
vip_status_t __wrap_vip_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                       char **path_out, vip_error_t *error, void *userdata) {
    if (!request)
        return VIP_ERR_INVALID_ARGUMENT;

    if (has_artwork(request))
        return __real_vip_thumbnail_capture_with_decoder(request, path_out, error, userdata);

    /* Belt-and-suspenders guard: background logo-less work should have been
       filtered by enqueue already. Do not open a stream if it reaches here. */
    if (request->priority < THUMB_INTERACTIVE_PRIORITY) {
        vip_error_set(error, VIP_ERR_CANCELLED, "captura de frame ignorada fora do viewport");
        return VIP_ERR_CANCELLED;
    }

    /* Visible movie/series cards try the central resolver before any expensive
       frame capture. The Worker keeps the TMDB token private and remembers the
       result centrally, so another device can reuse the same discovery. */
    if (can_resolve_artwork(request)) {
        char *resolved_url = NULL;
        vip_error_t lookup_error = {0};
        if (resolve_remote_artwork(request, &resolved_url, &lookup_error) == VIP_OK && resolved_url) {
            vip_thumbnail_request_t resolved_request = *request;
            resolved_request.logo_url = resolved_url;
            vip_status_t resolved_status =
                __real_vip_thumbnail_capture_with_decoder(&resolved_request, path_out, error, userdata);
            free(resolved_url);
            return resolved_status;
        }
        free(resolved_url);

        if (request->stream_url && !strncmp(request->stream_url, "series://", 9u)) {
            vip_error_set(error, VIP_ERR_CANCELLED, "série sem capa conhecida");
            return VIP_ERR_CANCELLED;
        }
    }

    if (remote_stream(request) && !remote_capture_enabled()) {
        vip_error_set(error, VIP_ERR_CANCELLED, "captura remota de frame desativada por privacidade");
        return VIP_ERR_CANCELLED;
    }

    /* FFmpeg stream opens can take seconds. Limit them independently from
       artwork downloads so two bad/no-logo channels cannot occupy the whole
       8-16 worker pool. Busy items are retried by normal viewport redraws. */
    if (!frame_slot_try_acquire()) {
        vip_error_set(error, VIP_ERR_CANCELLED, "captura de frame adiada: limite de FFmpeg ativo");
        return VIP_ERR_CANCELLED;
    }

    vip_status_t st = __real_vip_thumbnail_capture_with_decoder(request, path_out, error, userdata);
    atomic_fetch_sub_explicit(&active_frame_captures, 1u, memory_order_release);
    return st;
}
