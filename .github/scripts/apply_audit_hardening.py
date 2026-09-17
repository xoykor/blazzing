from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# ---- Thumbnail scheduler: stop promptly and make cancellation meaningful. ----
p = "src/thumbnails/thumbnails.c"
s = read(p)
s = one(s,
'''#include <pthread.h>\n#include <stdbool.h>\n''',
'''#include <pthread.h>\n#include <setjmp.h>\n#include <stdbool.h>\n''', "thumbnail setjmp include")
s = one(s,
'''static bool next_request(vip_thumbnail_scheduler_t *s, vip_thumbnail_request_t *out) {\n    pthread_mutex_lock(&s->mutex);\n    for (;;) {\n        while (!s->paused && s->heap_len > 0) {\n''',
'''static bool next_request(vip_thumbnail_scheduler_t *s, vip_thumbnail_request_t *out) {\n    pthread_mutex_lock(&s->mutex);\n    for (;;) {\n        /* Shutdown must not drain a potentially huge queue of slow network\n         * jobs. Running captures are allowed to finish; pending work is\n         * discarded by destroy after workers have exited. */\n        if (s->stopping) {\n            pthread_mutex_unlock(&s->mutex);\n            return false;\n        }\n        while (!s->paused && s->heap_len > 0) {\n''', "thumbnail prompt stop")
s = one(s,
'''        if (s->stopping) {\n            pthread_mutex_unlock(&s->mutex);\n            return false;\n        }\n        pthread_cond_wait(&s->cond, &s->mutex);\n''',
'''        pthread_cond_wait(&s->cond, &s->mutex);\n''', "thumbnail duplicate stop check")

# libjpeg must never terminate the whole app on corrupt provider artwork.
needle = '''typedef struct {\n    uint8_t *data;\n    size_t width;\n    size_t height;\n    size_t stride;\n} decoded_image_t;\n\n'''
insert = '''typedef struct {\n    struct jpeg_error_mgr pub;\n    jmp_buf env;\n} thumbnail_jpeg_error_t;\n\nstatic void thumbnail_jpeg_fail(j_common_ptr cinfo) {\n    thumbnail_jpeg_error_t *err = (thumbnail_jpeg_error_t *)cinfo->err;\n    longjmp(err->env, 1);\n}\n\n'''
s = one(s, needle, needle + insert, "thumbnail jpeg error handler")

