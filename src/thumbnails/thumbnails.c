/* SPDX-License-Identifier: MIT */
/*
 * Concurrent thumbnail pipeline.
 *
 * A priority heap feeds background workers.  Requests are deduplicated by
 * provider/item key so viewport changes can raise priority without creating
 * duplicate downloads.  JPEG/PNG/WebP artwork is decoded directly; FFmpeg is
 * only used as a fallback when a stream frame must be captured.
 */
#include "visual_iptv/thumbnails.h"

#include <errno.h>
#include <stdio.h>
#include <jpeglib.h>
#include <curl/curl.h>
#include <png.h>
#include <webp/decode.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAP_BUCKETS 4096u
#define THUMB_MAX_PENDING 512u
#define THUMB_INTERACTIVE_PRIORITY 500000LL

/* Cache files are always generated as JPEG.  A valid cache entry must at
 * least have JPEG SOI/EOI markers; visible decode failures are also removed
 * by the X11 layer and will be re-enqueued. */
static bool cached_jpeg_valid(const char *path) {
    if (!path) return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    unsigned char head[2] = {0}, tail[2] = {0};
    bool ok = fread(head, 1u, 2u, fp) == 2u;
    if (ok && fseek(fp, -2L, SEEK_END) == 0)
        ok = fread(tail, 1u, 2u, fp) == 2u;
    fclose(fp);
    return ok &&
           head[0] == 0xffu && head[1] == 0xd8u &&
           tail[0] == 0xffu && tail[1] == 0xd9u;
}

static char *temporary_cache_path(const char *path) {
    if (!path) return NULL;
    size_t n = strlen(path) + 5u;
    char *tmp = malloc(n);
    if (tmp) snprintf(tmp, n, "%s.tmp", path);
    return tmp;
}

typedef struct sched_entry {
    char *provider_id;
    char *channel_id;
    uint64_t generation;
    int64_t priority;
    bool generating;
    struct sched_entry *next;
} sched_entry_t;

typedef struct {
    vip_thumbnail_request_t request;
    uint64_t generation;
} queue_item_t;

struct vip_thumbnail_scheduler {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool stopping;
    bool paused;
    queue_item_t *heap;
    size_t heap_len;
    size_t heap_cap;
    sched_entry_t *buckets[MAP_BUCKETS];
    uint64_t next_generation;
    pthread_t *workers;
    size_t worker_count;
    vip_thumbnail_capture_fn capture;
    void *capture_userdata;
    vip_thumbnail_ready_fn ready;
    void *ready_userdata;
};

static uint64_t key_hash(const char *a, const char *b) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)a; p && *p; ++p) { h ^= *p; h *= UINT64_C(1099511628211); }
    h ^= 0xff; h *= UINT64_C(1099511628211);
    for (const unsigned char *p = (const unsigned char *)b; p && *p; ++p) { h ^= *p; h *= UINT64_C(1099511628211); }
    return h;
}

static void request_clear(vip_thumbnail_request_t *r) {
    if (!r) return;
    free(r->provider_id); free(r->channel_id); free(r->logo_url); free(r->stream_url);
    memset(r, 0, sizeof(*r));
}

static vip_status_t request_copy(vip_thumbnail_request_t *dst,
                                 const vip_thumbnail_request_t *src,
                                 vip_error_t *error) {
    memset(dst, 0, sizeof(*dst));
    dst->provider_id = vip_strdup(src->provider_id);
    dst->channel_id = vip_strdup(src->channel_id);
    dst->logo_url = vip_strdup_nullable(src->logo_url);
    dst->stream_url = vip_strdup(src->stream_url);
    dst->priority = src->priority;
    if (!dst->provider_id || !dst->channel_id || !dst->stream_url) {
        request_clear(dst);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para pedido de thumbnail");
        return VIP_ERR_NOMEM;
    }
    return VIP_OK;
}

static sched_entry_t *map_find(vip_thumbnail_scheduler_t *s, const char *provider, const char *channel) {
    size_t bucket = (size_t)(key_hash(provider, channel) % MAP_BUCKETS);
    for (sched_entry_t *e = s->buckets[bucket]; e; e = e->next)
        if (!strcmp(e->provider_id, provider) && !strcmp(e->channel_id, channel)) return e;
    return NULL;
}

