/* SPDX-License-Identifier: MIT */
/*
 * Xtream Codes provider adapter.
 *
 * Network requests are performed with libcurl and parsed with json-c.  API
 * payloads are normalized into the provider-independent category/channel and
 * metadata types declared in core.h.
 */
#include "visual_iptv/provider.h"

#include <curl/curl.h>
#include <json-c/json.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define VIP_HTTP_MAX_RESPONSE (32u * 1024u * 1024u)

struct vip_xtream_client {
    CURL *curl;
    vip_credentials_t credentials;
};

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool overflow;
} response_buf_t;

static size_t write_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
    response_buf_t *buf = userdata;
    size_t bytes = size * nmemb;
    if (bytes > VIP_HTTP_MAX_RESPONSE || buf->len > VIP_HTTP_MAX_RESPONSE - bytes) {
        buf->overflow = true;
        return 0;
    }
    size_t need = buf->len + bytes + 1;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 4096;
        while (cap < need) cap *= 2;
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

static vip_status_t copy_credentials(vip_credentials_t *dst,
                                     const vip_credentials_t *src,
                                     vip_error_t *error) {
    return vip_credentials_init(dst, src->server, src->username, src->password, error);
}

vip_status_t vip_xtream_client_create(vip_xtream_client_t **out,
                                      const vip_credentials_t *credentials,
                                      vip_error_t *error) {
    if (!out || !credentials) return VIP_ERR_INVALID_ARGUMENT;
    *out = NULL;
    vip_xtream_client_t *client = calloc(1, sizeof(*client));
    if (!client) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para cliente Xtream");
        return VIP_ERR_NOMEM;
    }
    vip_status_t st = copy_credentials(&client->credentials, credentials, error);
    if (st != VIP_OK) {
        free(client);
        return st;
    }
    client->curl = curl_easy_init();
    if (!client->curl) {
        vip_credentials_clear(&client->credentials);
        free(client);
        vip_error_set(error, VIP_ERR_NETWORK, "falha ao inicializar libcurl");
        return VIP_ERR_NETWORK;
    }
    *out = client;
    return VIP_OK;
}

void vip_xtream_client_destroy(vip_xtream_client_t *client) {
    if (!client) return;
    if (client->curl) curl_easy_cleanup(client->curl);
    vip_credentials_clear(&client->credentials);
    free(client);
}

const char *vip_xtream_provider_id(const vip_xtream_client_t *client) {
    return client ? client->credentials.provider_id : NULL;
}

static char *escape(CURL *curl, const char *text) {
    return curl_easy_escape(curl, text ? text : "", 0);
}

/* Credentials are percent-encoded before entering query parameters. */
static char *make_api_url(vip_xtream_client_t *client, const char *action, vip_error_t *error) {
    char *u = escape(client->curl, client->credentials.username);
    char *p = escape(client->curl, client->credentials.password);
    char *a = action ? escape(client->curl, action) : NULL;
    if (!u || !p || (action && !a)) {
        if (u) curl_free(u);
        if (p) curl_free(p);
        if (a) curl_free(a);
        vip_error_set(error, VIP_ERR_NOMEM, "falha ao codificar URL Xtream");
        return NULL;
    }
    size_t n = strlen(client->credentials.server) + strlen(u) + strlen(p) + 64 + (a ? strlen(a) : 0);
    char *url = malloc(n);
    if (!url) {
        curl_free(u); curl_free(p); if (a) curl_free(a);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL Xtream");
        return NULL;
    }
    snprintf(url, n, "%splayer_api.php?username=%s&password=%s%s%s",
             client->credentials.server, u, p,
             a ? "&action=" : "", a ? a : "");
    curl_free(u); curl_free(p); if (a) curl_free(a);
    return url;
}