old = '''static vip_status_t decode_jpeg_memory(const uint8_t *data, size_t len, decoded_image_t *out, vip_error_t *error) {\n    struct jpeg_decompress_struct cinfo;\n    struct jpeg_error_mgr jerr;\n    cinfo.err = jpeg_std_error(&jerr);\n    jpeg_create_decompress(&cinfo);\n    jpeg_mem_src(&cinfo, data, len);\n    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {\n        jpeg_destroy_decompress(&cinfo);\n        return VIP_ERR_INVALID_FRAME;\n    }\n    cinfo.out_color_space = JCS_RGB;\n    if (!jpeg_start_decompress(&cinfo)) { jpeg_destroy_decompress(&cinfo); return VIP_ERR_INVALID_FRAME; }\n    size_t width = cinfo.output_width, height = cinfo.output_height;\n    if (!width || !height || width > 8192u || height > 8192u || width > SIZE_MAX / 3u / height) {\n        jpeg_destroy_decompress(&cinfo); return VIP_ERR_INVALID_FRAME;\n    }\n    uint8_t *rgb = malloc(width * height * 3u);\n    if (!rgb) { jpeg_destroy_decompress(&cinfo); return VIP_ERR_NOMEM; }\n    while (cinfo.output_scanline < cinfo.output_height) {\n        JSAMPROW row = rgb + (size_t)cinfo.output_scanline * width * 3u;\n        (void)jpeg_read_scanlines(&cinfo, &row, 1u);\n    }\n    (void)jpeg_finish_decompress(&cinfo);\n    jpeg_destroy_decompress(&cinfo);\n    out->data=rgb; out->width=width; out->height=height; out->stride=width*3u;\n    vip_error_clear(error);\n    return VIP_OK;\n}\n'''
new = '''static vip_status_t decode_jpeg_memory(const uint8_t *data, size_t len, decoded_image_t *out, vip_error_t *error) {\n    struct jpeg_decompress_struct cinfo;\n    memset(&cinfo, 0, sizeof(cinfo));\n    thumbnail_jpeg_error_t jerr;\n    volatile bool created = false;\n    uint8_t *volatile rgb = NULL;\n    cinfo.err = jpeg_std_error(&jerr.pub);\n    jerr.pub.error_exit = thumbnail_jpeg_fail;\n    if (setjmp(jerr.env)) {\n        free((void *)rgb);\n        if (created) jpeg_destroy_decompress(&cinfo);\n        vip_error_set(error, VIP_ERR_INVALID_FRAME, "JPEG de capa inválido ou corrompido");\n        return VIP_ERR_INVALID_FRAME;\n    }\n    jpeg_create_decompress(&cinfo);\n    created = true;\n    jpeg_mem_src(&cinfo, data, len);\n    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {\n        jpeg_destroy_decompress(&cinfo);\n        return VIP_ERR_INVALID_FRAME;\n    }\n    cinfo.out_color_space = JCS_RGB;\n    if (!jpeg_start_decompress(&cinfo)) { jpeg_destroy_decompress(&cinfo); return VIP_ERR_INVALID_FRAME; }\n    size_t width = cinfo.output_width, height = cinfo.output_height;\n    if (!width || !height || width > 8192u || height > 8192u || width > SIZE_MAX / 3u / height) {\n        jpeg_destroy_decompress(&cinfo); return VIP_ERR_INVALID_FRAME;\n    }\n    rgb = malloc(width * height * 3u);\n    if (!rgb) { jpeg_destroy_decompress(&cinfo); return VIP_ERR_NOMEM; }\n    while (cinfo.output_scanline < cinfo.output_height) {\n        JSAMPROW row = (uint8_t *)rgb + (size_t)cinfo.output_scanline * width * 3u;\n        (void)jpeg_read_scanlines(&cinfo, &row, 1u);\n    }\n    (void)jpeg_finish_decompress(&cinfo);\n    jpeg_destroy_decompress(&cinfo);\n    out->data=(uint8_t *)rgb; out->width=width; out->height=height; out->stride=width*3u;\n    vip_error_clear(error);\n    return VIP_OK;\n}\n'''
s = one(s, old, new, "safe JPEG decode")

