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
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define VIP_HTTP_MAX_MIB 128u
#define VIP_HTTP_MAX_RESPONSE ((size_t)VIP_HTTP_MAX_MIB * 1024u * 1024u)

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

/* Write response. */
static size_t write_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
    response_buf_t *buf = userdata;
    if (size != 0u && nmemb > SIZE_MAX / size) {
        buf->overflow = true;
        return 0u;
    }
    size_t bytes = size * nmemb;
    if (bytes > VIP_HTTP_MAX_RESPONSE || buf->len > VIP_HTTP_MAX_RESPONSE - bytes) {
        buf->overflow = true;
        return 0;
    }
    size_t need = buf->len + bytes + 1;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 4096u;
        while (cap < need && cap <= VIP_HTTP_MAX_RESPONSE / 2u)
            cap *= 2u;
        if (cap < need)
            cap = need;
        char *grown = realloc(buf->data, cap);
        if (!grown)
            return 0;
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

/* Copy credentials. */
static vip_status_t copy_credentials(vip_credentials_t *dst, const vip_credentials_t *src,
                                     vip_error_t *error) {
    return vip_credentials_init(dst, src->server, src->username, src->password, error);
}

/* Create the requested state in the xtream client. */
vip_status_t vip_xtream_client_create(vip_xtream_client_t **out, const vip_credentials_t *credentials,
                                      vip_error_t *error) {
    if (!out || !credentials)
        return VIP_ERR_INVALID_ARGUMENT;
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

/* Destroy the requested state in the xtream client. */
void vip_xtream_client_destroy(vip_xtream_client_t *client) {
    if (!client)
        return;
    if (client->curl)
        curl_easy_cleanup(client->curl);
    vip_credentials_clear(&client->credentials);
    free(client);
}

/* Handle the xtream provider id operation. */
const char *vip_xtream_provider_id(const vip_xtream_client_t *client) {
    return client ? client->credentials.provider_id : NULL;
}

/* Handle the escape operation. */
static char *escape(CURL *curl, const char *text) {
    return curl_easy_escape(curl, text ? text : "", 0);
}

/* Credentials are percent-encoded before entering query parameters. */
static char *make_api_url(vip_xtream_client_t *client, const char *action, vip_error_t *error) {
    char *u = escape(client->curl, client->credentials.username);
    char *p = escape(client->curl, client->credentials.password);
    char *a = action ? escape(client->curl, action) : NULL;
    if (!u || !p || (action && !a)) {
        if (u)
            curl_free(u);
        if (p)
            curl_free(p);
        if (a)
            curl_free(a);
        vip_error_set(error, VIP_ERR_NOMEM, "falha ao codificar URL Xtream");
        return NULL;
    }
    size_t n = strlen(client->credentials.server) + strlen(u) + strlen(p) + 64 + (a ? strlen(a) : 0);
    char *url = malloc(n);
    if (!url) {
        curl_free(u);
        curl_free(p);
        if (a)
            curl_free(a);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL Xtream");
        return NULL;
    }
    snprintf(url, n, "%splayer_api.php?username=%s&password=%s%s%s", client->credentials.server, u, p,
             a ? "&action=" : "", a ? a : "");
    curl_free(u);
    curl_free(p);
    if (a)
        curl_free(a);
    return url;
}

/* Return the requested state from the http. */
static vip_status_t http_get(vip_xtream_client_t *client, const char *url, char **body_out,
                             vip_error_t *error) {
    response_buf_t buf = {0};
    curl_easy_reset(client->curl);
    curl_easy_setopt(client->curl, CURLOPT_URL, url);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(client->curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(client->curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(client->curl, CURLOPT_USERAGENT, "Blazzing/1.2");
    curl_easy_setopt(client->curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(client->curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(client->curl, CURLOPT_ACCEPT_ENCODING, "");
#ifdef CURL_HTTP_VERSION_2TLS
    curl_easy_setopt(client->curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#endif
    CURLcode rc = curl_easy_perform(client->curl);
    long status = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK) {
        free(buf.data);
        if (buf.overflow)
            vip_error_set(error, VIP_ERR_NETWORK, "resposta do provider excedeu o limite de %u MiB",
                          VIP_HTTP_MAX_MIB);
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

/* Handle the json truthy operation. */
static bool json_truthy(json_object *value) {
    if (!value)
        return false;
    enum json_type type = json_object_get_type(value);
    if (type == json_type_boolean)
        return json_object_get_boolean(value);
    if (type == json_type_int)
        return json_object_get_int64(value) == 1;
    if (type == json_type_string) {
        const char *s = json_object_get_string(value);
        return s && (!strcmp(s, "1") || !strcasecmp(s, "true"));
    }
    return false;
}

/* Parse auth json using the Xtream provider. */
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
        const char *status_text =
            status && json_object_get_type(status) == json_type_string ? json_object_get_string(status) : "";
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

/* Handle the jstr operation. */
static const char *jstr(json_object *obj, const char *key) {
    json_object *v = NULL;
    if (!json_object_object_get_ex(obj, key, &v) || !v || json_object_get_type(v) == json_type_null)
        return "";
    return json_object_get_string(v);
}

/* Handle the jstr alias operation. */
static const char *jstr_alias(json_object *obj, const char *a, const char *b, const char *c) {
    const char *value = a ? jstr(obj, a) : "";
    if ((!value || !value[0]) && b)
        value = jstr(obj, b);
    if ((!value || !value[0]) && c)
        value = jstr(obj, c);
    return value ? value : "";
}

/* Handle the first string or array item operation. */
static const char *first_string_or_array_item(json_object *obj, const char *key) {
    json_object *value = NULL;
    if (!obj || !json_object_object_get_ex(obj, key, &value) || !value ||
        json_object_get_type(value) == json_type_null)
        return "";
    if (json_object_get_type(value) == json_type_array) {
        if (json_object_array_length(value) == 0u)
            return "";
        json_object *first = json_object_array_get_idx(value, 0u);
        return first && json_object_get_type(first) != json_type_null ? json_object_get_string(first) : "";
    }
    return json_object_get_string(value);
}

/* Metadata APIs are inconsistent across Xtream implementations; ignore
 * absent/empty values while preserving ownership guarantees. */
static bool metadata_assign(char **dst, const char *value) {
    if (!value || !value[0])
        return true;
    *dst = vip_strdup(value);
    return *dst != NULL;
}

typedef struct {
    const char *plot;
    const char *cover;
    const char *backdrop;
    const char *genre;
    const char *release_date;
    const char *rating;
    const char *duration;
    const char *cast;
    const char *director;
    const char *trailer;
} xtream_metadata_values_t;

/* Xtream forks use different aliases, and movie_data has a different cover
 * precedence than info. Keep that quirk explicit so fallback behavior remains
 * compatible with servers that expose both stream_icon and movie_image. */
static void metadata_values_read(json_object *obj, bool fallback_layout, xtream_metadata_values_t *values) {
    memset(values, 0, sizeof(*values));
    if (!obj || json_object_get_type(obj) != json_type_object)
        return;

    values->plot = jstr_alias(obj, "plot", "description", "overview");
    values->cover = fallback_layout ? jstr_alias(obj, "stream_icon", "movie_image", "cover")
                                    : jstr_alias(obj, "movie_image", "cover", "stream_icon");
    values->backdrop = first_string_or_array_item(obj, "backdrop_path");
    if (!values->backdrop[0])
        values->backdrop = jstr_alias(obj, "backdrop", "cover_big", "backdrop_url");
    values->genre = jstr_alias(obj, "genre", "genres", NULL);
    values->release_date = jstr_alias(obj, "release_date", "releaseDate", "releasedate");
    values->rating = jstr_alias(obj, "rating", "rating_5based", "imdb_rating");
    values->duration = jstr_alias(obj, "duration", "runtime", NULL);
    values->cast = jstr_alias(obj, "cast", "actors", NULL);
    values->director = jstr_alias(obj, "director", "directors", NULL);
    values->trailer = jstr_alias(obj, "youtube_trailer", "trailer", NULL);
}

static void metadata_values_fill_missing(xtream_metadata_values_t *values,
                                         const xtream_metadata_values_t *fallback) {
#define FILL_METADATA_FIELD(field)                                                                                     \
    do {                                                                                                               \
        if ((!values->field || !values->field[0]) && fallback->field && fallback->field[0])                           \
            values->field = fallback->field;                                                                           \
    } while (0)

    FILL_METADATA_FIELD(plot);
    FILL_METADATA_FIELD(cover);
    FILL_METADATA_FIELD(backdrop);
    FILL_METADATA_FIELD(genre);
    FILL_METADATA_FIELD(release_date);
    FILL_METADATA_FIELD(rating);
    FILL_METADATA_FIELD(duration);
    FILL_METADATA_FIELD(cast);
    FILL_METADATA_FIELD(director);
    FILL_METADATA_FIELD(trailer);

#undef FILL_METADATA_FIELD
}

static vip_status_t metadata_values_store(const xtream_metadata_values_t *values, vip_media_metadata_t *out,
                                          vip_error_t *error) {
    struct {
        char **dst;
        const char *value;
    } assignments[] = {
        {&out->plot, values->plot},
        {&out->cover_url, values->cover},
        {&out->backdrop_url, values->backdrop},
        {&out->genre, values->genre},
        {&out->release_date, values->release_date},
        {&out->rating, values->rating},
        {&out->duration, values->duration},
        {&out->cast, values->cast},
        {&out->director, values->director},
        {&out->youtube_trailer, values->trailer},
    };

    for (size_t i = 0; i < sizeof(assignments) / sizeof(assignments[0]); ++i) {
        if (!metadata_assign(assignments[i].dst, assignments[i].value)) {
            vip_media_metadata_clear(out);
            vip_error_set(error, VIP_ERR_NOMEM, "sem memória para metadados Xtream");
            return VIP_ERR_NOMEM;
        }
    }
    vip_error_clear(error);
    return VIP_OK;
}

/* Handle the metadata from info operation. */
static vip_status_t metadata_from_info(json_object *info, json_object *fallback, vip_media_metadata_t *out,
                                       vip_error_t *error) {
    if (!out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "destino de metadados inválido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    vip_media_metadata_init(out);

    json_object *primary = info;
    if (!primary || json_object_get_type(primary) != json_type_object)
        primary = fallback;
    if (!primary || json_object_get_type(primary) != json_type_object) {
        vip_error_set(error, VIP_ERR_MALFORMED, "provider não enviou metadados válidos");
        return VIP_ERR_MALFORMED;
    }

    xtream_metadata_values_t values;
    metadata_values_read(primary, false, &values);

    if (fallback && fallback != primary && json_object_get_type(fallback) == json_type_object) {
        xtream_metadata_values_t fallback_values;
        metadata_values_read(fallback, true, &fallback_values);
        metadata_values_fill_missing(&values, &fallback_values);
    }

    return metadata_values_store(&values, out, error);
}

/* Parse vod info json using the Xtream provider. */
vip_status_t vip_xtream_parse_vod_info_json(const char *json, vip_media_metadata_t *metadata_out,
                                            vip_error_t *error) {
    if (!json || !metadata_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "detalhes de filme inválidos");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root)
            json_object_put(root);
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

/* Parse series metadata json using the Xtream provider. */
vip_status_t vip_xtream_parse_series_metadata_json(const char *json, vip_media_metadata_t *metadata_out,
                                                   vip_error_t *error) {
    if (!json || !metadata_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "detalhes de série inválidos");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root)
            json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "detalhes da série Xtream inválidos");
        return VIP_ERR_MALFORMED;
    }
    json_object *info = NULL;
    (void)json_object_object_get_ex(root, "info", &info);
    vip_status_t st = metadata_from_info(info, root, metadata_out, error);
    json_object_put(root);
    return st;
}

/* Parse categories json using the Xtream provider. */
vip_status_t vip_xtream_parse_categories_json(const char *json, const char *provider_id,
                                              vip_category_list_t *out, vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root)
            json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "lista de categorias Xtream inválida");
        return VIP_ERR_MALFORMED;
    }
    vip_category_list_init(out);
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(root, i);
        if (!row || json_object_get_type(row) != json_type_object)
            continue;
        const char *id = jstr(row, "category_id");
        const char *name = jstr(row, "category_name");
        if (!id[0] || !name[0])
            continue;
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

/* Handle the stream id string operation. */
static char *stream_id_string(json_object *row) {
    json_object *id = NULL;
    if (!json_object_object_get_ex(row, "stream_id", &id) || !id)
        return NULL;
    if (json_object_get_type(id) == json_type_string)
        return vip_strdup(json_object_get_string(id));
    if (json_object_get_type(id) == json_type_int) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%lld", (long long)json_object_get_int64(id));
        return vip_strdup(tmp);
    }
    return NULL;
}

/* Create live url. */
static char *make_live_url(const vip_credentials_t *credentials, const char *stream_id) {
    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;
    char *u = curl_easy_escape(curl, credentials->username, 0);
    char *p = curl_easy_escape(curl, credentials->password, 0);
    char *id = curl_easy_escape(curl, stream_id, 0);
    if (!u || !p || !id) {
        if (u)
            curl_free(u);
        if (p)
            curl_free(p);
        if (id)
            curl_free(id);
        curl_easy_cleanup(curl);
        return NULL;
    }
    size_t n = strlen(credentials->server) + strlen(u) + strlen(p) + strlen(id) + 16;
    char *url = malloc(n);
    if (url)
        snprintf(url, n, "%slive/%s/%s/%s.ts", credentials->server, u, p, id);
    curl_free(u);
    curl_free(p);
    curl_free(id);
    curl_easy_cleanup(curl);
    return url;
}

/* Handle the prefixed id operation. */
static char *prefixed_id(const char *prefix, const char *id) {
    if (!prefix || !id || !id[0])
        return NULL;
    size_t n = strlen(prefix) + strlen(id) + 1u;
    char *out = malloc(n);
    if (out)
        snprintf(out, n, "%s%s", prefix, id);
    return out;
}

/* Create media url. */
static char *make_media_url(const vip_credentials_t *credentials, const char *route, const char *stream_id,
                            const char *extension) {
    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;
    char *u = curl_easy_escape(curl, credentials->username, 0);
    char *p = curl_easy_escape(curl, credentials->password, 0);
    char *id = curl_easy_escape(curl, stream_id, 0);
    const char *ext_src = extension && extension[0] ? extension : "mp4";
    char *ext = curl_easy_escape(curl, ext_src, 0);
    if (!u || !p || !id || !ext) {
        if (u)
            curl_free(u);
        if (p)
            curl_free(p);
        if (id)
            curl_free(id);
        if (ext)
            curl_free(ext);
        curl_easy_cleanup(curl);
        return NULL;
    }
    size_t n =
        strlen(credentials->server) + strlen(route) + strlen(u) + strlen(p) + strlen(id) + strlen(ext) + 20u;
    char *url = malloc(n);
    if (url)
        snprintf(url, n, "%s%s/%s/%s/%s.%s", credentials->server, route, u, p, id, ext);
    curl_free(u);
    curl_free(p);
    curl_free(id);
    curl_free(ext);
    curl_easy_cleanup(curl);
    return url;
}

/* Create api url param. */
static char *make_api_url_param(vip_xtream_client_t *client, const char *action, const char *param_name,
                                const char *param_value, vip_error_t *error) {
    char *u = escape(client->curl, client->credentials.username);
    char *p = escape(client->curl, client->credentials.password);
    char *a = escape(client->curl, action);
    char *v = escape(client->curl, param_value);
    if (!u || !p || !a || !v) {
        if (u)
            curl_free(u);
        if (p)
            curl_free(p);
        if (a)
            curl_free(a);
        if (v)
            curl_free(v);
        vip_error_set(error, VIP_ERR_NOMEM, "falha ao codificar URL Xtream");
        return NULL;
    }
    size_t n = strlen(client->credentials.server) + strlen(u) + strlen(p) + strlen(a) + strlen(param_name) +
               strlen(v) + 80u;
    char *url = malloc(n);
    if (!url) {
        curl_free(u);
        curl_free(p);
        curl_free(a);
        curl_free(v);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL Xtream");
        return NULL;
    }
    snprintf(url, n, "%splayer_api.php?username=%s&password=%s&action=%s&%s=%s", client->credentials.server,
             u, p, a, param_name, v);
    curl_free(u);
    curl_free(p);
    curl_free(a);
    curl_free(v);
    return url;
}

/* Parse streams json using the Xtream provider. */
vip_status_t vip_xtream_parse_streams_json(const char *json, const vip_credentials_t *credentials,
                                           vip_channel_list_t *out, vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root)
            json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "lista de canais Xtream inválida");
        return VIP_ERR_MALFORMED;
    }
    vip_channel_list_init(out);
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(root, i);
        if (!row || json_object_get_type(row) != json_type_object)
            continue;
        const char *stream_type = jstr(row, "stream_type");
        if (stream_type[0] && strcmp(stream_type, "live") != 0)
            continue;
        char *id = stream_id_string(row);
        const char *name = jstr(row, "name");
        if (!id || !name[0]) {
            free(id);
            continue;
        }
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