static vip_status_t http_get(vip_xtream_client_t *client,
                             const char *url,
                             char **body_out,
                             vip_error_t *error) {
    response_buf_t buf = {0};
    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(client->curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(client->curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(client->curl, CURLOPT_USERAGENT, "visual-iptv-c/0.1");
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(client->curl);
    long status = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK) {
        free(buf.data);
        if (buf.overflow)
            vip_error_set(error, VIP_ERR_NETWORK, "resposta do provider excedeu o limite de 32 MiB");
        else
            vip_error_set(error, VIP_ERR_NETWORK, "falha HTTP: %s", curl_easy_strerror(rc));
        return VIP_ERR_NETWORK;
    }
    if (status < 200 || status >= 300) {
        free(buf.data);
        vip_error_set(error, VIP_ERR_NETWORK, "provider retornou HTTP %ld", status);
        return VIP_ERR_NETWORK;
    }
    if (!buf.data) {
        buf.data = vip_strdup("");
        if (!buf.data) {
            vip_error_set(error, VIP_ERR_NOMEM, "sem memória para resposta HTTP");
            return VIP_ERR_NOMEM;
        }
    }
    *body_out = buf.data;
    return VIP_OK;
}

static bool json_truthy(json_object *value) {
    if (!value) return false;
    enum json_type type = json_object_get_type(value);
    if (type == json_type_boolean) return json_object_get_boolean(value);
    if (type == json_type_int) return json_object_get_int64(value) == 1;
    if (type == json_type_string) {
        const char *s = json_object_get_string(value);
        return s && (!strcmp(s, "1") || !strcasecmp(s, "true"));
    }
    return false;
}

vip_status_t vip_xtream_parse_auth_json(const char *json, vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root) {
        vip_error_set(error, VIP_ERR_MALFORMED, "JSON de autenticação inválido");
        return VIP_ERR_MALFORMED;
    }
    json_object *user = NULL;
    json_object *auth = NULL;
    json_object *status = NULL;
    bool ok = json_object_object_get_ex(root, "user_info", &user) && user &&
              json_object_get_type(user) == json_type_object;
    if (ok) {
        json_object_object_get_ex(user, "auth", &auth);
        json_object_object_get_ex(user, "status", &status);
        const char *status_text = status && json_object_get_type(status) == json_type_string
                                      ? json_object_get_string(status) : "";
        ok = json_truthy(auth) || (status_text && !strcasecmp(status_text, "active"));
    }
    json_object_put(root);
    if (!ok) {
        vip_error_set(error, VIP_ERR_AUTH, "provider rejeitou as credenciais");
        return VIP_ERR_AUTH;
    }
    vip_error_clear(error);
    return VIP_OK;
}

static const char *jstr(json_object *obj, const char *key) {
    json_object *v = NULL;
    if (!json_object_object_get_ex(obj, key, &v) || !v || json_object_get_type(v) == json_type_null)
        return "";
    return json_object_get_string(v);
}

static const char *jstr_alias(json_object *obj,
                              const char *a,
                              const char *b,
                              const char *c) {
    const char *value = a ? jstr(obj, a) : "";
    if ((!value || !value[0]) && b) value = jstr(obj, b);
    if ((!value || !value[0]) && c) value = jstr(obj, c);
    return value ? value : "";
}

static const char *first_string_or_array_item(json_object *obj, const char *key) {
    json_object *value = NULL;
    if (!obj || !json_object_object_get_ex(obj, key, &value) || !value ||
        json_object_get_type(value) == json_type_null) return "";
    if (json_object_get_type(value) == json_type_array) {
        if (json_object_array_length(value) == 0u) return "";
        json_object *first = json_object_array_get_idx(value, 0u);
        return first && json_object_get_type(first) != json_type_null ? json_object_get_string(first) : "";
    }
    return json_object_get_string(value);
}

/* Metadata APIs are inconsistent across Xtream implementations; ignore
 * absent/empty values while preserving ownership guarantees. */
static bool metadata_assign(char **dst, const char *value) {
    if (!value || !value[0]) return true;
    *dst = vip_strdup(value);
    return *dst != NULL;
}

