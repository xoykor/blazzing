/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/provider_pluto.h"

#include <curl/curl.h>
#include <json-c/json.h>
#include <openssl/rand.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLUTO_BOOT_URL "https://boot.pluto.tv/v4/start"
#define PLUTO_CHANNELS_URL "https://service-channels.clusters.pluto.tv/v2/guide/channels?channelIds=&offset=0&limit=1000&sort=number%3Aasc"
#define PLUTO_LEGACY_CHANNELS_URL "https://api.pluto.tv/v2/channels.json"
#define PLUTO_STITCHER_FALLBACK "https://cfd-v4-service-channel-stitcher-use1-1.prd.pluto.tv"
#define PLUTO_PROVIDER_ID "pluto-tv"
#define PLUTO_MAX_RESPONSE (32u * 1024u * 1024u)

struct vip_pluto_client {
    CURL *curl;
    char client_id[33];
    char *session_token;
    char *stitcher_base;
    char *stitcher_params;
};

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool overflow;
} response_buf_t;

static size_t write_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
    response_buf_t *buf = userdata;
    const size_t bytes = size * nmemb;
    if (bytes > PLUTO_MAX_RESPONSE || buf->len > PLUTO_MAX_RESPONSE - bytes) {
        buf->overflow = true;
        return 0;
    }
    const size_t need = buf->len + bytes + 1u;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 4096u;
        while (cap < need) cap *= 2u;
        char *grown = realloc(buf->data, cap);
        if (!grown) return 0;
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

static void random_client_id(char out[33]) {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, (int)sizeof(bytes)) != 1) {
        for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = (unsigned char)(17u * i + 31u);
    }
    for (size_t i = 0; i < sizeof(bytes); ++i)
        (void)snprintf(out + i * 2u, 3u, "%02x", (unsigned)bytes[i]);
    out[32] = '\0';
}