static sched_entry_t *map_insert(vip_thumbnail_scheduler_t *s,
                                 const char *provider,
                                 const char *channel,
                                 vip_error_t *error) {
    size_t bucket = (size_t)(key_hash(provider, channel) % MAP_BUCKETS);
    sched_entry_t *e = calloc(1, sizeof(*e));
    if (!e) { vip_error_set(error, VIP_ERR_NOMEM, "sem memória para deduplicação de thumbnails"); return NULL; }
    e->provider_id = vip_strdup(provider);
    e->channel_id = vip_strdup(channel);
    if (!e->provider_id || !e->channel_id) {
        free(e->provider_id); free(e->channel_id); free(e);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para deduplicação de thumbnails");
        return NULL;
    }
    e->next = s->buckets[bucket];
    s->buckets[bucket] = e;
    return e;
}

static void map_remove(vip_thumbnail_scheduler_t *s, const char *provider, const char *channel) {
    size_t bucket = (size_t)(key_hash(provider, channel) % MAP_BUCKETS);
    sched_entry_t **pp = &s->buckets[bucket];
    while (*pp) {
        sched_entry_t *e = *pp;
        if (!strcmp(e->provider_id, provider) && !strcmp(e->channel_id, channel)) {
            *pp = e->next;
            free(e->provider_id); free(e->channel_id); free(e);
            return;
        }
        pp = &e->next;
    }
}

/* The heap prefers visible/nearby cards (larger priority), then uses the
 * sequence number for deterministic FIFO ordering among equal priorities. */
static bool queue_higher(const queue_item_t *a, const queue_item_t *b) {
    if (a->request.priority != b->request.priority) return a->request.priority > b->request.priority;
    return a->generation < b->generation;
}

static vip_status_t heap_push(vip_thumbnail_scheduler_t *s, queue_item_t item, vip_error_t *error) {
    if (s->heap_len == s->heap_cap) {
        size_t cap = s->heap_cap ? s->heap_cap * 2 : 128;
        queue_item_t *heap = realloc(s->heap, cap * sizeof(*heap));
        if (!heap) {
            vip_error_set(error, VIP_ERR_NOMEM, "sem memória para fila de thumbnails");
            return VIP_ERR_NOMEM;
        }
        s->heap = heap; s->heap_cap = cap;
    }
    size_t i = s->heap_len++;
    s->heap[i] = item;
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (!queue_higher(&s->heap[i], &s->heap[parent])) break;
        queue_item_t tmp = s->heap[i]; s->heap[i] = s->heap[parent]; s->heap[parent] = tmp;
        i = parent;
    }
    return VIP_OK;
}

static queue_item_t heap_pop(vip_thumbnail_scheduler_t *s) {
    queue_item_t out = s->heap[0];
    s->heap[0] = s->heap[--s->heap_len];
    size_t i = 0;
    for (;;) {
        size_t left = i * 2 + 1, right = left + 1, best = i;
        if (left < s->heap_len && queue_higher(&s->heap[left], &s->heap[best])) best = left;
        if (right < s->heap_len && queue_higher(&s->heap[right], &s->heap[best])) best = right;
        if (best == i) break;
        queue_item_t tmp = s->heap[i]; s->heap[i] = s->heap[best]; s->heap[best] = tmp;
        i = best;
    }
    return out;
}

/* Generation numbers invalidate stale heap nodes after reprioritization or
 * cancellation without requiring expensive arbitrary removal from the heap. */
static bool next_request(vip_thumbnail_scheduler_t *s, vip_thumbnail_request_t *out) {
    pthread_mutex_lock(&s->mutex);
    for (;;) {
        while (!s->paused && s->heap_len > 0) {
            queue_item_t item = heap_pop(s);
            sched_entry_t *e = map_find(s, item.request.provider_id, item.request.channel_id);
            if (!e || e->generation != item.generation || e->generating) {
                request_clear(&item.request);
                continue;
            }
            e->generating = true;
            *out = item.request;
            pthread_mutex_unlock(&s->mutex);
            return true;
        }
        if (s->stopping) {
            pthread_mutex_unlock(&s->mutex);
            return false;
        }
        pthread_cond_wait(&s->cond, &s->mutex);
    }
}

