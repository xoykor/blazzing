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

static void normalize_channel_title(const char *raw, char *out, size_t out_size) {
    if (!out || out_size == 0u)
        return;
    out[0] = '\0';
    if (!raw)
        return;
    size_t j = 0u;
    bool prev_space = true;
    for (size_t i = 0u; raw[i] != '\0' && j + 2u < out_size; ++i) {
        unsigned char c = (unsigned char)raw[i];
        char mapped = 0;
        if (c == 0xC3u && raw[i + 1] != '\0') {
            unsigned char n = (unsigned char)raw[++i];
            if ((n >= 0x80u && n <= 0x85u) || (n >= 0xA0u && n <= 0xA5u))
                mapped = 'a';
            else if ((n >= 0x88u && n <= 0x8Bu) || (n >= 0xA8u && n <= 0xABu))
                mapped = 'e';
            else if ((n >= 0x8Cu && n <= 0x8Fu) || (n >= 0xACu && n <= 0xAFu))
                mapped = 'i';
            else if ((n >= 0x92u && n <= 0x96u) || (n >= 0xB2u && n <= 0xB6u))
                mapped = 'o';
            else if ((n >= 0x99u && n <= 0x9Cu) || (n >= 0xB9u && n <= 0xBCu))
                mapped = 'u';
            else if (n == 0x87u || n == 0xA7u)
                mapped = 'c';
            else
                mapped = ' ';
        } else if (c >= 'A' && c <= 'Z') {
            mapped = (char)(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '&' || c == '+') {
            mapped = (char)c;
        } else {
            mapped = ' ';
        }
        if (mapped == ' ') {
            if (!prev_space) {
                out[j++] = ' ';
                prev_space = true;
            }
        } else {
            out[j++] = mapped;
            prev_space = false;
        }
    }
    if (j > 0u && out[j - 1u] == ' ')
        --j;
    out[j] = '\0';
}

static char *resolve_channel_logo_url(const char *raw_title) {
    char norm[256];
    normalize_channel_title(raw_title, norm, sizeof(norm));
    if (norm[0] == '\0' || strncmp(norm, "radio ", 6u) == 0)
        return NULL;

    static const struct {
        const char *needle;
        const char *rel_path;
    } k_channel_logos[] = {
        {"globo news", "brazil/globo-news-br.png"},
        {"globonews", "brazil/globo-news-br.png"},
        {"globoplay novelas", "brazil/globoplay-novelas-br.png"},
        {"tv bahia", "brazil/tv-bahia-br.png"},
        {"tve bahia", "brazil/tv-bahia-br.png"},
        {"inter tv", "brazil/rede-inter-tv-br.png"},
        {"globo", "brazil/globo-br.png"},
        {"band news", "brazil/band-news-br.png"},
        {"bandnews", "brazil/band-news-br.png"},
        {"band sports", "brazil/band-sports-br.png"},
        {"bandsports", "brazil/band-sports-br.png"},
        {"band", "brazil/band-br.png"},
        {"sbt news", "brazil/sbt-news-br.png"},
        {"sbt", "brazil/sbt-br.png"},
        {"record news", "brazil/record-news-br.png"},
        {"recordnews", "brazil/record-news-br.png"},
        {"ric record", "brazil/ric-record-br.png"},
        {"record", "brazil/record-br.png"},
        {"redetv", "brazil/rede-tv-br.png"},
        {"rede tv", "brazil/rede-tv-br.png"},
        {"tv brasil", "brazil/tv-brasil-br.png"},
        {"cultura", "brazil/tv-cultura-br.png"},
        {"gazeta", "brazil/tv-gazeta-br.png"},
        {"cnt", "brazil/rede-cnt-br.png"},
        {"playtv", "brazil/play-tv-br.png"},
        {"play tv", "brazil/play-tv-br.png"},
        {"tv camara", "brazil/tv-camara-br.png"},
        {"tv senado", "brazil/tv-senado-br.png"},
        {"tv escola", "brazil/tv-escola-br.png"},
        {"canal gov", "brazil/canal-gov-br.png"},
        {"futura", "brazil/futura-br.png"},
        {"canal brasil", "brazil/canal-brasil-br.png"},
        {"canal off", "brazil/canal-off-br.png"},
        {"canal rural", "brazil/canal-rural-br.png"},
        {"terra viva", "brazil/terraviva-br.png"},
        {"terraviva", "brazil/terraviva-br.png"},
        {"agro mais", "brazil/agro-mais-br.png"},
        {"agromais", "brazil/agro-mais-br.png"},
        {"aparecida", "brazil/tv-aparecida-br.png"},
        {"cancao nova", "brazil/cancao-nova-tv-br.png"},
        {"rede vida", "brazil/rede-vida-br.png"},
        {"redevida", "brazil/rede-vida-br.png"},
        {"rede gospel", "brazil/rede-gospel-br.png"},
        {"novo tempo", "brazil/novo-tempo-br.png"},
        {"pai eterno", "brazil/tv-pai-eterno-br.png"},
        {"rit", "brazil/rit-br.png"},
        {"cnn brasil money", "brazil/cnn-brasil-money-br.png"},
        {"cnn", "brazil/cnn-brasil-br.png"},
        {"jovem pan news", "brazil/jovem-pan-news-br.png"},
        {"jovem pan", "brazil/jovem-pan-tv-br.png"},
        {"times brasil", "brazil/times-brasil-cnbc-br.png"},
        {"cnbc", "brazil/times-brasil-cnbc-br.png"},
        {"ge tv", "brazil/ge-tv-br.png"},
        {"sportv 2", "brazil/sportv2-br.png"},
        {"sportv2", "brazil/sportv2-br.png"},
        {"sportv 3", "brazil/sportv3-br.png"},
        {"sportv3", "brazil/sportv3-br.png"},
        {"sportv", "brazil/sportv-br.png"},
        {"premiere", "brazil/premiere-br.png"},
        {"espn 2", "united-states/espn-2-us.png"},
        {"espn 3", "united-states/espn-3-us.png"},
        {"espn 4", "brazil/espn-4-br.png"},
        {"espn 5", "brazil/espn-5-br.png"},
        {"espn extra", "brazil/espn-extra-br.png"},
        {"espn", "united-states/espn-us.png"},
        {"fox sports", "brazil/fox-sports-2-br.png"},
        {"woohoo", "brazil/woohoo-br.png"},
        {"fish tv", "brazil/fish-tv-br.png"},
        {"x sports", "brazil/x-sports-br.png"},
        {"dazn", "united-states/dazn-us.png"},
        {"disney", "united-states/disney-plus-us.png"},
        {"paramount+", "united-states/paramount-plus-us.png"},
        {"paramount +", "united-states/paramount-plus-us.png"},
        {"paramount", "brazil/paramount-network-br.png"},
        {"ufc fight pass", "united-states/ufc-fight-pass-us.png"},
        {"ufc", "united-states/ufc-us.png"},
        {"nba league pass", "united-states/nba-league-pass-us.png"},
        {"nba", "united-states/nba-tv-us.png"},
        {"cartoonito", "brazil/cartoonito-br.png"},
        {"cartoon network", "brazil/cartoon-network-br.png"},
        {"discovery kids", "brazil/discovery-kids-br.png"},
        {"discovery turbo", "brazil/discovery-turbo-br.png"},
        {"discovery science", "united-states/discovery-science-us.png"},
        {"discovery id", "united-states/investigation-discovery-us.png"},
        {"discovery", "united-states/discovery-channel-us.png"},
        {"animal planet", "united-states/animal-planet-us.png"},
        {"history 2", "united-states/history-channel-2-us.png"},
        {"history", "united-states/history-channel-us.png"},
        {"hgtv", "united-states/hgtv-us.png"},
        {"a&e", "brazil/a-and-e-br.png"},
        {"tooncast", "brazil/tooncast-br.png"},
        {"box kids", "brazil/box-kids-tv-br.png"},
        {"dumdum", "brazil/dumdum-br.png"},
        {"nicktoons", "united-states/nick-toons-us.png"},
        {"nickelodeon", "united-states/nickelodeon-us.png"},
        {"gloobinho", "brazil/gloobinho-br.png"},
        {"gloob", "brazil/gloob-br.png"},
        {"ra tim bum", "brazil/tv-ra-tim-bum-br.png"},
        {"zoomoo", "brazil/zoomoo-br.png"},
        {"dreamworks", "brazil/dreamworks-channel-br.png"},
        {"telecine action", "brazil/tele-cine-action-br.png"},
        {"telecine pipoca", "brazil/tele-cine-pipoca-br.png"},
        {"telecine premium", "brazil/tele-cine-premium-br.png"},
        {"telecine fun", "brazil/tele-cine-fun-br.png"},
        {"telecine cult", "brazil/tele-cine-cult-br.png"},
        {"telecine touch", "brazil/tele-cine-touch-br.png"},
        {"hbo 2", "brazil/hbo-2-br.png"},
        {"hbo2", "brazil/hbo-2-br.png"},
        {"hbo family", "brazil/hbo-family-br.png"},
        {"hbo mundi", "brazil/hbo-mundi-br.png"},
        {"hbo plus", "brazil/hbo-plus-br.png"},
        {"hbo pop", "brazil/hbo-pop-br.png"},
        {"hbo signature", "brazil/hbo-signature-br.png"},
        {"hbo xtreme", "brazil/hbo-xtreme-br.png"},
        {"hbo", "brazil/hbo-br.png"},
        {"megapix", "brazil/megapix-br.png"},
        {"sony movies", "brazil/sony-movies-br.png"},
        {"sony", "brazil/sony-channel-br.png"},
        {"warner", "brazil/warner-channel-br.png"},
        {"cinemax", "brazil/cinemax-br.png"},
        {"space", "brazil/space-br.png"},
        {"tnt series", "brazil/tnt-series-br.png"},
        {"tnt", "brazil/tnt-br.png"},
        {"studio universal", "brazil/studio-universal-br.png"},
        {"universal", "brazil/universal-tv-br.png"},
        {"usa", "brazil/usa-br.png"},
        {"star channel", "brazil/star-channel-br.png"},
        {"cinecanal", "brazil/cine-canal-br.png"},
        {"cine canal", "brazil/cine-canal-br.png"},
        {"axn", "brazil/axn-br.png"},
        {"tcm", "brazil/tcm-br.png"},
        {"amc", "united-states/amc-us.png"},
        {"lifetime", "united-states/lifetime-us.png"},
        {"adult swim", "united-states/adult-swim-us.png"},
        {"gnt", "brazil/gnt-br.png"},
        {"multishow", "brazil/multishow-br.png"},
        {"tlc", "united-states/tlc-us.png"},
        {"arte 1", "brazil/arte1-br.png"},
        {"arte1", "brazil/arte1-br.png"},
        {"food network", "united-states/food-network-us.png"},
        {"modo viagem", "brazil/modo-viagem-br.png"},
        {"sabor & arte", "brazil/sabor-and-arte-br.png"},
        {"trace", "brazil/trace-brasil-br.png"},
        {"mtv", "united-states/mtv-us.png"},
        {"bis", "brazil/bis-br.png"},
        {"prime box", "brazil/prime-box-brazil-br.png"},
    };

    const char *base_prefix = "https://raw.githubusercontent.com/tv-logo/tv-logos/main/countries/";
    for (size_t i = 0u; i < sizeof(k_channel_logos) / sizeof(k_channel_logos[0]); ++i) {
        if (strstr(norm, k_channel_logos[i].needle) != NULL) {
            size_t len = strlen(base_prefix) + strlen(k_channel_logos[i].rel_path) + 1u;
            char *url = malloc(len);
            if (url)
                snprintf(url, len, "%s%s", base_prefix, k_channel_logos[i].rel_path);
            return url;
        }
    }
    return NULL;
}

static vip_status_t resolve_remote_artwork(const vip_thumbnail_request_t *request, char **url_out,
                                           vip_error_t *error) {
    *url_out = NULL;
    if (!can_resolve_artwork(request)) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "item sem metadados para resolver capa");
        return VIP_ERR_INVALID_ARGUMENT;
    }

    if (strcmp(request->artwork_kind, "channel") == 0) {
        char *channel_logo = resolve_channel_logo_url(request->title);
        if (channel_logo) {
            *url_out = channel_logo;
            vip_error_clear(error);
            return VIP_OK;
        }
        /* Allow 24h show channels (e.g. "24H Os Simpsons") to fall through to TMDB TV lookup. */
        if (strncmp(request->title, "24H ", 4u) != 0 && strncmp(request->title, "24h ", 4u) != 0) {
            vip_error_set(error, VIP_ERR_MALFORMED, "logo de canal não encontrado");
            return VIP_ERR_MALFORMED;
        }
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
    const char *lookup_title = request->title;
    const char *lookup_kind = request->artwork_kind;
    if (strcmp(lookup_kind, "channel") == 0) {
        if (strncmp(lookup_title, "24H ", 4u) == 0 || strncmp(lookup_title, "24h ", 4u) == 0)
            lookup_title += 4u;
        lookup_kind = "tv";
    }
    json_object_object_add(item, "id", json_object_new_string("thumb"));
    json_object_object_add(item, "title", json_object_new_string(lookup_title));
    json_object_object_add(item, "kind", json_object_new_string(lookup_kind));
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

#include <sys/stat.h>
#include <time.h>

#define POLICY_BACKOFF_SECONDS 30

static bool policy_failure_backoff_active(const char *canonical_path) {
    if (!canonical_path)
        return false;
    size_t n = strlen(canonical_path) + 6u;
    char *marker = malloc(n);
    if (!marker)
        return false;
    snprintf(marker, n, "%s.fail", canonical_path);
    struct stat st;
    bool active = false;
    if (stat(marker, &st) == 0) {
        time_t now = time(NULL);
        active = now != (time_t)-1 && st.st_mtime <= now && now - st.st_mtime < POLICY_BACKOFF_SECONDS;
        if (!active)
            (void)remove(marker);
    }
    free(marker);
    return active;
}

static void policy_failure_marker_set(const char *canonical_path) {
    if (!canonical_path)
        return;
    size_t n = strlen(canonical_path) + 6u;
    char *marker = malloc(n);
    if (!marker)
        return;
    snprintf(marker, n, "%s.fail", canonical_path);
    FILE *fp = fopen(marker, "wb");
    if (fp)
        fclose(fp);
    free(marker);
}

static void policy_failure_marker_clear(const char *canonical_path) {
    if (!canonical_path)
        return;
    size_t n = strlen(canonical_path) + 6u;
    char *marker = malloc(n);
    if (!marker)
        return;
    snprintf(marker, n, "%s.fail", canonical_path);
    (void)remove(marker);
    free(marker);
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
        const vip_thumbnail_capture_context_t *ctx = userdata;
        char *canonical = (ctx && ctx->cache_dir && request->provider_id && request->channel_id)
                              ? vip_thumbnail_cache_path(ctx->cache_dir, request->provider_id,
                                                         request->channel_id, NULL)
                              : NULL;
        if (canonical) {
            struct stat stbuf;
            if (stat(canonical, &stbuf) == 0 && stbuf.st_size > 0) {
                policy_failure_marker_clear(canonical);
                if (path_out)
                    *path_out = canonical;
                else
                    free(canonical);
                vip_error_clear(error);
                return VIP_OK;
            }
            if (policy_failure_backoff_active(canonical)) {
                free(canonical);
                vip_error_set(error, VIP_ERR_CANCELLED, "resolvedor de capa em espera após falha recente");
                return VIP_ERR_CANCELLED;
            }
        }

        char *resolved_url = NULL;
        vip_error_t lookup_error = {0};
        if (resolve_remote_artwork(request, &resolved_url, &lookup_error) == VIP_OK && resolved_url) {
            vip_thumbnail_request_t resolved_request = *request;
            resolved_request.logo_url = resolved_url;
            vip_status_t resolved_status =
                __real_vip_thumbnail_capture_with_decoder(&resolved_request, path_out, error, userdata);
            free(resolved_url);
            if (resolved_status == VIP_OK && canonical && path_out && *path_out) {
                if (strcmp(*path_out, canonical) != 0) {
                    (void)remove(canonical);
                    if (rename(*path_out, canonical) == 0) {
                        free(*path_out);
                        *path_out = canonical;
                        canonical = NULL;
                    }
                }
                policy_failure_marker_clear(*path_out);
            } else if (resolved_status != VIP_OK && canonical) {
                policy_failure_marker_set(canonical);
            }
            free(canonical);
            return resolved_status;
        }
        free(resolved_url);
        if (canonical) {
            policy_failure_marker_set(canonical);
            free(canonical);
        }

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