static vip_status_t metadata_from_info(json_object *info,
                                       json_object *fallback,
                                       vip_media_metadata_t *out,
                                       vip_error_t *error) {
    if (!out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "destino de metadados inválido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    vip_media_metadata_init(out);
    if (!info || json_object_get_type(info) != json_type_object) info = fallback;
    if (!info || json_object_get_type(info) != json_type_object) {
        vip_error_set(error, VIP_ERR_MALFORMED, "provider não enviou metadados válidos");
        return VIP_ERR_MALFORMED;
    }

    const char *plot = jstr_alias(info, "plot", "description", "overview");
    const char *cover = jstr_alias(info, "movie_image", "cover", "stream_icon");
    const char *backdrop = first_string_or_array_item(info, "backdrop_path");
    if (!backdrop[0]) backdrop = jstr_alias(info, "backdrop", "cover_big", "backdrop_url");
    const char *genre = jstr_alias(info, "genre", "genres", NULL);
    const char *release_date = jstr_alias(info, "release_date", "releaseDate", "releasedate");
    const char *rating = jstr_alias(info, "rating", "rating_5based", "imdb_rating");
    const char *duration = jstr_alias(info, "duration", "runtime", NULL);
    const char *cast = jstr_alias(info, "cast", "actors", NULL);
    const char *director = jstr_alias(info, "director", "directors", NULL);
    const char *trailer = jstr_alias(info, "youtube_trailer", "trailer", NULL);

    /* Xtream forks disagree about whether metadata lives in `info`,
       `movie_data`, or at the response root. Fill any missing field from
       the fallback object rather than assuming one server layout. */
    if (fallback && fallback != info && json_object_get_type(fallback) == json_type_object) {
        if (!plot[0]) plot = jstr_alias(fallback, "plot", "description", "overview");
        if (!cover[0]) cover = jstr_alias(fallback, "stream_icon", "movie_image", "cover");
        if (!backdrop[0]) backdrop = first_string_or_array_item(fallback, "backdrop_path");
        if (!backdrop[0]) backdrop = jstr_alias(fallback, "backdrop", "cover_big", "backdrop_url");
        if (!genre[0]) genre = jstr_alias(fallback, "genre", "genres", NULL);
        if (!release_date[0]) release_date = jstr_alias(fallback, "release_date", "releaseDate", "releasedate");
        if (!rating[0]) rating = jstr_alias(fallback, "rating", "rating_5based", "imdb_rating");
        if (!duration[0]) duration = jstr_alias(fallback, "duration", "runtime", NULL);
        if (!cast[0]) cast = jstr_alias(fallback, "cast", "actors", NULL);
        if (!director[0]) director = jstr_alias(fallback, "director", "directors", NULL);
        if (!trailer[0]) trailer = jstr_alias(fallback, "youtube_trailer", "trailer", NULL);
    }

    bool ok = metadata_assign(&out->plot, plot) &&
              metadata_assign(&out->cover_url, cover) &&
              metadata_assign(&out->backdrop_url, backdrop) &&
              metadata_assign(&out->genre, genre) &&
              metadata_assign(&out->release_date, release_date) &&
              metadata_assign(&out->rating, rating) &&
              metadata_assign(&out->duration, duration) &&
              metadata_assign(&out->cast, cast) &&
              metadata_assign(&out->director, director) &&
              metadata_assign(&out->youtube_trailer, trailer);
    if (!ok) {
        vip_media_metadata_clear(out);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para metadados Xtream");
        return VIP_ERR_NOMEM;
    }
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_xtream_parse_vod_info_json(const char *json,
                                            vip_media_metadata_t *metadata_out,
                                            vip_error_t *error) {
    if (!json || !metadata_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "detalhes de filme inválidos");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "detalhes do filme Xtream inválidos");
        return VIP_ERR_MALFORMED;
    }
    json_object *info = NULL, *movie_data = NULL;
    (void)json_object_object_get_ex(root, "info", &info);
    (void)json_object_object_get_ex(root, "movie_data", &movie_data);
    vip_status_t st = metadata_from_info(info, movie_data, metadata_out, error);
    json_object_put(root);
    return st;
}