/* Workers never touch Xlib.  The ready callback only publishes completion
 * data; XImage creation remains on the UI side. */
static void *worker_main(void *userdata) {
    vip_thumbnail_scheduler_t *s = userdata;
    vip_thumbnail_request_t request;
    while (next_request(s, &request)) {
        vip_error_t error = {0};
        char *path = NULL;
        vip_status_t st = s->capture ? s->capture(&request, &path, &error, s->capture_userdata) : VIP_ERR_INVALID_ARGUMENT;
        if (s->ready) s->ready(&request, st, path, &error, s->ready_userdata);
        free(path);
        pthread_mutex_lock(&s->mutex);
        map_remove(s, request.provider_id, request.channel_id);
        pthread_mutex_unlock(&s->mutex);
        request_clear(&request);
    }
    return NULL;
}

vip_status_t vip_thumbnail_scheduler_create(vip_thumbnail_scheduler_t **out,
                                            size_t worker_count,
                                            vip_thumbnail_capture_fn capture,
                                            void *capture_userdata,
                                            vip_thumbnail_ready_fn ready,
                                            void *ready_userdata,
                                            vip_error_t *error) {
    if (!out || !capture) return VIP_ERR_INVALID_ARGUMENT;
    *out = NULL;
    vip_thumbnail_scheduler_t *s = calloc(1, sizeof(*s));
    if (!s) return VIP_ERR_NOMEM;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->worker_count = worker_count ? worker_count : 1;
    s->capture = capture; s->capture_userdata = capture_userdata; s->ready = ready; s->ready_userdata = ready_userdata;
    s->workers = calloc(s->worker_count, sizeof(*s->workers));
    if (!s->workers) {
        s->worker_count = 0;
        vip_thumbnail_scheduler_destroy(s);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para workers de thumbnail");
        return VIP_ERR_NOMEM;
    }
    size_t created = 0;
    for (; created < s->worker_count; ++created) {
        if (pthread_create(&s->workers[created], NULL, worker_main, s) != 0) break;
    }
    if (created != s->worker_count) {
        pthread_mutex_lock(&s->mutex); s->stopping = true; pthread_cond_broadcast(&s->cond); pthread_mutex_unlock(&s->mutex);
        for (size_t i = 0; i < created; ++i) pthread_join(s->workers[i], NULL);
        s->worker_count = 0;
        vip_thumbnail_scheduler_destroy(s);
        vip_error_set(error, VIP_ERR_IO, "falha ao criar worker pthread");
        return VIP_ERR_IO;
    }
    *out = s;
    return VIP_OK;
}

void vip_thumbnail_scheduler_destroy(vip_thumbnail_scheduler_t *s) {
    if (!s) return;
    pthread_mutex_lock(&s->mutex);
    s->stopping = true;
    pthread_cond_broadcast(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    for (size_t i = 0; i < s->worker_count; ++i) pthread_join(s->workers[i], NULL);
    for (size_t i = 0; i < s->heap_len; ++i) request_clear(&s->heap[i].request);
    free(s->heap); free(s->workers);
    for (size_t i = 0; i < MAP_BUCKETS; ++i) {
        sched_entry_t *e = s->buckets[i];
        while (e) { sched_entry_t *next = e->next; free(e->provider_id); free(e->channel_id); free(e); e = next; }
    }
    pthread_cond_destroy(&s->cond);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}

void vip_thumbnail_scheduler_set_paused(vip_thumbnail_scheduler_t *s, bool paused) {
    if (!s) return;
    pthread_mutex_lock(&s->mutex);
    s->paused = paused;
    pthread_cond_broadcast(&s->cond);
    pthread_mutex_unlock(&s->mutex);
}

void vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *s) {
    if (!s) return;
    pthread_mutex_lock(&s->mutex);
    for (size_t i = 0; i < s->heap_len; ++i) {
        vip_thumbnail_request_t *r = &s->heap[i].request;
        sched_entry_t *e = map_find(s, r->provider_id, r->channel_id);
        if (e && !e->generating) map_remove(s, r->provider_id, r->channel_id);
        request_clear(r);
    }
    s->heap_len = 0u;
    pthread_mutex_unlock(&s->mutex);
}

