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
static char (*build_card_lookup_keys(const vip_category_list_t *categories,
                                     vip_channel_list_t *channels))[17] {
    char (*keys)[17] = calloc(channels->len, sizeof(*keys));
    if (!keys)
        return NULL;

    for (size_t i = 0u; i < channels->len; ++i) {
        if (channels->items[i].logo_url && channels->items[i].logo_url[0])
            continue;
        const char *group = category_name_by_id(categories, channels->items[i].category_id);
        card_lookup_key(keys[i], channels->items[i].name, group);
    }
    return keys;
}

static char *build_card_shard_url(const char *base, const char *version, char prefix) {
    size_t base_len = strlen(base);
    bool slash = base_len > 0u && base[base_len - 1u] == '/';
    size_t version_len = version ? strlen(version) : 0u;
    bool remote = strncmp(base, "http://", 7u) == 0 ||
                  strncmp(base, "https://", 8u) == 0;
    size_t url_len = base_len + (slash ? 0u : 1u) + 6u +
                     (remote && version_len ? 3u + version_len : 0u) + 1u;

    char *url = malloc(url_len);
    if (!url)
        return NULL;
    if (remote && version_len)
        snprintf(url, url_len, "%s%s%c.json?v=%s",
                 base, slash ? "" : "/", prefix, version);
    else
        snprintf(url, url_len, "%s%s%c.json",
                 base, slash ? "" : "/", prefix);
    return url;
}

static bool card_logo_url_valid(const char *logo) {
    return logo &&
           (strncmp(logo, "http://", 7u) == 0 ||
            strncmp(logo, "https://", 8u) == 0 ||
            strncmp(logo, "file://", 7u) == 0);
}

static void apply_card_logo_value(vip_channel_t *channel, struct json_object *value) {
    if (!value || !json_object_is_type(value, json_type_string))
        return;
    const char *logo = json_object_get_string(value);
    if (!card_logo_url_valid(logo))
        return;

    char *copy = vip_strdup(logo);
    if (copy)
        channel->logo_url = copy;
}

static void apply_card_shard(struct json_object *root,
                             char prefix,
                             char (*keys)[17],
                             vip_channel_list_t *channels) {
    if (!root || !json_object_is_type(root, json_type_object))
        return;

    for (size_t i = 0u; i < channels->len; ++i) {
        if (!keys[i][0] || keys[i][0] != prefix)
            continue;
        if (channels->items[i].logo_url && channels->items[i].logo_url[0])
            continue;

        struct json_object *value = NULL;
        if (json_object_object_get_ex(root, keys[i], &value))
            apply_card_logo_value(&channels->items[i], value);
    }
}

static void load_and_apply_card_shard(const char *url,
                                      char prefix,
                                      char (*keys)[17],
                                      vip_channel_list_t *channels) {
    char *body = NULL;
    if (load_optional_text(url, &body) != VIP_OK || !body) {
        free(body);
        return;
    }

    struct json_object *root = json_tokener_parse(body);
    free(body);
    apply_card_shard(root, prefix, keys, channels);
    if (root)
        json_object_put(root);
}