/* Parse vod streams json. */
static vip_status_t parse_vod_streams_json(const char *json, const vip_credentials_t *credentials,
                                           vip_channel_list_t *out, vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root)
            json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "lista de filmes Xtream inválida");
        return VIP_ERR_MALFORMED;
    }
    vip_channel_list_init(out);
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(root, i);
        if (!row || json_object_get_type(row) != json_type_object)
            continue;
        char *raw_id = stream_id_string(row);
        const char *name = jstr(row, "name");
        if (!raw_id || !name[0]) {
            free(raw_id);
            continue;
        }
        char *id = prefixed_id("vod:", raw_id);
        const char *direct = jstr(row, "direct_source");
        const char *extension = jstr(row, "container_extension");
        char *generated = NULL;
        const char *stream_url = direct[0] ? direct
                                           : (generated = make_media_url(credentials, "movie", raw_id,
                                                                         extension[0] ? extension : "mp4"));
        if (!id || !stream_url) {
            free(id);
            free(raw_id);
            free(generated);
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
        free(id);
        free(raw_id);
        free(generated);
        if (st != VIP_OK) {
            vip_channel_list_clear(out);
            json_object_put(root);
            return st;
        }
    }
    json_object_put(root);
    return VIP_OK;
}

/* Handle the series id string operation. */
static char *series_id_string(json_object *row) {
    json_object *id = NULL;
    if (!json_object_object_get_ex(row, "series_id", &id) || !id)
        return NULL;
    if (json_object_get_type(id) == json_type_string)
        return vip_strdup(json_object_get_string(id));
    if (json_object_get_type(id) == json_type_int) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%lld", (long long)json_object_get_int64(id));
        return vip_strdup(tmp);
    }
    return NULL;
}