# Harden the provider-artwork writer as well: libjpeg I/O errors become a normal error.
old = '''    struct jpeg_compress_struct cinfo; struct jpeg_error_mgr jerr;\n    cinfo.err=jpeg_std_error(&jerr); jpeg_create_compress(&cinfo); jpeg_stdio_dest(&cinfo,fp);\n    cinfo.image_width=(JDIMENSION)width; cinfo.image_height=(JDIMENSION)height;\n    cinfo.input_components=3; cinfo.in_color_space=JCS_RGB; jpeg_set_defaults(&cinfo);\n    jpeg_set_quality(&cinfo, quality < 1 ? 82 : quality > 100 ? 100 : quality, TRUE);\n    jpeg_start_compress(&cinfo, TRUE);\n    while (cinfo.next_scanline < cinfo.image_height) {\n        JSAMPROW row=(JSAMPROW)(rgb + (size_t)cinfo.next_scanline * stride);\n        jpeg_write_scanlines(&cinfo,&row,1u);\n    }\n    jpeg_finish_compress(&cinfo); jpeg_destroy_compress(&cinfo);\n    int close_rc = fclose(fp);\n'''
new = '''    struct jpeg_compress_struct cinfo; memset(&cinfo, 0, sizeof(cinfo));\n    thumbnail_jpeg_error_t jerr;\n    volatile bool created = false;\n    cinfo.err=jpeg_std_error(&jerr.pub); jerr.pub.error_exit=thumbnail_jpeg_fail;\n    if (setjmp(jerr.env)) {\n        if (created) jpeg_destroy_compress(&cinfo);\n        fclose(fp);\n        (void)remove(tmp_path);\n        free(tmp_path);\n        vip_error_set(error, VIP_ERR_IO, "falha da libjpeg ao gravar capa");\n        return VIP_ERR_IO;\n    }\n    jpeg_create_compress(&cinfo); created = true; jpeg_stdio_dest(&cinfo,fp);\n    cinfo.image_width=(JDIMENSION)width; cinfo.image_height=(JDIMENSION)height;\n    cinfo.input_components=3; cinfo.in_color_space=JCS_RGB; jpeg_set_defaults(&cinfo);\n    jpeg_set_quality(&cinfo, quality < 1 ? 82 : quality > 100 ? 100 : quality, TRUE);\n    jpeg_start_compress(&cinfo, TRUE);\n    while (cinfo.next_scanline < cinfo.image_height) {\n        JSAMPROW row=(JSAMPROW)(rgb + (size_t)cinfo.next_scanline * stride);\n        jpeg_write_scanlines(&cinfo,&row,1u);\n    }\n    jpeg_finish_compress(&cinfo); jpeg_destroy_compress(&cinfo);\n    int close_rc = fclose(fp);\n'''
s = one(s, old, new, "safe JPEG artwork write")

# Generic RGB capture writer has the same libjpeg failure mode.
old = '''    struct jpeg_compress_struct cinfo;\n    struct jpeg_error_mgr jerr;\n    cinfo.err = jpeg_std_error(&jerr);\n    jpeg_create_compress(&cinfo);\n    jpeg_stdio_dest(&cinfo, fp);\n    cinfo.image_width = (JDIMENSION)out_w; cinfo.image_height = (JDIMENSION)out_h;\n    cinfo.input_components = 3; cinfo.in_color_space = JCS_RGB;\n    jpeg_set_defaults(&cinfo);\n    jpeg_set_quality(&cinfo, quality < 1 ? 1 : quality > 100 ? 100 : quality, TRUE);\n    jpeg_start_compress(&cinfo, TRUE);\n    while (cinfo.next_scanline < cinfo.image_height) {\n        JSAMPROW row = scaled + cinfo.next_scanline * out_w * 3;\n        jpeg_write_scanlines(&cinfo, &row, 1);\n    }\n    jpeg_finish_compress(&cinfo);\n    jpeg_destroy_compress(&cinfo);\n    int close_rc = fclose(fp);\n'''
new = '''    struct jpeg_compress_struct cinfo; memset(&cinfo, 0, sizeof(cinfo));\n    thumbnail_jpeg_error_t jerr;\n    volatile bool created = false;\n    cinfo.err = jpeg_std_error(&jerr.pub);\n    jerr.pub.error_exit = thumbnail_jpeg_fail;\n    if (setjmp(jerr.env)) {\n        if (created) jpeg_destroy_compress(&cinfo);\n        fclose(fp);\n        (void)remove(tmp_path);\n        free(tmp_path); free(scaled);\n        vip_error_set(error, VIP_ERR_IO, "falha da libjpeg ao gravar thumbnail");\n        return VIP_ERR_IO;\n    }\n    jpeg_create_compress(&cinfo); created = true;\n    jpeg_stdio_dest(&cinfo, fp);\n    cinfo.image_width = (JDIMENSION)out_w; cinfo.image_height = (JDIMENSION)out_h;\n    cinfo.input_components = 3; cinfo.in_color_space = JCS_RGB;\n    jpeg_set_defaults(&cinfo);\n    jpeg_set_quality(&cinfo, quality < 1 ? 1 : quality > 100 ? 100 : quality, TRUE);\n    jpeg_start_compress(&cinfo, TRUE);\n    while (cinfo.next_scanline < cinfo.image_height) {\n        JSAMPROW row = scaled + cinfo.next_scanline * out_w * 3;\n        jpeg_write_scanlines(&cinfo, &row, 1);\n    }\n    jpeg_finish_compress(&cinfo);\n    jpeg_destroy_compress(&cinfo);\n    int close_rc = fclose(fp);\n'''
s = one(s, old, new, "safe captured JPEG write")

