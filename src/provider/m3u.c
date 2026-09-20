/* SPDX-License-Identifier: MIT */
/* Local/HTTP M3U parser that maps playlists into the common catalog model. */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/provider_m3u.h"

#include <curl/curl.h>
#include <json-c/json.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIP_M3U_MAX_MIB 128u
#define VIP_M3U_MAX_BYTES ((size_t)VIP_M3U_MAX_MIB * 1024u * 1024u)

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool overflow;
} m3u_buf_t;

/* Update an FNV hash with the supplied text. */
static uint64_t fnv_update(uint64_t h, const char *text) {
    for (const unsigned char *p = (const unsigned char *)text; p && *p; ++p) {
        h ^= *p;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

/* Return a stable FNV hash for the supplied text. */
static uint64_t fnv_text(const char *text) {
    return fnv_update(UINT64_C(14695981039346656037), text ? text : "");
}

/* Hash UTF-8 text using the same UTF-16 code units that JavaScript's
 * charCodeAt() sees. This keeps Linux lookup keys identical to Lista/Tizen,
 * including accented titles and astral Unicode characters. */
static uint32_t card_hash_js_text(uint32_t hash, const char *text) {
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        uint32_t cp;
        if (*p < 0x80u) {
            cp = *p++;
        } else if ((*p & 0xe0u) == 0xc0u && p[1]) {
            cp = ((uint32_t)(p[0] & 0x1fu) << 6) |
                 (uint32_t)(p[1] & 0x3fu);
            p += 2;
        } else if ((*p & 0xf0u) == 0xe0u && p[1] && p[2]) {
            cp = ((uint32_t)(p[0] & 0x0fu) << 12) |
                 ((uint32_t)(p[1] & 0x3fu) << 6) |
                 (uint32_t)(p[2] & 0x3fu);
            p += 3;
        } else if ((*p & 0xf8u) == 0xf0u && p[1] && p[2] && p[3]) {
            cp = ((uint32_t)(p[0] & 0x07u) << 18) |
                 ((uint32_t)(p[1] & 0x3fu) << 12) |
                 ((uint32_t)(p[2] & 0x3fu) << 6) |
                 (uint32_t)(p[3] & 0x3fu);
            p += 4;
        } else {
            /* Invalid UTF-8: preserve the raw byte deterministically. */
            cp = *p++;
        }

        if (cp <= 0xffffu) {
            hash = hash * 31u + cp;
        } else {
            cp -= 0x10000u;
            uint32_t high = 0xd800u + (cp >> 10);
            uint32_t low = 0xdc00u + (cp & 0x3ffu);
            hash = hash * 31u + high;
            hash = hash * 31u + low;
        }
    }
    return hash;
}

/* Build the 16-hex card lookup key from canonical group + item name. */
static void card_lookup_key(char out[17], const char *name, const char *group) {
    uint32_t a = UINT32_C(0x13579bdf);
    uint32_t b = UINT32_C(0x2468ace1);

    a = card_hash_js_text(a, group);
    b = card_hash_js_text(b, group);
    a = a * 31u; /* embedded NUL separator */
    b = b * 31u;
    a = card_hash_js_text(a, name);
    b = card_hash_js_text(b, name);

    snprintf(out, 17u, "%08x%08x", a, b);
}

/* Format a deterministic short identifier derived from text. */
static void stable_id(char out[17], const char *text) {
    snprintf(out, 17u, "%016llx", (unsigned long long)fnv_text(text));
}

/* Append one libcurl response chunk to the bounded M3U download buffer. */
static size_t curl_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    m3u_buf_t *buf = userdata;
    if (size != 0u && nmemb > SIZE_MAX / size) {
        buf->overflow = true;
        return 0u;
    }
    size_t bytes = size * nmemb;
    if (bytes > VIP_M3U_MAX_BYTES || buf->len > VIP_M3U_MAX_BYTES - bytes) {
        buf->overflow = true;
        return 0u;
    }
    size_t need = buf->len + bytes + 1u;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 8192u;
        while (cap < need && cap < VIP_M3U_MAX_BYTES + 1u)
            cap *= 2u;
        if (cap > VIP_M3U_MAX_BYTES + 1u)
            cap = VIP_M3U_MAX_BYTES + 1u;
        char *grown = realloc(buf->data, cap);
        if (!grown)
            return 0u;
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

/* Load http. */
static vip_status_t load_http(const char *url, char **body_out, vip_error_t *error) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        vip_error_set(error, VIP_ERR_NETWORK, "falha ao inicializar HTTP para M3U");
        return VIP_ERR_NETWORK;
    }
    m3u_buf_t buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 6000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 60000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Blazzing/1.2");