static void apply_external_card_artwork(const char *base, const char *version,
                                        const vip_category_list_t *categories,
                                        vip_channel_list_t *channels) {
    if (!base || !base[0] || !channels || channels->len == 0u)
        return;

    char (*keys)[17] = build_card_lookup_keys(categories, channels);
    if (!keys)
        return;

    for (unsigned shard = 0u; shard < 16u; ++shard) {
        char prefix = "0123456789abcdef"[shard];
        char *url = build_card_shard_url(base, version, prefix);
        if (!url)
            break;
        load_and_apply_card_shard(url, prefix, keys, channels);
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
static void clear_fallback_outputs(char **url_out, char **referer_out, char **user_agent_out) {
    if (url_out)
        *url_out = NULL;
    if (referer_out)
        *referer_out = NULL;
    if (user_agent_out)
        *user_agent_out = NULL;
}

static bool fallback_channel_valid(const vip_channel_t *channel, char **url_out) {
    return channel && url_out &&
           channel->fallback_id && channel->fallback_id[0] &&
           channel->fallback_base && channel->fallback_base[0];
}

static int fallback_shard_length(const vip_channel_t *channel) {
    size_t id_len = strlen(channel->fallback_id);
    int shard_len = channel->fallback_shard_length;
    if (shard_len < 1 || shard_len > 4)
        shard_len = 2;
    if ((size_t)shard_len > id_len)
        shard_len = (int)id_len;
    return shard_len;
}

static char *build_fallback_shard_url(const vip_channel_t *channel) {
    int shard_len = fallback_shard_length(channel);
    size_t base_len = strlen(channel->fallback_base);
    bool slash = base_len > 0u && channel->fallback_base[base_len - 1u] == '/';
    size_t version_len = channel->fallback_version ? strlen(channel->fallback_version) : 0u;
    size_t need = base_len + (slash ? 0u : 1u) + (size_t)shard_len + 5u +
                  (version_len ? 3u + version_len : 0u) + 1u;

    char *url = malloc(need);
    if (!url)
        return NULL;

    if (version_len) {
        snprintf(url, need, "%s%s%.*s.json?v=%s", channel->fallback_base,
                 slash ? "" : "/", shard_len, channel->fallback_id,
                 channel->fallback_version);
    } else {
        snprintf(url, need, "%s%s%.*s.json", channel->fallback_base,
                 slash ? "" : "/", shard_len, channel->fallback_id);
    }
    return url;
}

static vip_status_t load_fallback_root(const vip_channel_t *channel,
                                       struct json_object **root_out,
                                       vip_error_t *error) {
    char *shard_url = build_fallback_shard_url(channel);
    if (!shard_url) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para URL de fallback");
        return VIP_ERR_NOMEM;
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

    *root_out = root;
    return VIP_OK;
}

static struct json_object *fallback_rows_for_channel(struct json_object *root,
                                                     const vip_channel_t *channel) {
    struct json_object *rows = NULL;
    if (!json_object_object_get_ex(root, channel->fallback_id, &rows))
        return NULL;
    if (!rows || !json_object_is_type(rows, json_type_array))
        return NULL;
    return rows;
}

static const char *fallback_row_url(struct json_object *row) {
    if (!row || !json_object_is_type(row, json_type_array) ||
        json_object_array_length(row) < 1u)
        return NULL;

    struct json_object *url_obj = json_object_array_get_idx(row, 0u);
    if (!url_obj || !json_object_is_type(url_obj, json_type_string))
        return NULL;

    const char *url = json_object_get_string(url_obj);
    if (!url)
        return NULL;
    if (strncmp(url, "http://", 7u) != 0 && strncmp(url, "https://", 8u) != 0)
        return NULL;
    return url;
}

static char *fallback_row_optional_string(struct json_object *row, size_t index) {
    if (json_object_array_length(row) <= index)
        return NULL;
    struct json_object *value = json_object_array_get_idx(row, index);
    if (!value || !json_object_is_type(value, json_type_string))
        return NULL;
    return vip_strdup_nullable(json_object_get_string(value));
}

static bool fallback_row_is_candidate(const vip_channel_t *channel,
                                      struct json_object *row,
                                      const char **url_out) {
    const char *url = fallback_row_url(row);
    if (!url)
        return false;
    if (channel->stream_url && strcmp(url, channel->stream_url) == 0)
        return false;
    *url_out = url;
    return true;
}

static vip_status_t copy_fallback_row(struct json_object *row,
                                      const char *url,
                                      char **url_out,
                                      char **referer_out,
                                      char **user_agent_out,
                                      vip_error_t *error) {
    char *url_copy = vip_strdup(url);
    if (!url_copy) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para fallback");
        return VIP_ERR_NOMEM;
    }

    char *referer_copy = fallback_row_optional_string(row, 2u);
    char *user_agent_copy = fallback_row_optional_string(row, 3u);
    *url_out = url_copy;

    if (referer_out)
        *referer_out = referer_copy;
    else
        free(referer_copy);

    if (user_agent_out)
        *user_agent_out = user_agent_copy;
    else
        free(user_agent_copy);

    vip_error_clear(error);
    return VIP_OK;
}

static vip_status_t select_fallback_variant(const vip_channel_t *channel,
                                            struct json_object *rows,
                                            size_t alternative_index,
                                            char **url_out,
                                            char **referer_out,
                                            char **user_agent_out,
                                            vip_error_t *error) {
    size_t seen = 0u;
    size_t count = json_object_array_length(rows);
    for (size_t i = 0u; i < count; ++i) {
        struct json_object *row = json_object_array_get_idx(rows, i);
        const char *url = NULL;
        if (!fallback_row_is_candidate(channel, row, &url))
            continue;
        if (seen++ != alternative_index)
            continue;
        return copy_fallback_row(row, url, url_out, referer_out, user_agent_out, error);
    }

    vip_error_set(error, VIP_ERR_MALFORMED, "nenhuma alternativa de fallback restante");
    return VIP_ERR_MALFORMED;
}

vip_status_t vip_m3u_fallback_variant(const vip_channel_t *channel, size_t alternative_index,
                                      char **url_out, char **referer_out, char **user_agent_out,
                                      vip_error_t *error) {
    clear_fallback_outputs(url_out, referer_out, user_agent_out);
    if (!fallback_channel_valid(channel, url_out)) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "item sem fallback estático");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    struct json_object *root = NULL;
    vip_status_t st = load_fallback_root(channel, &root, error);
    if (st != VIP_OK)
        return st;

    struct json_object *rows = fallback_rows_for_channel(root, channel);
    if (!rows) {
        json_object_put(root);
        vip_error_set(error, VIP_ERR_MALFORMED, "fallback não encontrado no shard");
        return VIP_ERR_MALFORMED;
    }

    st = select_fallback_variant(channel, rows, alternative_index,
                                 url_out, referer_out, user_agent_out, error);
    json_object_put(root);
    return st;
}