vip_status_t vip_thumbnail_scheduler_enqueue(vip_thumbnail_scheduler_t *s,
                                             const vip_thumbnail_request_t *request,
                                             vip_error_t *error) {
    if (!s || !request || !request->provider_id || !request->channel_id || !request->stream_url)
        return VIP_ERR_INVALID_ARGUMENT;
    queue_item_t item = {0};
    vip_status_t st = request_copy(&item.request, request, error);
    if (st != VIP_OK) return st;
    pthread_mutex_lock(&s->mutex);
    if (s->stopping) {
        pthread_mutex_unlock(&s->mutex); request_clear(&item.request); return VIP_ERR_CANCELLED;
    }
    /* Backpressure: background prefetch is intentionally lossy when the
       queue is already full. The UI keeps scanning continuously and will
       enqueue those items later as workers drain the queue. Interactive
       viewport/detail requests always bypass this cap. */
    if (s->heap_len >= THUMB_MAX_PENDING &&
        request->priority < THUMB_INTERACTIVE_PRIORITY) {
        pthread_mutex_unlock(&s->mutex);
        request_clear(&item.request);
        return VIP_OK;
    }
    sched_entry_t *e = map_find(s, request->provider_id, request->channel_id);
    bool inserted = false;
    if (e && e->generating) {
        pthread_mutex_unlock(&s->mutex); request_clear(&item.request); return VIP_OK;
    }
    /* Repetir a mesma prioridade (ou uma menor) não cria outro nó no heap.
       Uma prioridade maior ainda pode promover um card que acabou de entrar
       no viewport, preservando a responsividade da interface. */
    if (e && request->priority <= e->priority) {
        pthread_mutex_unlock(&s->mutex); request_clear(&item.request); return VIP_OK;
    }
    if (!e) {
        e = map_insert(s, request->provider_id, request->channel_id, error);
        if (!e) { pthread_mutex_unlock(&s->mutex); request_clear(&item.request); return VIP_ERR_NOMEM; }
        inserted = true;
    }
    uint64_t generation = ++s->next_generation;
    item.generation = generation;
    st = heap_push(s, item, error);
    if (st == VIP_OK) {
        e->generation = generation;
        e->priority = request->priority;
        pthread_cond_signal(&s->cond);
    } else {
        if (inserted) map_remove(s, request->provider_id, request->channel_id);
        request_clear(&item.request);
    }
    pthread_mutex_unlock(&s->mutex);
    return st;
}

static vip_status_t mkdir_parents(const char *path, vip_error_t *error) {
    char *tmp = vip_strdup(path);
    if (!tmp) return VIP_ERR_NOMEM;
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                vip_error_set(error, VIP_ERR_IO, "não foi possível criar %s: %s", tmp, strerror(errno));
                free(tmp); return VIP_ERR_IO;
            }
            *p = '/';
        }
    }
    free(tmp);
    return VIP_OK;
}

char *vip_thumbnail_cache_path(const char *cache_dir,
                               const char *provider_id,
                               const char *channel_id,
                               vip_error_t *error) {
    if (!cache_dir || !provider_id || !channel_id) return NULL;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char digest[32]; unsigned int len = 0;
    if (!ctx || EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, provider_id, strlen(provider_id)) != 1 ||
        EVP_DigestUpdate(ctx, "\0", 1) != 1 ||
        EVP_DigestUpdate(ctx, channel_id, strlen(channel_id)) != 1 ||
        EVP_DigestUpdate(ctx, "\0aspect-v2", 10u) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &len) != 1) {
        EVP_MD_CTX_free(ctx); vip_error_set(error, VIP_ERR_IO, "falha ao calcular chave SHA-256"); return NULL;
    }
    EVP_MD_CTX_free(ctx);
    char hex[65];
    for (unsigned i = 0; i < len; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[64] = '\0';
    size_t n = strlen(cache_dir) + 1 + 64 + 5;
    char *path = malloc(n);
    if (!path) { vip_error_set(error, VIP_ERR_NOMEM, "sem memória para caminho de thumbnail"); return NULL; }
    snprintf(path, n, "%s/%s.jpg", cache_dir, hex);
    return path;
}