vip_status_t vip_xtream_parse_series_metadata_json(const char *json,
                                                   vip_media_metadata_t *metadata_out,
                                                   vip_error_t *error) {
    if (!json || !metadata_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "detalhes de série inválidos");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "detalhes da série Xtream inválidos");
        return VIP_ERR_MALFORMED;
    }
    json_object *info = NULL;
    (void)json_object_object_get_ex(root, "info", &info);
    vip_status_t st = metadata_from_info(info, root, metadata_out, error);
    json_object_put(root);
    return st;
}

vip_status_t vip_xtream_parse_categories_json(const char *json,
                                              const char *provider_id,
                                              vip_category_list_t *out,
                                              vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "lista de categorias Xtream inválida");
        return VIP_ERR_MALFORMED;
    }
    vip_category_list_init(out);
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(root, i);
        if (!row || json_object_get_type(row) != json_type_object) continue;
        const char *id = jstr(row, "category_id");
        const char *name = jstr(row, "category_name");
        if (!id[0] || !name[0]) continue;
        vip_category_t category = {
            .provider_id = (char *)provider_id,
            .id = (char *)id,
            .name = (char *)name,
            .position = (int)i,
        };
        vip_status_t st = vip_category_list_push(out, &category, error);
        if (st != VIP_OK) {
            vip_category_list_clear(out);
            json_object_put(root);
            return st;
        }
    }
    json_object_put(root);
    return VIP_OK;
}

static char *stream_id_string(json_object *row) {
    json_object *id = NULL;
    if (!json_object_object_get_ex(row, "stream_id", &id) || !id) return NULL;
    if (json_object_get_type(id) == json_type_string) return vip_strdup(json_object_get_string(id));
    if (json_object_get_type(id) == json_type_int) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%lld", (long long)json_object_get_int64(id));
        return vip_strdup(tmp);
    }
    return NULL;
}

static char *make_live_url(const vip_credentials_t *credentials, const char *stream_id) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    char *u = curl_easy_escape(curl, credentials->username, 0);
    char *p = curl_easy_escape(curl, credentials->password, 0);
    char *id = curl_easy_escape(curl, stream_id, 0);
    if (!u || !p || !id) {
        if (u) curl_free(u);
        if (p) curl_free(p);
        if (id) curl_free(id);
        curl_easy_cleanup(curl);
        return NULL;
    }
    size_t n = strlen(credentials->server) + strlen(u) + strlen(p) + strlen(id) + 16;
    char *url = malloc(n);
    if (url) snprintf(url, n, "%slive/%s/%s/%s.ts", credentials->server, u, p, id);
    curl_free(u); curl_free(p); curl_free(id); curl_easy_cleanup(curl);
    return url;
}


static char *prefixed_id(const char *prefix, const char *id) {
    if (!prefix || !id || !id[0]) return NULL;
    size_t n = strlen(prefix) + strlen(id) + 1u;
    char *out = malloc(n);
    if (out) snprintf(out, n, "%s%s", prefix, id);
    return out;
}

static char *make_media_url(const vip_credentials_t *credentials,
                            const char *route,
                            const char *stream_id,
                            const char *extension) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    char *u = curl_easy_escape(curl, credentials->username, 0);
    char *p = curl_easy_escape(curl, credentials->password, 0);
    char *id = curl_easy_escape(curl, stream_id, 0);
    const char *ext_src = extension && extension[0] ? extension : "mp4";
    char *ext = curl_easy_escape(curl, ext_src, 0);
    if (!u || !p || !id || !ext) {
        if (u) curl_free(u);
        if (p) curl_free(p);
        if (id) curl_free(id);
        if (ext) curl_free(ext);
        curl_easy_cleanup(curl);
        return NULL;
    }
    size_t n = strlen(credentials->server) + strlen(route) + strlen(u) + strlen(p) +
               strlen(id) + strlen(ext) + 20u;
    char *url = malloc(n);
    if (url) snprintf(url, n, "%s%s/%s/%s/%s.%s", credentials->server, route, u, p, id, ext);
    curl_free(u); curl_free(p); curl_free(id); curl_free(ext); curl_easy_cleanup(curl);
    return url;
}