/* Load the requested state using the M3U provider. */
typedef struct {
    char *pending_name;
    char *pending_logo;
    char *pending_group;
    char *pending_tvg_id;
    char *pending_fallback_id;
    char *fallback_index_base;
    char *fallback_index_version;
    int fallback_index_shard_length;
    char *card_index_base;
    char *card_index_version;
    int position;
} m3u_parse_state_t;

static void clear_m3u_pending_entry(m3u_parse_state_t *state) {
    free(state->pending_name);
    free(state->pending_logo);
    free(state->pending_group);
    free(state->pending_tvg_id);
    free(state->pending_fallback_id);
    state->pending_name = NULL;
    state->pending_logo = NULL;
    state->pending_group = NULL;
    state->pending_tvg_id = NULL;
    state->pending_fallback_id = NULL;
}

static void clear_m3u_parse_state(m3u_parse_state_t *state) {
    clear_m3u_pending_entry(state);
    free(state->fallback_index_base);
    free(state->fallback_index_version);
    free(state->card_index_base);
    free(state->card_index_version);
}

static void replace_m3u_string(char **slot, const char *value) {
    free(*slot);
    *slot = vip_strdup(value);
}

static bool handle_m3u_index_directive(m3u_parse_state_t *state, char *line) {
    static const char fallback_prefix[] = "#EXT-X-LISTA-FALLBACK:";
    static const char fallback_version_prefix[] = "#EXT-X-LISTA-FALLBACK-VERSION:";
    static const char fallback_shard_prefix[] = "#EXT-X-LISTA-FALLBACK-SHARD-LEN:";
    static const char cards_prefix[] = "#EXT-X-LISTA-CARDS:";
    static const char cards_version_prefix[] = "#EXT-X-LISTA-CARDS-VERSION:";

    if (strncmp(line, fallback_prefix, sizeof(fallback_prefix) - 1u) == 0) {
        replace_m3u_string(&state->fallback_index_base,
                           trim(line + sizeof(fallback_prefix) - 1u));
        return true;
    }
    if (strncmp(line, fallback_version_prefix, sizeof(fallback_version_prefix) - 1u) == 0) {
        replace_m3u_string(&state->fallback_index_version,
                           trim(line + sizeof(fallback_version_prefix) - 1u));
        return true;
    }
    if (strncmp(line, fallback_shard_prefix, sizeof(fallback_shard_prefix) - 1u) == 0) {
        char *value = trim(line + sizeof(fallback_shard_prefix) - 1u);
        long parsed = strtol(value, NULL, 10);
        if (parsed >= 1 && parsed <= 4)
            state->fallback_index_shard_length = (int)parsed;
        return true;
    }
    if (strncmp(line, cards_prefix, sizeof(cards_prefix) - 1u) == 0) {
        replace_m3u_string(&state->card_index_base,
                           trim(line + sizeof(cards_prefix) - 1u));
        return true;
    }
    if (strncmp(line, cards_version_prefix, sizeof(cards_version_prefix) - 1u) == 0) {
        replace_m3u_string(&state->card_index_version,
                           trim(line + sizeof(cards_version_prefix) - 1u));
        return true;
    }
    return false;
}

static void parse_m3u_extinf(m3u_parse_state_t *state, const char *line) {
    clear_m3u_pending_entry(state);
    state->pending_name = extinf_name(line);
    state->pending_logo = attr_dup(line, "tvg-logo");
    state->pending_group = attr_dup(line, "group-title");
    state->pending_tvg_id = attr_dup(line, "tvg-id");
    state->pending_fallback_id = attr_dup(line, "x-lista-fallback");
}