vip_status_t vip_thumbnail_validate_rgb(const uint8_t *rgb,
                                        size_t width,
                                        size_t height,
                                        size_t stride,
                                        vip_error_t *error) {
    if (!rgb || width == 0 || height == 0 || stride < width * 3) return VIP_ERR_INVALID_ARGUMENT;
    size_t sx = width / 64; if (sx == 0) sx = 1;
    size_t sy = height / 36; if (sy == 0) sy = 1;
    double n = 0.0, sum = 0.0, sumsq = 0.0;
    for (size_t y = 0; y < height; y += sy) {
        const uint8_t *row = rgb + y * stride;
        for (size_t x = 0; x < width; x += sx) {
            const uint8_t *p = row + x * 3;
            double lum = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++n; sum += lum; sumsq += lum * lum;
        }
    }
    double mean = sum / n;
    double variance = sumsq / n - mean * mean;
    if (mean < 7.0 || variance < 18.0) {
        vip_error_set(error, VIP_ERR_INVALID_FRAME, "frame preto ou uniforme");
        return VIP_ERR_INVALID_FRAME;
    }
    return VIP_OK;
}

vip_status_t vip_thumbnail_save_rgb_jpeg(const uint8_t *rgb,
                                         size_t width,
                                         size_t height,
                                         size_t stride,
                                         const char *path,
                                         int quality,
                                         vip_error_t *error) {
    vip_status_t st = vip_thumbnail_validate_rgb(rgb, width, height, stride, error);
    if (st != VIP_OK) return st;
    st = mkdir_parents(path, error);
    if (st != VIP_OK) return st;
    const size_t out_w = 320, out_h = 180;
    uint8_t *scaled = malloc(out_w * out_h * 3);
    if (!scaled) return VIP_ERR_NOMEM;
    for (size_t y = 0; y < out_h; ++y) {
        size_t src_y = y * height / out_h;
        for (size_t x = 0; x < out_w; ++x) {
            size_t src_x = x * width / out_w;
            memcpy(scaled + (y * out_w + x) * 3, rgb + src_y * stride + src_x * 3, 3);
        }
    }
    char *tmp_path = temporary_cache_path(path);
    if (!tmp_path) {
        free(scaled);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para cache temporário");
        return VIP_ERR_NOMEM;
    }
    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        free(tmp_path); free(scaled);
        vip_error_set(error, VIP_ERR_IO, "não foi possível abrir cache temporário: %s", strerror(errno));
        return VIP_ERR_IO;
    }
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width = (JDIMENSION)out_w; cinfo.image_height = (JDIMENSION)out_h;
    cinfo.input_components = 3; cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality < 1 ? 1 : quality > 100 ? 100 : quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row = scaled + cinfo.next_scanline * out_w * 3;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    int close_rc = fclose(fp);
    int rename_rc = close_rc == 0 ? rename(tmp_path, path) : -1;
    if (close_rc != 0 || rename_rc != 0) {
        int saved_errno = errno;
        (void)remove(tmp_path);
        free(tmp_path); free(scaled);
        vip_error_set(error, VIP_ERR_IO, "falha ao publicar thumbnail no cache: %s",
                      strerror(saved_errno));
        return VIP_ERR_IO;
    }
    free(tmp_path); free(scaled);
    vip_error_clear(error);
    return VIP_OK;
}


typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    bool overflow;
} image_download_t;

typedef struct {
    uint8_t *data;
    size_t width;
    size_t height;
    size_t stride;
} decoded_image_t;

static void decoded_image_clear(decoded_image_t *image) {
    if (!image) return;
    free(image->data);
    memset(image, 0, sizeof(*image));
}

static size_t image_download_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
    image_download_t *buf = userdata;
    const size_t max_bytes = 12u * 1024u * 1024u;
    size_t bytes = size * nmemb;
    if (bytes > max_bytes || buf->len > max_bytes - bytes) {
        buf->overflow = true;
        return 0u;
    }
    size_t need = buf->len + bytes;
    if (need > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 32768u;
        while (cap < need) cap *= 2u;
        uint8_t *grown = realloc(buf->data, cap);
        if (!grown) return 0u;
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    return bytes;
}