#ifdef CURL_HTTP_VERSION_2TLS
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#endif
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK || buf.overflow || http >= 400) {
        free(buf.data);
        if (buf.overflow)
            vip_error_set(error, VIP_ERR_NETWORK, "playlist M3U excede o limite de %u MiB", VIP_M3U_MAX_MIB);
        else if (http >= 400)
            vip_error_set(error, VIP_ERR_NETWORK, "servidor M3U respondeu HTTP %ld", http);
        else
            vip_error_set(error, VIP_ERR_NETWORK, "não foi possível baixar a playlist M3U");
        return VIP_ERR_NETWORK;
    }
    if (!buf.data)
        buf.data = vip_strdup("");
    if (!buf.data)
        return VIP_ERR_NOMEM;
    *body_out = buf.data;
    return VIP_OK;
}

/* Load file. */
static vip_status_t load_file(const char *path, char **body_out, vip_error_t *error) {
    const char *real_path = strncmp(path, "file://", 7u) == 0 ? path + 7 : path;
    FILE *fp = fopen(real_path, "rb");
    if (!fp) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível abrir M3U local: %s", strerror(errno));
        return VIP_ERR_IO;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return VIP_ERR_IO;
    }
    long n = ftell(fp);
    if (n < 0 || (unsigned long)n > VIP_M3U_MAX_BYTES) {
        fclose(fp);
        vip_error_set(error, VIP_ERR_IO, "playlist M3U local inválida ou maior que %u MiB", VIP_M3U_MAX_MIB);
        return VIP_ERR_IO;
    }
    rewind(fp);
    char *body = malloc((size_t)n + 1u);
    if (!body) {
        fclose(fp);
        return VIP_ERR_NOMEM;
    }
    size_t got = fread(body, 1u, (size_t)n, fp);
    fclose(fp);
    if (got != (size_t)n) {
        free(body);
        vip_error_set(error, VIP_ERR_IO, "falha ao ler M3U local");
        return VIP_ERR_IO;
    }
    body[got] = '\0';
    *body_out = body;
    return VIP_OK;
}

/* Best-effort text fetch for optional metadata such as card shards. */
static vip_status_t load_optional_text(const char *source, char **body_out) {
    vip_error_t ignored = {0};
    if (!source || !source[0] || !body_out)
        return VIP_ERR_INVALID_ARGUMENT;
    *body_out = NULL;
    if (strncmp(source, "http://", 7u) == 0 || strncmp(source, "https://", 8u) == 0)
        return load_http(source, body_out, &ignored);
    return load_file(source, body_out, &ignored);
}

/* Resolve a category name from its stable category id. */
static const char *category_name_by_id(const vip_category_list_t *categories, const char *id) {
    if (!categories || !id)
        return "";
    for (size_t i = 0u; i < categories->len; ++i) {
        if (categories->items[i].id && strcmp(categories->items[i].id, id) == 0)
            return categories->items[i].name ? categories->items[i].name : "";
    }
    return "";
}

/* Fill missing logo_url values from the compact card-artwork shards published
 * beside xoykor/Lista. This is optional metadata: failures never make an M3U
 * unusable and standard playlists without the custom header do zero requests. */