static char *make_api_url_param(vip_xtream_client_t *client,
                                const char *action,
                                const char *param_name,
                                const char *param_value,
                                vip_error_t *error) {
    char *u = escape(client->curl, client->credentials.username);
    char *p = escape(client->curl, client->credentials.password);
    char *a = escape(client->curl, action);
    char *v = escape(client->curl, param_value);
    if (!u || !p || !a || !v) {
        if (u) curl_free(u);
        if (p) curl_free(p);
        if (a) curl_free(a);
        if (v) curl_free(v);
        vip_error_set(error, VIP_ERR_NOMEM, "falha ao codificar URL Xtream");
        return NULL;
    }
    size_t n = strlen(client->credentials.server) + strlen(u) + strlen(p) + strlen(a) +
               strlen(param_name) + strlen(v) + 80u;
    char *url = malloc(n);
    if (!url) {
        curl_free(u); curl_free(p); curl_free(a); curl_free(v);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL Xtream");
        return NULL;
    }
    snprintf(url, n, "%splayer_api.php?username=%s&password=%s&action=%s&%s=%s",
             client->credentials.server, u, p, a, param_name, v);
    curl_free(u); curl_free(p); curl_free(a); curl_free(v);
    return url;
}

vip_status_t vip_xtream_parse_streams_json(const char *json,
                                           const vip_credentials_t *credentials,
                                           vip_channel_list_t *out,
                                           vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "lista de canais Xtream inválida");
        return VIP_ERR_MALFORMED;
    }
    vip_channel_list_init(out);
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(root, i);
        if (!row || json_object_get_type(row) != json_type_object) continue;
        const char *stream_type = jstr(row, "stream_type");
        if (stream_type[0] && strcmp(stream_type, "live") != 0) continue;
        char *id = stream_id_string(row);
        const char *name = jstr(row, "name");
        if (!id || !name[0]) { free(id); continue; }
        const char *direct = jstr(row, "direct_source");
        char *generated = NULL;
        const char *stream_url = direct[0] ? direct : (generated = make_live_url(credentials, id));
        if (!stream_url) {
            free(id);
            vip_channel_list_clear(out);
            json_object_put(root);
            vip_error_set(error, VIP_ERR_NOMEM, "falha ao criar URL do stream");
            return VIP_ERR_NOMEM;
        }
        vip_channel_t channel = {
            .provider_id = (char *)credentials->provider_id,
            .id = id,
            .category_id = (char *)jstr(row, "category_id"),
            .name = (char *)name,
            .logo_url = (char *)jstr(row, "stream_icon"),
            .stream_url = (char *)stream_url,
            .epg_channel_id = (char *)jstr(row, "epg_channel_id"),
            .position = (int)i,
        };
        vip_status_t st = vip_channel_list_push(out, &channel, error);
        free(generated);
        free(id);
        if (st != VIP_OK) {
            vip_channel_list_clear(out);
            json_object_put(root);
            return st;
        }
    }
    json_object_put(root);
    return VIP_OK;
}


static vip_status_t parse_vod_streams_json(const char *json,
                                           const vip_credentials_t *credentials,
                                           vip_channel_list_t *out,
                                           vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "lista de filmes Xtream inválida");
        return VIP_ERR_MALFORMED;
    }
    vip_channel_list_init(out);
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(root, i);
        if (!row || json_object_get_type(row) != json_type_object) continue;
        char *raw_id = stream_id_string(row);
        const char *name = jstr(row, "name");
        if (!raw_id || !name[0]) { free(raw_id); continue; }
        char *id = prefixed_id("vod:", raw_id);
        const char *direct = jstr(row, "direct_source");
        const char *extension = jstr(row, "container_extension");
        char *generated = NULL;
        const char *stream_url = direct[0] ? direct :
                                 (generated = make_media_url(credentials, "movie", raw_id,
                                                              extension[0] ? extension : "mp4"));
        if (!id || !stream_url) {
            free(id); free(raw_id); free(generated);
            vip_channel_list_clear(out);
            json_object_put(root);
            vip_error_set(error, VIP_ERR_NOMEM, "falha ao criar filme Xtream");
            return VIP_ERR_NOMEM;
        }
        vip_channel_t item = {
            .provider_id = (char *)credentials->provider_id,
            .id = id,
            .category_id = (char *)jstr(row, "category_id"),
            .name = (char *)name,
            .logo_url = (char *)jstr(row, "stream_icon"),
            .stream_url = (char *)stream_url,
            .epg_channel_id = NULL,
            .position = (int)i,
        };
        vip_status_t st = vip_channel_list_push(out, &item, error);
        free(id); free(raw_id); free(generated);
        if (st != VIP_OK) {
            vip_channel_list_clear(out);
            json_object_put(root);
            return st;
        }
    }
    json_object_put(root);
    return VIP_OK;
}