# Slow/dead artwork hosts should not occupy workers for the full timeout.
s = one(s,
'''    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3500L);\n    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);\n''',
'''    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3500L);\n    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);\n    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);\n    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 4L);\n''', "thumbnail low speed cutoff")
s = one(s,
'''    if (rc != CURLE_OK) {\n        vip_error_set(error, VIP_ERR_NETWORK, "download da capa: %s", curl_easy_strerror(rc));\n        return VIP_ERR_NETWORK;\n    }\n    if (buf->overflow) {\n        vip_error_set(error, VIP_ERR_NETWORK, "capa excede 12 MiB");\n        return VIP_ERR_NETWORK;\n    }\n''',
'''    if (buf->overflow) {\n        vip_error_set(error, VIP_ERR_NETWORK, "capa excede 12 MiB");\n        return VIP_ERR_NETWORK;\n    }\n    if (rc != CURLE_OK) {\n        vip_error_set(error, VIP_ERR_NETWORK, "download da capa: %s", curl_easy_strerror(rc));\n        return VIP_ERR_NETWORK;\n    }\n''', "thumbnail overflow diagnostic")
write(p, s)

# ---- App thumbnail policy: only provider/list switches cancel queued jobs. ----
p = "src/thumbnails/thumbnail_policy.c"
s = read(p)
s = one(s,
'''void __wrap_vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler) {\n    /* Scroll/filter rebuilds used to erase the entire queue. Keeping queued\n       requests alive means cache warming continues while the user navigates.\n       Requests own copies of their strings, so retaining them is safe across\n       viewport changes and even provider/profile switches. */\n    (void)scheduler;\n}\n''',
'''void __wrap_vip_thumbnail_scheduler_cancel_pending(vip_thumbnail_scheduler_t *scheduler) {\n    /* Callers use cancellation only at provider/list boundaries. Viewport and\n       search changes no longer call this function, so ordinary navigation\n       keeps cache warming while a provider switch drops stale queued jobs. */\n    __real_vip_thumbnail_scheduler_cancel_pending(scheduler);\n}\n''', "restore real thumbnail cancellation")
write(p, s)

