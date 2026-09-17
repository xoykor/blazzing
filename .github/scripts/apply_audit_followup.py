from pathlib import Path


def rw(path):
    return Path(path).read_text()


def put(path, text):
    Path(path).write_text(text)


def one(text, old, new, label):
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected one match, got {n}")
    return text.replace(old, new, 1)


# Database: replace N SQL queries for series progress with one bulk query.
p = "include/visual_iptv/database.h"
s = rw(p)
s = one(s,
'''vip_status_t vip_database_get_series_progress(vip_database_t *db,
                                              const char *provider_id,
                                              const char *series_id,
                                              vip_series_progress_t *out,
                                              vip_error_t *error);
void vip_series_progress_clear(vip_series_progress_t *progress);
''',
'''vip_status_t vip_database_get_series_progress(vip_database_t *db,
                                              const char *provider_id,
                                              const char *series_id,
                                              vip_series_progress_t *out,
                                              vip_error_t *error);
/** Bulk-load aggregate series progress aligned with a series catalog. */
vip_status_t vip_database_load_series_progress(vip_database_t *db,
                                               const char *provider_id,
                                               const vip_channel_list_t *series,
                                               int *watched,
                                               int *total,
                                               size_t len,
                                               vip_error_t *error);
void vip_series_progress_clear(vip_series_progress_t *progress);
''', "database header bulk series")
put(p, s)

p = "src/database/database.c"
s = rw(p)
needle = '''void vip_series_progress_clear(vip_series_progress_t *progress) {
    if (!progress) return;
    free(progress->last_episode_id);
    memset(progress, 0, sizeof(*progress));
}
'''
insert = '''vip_status_t vip_database_load_series_progress(vip_database_t *db,
                                               const char *provider_id,
                                               const vip_channel_list_t *series,
                                               int *watched,
                                               int *total,
                                               size_t len,
                                               vip_error_t *error) {
    if (!db || !provider_id || !series || !watched || !total || len < series->len)
        return VIP_ERR_INVALID_ARGUMENT;
    memset(watched, 0, len * sizeof(*watched));
    memset(total, 0, len * sizeof(*total));
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->conn,
            "SELECT series_id,watched_count,total_count FROM series_progress WHERE provider_id=?1",
            -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao carregar progresso das séries");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    int rc = SQLITE_DONE;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        if (!id) continue;
        for (size_t i = 0; i < series->len; ++i) {
            if (series->items[i].id && strcmp(series->items[i].id, id) == 0) {
                watched[i] = sqlite3_column_int(stmt, 1);
                total[i] = sqlite3_column_int(stmt, 2);
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_DONE) return db_error(db, error, "falha ao carregar progresso das séries");
    vip_error_clear(error);
    return VIP_OK;
}

'''
s = one(s, needle, insert + needle, "database bulk series implementation")
put(p, s)