static char *series_id_string(json_object *row) {
    json_object *id = NULL;
    if (!json_object_object_get_ex(row, "series_id", &id) || !id) return NULL;
    if (json_object_get_type(id) == json_type_string) return vip_strdup(json_object_get_string(id));
    if (json_object_get_type(id) == json_type_int) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%lld", (long long)json_object_get_int64(id));
        return vip_strdup(tmp);
    }
    return NULL;
}

static vip_status_t parse_series_json(const char *json,
                                      const vip_credentials_t *credentials,
                                      vip_channel_list_t *out,
                                      vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "lista de séries Xtream inválida");
        return VIP_ERR_MALFORMED;
    }
    vip_channel_list_init(out);
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(root, i);
        if (!row || json_object_get_type(row) != json_type_object) continue;
        char *raw_id = series_id_string(row);
        const char *name = jstr(row, "name");
        if (!name[0]) name = jstr(row, "title");
        if (!raw_id || !name[0]) { free(raw_id); continue; }
        char *id = prefixed_id("series:", raw_id);
        size_t un = strlen(raw_id) + 10u;
        char *url = malloc(un);
        if (url) snprintf(url, un, "series://%s", raw_id);
        if (!id || !url) {
            free(id); free(url); free(raw_id);
            vip_channel_list_clear(out);
            json_object_put(root);
            vip_error_set(error, VIP_ERR_NOMEM, "falha ao criar série Xtream");
            return VIP_ERR_NOMEM;
        }
        const char *cover = jstr(row, "cover");
        if (!cover[0]) cover = jstr(row, "stream_icon");
        vip_channel_t item = {
            .provider_id = (char *)credentials->provider_id,
            .id = id,
            .category_id = (char *)jstr(row, "category_id"),
            .name = (char *)name,
            .logo_url = (char *)cover,
            .stream_url = url,
            .epg_channel_id = NULL,
            .position = (int)i,
        };
        vip_status_t st = vip_channel_list_push(out, &item, error);
        free(id); free(url); free(raw_id);
        if (st != VIP_OK) {
            vip_channel_list_clear(out);
            json_object_put(root);
            return st;
        }
    }
    json_object_put(root);
    return VIP_OK;
}

static const char *episode_image(json_object *episode) {
    json_object *info = NULL;
    if (!json_object_object_get_ex(episode, "info", &info) || !info ||
        json_object_get_type(info) != json_type_object) return "";
    const char *image = jstr(info, "movie_image");
    if (!image[0]) image = jstr(info, "cover_big");
    return image;
}