# ---- X11 UI: active-catalog-first prefetch, safe UTF-8, cleaner focus/cancel. ----
p = "src/ui_x11/x11_app.c"
s = read(p)
old = '''static size_t utf8_to_latin1(char *dst, size_t cap, const char *src) {\n    if (!dst || cap == 0u) return 0u;\n    if (!src) src = "";\n    size_t out = 0u;\n    const unsigned char *p = (const unsigned char *)src;\n    while (*p && out + 1u < cap) {\n        uint32_t cp = 0u;\n        size_t advance = 1u;\n        if (*p < 0x80u) {\n            cp = *p;\n        } else if ((*p & 0xe0u) == 0xc0u && (p[1] & 0xc0u) == 0x80u) {\n            cp = ((uint32_t)(p[0] & 0x1fu) << 6) | (uint32_t)(p[1] & 0x3fu);\n            advance = 2u;\n        } else if ((*p & 0xf0u) == 0xe0u && (p[1] & 0xc0u) == 0x80u && (p[2] & 0xc0u) == 0x80u) {\n            cp = ((uint32_t)(p[0] & 0x0fu) << 12) | ((uint32_t)(p[1] & 0x3fu) << 6) | (uint32_t)(p[2] & 0x3fu);\n            advance = 3u;\n        } else if ((*p & 0xf8u) == 0xf0u && (p[1] & 0xc0u) == 0x80u &&\n                   (p[2] & 0xc0u) == 0x80u && (p[3] & 0xc0u) == 0x80u) {\n            cp = ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3fu) << 12) |\n                 ((uint32_t)(p[2] & 0x3fu) << 6) | (uint32_t)(p[3] & 0x3fu);\n            advance = 4u;\n        } else {\n            cp = (uint32_t)'?';\n        }\n        if (cp <= 0xffu) dst[out++] = (char)cp;\n        else if (cp == 0x2018u || cp == 0x2019u) dst[out++] = '\\'';\n        else if (cp == 0x201cu || cp == 0x201du) dst[out++] = '\"';\n        else if (cp == 0x2013u || cp == 0x2014u) dst[out++] = '-';\n        else dst[out++] = '?';\n        p += advance;\n    }\n    dst[out] = '\\0';\n    return out;\n}\n'''
new = '''static size_t utf8_to_latin1(char *dst, size_t cap, const char *src) {\n    if (!dst || cap == 0u) return 0u;\n    if (!src) src = "";\n    size_t out = 0u;\n    const unsigned char *p = (const unsigned char *)src;\n    const unsigned char *end = p + strlen(src);\n    while (p < end && out + 1u < cap) {\n        size_t remaining = (size_t)(end - p);\n        uint32_t cp = 0u;\n        size_t advance = 1u;\n        if (*p < 0x80u) {\n            cp = *p;\n        } else if (remaining >= 2u && (*p & 0xe0u) == 0xc0u && (p[1] & 0xc0u) == 0x80u) {\n            cp = ((uint32_t)(p[0] & 0x1fu) << 6) | (uint32_t)(p[1] & 0x3fu);\n            advance = 2u;\n        } else if (remaining >= 3u && (*p & 0xf0u) == 0xe0u &&\n                   (p[1] & 0xc0u) == 0x80u && (p[2] & 0xc0u) == 0x80u) {\n            cp = ((uint32_t)(p[0] & 0x0fu) << 12) | ((uint32_t)(p[1] & 0x3fu) << 6) | (uint32_t)(p[2] & 0x3fu);\n            advance = 3u;\n        } else if (remaining >= 4u && (*p & 0xf8u) == 0xf0u &&\n                   (p[1] & 0xc0u) == 0x80u && (p[2] & 0xc0u) == 0x80u && (p[3] & 0xc0u) == 0x80u) {\n            cp = ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3fu) << 12) |\n                 ((uint32_t)(p[2] & 0x3fu) << 6) | (uint32_t)(p[3] & 0x3fu);\n            advance = 4u;\n        } else {\n            cp = (uint32_t)'?';\n        }\n        if (cp <= 0xffu) dst[out++] = (char)cp;\n        else if (cp == 0x2018u || cp == 0x2019u) dst[out++] = '\\'';\n        else if (cp == 0x201cu || cp == 0x201du) dst[out++] = '\"';\n        else if (cp == 0x2013u || cp == 0x2014u) dst[out++] = '-';\n        else dst[out++] = '?';\n        p += advance;\n    }\n    dst[out] = '\\0';\n    return out;\n}\n'''
s = one(s, old, new, "bounded UTF-8 decode")

# Search/filtering and wheel navigation should not throw away background work.
s = one(s,
'''    a->grid_scroll = 0;\n    a->focused_filtered = 0;\n    if (a->thumbs) vip_thumbnail_scheduler_cancel_pending(a->thumbs);\n}\n\nstatic void recalc_category_counts''',
'''    a->grid_scroll = 0;\n    a->focused_filtered = 0;\n}\n\nstatic void recalc_category_counts''', "filter keeps thumbnail queue")
s = one(s,
'''        a->grid_scroll+=direction*(layout.mode==ART_PORTRAIT?220:180); if(a->grid_scroll<0)a->grid_scroll=0; if(a->grid_scroll>maxscroll)a->grid_scroll=maxscroll;\n        if (a->thumbs) vip_thumbnail_scheduler_cancel_pending(a->thumbs);\n''',
'''        a->grid_scroll+=direction*(layout.mode==ART_PORTRAIT?220:180); if(a->grid_scroll<0)a->grid_scroll=0; if(a->grid_scroll>maxscroll)a->grid_scroll=maxscroll;\n''', "wheel keeps thumbnail queue")