static vip_status_t push_m3u_stream(const char *source,
                                    const char provider_id[17],
                                    m3u_parse_state_t *state,
                                    const char *line,
                                    vip_category_list_t *categories,
                                    vip_channel_list_t *channels,
                                    vip_error_t *error) {
    char *stream_url = resolve_url(source, line);
    if (!stream_url)
        return VIP_ERR_NOMEM;

    char category_id[32];
    vip_status_t st = ensure_category(categories, provider_id,
                                      state->pending_group, category_id, error);
    if (st != VIP_OK) {
        free(stream_url);
        return st;
    }

    const char *identity = state->pending_tvg_id && state->pending_tvg_id[0]
                               ? state->pending_tvg_id
                               : stream_url;
    char id_hash[17];
    stable_id(id_hash, identity);
    char channel_id[32];
    snprintf(channel_id, sizeof(channel_id), "m3u:%s", id_hash);

    vip_channel_t item = {
        .provider_id = provider_id,
        .id = channel_id,
        .category_id = category_id,
        .name = state->pending_name && state->pending_name[0] ? state->pending_name : "Canal",
        .logo_url = state->pending_logo,
        .stream_url = stream_url,
        .epg_channel_id = NULL,
        .fallback_id = state->pending_fallback_id,
        .fallback_base = state->pending_fallback_id ? state->fallback_index_base : NULL,
        .fallback_version = state->pending_fallback_id ? state->fallback_index_version : NULL,
        .fallback_shard_length = state->pending_fallback_id
                                     ? state->fallback_index_shard_length
                                     : 0,
        .position = state->position,
    };

    st = vip_channel_list_push(channels, &item, error);
    free(stream_url);
    if (st == VIP_OK)
        ++state->position;
    return st;
}

static vip_status_t process_m3u_line(const char *source,
                                     const char provider_id[17],
                                     m3u_parse_state_t *state,
                                     char *line,
                                     vip_category_list_t *categories,
                                     vip_channel_list_t *channels,
                                     vip_error_t *error) {
    line = trim(line);
    if (!line[0] || strcmp(line, "#EXTM3U") == 0)
        return VIP_OK;
    if (handle_m3u_index_directive(state, line))
        return VIP_OK;
    if (strncmp(line, "#EXTINF", 7u) == 0) {
        parse_m3u_extinf(state, line);
        return VIP_OK;
    }
    if (line[0] == '#')
        return VIP_OK;

    vip_status_t st = push_m3u_stream(source, provider_id, state, line,
                                      categories, channels, error);
    clear_m3u_pending_entry(state);
    return st;
}

static vip_status_t finalize_m3u_load(vip_category_list_t *categories,
                                      vip_channel_list_t *channels,
                                      vip_status_t st,
                                      vip_error_t *error) {
    if (st != VIP_OK) {
        vip_category_list_clear(categories);
        vip_channel_list_clear(channels);
        if (error && error->code == VIP_OK)
            vip_error_set(error, st, "falha ao processar playlist M3U");
        return st;
    }
    if (channels->len == 0u) {
        vip_category_list_clear(categories);
        vip_channel_list_clear(channels);
        vip_error_set(error, VIP_ERR_MALFORMED,
                      "nenhum stream encontrado na playlist M3U");
        return VIP_ERR_MALFORMED;
    }
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_m3u_load(const char *source, vip_category_list_t *categories_out,
                          vip_channel_list_t *channels_out, char provider_id_out[17],
                          vip_error_t *error) {
    if (!source || !source[0] || !categories_out || !channels_out || !provider_id_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "fonte M3U inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    char *body = NULL;
    bool remote = strncmp(source, "http://", 7u) == 0 ||
                  strncmp(source, "https://", 8u) == 0;
    vip_status_t st = remote ? load_http(source, &body, error)
                             : load_file(source, &body, error);
    if (st != VIP_OK)
        return st;

    stable_id(provider_id_out, source);
    vip_category_list_init(categories_out);
    vip_channel_list_init(channels_out);

    m3u_parse_state_t state = {
        .fallback_index_shard_length = 2,
    };
    char *saveptr = NULL;
    for (char *line = strtok_r(body, "\n", &saveptr);
         line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        st = process_m3u_line(source, provider_id_out, &state, line,
                              categories_out, channels_out, error);
        if (st != VIP_OK)
            break;
    }

    clear_m3u_pending_entry(&state);
    if (st == VIP_OK && state.card_index_base && state.card_index_base[0])
        apply_external_card_artwork(state.card_index_base, state.card_index_version,
                                    categories_out, channels_out);

    clear_m3u_parse_state(&state);
    free(body);
    return finalize_m3u_load(categories_out, channels_out, st, error);
}