static vip_status_t http_get(vip_pluto_client_t *client,
                             const char *url,
                             struct curl_slist *headers,
                             char **body_out,
                             vip_error_t *error) {
    if (!client || !client->curl || !url || !body_out) return VIP_ERR_INVALID_ARGUMENT;
    *body_out = NULL;
    response_buf_t buf = {0};
    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(client->curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(client->curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(client->curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/122 Safari/537.36");

    const CURLcode rc = curl_easy_perform(client->curl);
    long status = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK) {
        free(buf.data);
        vip_error_set(error, VIP_ERR_NETWORK, buf.overflow ?
                      "resposta do Pluto excedeu 32 MiB" : "falha HTTP Pluto: %s",
                      buf.overflow ? "" : curl_easy_strerror(rc));
        return VIP_ERR_NETWORK;
    }
    if (status < 200 || status >= 300) {
        free(buf.data);
        vip_error_set(error, VIP_ERR_NETWORK, "Pluto retornou HTTP %ld", status);
        return VIP_ERR_NETWORK;
    }
    if (!buf.data) {
        buf.data = vip_strdup("");
        if (!buf.data) {
            vip_error_set(error, VIP_ERR_NOMEM, "sem memória para resposta Pluto");
            return VIP_ERR_NOMEM;
        }
    }
    *body_out = buf.data;
    return VIP_OK;
}

static const char *jstr(json_object *obj, const char *key) {
    json_object *v = NULL;
    if (!obj || !json_object_object_get_ex(obj, key, &v) || !v ||
        json_object_get_type(v) == json_type_null) return "";
    return json_object_get_string(v);
}

static char *dup_json_string(json_object *obj, const char *key) {
    const char *s = jstr(obj, key);
    return s && s[0] ? vip_strdup(s) : NULL;
}

vip_status_t vip_pluto_client_create(vip_pluto_client_t **out, vip_error_t *error) {
    if (!out) return VIP_ERR_INVALID_ARGUMENT;
    *out = NULL;
    vip_pluto_client_t *client = calloc(1, sizeof(*client));
    if (!client) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para cliente Pluto");
        return VIP_ERR_NOMEM;
    }
    random_client_id(client->client_id);
    client->curl = curl_easy_init();
    if (!client->curl) {
        free(client);
        vip_error_set(error, VIP_ERR_NETWORK, "falha ao inicializar libcurl para Pluto");
        return VIP_ERR_NETWORK;
    }
    *out = client;
    vip_error_clear(error);
    return VIP_OK;
}

void vip_pluto_client_destroy(vip_pluto_client_t *client) {
    if (!client) return;
    if (client->curl) curl_easy_cleanup(client->curl);
    free(client->session_token);
    free(client->stitcher_base);
    free(client->stitcher_params);
    free(client);
}

const char *vip_pluto_provider_id(void) { return PLUTO_PROVIDER_ID; }

vip_status_t vip_pluto_boot(vip_pluto_client_t *client, vip_error_t *error) {
    if (!client) return VIP_ERR_INVALID_ARGUMENT;
    char url[2048];
    (void)snprintf(url, sizeof(url),
                   PLUTO_BOOT_URL
                   "?appName=web&appVersion=8.0.0&deviceVersion=122.0.0"
                   "&deviceModel=web&deviceMake=chrome&deviceType=web"
                   "&clientID=%s&clientModelNumber=1.0.0&serverSideAds=false"
                   "&drmCapabilities=widevine%%3AL3",
                   client->client_id);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Origin: https://pluto.tv");
    headers = curl_slist_append(headers, "Referer: https://pluto.tv/");
    char *body = NULL;
    vip_status_t st = http_get(client, url, headers, &body, error);
    curl_slist_free_all(headers);
    if (st != VIP_OK) return st;

    json_object *root = json_tokener_parse(body);
    free(body);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "bootstrap do Pluto retornou JSON inválido");
        return VIP_ERR_MALFORMED;
    }

    char *token = dup_json_string(root, "sessionToken");
    char *stitcher = NULL;
    char *params = dup_json_string(root, "stitcherParams");
    json_object *servers = NULL;
    if (json_object_object_get_ex(root, "servers", &servers) && servers &&
        json_object_get_type(servers) == json_type_object)
        stitcher = dup_json_string(servers, "stitcher");
    json_object_put(root);

    if (!token || !token[0]) {
        free(token); free(stitcher); free(params);
        vip_error_set(error, VIP_ERR_AUTH, "Pluto não forneceu token de sessão anônima");
        return VIP_ERR_AUTH;
    }
    if (!stitcher) stitcher = vip_strdup(PLUTO_STITCHER_FALLBACK);
    if (!stitcher) {
        free(token); free(params);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para endpoint do Pluto");
        return VIP_ERR_NOMEM;
    }

    free(client->session_token);
    free(client->stitcher_base);
    free(client->stitcher_params);
    client->session_token = token;
    client->stitcher_base = stitcher;
    client->stitcher_params = params;
    vip_error_clear(error);
    return VIP_OK;
}

static const char *channel_logo(json_object *channel) {
    json_object *images = NULL;
    if (json_object_object_get_ex(channel, "images", &images) && images &&
        json_object_get_type(images) == json_type_array) {
        const size_t n = json_object_array_length(images);
        for (size_t i = 0; i < n; ++i) {
            json_object *img = json_object_array_get_idx(images, i);
            const char *type = jstr(img, "type");
            const char *url = jstr(img, "url");
            if (url[0] && (!strcmp(type, "colorLogoPNG") || !strcmp(type, "logo"))) return url;
        }
    }
    json_object *logo = NULL;
    if (json_object_object_get_ex(channel, "colorLogoPNG", &logo) && logo &&
        json_object_get_type(logo) == json_type_object) {
        const char *path = jstr(logo, "path");
        if (path[0]) return path;
    }
    if (json_object_object_get_ex(channel, "logo", &logo) && logo &&
        json_object_get_type(logo) == json_type_object) return jstr(logo, "path");
    return "";
}