static void apply_external_card_artwork(const char *base, const char *version,
                                        const vip_category_list_t *categories,
                                        vip_channel_list_t *channels) {
    if (!base || !base[0] || !channels || channels->len == 0u)
        return;

    char (*keys)[17] = calloc(channels->len, sizeof(*keys));
    if (!keys)
        return;

    for (size_t i = 0u; i < channels->len; ++i) {
        if (channels->items[i].logo_url && channels->items[i].logo_url[0])
            continue;
        const char *group = category_name_by_id(categories, channels->items[i].category_id);
        card_lookup_key(keys[i], channels->items[i].name, group);
    }

    size_t base_len = strlen(base);
    bool slash = base_len > 0u && base[base_len - 1u] == '/';
    for (unsigned shard = 0u; shard < 16u; ++shard) {
        char prefix = "0123456789abcdef"[shard];
        size_t version_len = version ? strlen(version) : 0u;
        bool remote = strncmp(base, "http://", 7u) == 0 ||
                      strncmp(base, "https://", 8u) == 0;
        size_t url_len = base_len + (slash ? 0u : 1u) + 6u +
                         (remote && version_len ? 3u + version_len : 0u) + 1u;
        char *url = malloc(url_len);
        if (!url)
            break;
        if (remote && version_len)
            snprintf(url, url_len, "%s%s%c.json?v=%s", base, slash ? "" : "/", prefix, version);
        else
            snprintf(url, url_len, "%s%s%c.json", base, slash ? "" : "/", prefix);

        char *body = NULL;
        if (load_optional_text(url, &body) == VIP_OK && body) {
            struct json_object *root = json_tokener_parse(body);
            if (root && json_object_is_type(root, json_type_object)) {
                for (size_t i = 0u; i < channels->len; ++i) {
                    if (!keys[i][0] || keys[i][0] != prefix)
                        continue;
                    if (channels->items[i].logo_url && channels->items[i].logo_url[0])
                        continue;
                    struct json_object *value = NULL;
                    if (json_object_object_get_ex(root, keys[i], &value) &&
                        json_object_is_type(value, json_type_string)) {
                        const char *logo = json_object_get_string(value);
                        if (logo && (strncmp(logo, "http://", 7u) == 0 ||
                                     strncmp(logo, "https://", 8u) == 0 ||
                                     strncmp(logo, "file://", 7u) == 0)) {
                            char *copy = vip_strdup(logo);
                            if (copy)
                                channels->items[i].logo_url = copy;
                        }
                    }
                }
            }
            if (root)
                json_object_put(root);
            free(body);
        }
        free(url);
    }
    free(keys);
}

/* Trim trim. */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s))
        ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

/* Handle the attr dup operation. */
static char *attr_dup(const char *line, const char *key) {
    size_t kn = strlen(key);
    const char *p = line;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == line || isspace((unsigned char)p[-1]) || p[-1] == ',') && p[kn] == '=') {
            p += kn + 1u;
            if (*p == '"') {
                ++p;
                const char *end = strchr(p, '"');
                if (!end)
                    return NULL;
                size_t n = (size_t)(end - p);
                char *out = malloc(n + 1u);
                if (!out)
                    return NULL;
                memcpy(out, p, n);
                out[n] = '\0';
                return out;
            }
            const char *end = p;
            while (*end && *end != ',' && !isspace((unsigned char)*end))
                ++end;
            size_t n = (size_t)(end - p);
            char *out = malloc(n + 1u);
            if (!out)
                return NULL;
            memcpy(out, p, n);
            out[n] = '\0';
            return out;
        }
        p += kn;
    }
    return NULL;
}

/* Return the name of the requested state in the extinf. */
static char *extinf_name(const char *line) {
    /* The title begins at the first comma outside a quoted attribute. Using
       strrchr() truncated ordinary titles such as "News, HD" to " HD". */
    bool quoted = false;
    const char *comma = NULL;
    for (const char *p = line; p && *p; ++p) {
        if (*p == '"')
            quoted = !quoted;
        else if (*p == ',' && !quoted) {
            comma = p;
            break;
        }
    }
    const char *name = comma ? comma + 1 : "Canal";
    while (*name && isspace((unsigned char)*name))
        ++name;
    return vip_strdup(*name ? name : "Canal");
}

/* Find category. */
static int find_category(const vip_category_list_t *list, const char *name) {
    for (size_t i = 0; i < list->len; ++i)
        if (list->items[i].name && strcmp(list->items[i].name, name) == 0)
            return (int)i;
    return -1;
}

/* Ensure category. */
static vip_status_t ensure_category(vip_category_list_t *cats, const char *provider_id, const char *group,
                                    char id_out[32], vip_error_t *error) {
    const char *name = group && group[0] ? group : "Sem grupo";
    int existing = find_category(cats, name);
    if (existing >= 0) {
        snprintf(id_out, 32u, "%s", cats->items[(size_t)existing].id);
        return VIP_OK;
    }
    char hash[17];
    stable_id(hash, name);
    snprintf(id_out, 32u, "m3ug:%s", hash);
    vip_category_t cat = {
        .provider_id = (char *)provider_id,
        .id = id_out,
        .name = (char *)name,
        .position = (int)cats->len,
    };
    return vip_category_list_push(cats, &cat, error);
}