old = '''static void prefetch_thumbnail_batch(app_t *a) {\n    if (!a || !a->thumbs || a->screen == SCREEN_LOGIN || !a->active_profile_id[0]) return;\n\n    int64_t now = monotonic_ms();\n    if (now < a->thumb_prefetch_next_ms) return;\n    a->thumb_prefetch_next_ms = now + THUMB_PREFETCH_INTERVAL_MS;\n\n    bool episode_source = a->series_episode_mode && a->episode_channels.len > 0u;\n    size_t sources = episode_source ? 1u : 0u;\n    for (int k = 0; k < 3; ++k) {\n        if (a->catalogs[k].loaded && a->catalogs[k].channels.len > 0u) ++sources;\n    }\n    if (sources == 0u) return;\n\n    size_t budget = THUMB_PREFETCH_BATCH;\n    for (int k = 0; k < 3 && budget > 0u; ++k) {\n        catalog_t *catalog = &a->catalogs[k];\n        if (!catalog->loaded || catalog->channels.len == 0u) continue;\n\n        size_t quota = (budget + sources - 1u) / sources;\n        size_t used = prefetch_thumbnail_list(a, &catalog->channels,\n                                              &a->thumb_prefetch_cursor[k],\n                                              quota, THUMB_BACKGROUND_PRIORITY);\n        budget -= used;\n        --sources;\n    }\n\n    if (episode_source && budget > 0u) {\n        (void)prefetch_thumbnail_list(a, &a->episode_channels,\n                                      &a->episode_prefetch_cursor,\n                                      budget, THUMB_BACKGROUND_PRIORITY);\n    }\n}\n'''
new = '''static void prefetch_thumbnail_batch(app_t *a) {\n    if (!a || !a->thumbs || a->screen == SCREEN_LOGIN || !a->active_profile_id[0]) return;\n\n    int64_t now = monotonic_ms();\n    if (now < a->thumb_prefetch_next_ms) return;\n    a->thumb_prefetch_next_ms = now + THUMB_PREFETCH_INTERVAL_MS;\n\n    size_t budget = THUMB_PREFETCH_BATCH;\n    const size_t primary_budget = (THUMB_PREFETCH_BATCH * 3u) / 4u;\n    bool episode_source = a->series_episode_mode && a->episode_channels.len > 0u;\n    int active_kind = (int)a->content_kind;\n\n    /* Spend most of every batch on what the user can actually see. Older\n       versions split bandwidth evenly across TV/VOD/series, making the active\n       catalog look slow even while invisible catalogs were downloading. */\n    if (episode_source) {\n        size_t used = prefetch_thumbnail_list(a, &a->episode_channels,\n                                              &a->episode_prefetch_cursor,\n                                              primary_budget, THUMB_BACKGROUND_PRIORITY + 1000LL);\n        budget -= used > budget ? budget : used;\n    } else if (active_kind >= 0 && active_kind < 3 && a->catalogs[active_kind].loaded) {\n        size_t used = prefetch_thumbnail_list(a, &a->catalogs[active_kind].channels,\n                                              &a->thumb_prefetch_cursor[active_kind],\n                                              primary_budget, THUMB_BACKGROUND_PRIORITY + 1000LL);\n        budget -= used > budget ? budget : used;\n    }\n\n    size_t secondary_sources = 0u;\n    for (int k = 0; k < 3; ++k) {\n        if (!episode_source && k == active_kind) continue;\n        if (a->catalogs[k].loaded && a->catalogs[k].channels.len > 0u) ++secondary_sources;\n    }\n    for (int k = 0; k < 3 && budget > 0u && secondary_sources > 0u; ++k) {\n        if (!episode_source && k == active_kind) continue;\n        catalog_t *catalog = &a->catalogs[k];\n        if (!catalog->loaded || catalog->channels.len == 0u) continue;\n        size_t quota = (budget + secondary_sources - 1u) / secondary_sources;\n        size_t used = prefetch_thumbnail_list(a, &catalog->channels,\n                                              &a->thumb_prefetch_cursor[k],\n                                              quota, THUMB_BACKGROUND_PRIORITY);\n        budget -= used > budget ? budget : used;\n        --secondary_sources;\n    }\n}\n'''
s = one(s, old, new, "active-first thumbnail prefetch")