static vip_status_t push_episode(json_object *episode,
                                 const char *season,
                                 const vip_credentials_t *credentials,
                                 vip_channel_list_t *episodes_out,
                                 int position,
                                 vip_error_t *error) {
    if (!episode || json_object_get_type(episode) != json_type_object) return VIP_OK;
    const char *raw_id = jstr(episode, "id");
    if (!raw_id[0]) raw_id = jstr(episode, "stream_id");
    if (!raw_id[0]) return VIP_OK;
    const char *title = jstr(episode, "title");
    char fallback[96];
    if (!title[0]) {
        const char *num = jstr(episode, "episode_num");
        snprintf(fallback, sizeof(fallback), "Episódio %s", num[0] ? num : raw_id);
        title = fallback;
    }
    const char *extension = jstr(episode, "container_extension");
    char *url = make_media_url(credentials, "series", raw_id, extension[0] ? extension : "mp4");
    char *id = prefixed_id("episode:", raw_id);
    if (!url || !id) {
        free(url); free(id);
        vip_error_set(error, VIP_ERR_NOMEM, "falha ao criar episódio Xtream");
        return VIP_ERR_NOMEM;
    }
    vip_channel_t item = {
        .provider_id = (char *)credentials->provider_id,
        .id = id,
        .category_id = (char *)season,
        .name = (char *)title,
        .logo_url = (char *)episode_image(episode),
        .stream_url = url,
        .epg_channel_id = NULL,
        .position = position,
    };
    vip_status_t st = vip_channel_list_push(episodes_out, &item, error);
    free(url); free(id);
    return st;
}

static vip_status_t parse_series_info_json(const char *json,
                                           const vip_credentials_t *credentials,
                                           vip_media_metadata_t *metadata_out,
                                           vip_category_list_t *seasons_out,
                                           vip_channel_list_t *episodes_out,
                                           vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "detalhes da série Xtream inválidos");
        return VIP_ERR_MALFORMED;
    }
    json_object *episodes = NULL;
    if (!json_object_object_get_ex(root, "episodes", &episodes) || !episodes) {
        json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "série sem episódios");
        return VIP_ERR_MALFORMED;
    }
    if (metadata_out) {
        json_object *info = NULL;
        (void)json_object_object_get_ex(root, "info", &info);
        vip_status_t metadata_st = metadata_from_info(info, root, metadata_out, error);
        if (metadata_st != VIP_OK) {
            json_object_put(root);
            return metadata_st;
        }
    }
    vip_category_list_init(seasons_out);
    vip_channel_list_init(episodes_out);
    int position = 0;
    if (json_object_get_type(episodes) == json_type_object) {
        json_object_object_foreach(episodes, season, arr) {
            if (!arr || json_object_get_type(arr) != json_type_array) continue;
            char season_name[96];
            snprintf(season_name, sizeof(season_name), "Temporada %s", season);
            vip_category_t cat = {
                .provider_id = (char *)credentials->provider_id,
                .id = (char *)season,
                .name = season_name,
                .position = (int)seasons_out->len,
            };
            vip_status_t st = vip_category_list_push(seasons_out, &cat, error);
            if (st != VIP_OK) goto fail;
            size_t count = json_object_array_length(arr);
            for (size_t i = 0; i < count; ++i) {
                st = push_episode(json_object_array_get_idx(arr, i), season, credentials,
                                  episodes_out, position++, error);
                if (st != VIP_OK) goto fail;
            }
        }
    } else if (json_object_get_type(episodes) == json_type_array) {
        vip_category_t cat = {
            .provider_id = (char *)credentials->provider_id,
            .id = "1", .name = "Episódios", .position = 0,
        };
        vip_status_t st = vip_category_list_push(seasons_out, &cat, error);
        if (st != VIP_OK) goto fail;
        size_t count = json_object_array_length(episodes);
        for (size_t i = 0; i < count; ++i) {
            st = push_episode(json_object_array_get_idx(episodes, i), "1", credentials,
                              episodes_out, position++, error);
            if (st != VIP_OK) goto fail;
        }
    }
    json_object_put(root);
    if (episodes_out->len == 0u) {
        vip_category_list_clear(seasons_out);
        vip_channel_list_clear(episodes_out);
        vip_error_set(error, VIP_ERR_MALFORMED, "nenhum episódio encontrado");
        return VIP_ERR_MALFORMED;
    }
    vip_error_clear(error);
    return VIP_OK;
fail:
    if (metadata_out) vip_media_metadata_clear(metadata_out);
    vip_category_list_clear(seasons_out);
    vip_channel_list_clear(episodes_out);
    json_object_put(root);
    return error ? error->code : VIP_ERR_NOMEM;
}