p = "src/ui_x11/x11_app.c"
s = rw(p)
s = one(s,
'''    if (a->series_watched && a->series_total) {
        for (size_t i = 0; i < count; ++i) {
            vip_series_progress_t sp = {0};
            vip_error_clear(&error);
            if (vip_database_get_series_progress(a->db, ACTIVE_CHANNELS(a).items[i].provider_id,
                                                 ACTIVE_CHANNELS(a).items[i].id, &sp, &error) == VIP_OK) {
                a->series_watched[i] = sp.watched_count;
                a->series_total[i] = sp.total_count;
            }
            vip_series_progress_clear(&sp);
        }
    }
''',
'''    if (a->series_watched && a->series_total) {
        vip_error_clear(&error);
        (void)vip_database_load_series_progress(a->db, ACTIVE_CHANNELS(a).items[0].provider_id,
                                                &ACTIVE_CHANNELS(a), a->series_watched,
                                                a->series_total, count, &error);
    }
''', "UI bulk series progress")
# Block switching while a completed series worker still awaits join.
s = one(s,
'    if (atomic_load(&a->series_running)) return;\n',
'    if (atomic_load(&a->series_running) || a->series_thread_started) return;\n',
"switch content series join gate")
# Secure temporary password copies if pthread creation fails.
s = one(s,
'''    if (pthread_create(&a->login_thread, NULL, login_worker, job) != 0) {
        atomic_store(&a->login_running, false);
        free(job->server); free(job->server_alt); free(job->username); free(job->password); free(job->profile_name); free(job);
''',
'''    if (pthread_create(&a->login_thread, NULL, login_worker, job) != 0) {
        atomic_store(&a->login_running, false);
        if (job->password) { volatile char *wipe = job->password; size_t n = strlen(job->password); while (n-- > 0u) *wipe++ = 0; }
        free(job->server); free(job->server_alt); free(job->username); free(job->password); free(job->profile_name); free(job);
''', "wipe login password on pthread failure")
s = one(s,
'''    if (pthread_create(&a->series_thread, NULL, series_worker, job) != 0) {
        atomic_store(&a->series_running, false);
        free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job);
''',
'''    if (pthread_create(&a->series_thread, NULL, series_worker, job) != 0) {
        atomic_store(&a->series_running, false);
        if (job->password) { volatile char *wipe = job->password; size_t n = strlen(job->password); while (n-- > 0u) *wipe++ = 0; }
        free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job);
''', "wipe series password on pthread failure")
# Reject oversized XDG paths instead of silently using truncated, unintended paths.
s = one(s,
'''    snprintf(a->cache_dir, sizeof(a->cache_dir), "%s/visual-iptv-x11/thumbnails", cache_base);
    char data_dir[1024];
    snprintf(data_dir, sizeof(data_dir), "%s/visual-iptv-x11", data_base);
    if (mkdir_parents(a->cache_dir) != 0)
''',
'''    int cache_n = snprintf(a->cache_dir, sizeof(a->cache_dir), "%s/visual-iptv-x11/thumbnails", cache_base);
    char data_dir[1024];
    int data_n = snprintf(data_dir, sizeof(data_dir), "%s/visual-iptv-x11", data_base);
    if (cache_n < 0 || (size_t)cache_n >= sizeof(a->cache_dir))
        snprintf(a->cache_dir, sizeof(a->cache_dir), "/tmp/visual-iptv-x11-%ld/thumbnails", (long)getuid());
    if (data_n < 0 || (size_t)data_n >= sizeof(data_dir))
        snprintf(data_dir, sizeof(data_dir), "/tmp/visual-iptv-x11-%ld", (long)getuid());
    if (mkdir_parents(a->cache_dir) != 0)
''', "bounded XDG UI paths")
put(p, s)

# M3U: stable identity from tvg-id, and make side effects explicit.
p = "src/provider/m3u.c"
s = rw(p)
s = one(s,
'''    char *pending_name = NULL;
    char *pending_logo = NULL;
    char *pending_group = NULL;
''',
'''    char *pending_name = NULL;
    char *pending_logo = NULL;
    char *pending_group = NULL;
    char *pending_tvg_id = NULL;
''', "M3U pending tvg id")
s = one(s,
'''            free(pending_name); free(pending_logo); free(pending_group);
            pending_name = extinf_name(line);
            pending_logo = attr_dup(line, "tvg-logo");
            pending_group = attr_dup(line, "group-title");
''',
'''            free(pending_name); free(pending_logo); free(pending_group); free(pending_tvg_id);
            pending_name = extinf_name(line);
            pending_logo = attr_dup(line, "tvg-logo");
            pending_group = attr_dup(line, "group-title");
            pending_tvg_id = attr_dup(line, "tvg-id");
''', "M3U parse tvg id")
s = one(s,
'''        char id_hash[17]; stable_id(id_hash, stream_url);
        char channel_id[32]; snprintf(channel_id, sizeof(channel_id), "m3u:%s", id_hash);
''',
'''        char id_hash[17];
        const char *identity = pending_tvg_id && pending_tvg_id[0] ? pending_tvg_id : stream_url;
        stable_id(id_hash, identity);
        char channel_id[32]; snprintf(channel_id, sizeof(channel_id), "m3u:%s", id_hash);
''', "M3U stable tvg id")
s = one(s,
'            .position = position++,\n',
'            .position = position,\n',
"M3U explicit position")
s = one(s,
'''        st = vip_channel_list_push(channels_out, &item, error);
        free(stream_url);
        free(pending_name); pending_name = NULL;
        free(pending_logo); pending_logo = NULL;
        free(pending_group); pending_group = NULL;
''',
'''        st = vip_channel_list_push(channels_out, &item, error);
        if (st == VIP_OK) ++position;
        free(stream_url);
        free(pending_name); pending_name = NULL;
        free(pending_logo); pending_logo = NULL;
        free(pending_group); pending_group = NULL;
        free(pending_tvg_id); pending_tvg_id = NULL;
''', "M3U cleanup and position")
s = one(s,
'    free(pending_name); free(pending_logo); free(pending_group);\n',
'    free(pending_name); free(pending_logo); free(pending_group); free(pending_tvg_id);\n',
"M3U final tvg cleanup")
put(p, s)