# Wipe duplicated credentials on allocation failure too.
s = one(s,
'''    if (!job->server || !job->server_alt || !job->username || !job->password || !job->profile_name) {\n        free(job->server); free(job->server_alt); free(job->username); free(job->password); free(job->profile_name); free(job);\n''',
'''    if (!job->server || !job->server_alt || !job->username || !job->password || !job->profile_name) {\n        if (job->password) { volatile char *wipe = job->password; size_t n = strlen(job->password); while (n-- > 0u) *wipe++ = 0; }\n        free(job->server); free(job->server_alt); free(job->username); free(job->password); free(job->profile_name); free(job);\n''', "wipe login password on allocation failure")
s = one(s,
'''    if (!job->server || !job->username || !job->password || !job->series_id || !job->title) {\n        free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job);\n''',
'''    if (!job->server || !job->username || !job->password || !job->series_id || !job->title) {\n        if (job->password) { volatile char *wipe = job->password; size_t n = strlen(job->password); while (n-- > 0u) *wipe++ = 0; }\n        free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job);\n''', "wipe series password on allocation failure")
write(p, s)

# ---- mpv IPC parser: make buffer-reset/compaction explicit for analyzers and safety. ----
p = "src/player_mpv/player_mpv.c"
s = read(p)
s = one(s,
'''    for (;;) {\n        if (*len + 1u >= VIP_MPV_IPC_BUF_CAP) *len = 0u;\n        ssize_t n = read(fd, buf + *len, VIP_MPV_IPC_BUF_CAP - *len - 1u);\n''',
'''    for (;;) {\n        if (*len + 1u >= VIP_MPV_IPC_BUF_CAP) { *len = 0u; buf[0] = '\\0'; }\n        ssize_t n = read(fd, buf + *len, VIP_MPV_IPC_BUF_CAP - *len - 1u);\n''', "mpv IPC reset")
s = one(s,
'''            if (start > 0u) {\n                memmove(buf, buf + start, *len - start);\n                *len -= start;\n                buf[*len] = '\\0';\n            }\n''',
'''            if (start > 0u && start <= *len) {\n                size_t remaining = *len - start;\n                if (remaining > 0u) memmove(buf, buf + start, remaining);\n                *len = remaining;\n                buf[remaining] = '\\0';\n            }\n''', "mpv IPC compaction")
s = one(s,
''' * A single idle mpv process is controlled through JSON IPC.  Media URLs are\n * sent over the private Unix socket instead of argv.  On X11, mpv creates its\n * own native rendering window; the monitor discovers it by _NET_WM_PID and\n * reparents it into the application's video container.\n''',
''' * A single idle mpv process is controlled through JSON IPC. Media URLs are\n * sent over the private Unix socket instead of argv. On X11, mpv is embedded\n * directly into the application's video container with --wid.\n''', "mpv direct embed comment")
s = one(s,
'''    if (player->debug)\n        debug_log(player, "diagnóstico habilitado; janela X11 nativa do mpv reparentada + overlay de entrada + IPC JSON");\n''',
'''    if (player->debug)\n        debug_log(player, "diagnóstico habilitado; mpv embutido via --wid + overlay de entrada + IPC JSON");\n''', "mpv debug embed wording")
write(p, s)