/* Return whether scheme. */
static bool has_scheme(const char *s) {
    return strstr(s, "://") != NULL;
}

/* Relative media URLs are resolved against the playlist source while
 * absolute HTTP(S), file:// and local paths pass through unchanged. */
static char *resolve_url(const char *source, const char *stream) {
    if (!stream || !stream[0])
        return NULL;
    if (has_scheme(stream) || strncmp(stream, "rtmp:", 5u) == 0 || strncmp(stream, "udp:", 4u) == 0)
        return vip_strdup(stream);
    if (strncmp(source, "http://", 7u) == 0 || strncmp(source, "https://", 8u) == 0) {
        if (stream[0] == '/') {
            const char *authority = strstr(source, "://");
            authority = authority ? authority + 3 : source;
            const char *path = strchr(authority, '/');
            size_t origin = path ? (size_t)(path - source) : strlen(source);
            char *out = malloc(origin + strlen(stream) + 1u);
            if (!out)
                return NULL;
            memcpy(out, source, origin);
            strcpy(out + origin, stream);
            return out;
        }
        const char *slash = strrchr(source, '/');
        if (!slash)
            return vip_strdup(stream);
        size_t base = (size_t)(slash - source + 1);
        char *out = malloc(base + strlen(stream) + 1u);
        if (!out)
            return NULL;
        memcpy(out, source, base);
        strcpy(out + base, stream);
        return out;
    }
    const char *path = strncmp(source, "file://", 7u) == 0 ? source + 7 : source;
    const char *slash = strrchr(path, '/');
    if (!slash)
        return vip_strdup(stream);
    size_t base = (size_t)(slash - path + 1);
    char *out = malloc(base + strlen(stream) + 1u);
    if (!out)
        return NULL;
    memcpy(out, path, base);
    strcpy(out + base, stream);
    return out;
}