static vip_status_t download_logo(const char *url, image_download_t *buf, vip_error_t *error) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        vip_error_set(error, VIP_ERR_NETWORK, "falha ao inicializar download da capa");
        return VIP_ERR_NETWORK;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Visual-IPTV/1.1");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, image_download_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        vip_error_set(error, VIP_ERR_NETWORK, "download da capa: %s", curl_easy_strerror(rc));
        return VIP_ERR_NETWORK;
    }
    if (buf->overflow) {
        vip_error_set(error, VIP_ERR_NETWORK, "capa excede 12 MiB");
        return VIP_ERR_NETWORK;
    }
    if (http >= 400) {
        vip_error_set(error, VIP_ERR_NETWORK, "servidor da capa respondeu HTTP %ld", http);
        return VIP_ERR_NETWORK;
    }
    if (buf->len == 0u) {
        vip_error_set(error, VIP_ERR_NETWORK, "servidor retornou capa vazia");
        return VIP_ERR_NETWORK;
    }
    return VIP_OK;
}

static vip_status_t read_local_logo(const char *source, image_download_t *buf, vip_error_t *error) {
    const char *path = strncmp(source, "file://", 7u) == 0 ? source + 7u : source;
    FILE *fp = fopen(path, "rb");
    if (!fp) { vip_error_set(error, VIP_ERR_IO, "não foi possível abrir capa local"); return VIP_ERR_IO; }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return VIP_ERR_IO; }
    long n = ftell(fp);
    if (n <= 0 || n > (long)(12u * 1024u * 1024u)) { fclose(fp); return VIP_ERR_INVALID_FRAME; }
    rewind(fp);
    buf->data = malloc((size_t)n);
    if (!buf->data) { fclose(fp); return VIP_ERR_NOMEM; }
    buf->len = fread(buf->data, 1u, (size_t)n, fp);
    fclose(fp);
    if (buf->len != (size_t)n) { free(buf->data); memset(buf,0,sizeof(*buf)); return VIP_ERR_IO; }
    buf->cap = buf->len;
    return VIP_OK;
}

static vip_status_t decode_jpeg_memory(const uint8_t *data, size_t len, decoded_image_t *out, vip_error_t *error) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, len);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return VIP_ERR_INVALID_FRAME;
    }
    cinfo.out_color_space = JCS_RGB;
    if (!jpeg_start_decompress(&cinfo)) { jpeg_destroy_decompress(&cinfo); return VIP_ERR_INVALID_FRAME; }
    size_t width = cinfo.output_width, height = cinfo.output_height;
    if (!width || !height || width > 8192u || height > 8192u || width > SIZE_MAX / 3u / height) {
        jpeg_destroy_decompress(&cinfo); return VIP_ERR_INVALID_FRAME;
    }
    uint8_t *rgb = malloc(width * height * 3u);
    if (!rgb) { jpeg_destroy_decompress(&cinfo); return VIP_ERR_NOMEM; }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = rgb + (size_t)cinfo.output_scanline * width * 3u;
        (void)jpeg_read_scanlines(&cinfo, &row, 1u);
    }
    (void)jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    out->data=rgb; out->width=width; out->height=height; out->stride=width*3u;
    vip_error_clear(error);
    return VIP_OK;
}

static vip_status_t decode_png_memory(const uint8_t *data, size_t len, decoded_image_t *out, vip_error_t *error) {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, len)) return VIP_ERR_INVALID_FRAME;
    image.format = PNG_FORMAT_RGB;
    if (!image.width || !image.height || image.width > 8192u || image.height > 8192u) {
        png_image_free(&image); return VIP_ERR_INVALID_FRAME;
    }
    size_t bytes = PNG_IMAGE_SIZE(image);
    uint8_t *rgb = malloc(bytes);
    if (!rgb) { png_image_free(&image); return VIP_ERR_NOMEM; }
    if (!png_image_finish_read(&image, NULL, rgb, 0, NULL)) {
        free(rgb); png_image_free(&image); return VIP_ERR_INVALID_FRAME;
    }
    out->data=rgb; out->width=image.width; out->height=image.height; out->stride=(size_t)image.width*3u;
    png_image_free(&image);
    vip_error_clear(error);
    return VIP_OK;
}