/* Parse series json. */
static vip_status_t parse_series_json(const char *json, const vip_credentials_t *credentials,
                                      vip_channel_list_t *out, vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root)
            json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "lista de séries Xtream inválida");
        return VIP_ERR_MALFORMED;
    }
    vip_channel_list_init(out);
    size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object *row = json_object_array_get_idx(root, i);
        if (!row || json_object_get_type(row) != json_type_object)
            continue;
        char *raw_id = series_id_string(row);
        const char *name = jstr(row, "name");
        if (!name[0])
            name = jstr(row, "title");
        if (!raw_id || !name[0]) {
            free(raw_id);
            continue;
        }
        char *id = prefixed_id("series:", raw_id);
        size_t un = strlen(raw_id) + 10u;
        char *url = malloc(un);
        if (url)
            snprintf(url, un, "series://%s", raw_id);
        if (!id || !url) {
            free(id);
            free(url);
            free(raw_id);
            vip_channel_list_clear(out);
            json_object_put(root);
            vip_error_set(error, VIP_ERR_NOMEM, "falha ao criar série Xtream");
            return VIP_ERR_NOMEM;
        }
        const char *cover = jstr(row, "cover");
        if (!cover[0])
            cover = jstr(row, "stream_icon");
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
        free(id);
        free(url);
        free(raw_id);
        if (st != VIP_OK) {
            vip_channel_list_clear(out);
            json_object_put(root);
            return st;
        }
    }
    json_object_put(root);
    return VIP_OK;
}