static vip_status_t append_channel(json_object *obj,
                                   size_t index,
                                   const char *session_token,
                                   const char *stitcher_base,
                                   vip_channel_list_t *out,
                                   vip_error_t *error) {
    const char *id = jstr(obj, "id");
    if (!id[0]) id = jstr(obj, "_id");
    const char *name = jstr(obj, "name");
    if (!id[0] || !name[0]) return VIP_OK;

    const char *logo = channel_logo(obj);
    size_t url_n = strlen(stitcher_base) + strlen(id) + strlen(session_token) + 96u;
    char *stream = malloc(url_n);
    if (!stream) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL de canal Pluto");
        return VIP_ERR_NOMEM;
    }
    (void)snprintf(stream, url_n,
                   "%s/v2/stitch/hls/channel/%s/master.m3u8?jwt=%s&masterJWTPassthrough=true",
                   stitcher_base, id, session_token);

    int position = (int)index;
    json_object *number = NULL;
    if (json_object_object_get_ex(obj, "number", &number) && number)
        position = json_object_get_int(number);

    vip_channel_t item = {
        .provider_id = (char *)PLUTO_PROVIDER_ID,
        .id = (char *)id,
        .category_id = (char *)"pluto-live",
        .name = (char *)name,
        .logo_url = (char *)(logo[0] ? logo : NULL),
        .stream_url = stream,
        .epg_channel_id = (char *)id,
        .position = position
    };
    const vip_status_t st = vip_channel_list_push(out, &item, error);
    free(stream);
    return st;
}

vip_status_t vip_pluto_parse_channels_json(const char *json,
                                            const char *session_token,
                                            const char *stitcher_base,
                                            vip_channel_list_t *out,
                                            vip_error_t *error) {
    if (!json || !session_token || !session_token[0] || !stitcher_base || !out)
        return VIP_ERR_INVALID_ARGUMENT;
    json_object *root = json_tokener_parse(json);
    if (!root) {
        vip_error_set(error, VIP_ERR_MALFORMED, "JSON de canais Pluto inválido");
        return VIP_ERR_MALFORMED;
    }
    json_object *array = root;
    if (json_object_get_type(root) == json_type_object) {
        json_object *data = NULL;
        if (!json_object_object_get_ex(root, "data", &data) || !data) {
            json_object_put(root);
            vip_error_set(error, VIP_ERR_MALFORMED, "catálogo Pluto sem campo data");
            return VIP_ERR_MALFORMED;
        }
        array = data;
    }
    if (json_object_get_type(array) != json_type_array) {
        json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "catálogo Pluto não é uma lista");
        return VIP_ERR_MALFORMED;
    }

    const size_t n = json_object_array_length(array);
    for (size_t i = 0; i < n; ++i) {
        json_object *obj = json_object_array_get_idx(array, i);
        if (!obj || json_object_get_type(obj) != json_type_object) continue;
        vip_status_t st = append_channel(obj, i, session_token, stitcher_base, out, error);
        if (st != VIP_OK) {
            json_object_put(root);
            return st;
        }
    }
    json_object_put(root);
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_pluto_live_catalog(vip_pluto_client_t *client,
                                    vip_category_list_t *categories_out,
                                    vip_channel_list_t *channels_out,
                                    vip_error_t *error) {
    if (!client || !categories_out || !channels_out) return VIP_ERR_INVALID_ARGUMENT;
    if (!client->session_token) {
        vip_status_t st = vip_pluto_boot(client, error);
        if (st != VIP_OK) return st;
    }

    struct curl_slist *headers = NULL;
    char auth[8192];
    (void)snprintf(auth, sizeof(auth), "Authorization: Bearer %s", client->session_token);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Origin: https://pluto.tv");
    headers = curl_slist_append(headers, "Referer: https://pluto.tv/");

    char *body = NULL;
    vip_status_t st = http_get(client, PLUTO_CHANNELS_URL, headers, &body, error);
    curl_slist_free_all(headers);
    if (st != VIP_OK) {
        /* The legacy endpoint remains useful as a compatibility fallback and
         * does not need a bearer header. */
        vip_error_clear(error);
        st = http_get(client, PLUTO_LEGACY_CHANNELS_URL, NULL, &body, error);
        if (st != VIP_OK) return st;
    }

    vip_category_t category = {
        .provider_id = (char *)PLUTO_PROVIDER_ID,
        .id = (char *)"pluto-live",
        .name = (char *)"Pluto TV",
        .position = 0
    };
    st = vip_category_list_push(categories_out, &category, error);
    if (st == VIP_OK)
        st = vip_pluto_parse_channels_json(body, client->session_token,
                                           client->stitcher_base ? client->stitcher_base : PLUTO_STITCHER_FALLBACK,
                                           channels_out, error);
    free(body);
    return st;
}
