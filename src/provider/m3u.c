/* SPDX-License-Identifier: MIT */
/* Local/HTTP M3U parser that maps playlists into the common catalog model. */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/provider_m3u.h"

#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIP_M3U_MAX_BYTES (32u * 1024u * 1024u)

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool overflow;
} m3u_buf_t;

static uint64_t fnv_update(uint64_t h, const char *text) {
    for (const unsigned char *p = (const unsigned char *)text; p && *p; ++p) {
        h ^= *p;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static uint64_t fnv_text(const char *text) {
    return fnv_update(UINT64_C(14695981039346656037), text ? text : "");
}

static void stable_id(char out[17], const char *text) {
    snprintf(out, 17u, "%016llx", (unsigned long long)fnv_text(text));
}

static size_t curl_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    m3u_buf_t *buf = userdata;
    size_t bytes = size * nmemb;
    if (bytes > VIP_M3U_MAX_BYTES || buf->len > VIP_M3U_MAX_BYTES - bytes) {
        buf->overflow = true;
        return 0u;
    }
    size_t need = buf->len + bytes + 1u;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 8192u;
        while (cap < need && cap < VIP_M3U_MAX_BYTES + 1u) cap *= 2u;
        if (cap > VIP_M3U_MAX_BYTES + 1u) cap = VIP_M3U_MAX_BYTES + 1u;
        char *grown = realloc(buf->data, cap);
        if (!grown) return 0u;
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Visual-IPTV/1.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK || buf.overflow || http >= 400) {
        free(buf.data);
        if (buf.overflow) vip_error_set(error, VIP_ERR_NETWORK, "playlist M3U excede o limite de 32 MiB");
        else if (http >= 400) vip_error_set(error, VIP_ERR_NETWORK, "servidor M3U respondeu HTTP %ld", http);
        else vip_error_set(error, VIP_ERR_NETWORK, "não foi possível baixar a playlist M3U");
        return VIP_ERR_NETWORK;
    }
    if (!buf.data) buf.data = vip_strdup("");
    if (!buf.data) return VIP_ERR_NOMEM;
    *body_out = buf.data;
    return VIP_OK;
}

static vip_status_t load_file(const char *path, char **body_out, vip_error_t *error) {
    const char *real_path = strncmp(path, "file://", 7u) == 0 ? path + 7 : path;
    FILE *fp = fopen(real_path, "rb");
    if (!fp) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível abrir M3U local: %s", strerror(errno));
        return VIP_ERR_IO;
    }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return VIP_ERR_IO; }
    long n = ftell(fp);
    if (n < 0 || (unsigned long)n > VIP_M3U_MAX_BYTES) {
        fclose(fp);
        vip_error_set(error, VIP_ERR_IO, "playlist M3U local inválida ou maior que 32 MiB");
        return VIP_ERR_IO;
    }
    rewind(fp);
    char *body = malloc((size_t)n + 1u);
    if (!body) { fclose(fp); return VIP_ERR_NOMEM; }
    size_t got = fread(body, 1u, (size_t)n, fp);
    fclose(fp);
    if (got != (size_t)n) { free(body); vip_error_set(error, VIP_ERR_IO, "falha ao ler M3U local"); return VIP_ERR_IO; }
    body[got] = '\0';
    *body_out = body;
    return VIP_OK;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static char *attr_dup(const char *line, const char *key) {
    size_t kn = strlen(key);
    const char *p = line;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == line || isspace((unsigned char)p[-1]) || p[-1] == ',') && p[kn] == '=') {
            p += kn + 1u;
            if (*p == '"') {
                ++p;
                const char *end = strchr(p, '"');
                if (!end) return NULL;
                size_t n = (size_t)(end - p);
                char *out = malloc(n + 1u);
                if (!out) return NULL;
                memcpy(out, p, n); out[n] = '\0';
                return out;
            }
            const char *end = p;
            while (*end && *end != ',' && !isspace((unsigned char)*end)) ++end;
            size_t n = (size_t)(end - p);
            char *out = malloc(n + 1u);
            if (!out) return NULL;
            memcpy(out, p, n); out[n] = '\0';
            return out;
        }
        p += kn;
    }
    return NULL;
}

static char *extinf_name(const char *line) {
    const char *comma = strrchr(line, ',');
    const char *name = comma ? comma + 1 : "Canal";
    while (*name && isspace((unsigned char)*name)) ++name;
    return vip_strdup(*name ? name : "Canal");
}

static int find_category(const vip_category_list_t *list, const char *name) {
    for (size_t i = 0; i < list->len; ++i)
        if (list->items[i].name && strcmp(list->items[i].name, name) == 0) return (int)i;
    return -1;
}