/* Handle the episode image operation. */
static const char *episode_image(json_object *episode) {
    json_object *info = NULL;
    if (!json_object_object_get_ex(episode, "info", &info) || !info ||
        json_object_get_type(info) != json_type_object)
        return "";
    const char *image = jstr(info, "movie_image");
    if (!image[0])
        image = jstr(info, "cover_big");
    return image;
}

/* Append episode. */
static vip_status_t push_episode(json_object *episode, const char *season,
                                 const vip_credentials_t *credentials, vip_channel_list_t *episodes_out,
                                 int fallback_position, vip_error_t *error) {
    if (!episode || json_object_get_type(episode) != json_type_object)
        return VIP_OK;
    const char *raw_id = jstr(episode, "id");
    if (!raw_id[0])
        raw_id = jstr(episode, "stream_id");
    if (!raw_id[0])
        return VIP_OK;
    const char *episode_num = jstr(episode, "episode_num");
    int episode_number = fallback_position + 1;
    if (episode_num[0]) {
        char *end = NULL;
        long parsed = strtol(episode_num, &end, 10);
        if (end != episode_num && end && *end == '\0' && parsed > 0 && parsed <= INT_MAX)
            episode_number = (int)parsed;
    }
    const char *title = jstr(episode, "title");
    char fallback[96];
    if (!title[0]) {
        snprintf(fallback, sizeof(fallback), "Episódio %s", episode_num[0] ? episode_num : raw_id);
        title = fallback;
    }
    const char *extension = jstr(episode, "container_extension");
    char *url = make_media_url(credentials, "series", raw_id, extension[0] ? extension : "mp4");
    char *id = prefixed_id("episode:", raw_id);
    if (!url || !id) {
        free(url);
        free(id);
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
        .position = episode_number,
    };
    vip_status_t st = vip_channel_list_push(episodes_out, &item, error);
    free(url);
    free(id);
    return st;
}