vip_status_t vip_xtream_authenticate(vip_xtream_client_t *client, vip_error_t *error) {
    char *url = make_api_url(client, NULL, error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK) st = vip_xtream_parse_auth_json(body, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_live_categories(vip_xtream_client_t *client,
                                        vip_category_list_t *out,
                                        vip_error_t *error) {
    char *url = make_api_url(client, "get_live_categories", error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_categories_json(body, client->credentials.provider_id, out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_live_streams(vip_xtream_client_t *client,
                                     vip_channel_list_t *out,
                                     vip_error_t *error) {
    char *url = make_api_url(client, "get_live_streams", error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_streams_json(body, &client->credentials, out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_vod_categories(vip_xtream_client_t *client,
                                       vip_category_list_t *out,
                                       vip_error_t *error) {
    char *url = make_api_url(client, "get_vod_categories", error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_categories_json(body, client->credentials.provider_id, out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_vod_streams(vip_xtream_client_t *client,
                                    vip_channel_list_t *out,
                                    vip_error_t *error) {
    char *url = make_api_url(client, "get_vod_streams", error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = parse_vod_streams_json(body, &client->credentials, out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_vod_info(vip_xtream_client_t *client,
                                 const char *vod_id,
                                 vip_media_metadata_t *metadata_out,
                                 vip_error_t *error) {
    if (!client || !vod_id || !vod_id[0] || !metadata_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "filme inválido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *raw = vod_id;
    if (strncmp(raw, "vod:", 4u) == 0) raw += 4;
    char *url = make_api_url_param(client, "get_vod_info", "vod_id", raw, error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK) st = vip_xtream_parse_vod_info_json(body, metadata_out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_series_categories(vip_xtream_client_t *client,
                                          vip_category_list_t *out,
                                          vip_error_t *error) {
    char *url = make_api_url(client, "get_series_categories", error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_categories_json(body, client->credentials.provider_id, out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_series(vip_xtream_client_t *client,
                               vip_channel_list_t *out,
                               vip_error_t *error) {
    char *url = make_api_url(client, "get_series", error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = parse_series_json(body, &client->credentials, out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_series_metadata(vip_xtream_client_t *client,
                                        const char *series_id,
                                        vip_media_metadata_t *metadata_out,
                                        vip_error_t *error) {
    if (!client || !series_id || !series_id[0] || !metadata_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "série inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *raw = series_id;
    if (strncmp(raw, "series:", 7u) == 0) raw += 7;
    char *url = make_api_url_param(client, "get_series_info", "series_id", raw, error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK) st = vip_xtream_parse_series_metadata_json(body, metadata_out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_series_info(vip_xtream_client_t *client,
                                    const char *series_id,
                                    vip_media_metadata_t *metadata_out,
                                    vip_category_list_t *seasons_out,
                                    vip_channel_list_t *episodes_out,
                                    vip_error_t *error) {
    if (!client || !series_id || !series_id[0] || !metadata_out || !seasons_out || !episodes_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "série inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *raw = series_id;
    if (strncmp(raw, "series:", 7u) == 0) raw += 7;
    char *url = make_api_url_param(client, "get_series_info", "series_id", raw, error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = parse_series_info_json(body, &client->credentials, metadata_out,
                                    seasons_out, episodes_out, error);
    free(body);
    return st;
}

vip_status_t vip_xtream_series_episodes(vip_xtream_client_t *client,
                                        const char *series_id,
                                        vip_category_list_t *seasons_out,
                                        vip_channel_list_t *episodes_out,
                                        vip_error_t *error) {
    if (!client || !series_id || !series_id[0] || !seasons_out || !episodes_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "série inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *raw = series_id;
    if (strncmp(raw, "series:", 7u) == 0) raw += 7;
    char *url = make_api_url_param(client, "get_series_info", "series_id", raw, error);
    if (!url) return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = parse_series_info_json(body, &client->credentials, NULL, seasons_out, episodes_out, error);
    free(body);
    return st;
}