/* Resolve one alternative URL from a static fallback shard. */
vip_status_t vip_m3u_fallback_variant(const vip_channel_t *channel, size_t alternative_index,
                                      char **url_out, char **referer_out, char **user_agent_out,
                                      vip_error_t *error) {
    if (url_out)
        *url_out = NULL;
    if (referer_out)
        *referer_out = NULL;
    if (user_agent_out)
        *user_agent_out = NULL;

    if (!channel || !url_out || !channel->fallback_id || !channel->fallback_id[0] ||
        !channel->fallback_base || !channel->fallback_base[0]) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "item sem fallback estático");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    size_t id_len = strlen(channel->fallback_id);
    int shard_len = channel->fallback_shard_length;
    if (shard_len < 1 || shard_len > 4)
        shard_len = 2;
    if ((size_t)shard_len > id_len)
        shard_len = (int)id_len;

    size_t base_len = strlen(channel->fallback_base);
    bool slash = base_len > 0u && channel->fallback_base[base_len - 1u] == '/';
    size_t version_len = channel->fallback_version ? strlen(channel->fallback_version) : 0u;
    size_t need = base_len + (slash ? 0u : 1u) + (size_t)shard_len + 5u +
                  (version_len ? 3u + version_len : 0u) + 1u;
    char *shard_url = malloc(need);
    if (!shard_url) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL de fallback");
        return VIP_ERR_NOMEM;
    }

    if (version_len) {
        snprintf(shard_url, need, "%s%s%.*s.json?v=%s", channel->fallback_base,
                 slash ? "" : "/", shard_len, channel->fallback_id, channel->fallback_version);
    } else {
        snprintf(shard_url, need, "%s%s%.*s.json", channel->fallback_base,
                 slash ? "" : "/", shard_len, channel->fallback_id);
    }

    char *body = NULL;
    vip_status_t st = load_optional_text(shard_url, &body);
    free(shard_url);
    if (st != VIP_OK || !body) {
        free(body);
        vip_error_set(error, VIP_ERR_NETWORK, "não foi possível carregar shard de fallback");
        return VIP_ERR_NETWORK;
    }

    struct json_object *root = json_tokener_parse(body);
    free(body);
    if (!root || !json_object_is_type(root, json_type_object)) {
        if (root)
            json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "shard de fallback inválido");
        return VIP_ERR_MALFORMED;
    }

    struct json_object *rows = NULL;
    if (!json_object_object_get_ex(root, channel->fallback_id, &rows) ||
        !rows || !json_object_is_type(rows, json_type_array)) {
        json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "fallback não encontrado no shard");
        return VIP_ERR_MALFORMED;
    }

    size_t seen = 0u;
    size_t count = json_object_array_length(rows);
    for (size_t i = 0u; i < count; ++i) {
        struct json_object *row = json_object_array_get_idx(rows, i);
        if (!row || !json_object_is_type(row, json_type_array) ||
            json_object_array_length(row) < 1u)
            continue;

        struct json_object *url_obj = json_object_array_get_idx(row, 0u);
        if (!url_obj || !json_object_is_type(url_obj, json_type_string))
            continue;
        const char *url = json_object_get_string(url_obj);
        if (!url || (strncmp(url, "http://", 7u) != 0 &&
                     strncmp(url, "https://", 8u) != 0))
            continue;
        if (channel->stream_url && strcmp(url, channel->stream_url) == 0)
            continue;

        if (seen++ != alternative_index)
            continue;

        char *url_copy = vip_strdup(url);
        char *referer_copy = NULL;
        char *ua_copy = NULL;
        if (!url_copy) {
            json_object_put(root);
            vip_error_set(error, VIP_ERR_NOMEM, "sem memória para fallback");
            return VIP_ERR_NOMEM;
        }

        if (json_object_array_length(row) > 2u) {
            struct json_object *ref_obj = json_object_array_get_idx(row, 2u);
            if (ref_obj && json_object_is_type(ref_obj, json_type_string)) {
                const char *ref = json_object_get_string(ref_obj);
                referer_copy = vip_strdup_nullable(ref);
            }
        }
        if (json_object_array_length(row) > 3u) {
            struct json_object *ua_obj = json_object_array_get_idx(row, 3u);
            if (ua_obj && json_object_is_type(ua_obj, json_type_string)) {
                const char *ua = json_object_get_string(ua_obj);
                ua_copy = vip_strdup_nullable(ua);
            }
        }

        *url_out = url_copy;
        if (referer_out)
            *referer_out = referer_copy;
        else
            free(referer_copy);
        if (user_agent_out)
            *user_agent_out = ua_copy;
        else
            free(ua_copy);

        json_object_put(root);
        vip_error_clear(error);
        return VIP_OK;
    }

    json_object_put(root);
    vip_error_set(error, VIP_ERR_MALFORMED, "nenhuma alternativa de fallback restante");
    return VIP_ERR_MALFORMED;
}