# ---- Hub: make external DRM behavior impossible to mistake for integrated login. ----
p = "src/app/hub.c"
s = read(p)
s = one(s,
'''    draw_service_card(h, 2, "Prime Video", "Conta Amazon no site oficial", "WEB  |  DRM OFICIAL");\n    draw_service_card(h, 3, "Max", "Conta Max no site oficial", "WEB  |  DRM OFICIAL");\n    draw_service_card(h, 4, "Globoplay", "Conta Globo no site oficial", "WEB  |  DRM OFICIAL");\n''',
'''    draw_service_card(h, 2, "Prime Video", "Login e sessão ficam no navegador", "ABRIR NO NAVEGADOR");\n    draw_service_card(h, 3, "Max", "Login e sessão ficam no navegador", "ABRIR NO NAVEGADOR");\n    draw_service_card(h, 4, "Globoplay", "Login e sessão ficam no navegador", "ABRIR NO NAVEGADOR");\n''', "hub external DRM wording")
write(p, s)

# ---- Regression: corrupt JPEG artwork must return an error instead of aborting. ----
p = "tests/test_thumbnails.c"
s = read(p)
s = one(s,
'''#include <stdlib.h>\n#include <string.h>\n#include <time.h>\n''',
'''#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <time.h>\n#include <unistd.h>\n''', "thumbnail test includes")
s = one(s,
'''    char *a = vip_thumbnail_cache_path("/tmp/cache", "p", "42", &error);\n    char *b = vip_thumbnail_cache_path("/tmp/cache", "p", "42", &error);\n    TEST_CHECK(a && b && strcmp(a, b) == 0);\n    free(a); free(b);\n\n''',
'''    char *a = vip_thumbnail_cache_path("/tmp/cache", "p", "42", &error);\n    char *b = vip_thumbnail_cache_path("/tmp/cache", "p", "42", &error);\n    TEST_CHECK(a && b && strcmp(a, b) == 0);\n    free(a); free(b);\n\n    char bad_logo[] = "/tmp/blazzing-bad-jpeg-XXXXXX";\n    int bad_fd = mkstemp(bad_logo);\n    TEST_CHECK(bad_fd >= 0);\n    FILE *bad = fdopen(bad_fd, "wb");\n    TEST_CHECK(bad != NULL);\n    const unsigned char broken_jpeg[16] = {0xff,0xd8,0xff,0xe0,0,16,'J','F','I','F',0,1,2,3,4,5};\n    TEST_CHECK(fwrite(broken_jpeg, 1u, sizeof(broken_jpeg), bad) == sizeof(broken_jpeg));\n    TEST_CHECK(fclose(bad) == 0);\n    vip_thumbnail_decoder_t *decoder = NULL;\n    vip_ffmpeg_decoder_config_t cfg = {.ffmpeg_path="ffmpeg", .timeout_ms=1000, .candidate_frames=1, .output_width=64, .output_height=36};\n    TEST_STATUS(vip_ffmpeg_decoder_create(&decoder, &cfg, &error), VIP_OK, &error);\n    vip_thumbnail_capture_context_t context = {0};\n    TEST_STATUS(vip_thumbnail_capture_context_init(&context, decoder, "/tmp/blazzing-thumb-test-cache", 82, &error), VIP_OK, &error);\n    vip_thumbnail_request_t bad_req = {.provider_id="p", .channel_id="broken-jpeg", .logo_url=bad_logo, .stream_url="http://unused", .priority=999999};\n    char *bad_path = NULL;\n    vip_status_t bad_status = vip_thumbnail_capture_with_decoder(&bad_req, &bad_path, &error, &context);\n    TEST_CHECK(bad_status != VIP_OK);\n    free(bad_path);\n    vip_thumbnail_capture_context_clear(&context);\n    vip_thumbnail_decoder_destroy(decoder);\n    unlink(bad_logo);\n\n''', "corrupt JPEG regression")
write(p, s)

print("final audit hardening patch applied")