/* Parse series info json. */
static vip_status_t parse_series_info_json(const char *json, const vip_credentials_t *credentials,
                                           vip_media_metadata_t *metadata_out,
                                           vip_category_list_t *seasons_out, vip_channel_list_t *episodes_out,
                                           vip_error_t *error) {
    json_object *root = json_tokener_parse(json);
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root)
            json_object_put(root);
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
            if (!arr || json_object_get_type(arr) != json_type_array)
                continue;
            char season_name[96];
            snprintf(season_name, sizeof(season_name), "Temporada %s", season);
            vip_category_t cat = {
                .provider_id = (char *)credentials->provider_id,
                .id = (char *)season,
                .name = season_name,
                .position = (int)seasons_out->len,
            };
            vip_status_t st = vip_category_list_push(seasons_out, &cat, error);
            if (st != VIP_OK)
                goto fail;
            size_t count = json_object_array_length(arr);
            for (size_t i = 0; i < count; ++i) {
                st = push_episode(json_object_array_get_idx(arr, i), season, credentials, episodes_out,
                                  position++, error);
                if (st != VIP_OK)
                    goto fail;
            }
        }
    } else if (json_object_get_type(episodes) == json_type_array) {
        vip_category_t cat = {
            .provider_id = (char *)credentials->provider_id,
            .id = "1",
            .name = "Episódios",
            .position = 0,
        };
        vip_status_t st = vip_category_list_push(seasons_out, &cat, error);
        if (st != VIP_OK)
            goto fail;
        size_t count = json_object_array_length(episodes);
        for (size_t i = 0; i < count; ++i) {
            st = push_episode(json_object_array_get_idx(episodes, i), "1", credentials, episodes_out,
                              position++, error);
            if (st != VIP_OK)
                goto fail;
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
    if (metadata_out)
        vip_media_metadata_clear(metadata_out);
    vip_category_list_clear(seasons_out);
    vip_channel_list_clear(episodes_out);
    json_object_put(root);
    return error ? error->code : VIP_ERR_NOMEM;
}

/* Authenticate the requested state using the Xtream provider. */
vip_status_t vip_xtream_authenticate(vip_xtream_client_t *client, vip_error_t *error) {
    char *url = make_api_url(client, NULL, error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_auth_json(body, error);
    free(body);
    return st;
}

/* Handle the xtream live categories operation. */
vip_status_t vip_xtream_live_categories(vip_xtream_client_t *client, vip_category_list_t *out,
                                        vip_error_t *error) {
    char *url = make_api_url(client, "get_live_categories", error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_categories_json(body, client->credentials.provider_id, out, error);
    free(body);
    return st;
}

/* Handle the xtream live streams operation. */
vip_status_t vip_xtream_live_streams(vip_xtream_client_t *client, vip_channel_list_t *out,
                                     vip_error_t *error) {
    char *url = make_api_url(client, "get_live_streams", error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_streams_json(body, &client->credentials, out, error);
    free(body);
    return st;
}

/* Handle the xtream vod categories operation. */
vip_status_t vip_xtream_vod_categories(vip_xtream_client_t *client, vip_category_list_t *out,
                                       vip_error_t *error) {
    char *url = make_api_url(client, "get_vod_categories", error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_categories_json(body, client->credentials.provider_id, out, error);
    free(body);
    return st;
}

/* Handle the xtream vod streams operation. */
vip_status_t vip_xtream_vod_streams(vip_xtream_client_t *client, vip_channel_list_t *out,
                                    vip_error_t *error) {
    char *url = make_api_url(client, "get_vod_streams", error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = parse_vod_streams_json(body, &client->credentials, out, error);
    free(body);
    return st;
}

/* Handle the xtream vod info operation. */
vip_status_t vip_xtream_vod_info(vip_xtream_client_t *client, const char *vod_id,
                                 vip_media_metadata_t *metadata_out, vip_error_t *error) {
    if (!client || !vod_id || !vod_id[0] || !metadata_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "filme inválido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *raw = vod_id;
    if (strncmp(raw, "vod:", 4u) == 0)
        raw += 4;
    char *url = make_api_url_param(client, "get_vod_info", "vod_id", raw, error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_vod_info_json(body, metadata_out, error);
    free(body);
    return st;
}

/* Handle the xtream series categories operation. */
vip_status_t vip_xtream_series_categories(vip_xtream_client_t *client, vip_category_list_t *out,
                                          vip_error_t *error) {
    char *url = make_api_url(client, "get_series_categories", error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_categories_json(body, client->credentials.provider_id, out, error);
    free(body);
    return st;
}

/* Handle the xtream series operation. */
vip_status_t vip_xtream_series(vip_xtream_client_t *client, vip_channel_list_t *out, vip_error_t *error) {
    char *url = make_api_url(client, "get_series", error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = parse_series_json(body, &client->credentials, out, error);
    free(body);
    return st;
}

/* Handle the xtream series metadata operation. */
vip_status_t vip_xtream_series_metadata(vip_xtream_client_t *client, const char *series_id,
                                        vip_media_metadata_t *metadata_out, vip_error_t *error) {
    if (!client || !series_id || !series_id[0] || !metadata_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "série inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *raw = series_id;
    if (strncmp(raw, "series:", 7u) == 0)
        raw += 7;
    char *url = make_api_url_param(client, "get_series_info", "series_id", raw, error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = vip_xtream_parse_series_metadata_json(body, metadata_out, error);
    free(body);
    return st;
}

/* Handle the xtream series info operation. */
vip_status_t vip_xtream_series_info(vip_xtream_client_t *client, const char *series_id,
                                    vip_media_metadata_t *metadata_out, vip_category_list_t *seasons_out,
                                    vip_channel_list_t *episodes_out, vip_error_t *error) {
    if (!client || !series_id || !series_id[0] || !metadata_out || !seasons_out || !episodes_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "série inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *raw = series_id;
    if (strncmp(raw, "series:", 7u) == 0)
        raw += 7;
    char *url = make_api_url_param(client, "get_series_info", "series_id", raw, error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = parse_series_info_json(body, &client->credentials, metadata_out, seasons_out, episodes_out,
                                    error);
    free(body);
    return st;
}

/* Handle the xtream series episodes operation. */
vip_status_t vip_xtream_series_episodes(vip_xtream_client_t *client, const char *series_id,
                                        vip_category_list_t *seasons_out, vip_channel_list_t *episodes_out,
                                        vip_error_t *error) {
    if (!client || !series_id || !series_id[0] || !seasons_out || !episodes_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "série inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    const char *raw = series_id;
    if (strncmp(raw, "series:", 7u) == 0)
        raw += 7;
    char *url = make_api_url_param(client, "get_series_info", "series_id", raw, error);
    if (!url)
        return error ? error->code : VIP_ERR_NOMEM;
    char *body = NULL;
    vip_status_t st = http_get(client, url, &body, error);
    free(url);
    if (st == VIP_OK)
        st = parse_series_info_json(body, &client->credentials, NULL, seasons_out, episodes_out, error);
    free(body);
    return st;
}