# Pluto cache path truncation handling.
p = "src/app/pluto_app.c"
s = rw(p)
s = one(s,
'''    snprintf(a->cache_dir, sizeof(a->cache_dir), "%s/visual-iptv-x11/thumbnails", base);
    if (mkdir_parents(a->cache_dir) != 0)
''',
'''    int n = snprintf(a->cache_dir, sizeof(a->cache_dir), "%s/visual-iptv-x11/thumbnails", base);
    if (n < 0 || (size_t)n >= sizeof(a->cache_dir))
        snprintf(a->cache_dir, sizeof(a->cache_dir), "/tmp/visual-iptv-x11-%ld/thumbnails", (long)getuid());
    if (mkdir_parents(a->cache_dir) != 0)
''', "bounded Pluto XDG path")
put(p, s)

# Remove dead hub helper detected by GCC analyzer.
p = "src/app/hub.c"
s = rw(p)
s = one(s,
'''static void hub_center(hub_window_t *h, int x, int y, int w, const char *text, unsigned long color) {
    int tw = hub_text_width(h, text);
    hub_text(h, x + (w - tw) / 2, y, text, color);
}

''', '', "remove unused hub_center")
put(p, s)

# Tests for bulk series progress and stable M3U identity.
p = "tests/test_database.c"
s = rw(p)
s = one(s,
'''    TEST_CHECK(series_progress.last_episode_id && strcmp(series_progress.last_episode_id, "episode:2") == 0);
    vip_series_progress_clear(&series_progress);

''',
'''    TEST_CHECK(series_progress.last_episode_id && strcmp(series_progress.last_episode_id, "episode:2") == 0);
    vip_series_progress_clear(&series_progress);
    vip_channel_list_t series_catalog; vip_channel_list_init(&series_catalog);
    vip_channel_t series_item = {.provider_id="p", .id="series:7", .name="Série", .stream_url="series://7", .position=0};
    TEST_STATUS(vip_channel_list_push(&series_catalog, &series_item, &error), VIP_OK, &error);
    int watched[1] = {0}, total[1] = {0};
    TEST_STATUS(vip_database_load_series_progress(db, "p", &series_catalog, watched, total, 1, &error), VIP_OK, &error);
    TEST_CHECK(watched[0] == 2);
    TEST_CHECK(total[0] == 10);
    vip_channel_list_clear(&series_catalog);

''', "test bulk series progress")
put(p, s)

p = "tests/test_m3u.c"
s = rw(p)
s = one(s,
'          "#EXTINF:-1 tvg-logo=\\"https://img/a.jpg\\" group-title=\\"Notícias\\",Canal A, HD\\n"\n',
'          "#EXTINF:-1 tvg-id=\\"canal-a\\" tvg-logo=\\"https://img/a.jpg\\" group-title=\\"Notícias\\",Canal A, HD\\n"\n',
"M3U tvg-id fixture")
s = one(s,
'''    TEST_CHECK(strcmp(channels.items[0].logo_url, "https://img/a.jpg") == 0);
    TEST_CHECK(strcmp(channels.items[1].stream_url, "https://stream/b.ts") == 0);

''',
'''    TEST_CHECK(strcmp(channels.items[0].logo_url, "https://img/a.jpg") == 0);
    TEST_CHECK(strcmp(channels.items[1].stream_url, "https://stream/b.ts") == 0);
    char first_id[32]; snprintf(first_id, sizeof(first_id), "%s", channels.items[0].id);

    vip_channel_list_clear(&channels);
    vip_category_list_clear(&cats);
    fp = fopen(path, "w");
    TEST_CHECK(fp != NULL);
    fputs("#EXTM3U\\n"
          "#EXTINF:-1 tvg-id=\\"canal-a\\" group-title=\\"Notícias\\",Canal A, HD\\n"
          "https://other-host/new-token/a.m3u8\\n", fp);
    TEST_CHECK(fclose(fp) == 0);
    vip_category_list_init(&cats); vip_channel_list_init(&channels);
    TEST_STATUS(vip_m3u_load(path, &cats, &channels, provider_id, &error), VIP_OK, &error);
    TEST_CHECK(channels.len == 1);
    TEST_CHECK(strcmp(channels.items[0].id, first_id) == 0);

''', "M3U stable id regression")
put(p, s)

print("follow-up audit patch applied")