/* Load the requested state using the M3U provider. */
vip_status_t vip_m3u_load(const char *source, vip_category_list_t *categories_out,
                          vip_channel_list_t *channels_out, char provider_id_out[17], vip_error_t *error) {
    if (!source || !source[0] || !categories_out || !channels_out || !provider_id_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "fonte M3U inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    char *body = NULL;
    vip_status_t st = (strncmp(source, "http://", 7u) == 0 || strncmp(source, "https://", 8u) == 0)
                          ? load_http(source, &body, error)
                          : load_file(source, &body, error);
    if (st != VIP_OK)
        return st;

    stable_id(provider_id_out, source);
    vip_category_list_init(categories_out);
    vip_channel_list_init(channels_out);

    char *pending_name = NULL;
    char *pending_logo = NULL;
    char *pending_group = NULL;
    char *pending_tvg_id = NULL;
    char *pending_fallback_id = NULL;
    char *fallback_index_base = NULL;
    char *fallback_index_version = NULL;
    int fallback_index_shard_length = 2;
    char *card_index_base = NULL;
    char *card_index_version = NULL;
    int position = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(body, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        line = trim(line);
        if (!line[0] || strcmp(line, "#EXTM3U") == 0)
            continue;
        if (strncmp(line, "#EXT-X-LISTA-FALLBACK:", sizeof("#EXT-X-LISTA-FALLBACK:") - 1u) == 0) {
            free(fallback_index_base);
            fallback_index_base = vip_strdup(trim(line + sizeof("#EXT-X-LISTA-FALLBACK:") - 1u));
            continue;
        }
        if (strncmp(line, "#EXT-X-LISTA-FALLBACK-VERSION:",
                    sizeof("#EXT-X-LISTA-FALLBACK-VERSION:") - 1u) == 0) {
            free(fallback_index_version);
            fallback_index_version =
                vip_strdup(trim(line + sizeof("#EXT-X-LISTA-FALLBACK-VERSION:") - 1u));
            continue;
        }
        if (strncmp(line, "#EXT-X-LISTA-FALLBACK-SHARD-LEN:",
                    sizeof("#EXT-X-LISTA-FALLBACK-SHARD-LEN:") - 1u) == 0) {
            char *value = trim(line + sizeof("#EXT-X-LISTA-FALLBACK-SHARD-LEN:") - 1u);
            long parsed = strtol(value, NULL, 10);
            if (parsed >= 1 && parsed <= 4)
                fallback_index_shard_length = (int)parsed;
            continue;
        }
        if (strncmp(line, "#EXT-X-LISTA-CARDS:", 19u) == 0) {
            free(card_index_base);
            card_index_base = vip_strdup(trim(line + 19u));
            continue;
        }
        if (strncmp(line, "#EXT-X-LISTA-CARDS-VERSION:", 27u) == 0) {
            free(card_index_version);
            card_index_version = vip_strdup(trim(line + 27u));
            continue;
        }
        if (strncmp(line, "#EXTINF", 7u) == 0) {
            free(pending_name);
            free(pending_logo);
            free(pending_group);
            free(pending_tvg_id);
            free(pending_fallback_id);
            pending_name = extinf_name(line);
            pending_logo = attr_dup(line, "tvg-logo");
            pending_group = attr_dup(line, "group-title");
            pending_tvg_id = attr_dup(line, "tvg-id");
            pending_fallback_id = attr_dup(line, "x-lista-fallback");
            continue;
        }
        if (line[0] == '#')
            continue;

        char *stream_url = resolve_url(source, line);
        if (!stream_url) {
            st = VIP_ERR_NOMEM;
            break;
        }
        char cat_id[32];
        st = ensure_category(categories_out, provider_id_out, pending_group, cat_id, error);
        if (st != VIP_OK) {
            free(stream_url);
            break;
        }
        char id_hash[17];
        const char *identity = pending_tvg_id && pending_tvg_id[0] ? pending_tvg_id : stream_url;
        stable_id(id_hash, identity);
        char channel_id[32];
        snprintf(channel_id, sizeof(channel_id), "m3u:%s", id_hash);
        vip_channel_t item = {
            .provider_id = provider_id_out,
            .id = channel_id,
            .category_id = cat_id,
            .name = pending_name && pending_name[0] ? pending_name : "Canal",
            .logo_url = pending_logo,
            .stream_url = stream_url,
            .epg_channel_id = NULL,
            .fallback_id = pending_fallback_id,
            .fallback_base = pending_fallback_id ? fallback_index_base : NULL,
            .fallback_version = pending_fallback_id ? fallback_index_version : NULL,
            .fallback_shard_length = pending_fallback_id ? fallback_index_shard_length : 0,
            .position = position,
        };
        st = vip_channel_list_push(channels_out, &item, error);
        if (st == VIP_OK)
            ++position;
        free(stream_url);
        free(pending_name);
        pending_name = NULL;
        free(pending_logo);
        pending_logo = NULL;
        free(pending_group);
        pending_group = NULL;
        free(pending_tvg_id);
        pending_tvg_id = NULL;
        free(pending_fallback_id);
        pending_fallback_id = NULL;
        if (st != VIP_OK)
            break;
    }
    free(pending_name);
    free(pending_logo);
    free(pending_group);
    free(pending_tvg_id);
    free(pending_fallback_id);

    if (st == VIP_OK && card_index_base && card_index_base[0])
        apply_external_card_artwork(card_index_base, card_index_version,
                                    categories_out, channels_out);

    free(fallback_index_base);
    free(fallback_index_version);
    free(card_index_base);
    free(card_index_version);
    free(body);

    if (st != VIP_OK) {
        vip_category_list_clear(categories_out);
        vip_channel_list_clear(channels_out);
        if (error && error->code == VIP_OK)
            vip_error_set(error, st, "falha ao processar playlist M3U");
        return st;
    }
    if (channels_out->len == 0u) {
        vip_category_list_clear(categories_out);
        vip_channel_list_clear(channels_out);
        vip_error_set(error, VIP_ERR_MALFORMED, "nenhum stream encontrado na playlist M3U");
        return VIP_ERR_MALFORMED;
    }
    vip_error_clear(error);
    return VIP_OK;
}