static vip_status_t decode_webp_memory(const uint8_t *data, size_t len, decoded_image_t *out, vip_error_t *error) {
    int width=0,height=0;
    if (!WebPGetInfo(data, len, &width, &height) || width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return VIP_ERR_INVALID_FRAME;
    size_t stride=(size_t)width*3u, bytes=stride*(size_t)height;
    uint8_t *rgb=malloc(bytes);
    if (!rgb) return VIP_ERR_NOMEM;
    if (!WebPDecodeRGBInto(data, len, rgb, bytes, (int)stride)) { free(rgb); return VIP_ERR_INVALID_FRAME; }
    out->data=rgb; out->width=(size_t)width; out->height=(size_t)height; out->stride=stride;
    vip_error_clear(error);
    return VIP_OK;
}

static vip_status_t decode_image_memory(const uint8_t *data, size_t len, decoded_image_t *out, vip_error_t *error) {
    if (!data || len < 12u || !out) return VIP_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (len >= 8u && png_sig_cmp((png_bytep)data, 0, 8) == 0) return decode_png_memory(data, len, out, error);
    if (data[0] == 0xffu && data[1] == 0xd8u) return decode_jpeg_memory(data, len, out, error);
    if (len >= 12u && memcmp(data, "RIFF", 4u) == 0 && memcmp(data+8u, "WEBP", 4u) == 0)
        return decode_webp_memory(data, len, out, error);
    vip_error_set(error, VIP_ERR_INVALID_FRAME, "formato de capa não suportado");
    return VIP_ERR_INVALID_FRAME;
}

static vip_status_t save_rgb_jpeg_exact(const uint8_t *rgb, size_t width, size_t height, size_t stride,
                                        const char *path, int quality, vip_error_t *error) {
    vip_status_t st = mkdir_parents(path, error);
    if (st != VIP_OK) return st;
    char *tmp_path = temporary_cache_path(path);
    if (!tmp_path) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para cache temporário");
        return VIP_ERR_NOMEM;
    }
    FILE *fp=fopen(tmp_path,"wb");
    if (!fp) {
        free(tmp_path);
        vip_error_set(error, VIP_ERR_IO, "não foi possível abrir cache temporário de capa");
        return VIP_ERR_IO;
    }
    struct jpeg_compress_struct cinfo; struct jpeg_error_mgr jerr;
    cinfo.err=jpeg_std_error(&jerr); jpeg_create_compress(&cinfo); jpeg_stdio_dest(&cinfo,fp);
    cinfo.image_width=(JDIMENSION)width; cinfo.image_height=(JDIMENSION)height;
    cinfo.input_components=3; cinfo.in_color_space=JCS_RGB; jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality < 1 ? 82 : quality > 100 ? 100 : quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row=(JSAMPROW)(rgb + (size_t)cinfo.next_scanline * stride);
        jpeg_write_scanlines(&cinfo,&row,1u);
    }
    jpeg_finish_compress(&cinfo); jpeg_destroy_compress(&cinfo);
    int close_rc = fclose(fp);
    int rename_rc = close_rc == 0 ? rename(tmp_path, path) : -1;
    if (close_rc != 0 || rename_rc != 0) {
        int saved_errno = errno;
        (void)remove(tmp_path);
        free(tmp_path);
        vip_error_set(error, VIP_ERR_IO, "falha ao publicar capa no cache: %s",
                      strerror(saved_errno));
        return VIP_ERR_IO;
    }
    free(tmp_path);
    vip_error_clear(error); return VIP_OK;
}

static vip_status_t save_logo_preserving_aspect(const decoded_image_t *image, const char *path,
                                                int quality, vip_error_t *error) {
    if (!image || !image->data || !image->width || !image->height) return VIP_ERR_INVALID_ARGUMENT;
    const size_t max_w=480u,max_h=720u;
    double scale=1.0;
    if (image->width > max_w) scale=(double)max_w/(double)image->width;
    if ((double)image->height*scale > (double)max_h) scale=(double)max_h/(double)image->height;
    size_t out_w=(size_t)((double)image->width*scale+0.5), out_h=(size_t)((double)image->height*scale+0.5);
    if (out_w < 1u) out_w = 1u;
    if (out_h < 1u) out_h = 1u;
    if (out_w == image->width && out_h == image->height)
        return save_rgb_jpeg_exact(image->data,image->width,image->height,image->stride,path,quality,error);
    if (out_w > SIZE_MAX / out_h / 3u) return VIP_ERR_NOMEM;
    uint8_t *scaled=malloc(out_w*out_h*3u);
    if (!scaled) return VIP_ERR_NOMEM;
    for (size_t y=0;y<out_h;++y) {
        size_t sy=y*image->height/out_h;
        for (size_t x=0;x<out_w;++x) {
            size_t sx=x*image->width/out_w;
            memcpy(scaled+(y*out_w+x)*3u,image->data+sy*image->stride+sx*3u,3u);
        }
    }
    vip_status_t st=save_rgb_jpeg_exact(scaled,out_w,out_h,out_w*3u,path,quality,error);
    free(scaled); return st;
}