static vip_status_t ensure_category(vip_category_list_t *cats,
                                    const char *provider_id,
                                    const char *group,
                                    char id_out[32],
                                    vip_error_t *error) {
    const char *name = group && group[0] ? group : "Sem grupo";
    int existing = find_category(cats, name);
    if (existing >= 0) {
        snprintf(id_out, 32u, "%s", cats->items[(size_t)existing].id);
        return VIP_OK;
    }
    char hash[17]; stable_id(hash, name);
    snprintf(id_out, 32u, "m3ug:%s", hash);
    vip_category_t cat = {
        .provider_id = (char *)provider_id,
        .id = id_out,
        .name = (char *)name,
        .position = (int)cats->len,
    };
    return vip_category_list_push(cats, &cat, error);
}

static bool has_scheme(const char *s) {
    return strstr(s, "://") != NULL;
}

/* Relative media URLs are resolved against the playlist source while
 * absolute HTTP(S), file:// and local paths pass through unchanged. */
static char *resolve_url(const char *source, const char *stream) {
    if (!stream || !stream[0]) return NULL;
    if (has_scheme(stream) || strncmp(stream, "rtmp:", 5u) == 0 || strncmp(stream, "udp:", 4u) == 0)
        return vip_strdup(stream);
    if (strncmp(source, "http://", 7u) == 0 || strncmp(source, "https://", 8u) == 0) {
        const char *slash = strrchr(source, '/');
        if (!slash) return vip_strdup(stream);
        size_t base = (size_t)(slash - source + 1);
        char *out = malloc(base + strlen(stream) + 1u);
        if (!out) return NULL;
        memcpy(out, source, base);
        strcpy(out + base, stream);
        return out;
    }
    const char *path = strncmp(source, "file://", 7u) == 0 ? source + 7 : source;
    const char *slash = strrchr(path, '/');
    if (!slash) return vip_strdup(stream);
    size_t base = (size_t)(slash - path + 1);
    char *out = malloc(base + strlen(stream) + 1u);
    if (!out) return NULL;
    memcpy(out, path, base);
    strcpy(out + base, stream);
    return out;
}

vip_status_t vip_m3u_load(const char *source,
                          vip_category_list_t *categories_out,
                          vip_channel_list_t *channels_out,
                          char provider_id_out[17],
                          vip_error_t *error) {
    if (!source || !source[0] || !categories_out || !channels_out || !provider_id_out) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "fonte M3U inválida");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    char *body = NULL;
    vip_status_t st = (strncmp(source, "http://", 7u) == 0 || strncmp(source, "https://", 8u) == 0)
                        ? load_http(source, &body, error) : load_file(source, &body, error);
    if (st != VIP_OK) return st;

    stable_id(provider_id_out, source);
    vip_category_list_init(categories_out);
    vip_channel_list_init(channels_out);

    char *pending_name = NULL;
    char *pending_logo = NULL;
    char *pending_group = NULL;
    int position = 0;
    char *saveptr = NULL;
    for (char *line = strtok_r(body, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        line = trim(line);
        if (!line[0] || strcmp(line, "#EXTM3U") == 0) continue;
        if (strncmp(line, "#EXTINF", 7u) == 0) {
            free(pending_name); free(pending_logo); free(pending_group);
            pending_name = extinf_name(line);
            pending_logo = attr_dup(line, "tvg-logo");
            pending_group = attr_dup(line, "group-title");
            continue;
        }
        if (line[0] == '#') continue;

        char *stream_url = resolve_url(source, line);
        if (!stream_url) { st = VIP_ERR_NOMEM; break; }
        char cat_id[32];
        st = ensure_category(categories_out, provider_id_out, pending_group, cat_id, error);
        if (st != VIP_OK) { free(stream_url); break; }
        char id_hash[17]; stable_id(id_hash, stream_url);
        char channel_id[32]; snprintf(channel_id, sizeof(channel_id), "m3u:%s", id_hash);
        vip_channel_t item = {
            .provider_id = provider_id_out,
            .id = channel_id,
            .category_id = cat_id,
            .name = pending_name && pending_name[0] ? pending_name : "Canal",
            .logo_url = pending_logo,
            .stream_url = stream_url,
            .epg_channel_id = NULL,
            .position = position++,
        };
        st = vip_channel_list_push(channels_out, &item, error);
        free(stream_url);
        free(pending_name); pending_name = NULL;
        free(pending_logo); pending_logo = NULL;
        free(pending_group); pending_group = NULL;
        if (st != VIP_OK) break;
    }
    free(pending_name); free(pending_logo); free(pending_group);
    free(body);

    if (st != VIP_OK) {
        vip_category_list_clear(categories_out);
        vip_channel_list_clear(channels_out);
        if (error && error->code == VIP_OK) vip_error_set(error, st, "falha ao processar playlist M3U");
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