static vip_status_t capture_logo_direct(const char *logo_url, const char *path,
                                        int quality, vip_error_t *error) {
    image_download_t raw={0};
    vip_status_t st=(strncmp(logo_url,"http://",7u)==0 || strncmp(logo_url,"https://",8u)==0)
                      ? download_logo(logo_url,&raw,error) : read_local_logo(logo_url,&raw,error);
    if (st != VIP_OK) { free(raw.data); return st; }
    decoded_image_t image={0};
    st=decode_image_memory(raw.data,raw.len,&image,error);
    free(raw.data);
    if (st == VIP_OK) st=save_logo_preserving_aspect(&image,path,quality,error);
    decoded_image_clear(&image);
    return st;
}

vip_status_t vip_thumbnail_capture_context_init(vip_thumbnail_capture_context_t *context,
                                                vip_thumbnail_decoder_t *decoder,
                                                const char *cache_dir,
                                                int jpeg_quality,
                                                vip_error_t *error) {
    if (!context || !decoder || !cache_dir || cache_dir[0] == '\0') {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "contexto de captura inválido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    memset(context, 0, sizeof(*context));
    context->decoder = decoder;
    context->cache_dir = vip_strdup(cache_dir);
    context->jpeg_quality = jpeg_quality < 1 ? 82 : (jpeg_quality > 100 ? 100 : jpeg_quality);
    if (!context->cache_dir) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para diretório de thumbnails");
        return VIP_ERR_NOMEM;
    }
    vip_error_clear(error);
    return VIP_OK;
}

void vip_thumbnail_capture_context_clear(vip_thumbnail_capture_context_t *context) {
    if (!context) return;
    free(context->cache_dir);
    memset(context, 0, sizeof(*context));
}

vip_status_t vip_thumbnail_capture_with_decoder(const vip_thumbnail_request_t *request,
                                                char **path_out,
                                                vip_error_t *error,
                                                void *userdata) {
    vip_thumbnail_capture_context_t *context = userdata;
    if (!request || !path_out || !context || !context->decoder || !context->cache_dir) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "pedido/contexto de thumbnail inválido");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    *path_out = NULL;
    char *path = vip_thumbnail_cache_path(context->cache_dir, request->provider_id,
                                          request->channel_id, error);
    if (!path) return error ? error->code : VIP_ERR_IO;

    struct stat stbuf;
    if (stat(path, &stbuf) == 0) {
        if (stbuf.st_size > 0 && cached_jpeg_valid(path)) {
            *path_out = path;
            vip_error_clear(error);
            return VIP_OK;
        }
        /* A crash/disk error may leave a partial cache file. Never let a
           non-empty but invalid file permanently suppress future downloads. */
        (void)remove(path);
    }

    vip_rgb_frame_t frame = {0};
    vip_status_t st = VIP_ERR_INVALID_FRAME;
    bool has_artwork = request->logo_url && request->logo_url[0];
    if (has_artwork) {
        /* Provider artwork is the canonical thumbnail. A transient HTTP/CDN
           failure must not fan out into many expensive FFmpeg stream opens.
           Continuous prefetch/viewport redraws will retry the artwork later. */
        st = capture_logo_direct(request->logo_url, path, context->jpeg_quality, error);
    } else {
        st = vip_thumbnail_decoder_capture(context->decoder, request->stream_url, &frame, error);
        if (st == VIP_OK) {
            st = vip_thumbnail_save_rgb_jpeg(frame.data, frame.width, frame.height, frame.stride,
                                             path, context->jpeg_quality, error);
        }
    }
    vip_rgb_frame_clear(&frame);
    if (st != VIP_OK) {
        free(path);
        return st;
    }
    *path_out = path;
    return VIP_OK;
}
