/* SPDX-License-Identifier: MIT */
/*
 * Xlib application shell and integration layer.
 *
 * This module owns screens, input, drawing, asynchronous UI jobs and the
 * coordination between providers, SQLite, thumbnails and mpv.  Heavy network
 * and image work stays off the X11 event loop to keep navigation responsive.
 */
#include "x11_app_internal.h"


static int mkdir_one(const char *path) {
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST)
        return 0;
#else
    if (mkdir(path, 0700) == 0 || errno == EEXIST)
        return 0;
#endif
    return -1;
}

/* Handle the mkdir parents operation. */
static int mkdir_parents(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            if (p > tmp && p[-1] == ':')
                continue;
            *p = '\0';
            if (mkdir_one(tmp) != 0)
                return -1;
            *p = sep;
        }
    }
    return mkdir_one(tmp);
}

/* Initialize paths. */
static void init_paths(app_t *a) {
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home || !home[0])
        home = getenv("USERPROFILE");
    if (!home || !home[0])
        home = getenv("TEMP");
#endif
    if (!home || !home[0])
        home = "/tmp";

    char cache_fallback[1024];
    char data_fallback[1024];
    const char *cache_base = getenv("XDG_CACHE_HOME");
    const char *data_base = getenv("XDG_DATA_HOME");
#ifdef _WIN32
    if (!cache_base || !cache_base[0])
        cache_base = getenv("LOCALAPPDATA");
    if (!data_base || !data_base[0])
        data_base = getenv("APPDATA");
#endif
    if (!cache_base || !cache_base[0]) {
        snprintf(cache_fallback, sizeof(cache_fallback), "%s/.cache", home);
        cache_base = cache_fallback;
    }
    if (!data_base || !data_base[0]) {
        snprintf(data_fallback, sizeof(data_fallback), "%s/.local/share", home);
        data_base = data_fallback;
    }

    int cache_n = snprintf(a->cache_dir, sizeof(a->cache_dir), "%s/visual-iptv-x11/thumbnails", cache_base);
    char data_dir[1024];
    int data_n = snprintf(data_dir, sizeof(data_dir), "%s/visual-iptv-x11", data_base);
#ifdef _WIN32
    long uid_tag = 0L;
#else
    long uid_tag = (long)getuid();
#endif
    if (cache_n < 0 || (size_t)cache_n >= sizeof(a->cache_dir))
        snprintf(a->cache_dir, sizeof(a->cache_dir), "/tmp/visual-iptv-x11-%ld/thumbnails", uid_tag);
    if (data_n < 0 || (size_t)data_n >= sizeof(data_dir))
        snprintf(data_dir, sizeof(data_dir), "/tmp/visual-iptv-x11-%ld", uid_tag);
    if (mkdir_parents(a->cache_dir) != 0)
        fprintf(stderr, "[paths] não foi possível criar cache: %s\n", a->cache_dir);
    if (mkdir_parents(data_dir) != 0)
        fprintf(stderr, "[paths] não foi possível criar dados: %s\n", data_dir);
    size_t dn = strlen(data_dir);
    if (dn + sizeof("/catalog.db") <= sizeof(a->db_path)) {
        memcpy(a->db_path, data_dir, dn);
        memcpy(a->db_path + dn, "/catalog.db", sizeof("/catalog.db"));
    } else {
        snprintf(a->db_path, sizeof(a->db_path), "/tmp/visual-iptv-catalog.db");
    }
}

static void thumbnail_ready(const vip_thumbnail_request_t *request, vip_status_t status, const char *path,
                            const vip_error_t *error, void *userdata) {
    app_t *a = userdata;
    static atomic_uint_fast64_t failure_count = 0;
    if (status != VIP_OK && status != VIP_ERR_CANCELLED) {
        uint64_t n = atomic_fetch_add(&failure_count, 1u) + 1u;
        if (n <= 20u || (n % 100u) == 0u) {
            fprintf(stderr, "[thumbs] falha #%llu item=%s status=%d: %s\n", (unsigned long long)n,
                    request && request->channel_id ? request->channel_id : "?", (int)status,
                    error && error->message[0] ? error->message : "sem detalhe");
        }
    }
    if (status == VIP_OK && path && a->db) {
        vip_error_t db_error = {0};
        vip_thumbnail_source_t src =
            request->logo_url && request->logo_url[0] ? VIP_THUMB_SERVER_LOGO : VIP_THUMB_CAPTURED_FRAME;
        int64_t now = (int64_t)time(NULL);
        (void)vip_database_set_thumbnail(a->db, request->provider_id, request->channel_id, path, src, now,
                                         now, &db_error);
    }
    atomic_store(&a->thumbs_dirty, true);
}

/* Handle the enqueue thumbnail operation. */
void enqueue_thumbnail(app_t *a, const vip_channel_t *ch, int64_t priority) {
    if (!a->thumbs || !ch || !ch->provider_id || !ch->id || !ch->stream_url)
        return;
    vip_error_t error = {0};
    char *path = ch->logo_url && ch->logo_url[0]
                     ? vip_thumbnail_artwork_cache_path(a->cache_dir, ch->provider_id, ch->id,
                                                        ch->logo_url, &error)
                     : vip_thumbnail_cache_path(a->cache_dir, ch->provider_id, ch->id, &error);
    if (path) {
        struct stat st;
        bool exists = stat(path, &st) == 0 && st.st_size > 0;
        if (!exists) {
            size_t fn = strlen(path) + 6u;
            char *fail_path = malloc(fn);
            if (fail_path) {
                snprintf(fail_path, fn, "%s.fail", path);
                struct stat fst;
                time_t now = time(NULL);
                if (stat(fail_path, &fst) == 0 && now != (time_t)-1 && fst.st_mtime <= now &&
                    now - fst.st_mtime < 30) {
                    exists = true;
                }
                free(fail_path);
            }
        }
        free(path);
        if (exists)
            return;
    }
    const char *artwork_kind = NULL;
    if (a->content_kind == CONTENT_VOD)
        artwork_kind = "movie";
    else if (a->content_kind == CONTENT_SERIES || a->series_season_select)
        artwork_kind = "tv";

    char grouped_title[256];
    const char *clean_title = m3u_series_display_name(a, ch, grouped_title, sizeof(grouped_title));

    vip_thumbnail_request_t req = {
        .provider_id = ch->provider_id,
        .channel_id = ch->id,
        .logo_url = ch->logo_url,
        .stream_url = ch->stream_url,
        .title = (char *)clean_title,
        .artwork_kind = (char *)artwork_kind,
        .priority = priority,
    };
    (void)vip_thumbnail_scheduler_enqueue(a->thumbs, &req, &error);
}

/* Count the requested state in the thumbnail worker. */
static size_t thumbnail_worker_count(void) {
    const char *override = getenv("VIPTV_THUMB_WORKERS");
    if (override && override[0]) {
        char *end = NULL;
        errno = 0;
        unsigned long parsed = strtoul(override, &end, 10);
        if (errno == 0 && end != override && *end == '\0' && parsed >= 1ul && parsed <= 64ul)
            return (size_t)parsed;
    }

#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    long online = (long)sysinfo.dwNumberOfProcessors;
#else
    long online = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    size_t workers = online > 0 ? (size_t)online : 8u;
    if (workers < 8u)
        workers = 8u;
    if (workers > 16u)
        workers = 16u;
    return workers;
}

/* List the requested state using the thumbnail subsystem. */
static size_t prefetch_thumbnail_list(app_t *a, vip_channel_list_t *channels, size_t *cursor, size_t budget,
                                      int64_t priority) {
    if (!a || !channels || !cursor || channels->len == 0u || budget == 0u)
        return 0u;
    size_t limit = budget < channels->len ? budget : channels->len;
    for (size_t i = 0; i < limit; ++i) {
        size_t index = *cursor % channels->len;
        *cursor = (index + 1u) % channels->len;
        enqueue_thumbnail(a, &channels->items[index], priority);
    }
    return limit;
}

/*
 * Mantém o cache "aquecendo" em segundo plano. Cards visíveis e o painel de
 * detalhes continuam vencendo a fila por usarem prioridades muito maiores.
 * Quando uma imagem falha, a próxima volta pelo catálogo a coloca na fila
 * novamente; portanto o carregamento não morre silenciosamente.
 */
static size_t prefetch_primary_thumbnails(app_t *a, bool episode_source, int active_kind, size_t budget) {
    vip_channel_list_t *series_source = a->series_season_select ? &a->season_channels : &a->episode_channels;
    if (episode_source)
        return prefetch_thumbnail_list(a, series_source, &a->episode_prefetch_cursor, budget,
                                       THUMB_BACKGROUND_PRIORITY + 1000LL);
    if (active_kind >= 0 && active_kind < 3 && a->catalogs[active_kind].loaded)
        return prefetch_thumbnail_list(a, &a->catalogs[active_kind].channels,
                                       &a->thumb_prefetch_cursor[active_kind], budget,
                                       THUMB_BACKGROUND_PRIORITY + 1000LL);
    return 0u;
}

static size_t count_secondary_thumbnail_sources(app_t *a, bool episode_source, int active_kind) {
    size_t count = 0u;
    for (int k = 0; k < 3; ++k) {
        if (!episode_source && k == active_kind)
            continue;
        if (a->catalogs[k].loaded && a->catalogs[k].channels.len > 0u)
            ++count;
    }
    return count;
}

static void prefetch_secondary_thumbnails(app_t *a, bool episode_source, int active_kind, size_t budget) {
    size_t remaining_sources = count_secondary_thumbnail_sources(a, episode_source, active_kind);
    for (int k = 0; k < 3 && budget > 0u && remaining_sources > 0u; ++k) {
        if (!episode_source && k == active_kind)
            continue;
        catalog_t *catalog = &a->catalogs[k];
        if (!catalog->loaded || catalog->channels.len == 0u)
            continue;
        size_t quota = (budget + remaining_sources - 1u) / remaining_sources;
        size_t used = prefetch_thumbnail_list(a, &catalog->channels, &a->thumb_prefetch_cursor[k], quota,
                                              THUMB_BACKGROUND_PRIORITY);
        budget -= used > budget ? budget : used;
        --remaining_sources;
    }
}

static void prefetch_thumbnail_batch(app_t *a) {
    if (!a || !a->thumbs || a->screen == SCREEN_LOGIN || !a->active_profile_id[0])
        return;

    int64_t now = monotonic_ms();
    if (now < a->thumb_prefetch_next_ms)
        return;
    a->thumb_prefetch_next_ms = now + THUMB_PREFETCH_INTERVAL_MS;

    vip_channel_list_t *series_source = a->series_season_select ? &a->season_channels : &a->episode_channels;
    bool episode_source = a->series_episode_mode && series_source->len > 0u;
    int active_kind = (int)a->content_kind;
    size_t budget = THUMB_PREFETCH_BATCH;
    const size_t primary_budget = (THUMB_PREFETCH_BATCH * 3u) / 4u;
    size_t used = prefetch_primary_thumbnails(a, episode_source, active_kind, primary_budget);
    budget -= used > budget ? budget : used;
    prefetch_secondary_thumbnails(a, episode_source, active_kind, budget);
}

void refresh_profiles(app_t *a) {
    vip_profile_list_clear(&a->profiles);
    vip_profile_list_init(&a->profiles);
    if (!a->db)
        return;
    vip_error_t error = {0};
    if (vip_database_list_profiles(a->db, &a->profiles, &error) != VIP_OK)
        fprintf(stderr, "[profiles] %s\n", error.message);
    if (a->profile_scroll < 0)
        a->profile_scroll = 0;
    if ((size_t)a->profile_scroll > a->profiles.len)
        a->profile_scroll = (int)a->profiles.len;
    login_profile_ensure_visible(a);
}

/* Persist active profile. */
void save_active_profile(app_t *a) {
    if (!a->db || !a->active_profile_id[0] || !a->server[0])
        return;
    char generated_name[160];
    const char *name = a->profile_name;
    if (!name[0]) {
        if (a->login_mode == LOGIN_XTREAM && a->username[0])
            snprintf(generated_name, sizeof(generated_name), "%.127s", a->username);
        else
            snprintf(generated_name, sizeof(generated_name), "Lista M3U");
        name = generated_name;
        snprintf(a->profile_name, sizeof(a->profile_name), "%.127s", name);
    }
    vip_profile_t profile = {
        .profile_id = a->active_profile_id,
        .name = (char *)name,
        .type = a->login_mode == LOGIN_M3U ? VIP_PROFILE_M3U : VIP_PROFILE_XTREAM,
        .server = a->server,
        .server_alt = a->server_alt,
        .username = a->username,
    };
    vip_error_t error = {0};
    if (vip_database_save_profile(a->db, &profile, &error) != VIP_OK)
        fprintf(stderr, "[profiles] %s\n", error.message);
    else if (a->login_mode == LOGIN_XTREAM && a->password[0] &&
             !keyring_store_password(a->active_profile_id, a->password))
        fprintf(stderr, "[profiles] senha não persistida: Secret Service/secret-tool indisponível\n");
    refresh_profiles(a);
}

/* Load profile into form. */
void load_profile_into_form(app_t *a, size_t index) {
    if (!a || index >= a->profiles.len || atomic_load(&a->login_running))
        return;
    vip_profile_t *p = &a->profiles.items[index];
    a->login_mode = p->type == VIP_PROFILE_M3U ? LOGIN_M3U : LOGIN_XTREAM;
    snprintf(a->profile_name, sizeof(a->profile_name), "%s", p->name ? p->name : "");
    snprintf(a->server, sizeof(a->server), "%s", p->server ? p->server : "");
    snprintf(a->server_alt, sizeof(a->server_alt), "%s", p->server_alt ? p->server_alt : "");
    snprintf(a->username, sizeof(a->username), "%s", p->username ? p->username : "");
    a->password[0] = '\0';
    if (a->login_mode == LOGIN_XTREAM)
        (void)keyring_lookup_password(p->profile_id, a->password, sizeof(a->password));
    a->input_focus = a->login_mode == LOGIN_XTREAM && !a->password[0] ? INPUT_PASSWORD : INPUT_SERVER;
    snprintf(a->status, sizeof(a->status),
             a->login_mode == LOGIN_XTREAM && !a->password[0] ? "Perfil carregado; informe a senha"
                                                              : "Perfil carregado; pressione Conectar");
}


static bool init_x11(app_t *a, vip_error_t *error) {
    XInitThreads();
    a->dpy = XOpenDisplay(NULL);
    if (!a->dpy) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível abrir DISPLAY X11");
        return false;
    }
    a->screen_num = DefaultScreen(a->dpy);
    a->visual = DefaultVisual(a->dpy, a->screen_num);
    a->depth = DefaultDepth(a->dpy, a->screen_num);
    a->cmap = DefaultColormap(a->dpy, a->screen_num);
    a->width = DEFAULT_W;
    a->height = DEFAULT_H;
    a->win = XCreateSimpleWindow(a->dpy, RootWindow(a->dpy, a->screen_num), 20, 20, (unsigned)a->width,
                                 (unsigned)a->height, 0, BlackPixel(a->dpy, a->screen_num),
                                 BlackPixel(a->dpy, a->screen_num));
    XStoreName(a->dpy, a->win, APP_TITLE);
    XSelectInput(a->dpy, a->win,
                 ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     EnterWindowMask | LeaveWindowMask | StructureNotifyMask | FocusChangeMask |
                     PropertyChangeMask);
    a->wm_delete = XInternAtom(a->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(a->dpy, a->win, &a->wm_delete, 1);
    a->clipboard = XInternAtom(a->dpy, "CLIPBOARD", False);
    a->utf8 = XInternAtom(a->dpy, "UTF8_STRING", False);
    a->paste_property = XInternAtom(a->dpy, "VISUAL_IPTV_PASTE", False);
    a->gc = XCreateGC(a->dpy, a->win, 0, NULL);
    XSetGraphicsExposures(a->dpy, a->gc, False);
    a->draw = a->win;
    a->font = XLoadQueryFont(a->dpy, "-misc-fixed-medium-r-normal--15-*-*-*-*-*-iso8859-1");
    if (!a->font)
        a->font = XLoadQueryFont(a->dpy, "9x15");
    if (!a->font)
        a->font = XLoadQueryFont(a->dpy, "fixed");
    a->font_title = XLoadQueryFont(a->dpy, "-*-helvetica-bold-r-normal--24-*-*-*-*-*-iso8859-1");
    a->font_heading = XLoadQueryFont(a->dpy, "-*-helvetica-bold-r-normal--18-*-*-*-*-*-iso8859-1");
    a->font_small = XLoadQueryFont(a->dpy, "-*-helvetica-medium-r-normal--13-*-*-*-*-*-iso8859-1");
    if (a->font)
        XSetFont(a->dpy, a->gc, a->font->fid);
    a->video_win = XCreateSimpleWindow(a->dpy, a->win, 0, 0, 1, 1, 0, BlackPixel(a->dpy, a->screen_num),
                                       BlackPixel(a->dpy, a->screen_num));
    XSelectInput(a->dpy, a->video_win,
                 KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask);
    XSetWindowAttributes input_attrs;
    memset(&input_attrs, 0, sizeof(input_attrs));
    input_attrs.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    a->player_input_win =
        XCreateWindow(a->dpy, a->win, 0, 0, 1, 1, 0, 0, InputOnly, CopyFromParent, CWEventMask, &input_attrs);
    if (!a->player_input_win) {
        vip_error_set(error, VIP_ERR_IO, "não foi possível criar overlay de entrada do player");
        return false;
    }
    init_palette(a);
    XMapWindow(a->dpy, a->win);
    XFlush(a->dpy);
    return true;
}

/* Handle the pulse runtime available operation. */
static bool pulse_runtime_available(void) {
#ifdef _WIN32
    return true;
#else
    const char *server = getenv("PULSE_SERVER");
    if (server && server[0])
        return true;
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !runtime[0])
        return false;
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/pulse/native", runtime);
    return n > 0 && (size_t)n < sizeof(path) && access(path, F_OK) == 0;
#endif
}

/* Initialize runtime. */
static void init_runtime(app_t *a) {
    init_paths(a);
    vip_error_t error = {0};
    if (vip_database_open(&a->db, a->db_path, &error) != VIP_OK) {
        fprintf(stderr, "[db] %s\n", error.message);
        a->db = NULL;
    } else
        refresh_profiles(a);
    vip_ffmpeg_decoder_config_t dc = {.ffmpeg_path = "ffmpeg",
                                      .timeout_ms = 10000,
                                      .candidate_frames = 3,
                                      .output_width = 320,
                                      .output_height = 180};
    if (vip_ffmpeg_decoder_create(&a->decoder, &dc, &error) == VIP_OK &&
        vip_thumbnail_capture_context_init(&a->capture_context, a->decoder, a->cache_dir, 82, &error) ==
            VIP_OK) {
        size_t thumb_workers = thumbnail_worker_count();
        fprintf(stderr, "[thumbs] %zu workers; prefetch contínuo habilitado\n", thumb_workers);
        if (vip_thumbnail_scheduler_create(&a->thumbs, thumb_workers, vip_thumbnail_capture_with_decoder,
                                           &a->capture_context, thumbnail_ready, a, &error) != VIP_OK)
            fprintf(stderr, "[thumbs] %s\n", error.message);
    } else
        fprintf(stderr, "[decoder] %s\n", error.message);
    bool audio_requested = getenv("VIPTV_NO_AUDIO") == NULL && getenv("VIPTV_TEST_NO_AUDIO") == NULL;
    bool audio = audio_requested && pulse_runtime_available();
    if (audio_requested && !audio)
        fprintf(stderr, "[audio] Pulse/PipeWire-Pulse não detectado; reprodução continuará sem áudio\n");
    vip_mpv_player_config_t pc = {
        .mpv_path = "mpv", .window_id = (unsigned long)a->video_win, .audio = audio, .startup_grace_ms = 300};
    if (vip_mpv_player_create(&a->player, &pc, &error) != VIP_OK)
        fprintf(stderr, "[player] %s\n", error.message);
}

/* Destroy app. */
static void join_app_threads(app_t *a) {
    if (a->login_thread_started)
        pthread_join(a->login_thread, NULL);
    if (a->series_thread_started)
        pthread_join(a->series_thread, NULL);
    if (a->details_thread_started)
        pthread_join(a->details_thread, NULL);
}

static void destroy_app_services(app_t *a) {
    if (a->player)
        vip_mpv_player_destroy(a->player);
    if (a->thumbs)
        vip_thumbnail_scheduler_destroy(a->thumbs);
    vip_thumbnail_capture_context_clear(&a->capture_context);
    if (a->decoder)
        vip_thumbnail_decoder_destroy(a->decoder);
    if (a->db)
        vip_database_close(a->db);
}

static void clear_app_data(app_t *a) {
    image_cache_clear(a);
    free(a->filtered);
    free(a->category_counts);
    free(a->favorite_flags);
    free(a->progress_flags);
    free(a->series_watched);
    free(a->series_total);
    vip_media_metadata_clear(&a->details_metadata);
    vip_profile_list_clear(&a->profiles);
    for (int i = 0; i < 3; ++i) {
        vip_category_list_clear(&a->catalogs[i].categories);
        vip_channel_list_clear(&a->catalogs[i].channels);
    }
    vip_category_list_clear(&a->episode_categories);
    vip_channel_list_clear(&a->episode_channels);
    vip_channel_list_clear(&a->season_channels);
}

static void destroy_x11_resources(app_t *a) {
    if (!a->dpy)
        return;
    if (a->font_title)
        XFreeFont(a->dpy, a->font_title);
    if (a->font_heading)
        XFreeFont(a->dpy, a->font_heading);
    if (a->font_small)
        XFreeFont(a->dpy, a->font_small);
    if (a->font)
        XFreeFont(a->dpy, a->font);
    if (a->backbuffer)
        XFreePixmap(a->dpy, a->backbuffer);
    if (a->gc)
        XFreeGC(a->dpy, a->gc);
    if (a->player_input_win)
        XDestroyWindow(a->dpy, a->player_input_win);
    if (a->video_win)
        XDestroyWindow(a->dpy, a->video_win);
    if (a->win)
        XDestroyWindow(a->dpy, a->win);
    XCloseDisplay(a->dpy);
}

static void destroy_app(app_t *a) {
    join_app_threads(a);
    destroy_app_services(a);
    clear_app_data(a);
    stop_phone_pairing(a);
    pthread_mutex_destroy(&a->data_mutex);
    destroy_x11_resources(a);
    curl_global_cleanup();
}


/* Initialize test env. */
static void init_test_env(app_t *a) {
    const char *server = getenv("VIPTV_TEST_SERVER"), *alt = getenv("VIPTV_TEST_ALT_SERVER"),
               *user = getenv("VIPTV_TEST_USERNAME"), *pass = getenv("VIPTV_TEST_PASSWORD");
    if (server && user && pass) {
        fprintf(stderr, "[test] autoconnect habilitado\n");
        snprintf(a->server, sizeof(a->server), "%s", server);
        if (alt)
            snprintf(a->server_alt, sizeof(a->server_alt), "%s", alt);
        snprintf(a->username, sizeof(a->username), "%s", user);
        snprintf(a->password, sizeof(a->password), "%s", pass);
        a->test_autoplay = getenv("VIPTV_TEST_AUTOPLAY") != NULL;
        a->test_series = getenv("VIPTV_TEST_SERIES") != NULL;
        const char *back_ms = getenv("VIPTV_TEST_AUTOBACK_MS");
        if (back_ms)
            a->test_autoback_delay_ms = (int)strtol(back_ms, NULL, 10);
        const char *exit_ms = getenv("VIPTV_TEST_EXIT_MS");
        if (exit_ms)
            a->test_exit_at_ms = monotonic_ms() + strtoll(exit_ms, NULL, 10);
        start_login(a, true);
    }
}

/* Run the requested state in the x11 app. */
int vip_x11_app_run(void) {
    setlocale(LC_ALL, "");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    app_t a;
    memset(&a, 0, sizeof(a));
    pthread_mutex_init(&a.data_mutex, NULL);
    for (int i = 0; i < 3; ++i) {
        vip_category_list_init(&a.catalogs[i].categories);
        vip_channel_list_init(&a.catalogs[i].channels);
    }
    vip_category_list_init(&a.episode_categories);
    vip_channel_list_init(&a.episode_channels);
    vip_channel_list_init(&a.season_channels);
    vip_profile_list_init(&a.profiles);
    atomic_init(&a.login_running, false);
    atomic_init(&a.login_done, false);
    atomic_init(&a.login_success, false);
    atomic_init(&a.pairing_submission, false);
    atomic_init(&a.series_running, false);
    atomic_init(&a.series_done, false);
    atomic_init(&a.series_success, false);
    atomic_init(&a.details_running, false);
    atomic_init(&a.details_done, false);
    atomic_init(&a.details_success, false);
    atomic_init(&a.thumbs_dirty, false);
    vip_media_metadata_init(&a.details_metadata);
    a.content_kind = CONTENT_LIVE;
    a.login_mode = LOGIN_XTREAM;
    a.screen = SCREEN_LOGIN;
    a.input_focus = INPUT_MODE;
    a.selected_category = -1;
    snprintf(a.status, sizeof(a.status), "Cole as credenciais Xtream e conecte");
    vip_error_t error = {0};
    if (!init_x11(&a, &error)) {
        fprintf(stderr, "ERRO: %s\n", error.message);
        destroy_app(&a);
        return 1;
    }
    init_runtime(&a);
    init_test_env(&a);

    int xfd = ConnectionNumber(a.dpy);
    (void)xfd;
    int64_t next_draw = 0;
    while (!a.quit) {
        while (XPending(a.dpy)) {
            XEvent e;
            XNextEvent(a.dpy, &e);
            process_event(&a, &e);
        }
        handle_async(&a);
        maybe_start_details_load(&a);
        prefetch_thumbnail_batch(&a);
        maybe_failover_player(&a);
        maybe_enforce_fullscreen(&a);
        sync_video_window(&a);
        int64_t now = monotonic_ms();
        bool ui_animating = step_browse_animations(&a, now);
        if (a.screen == SCREEN_PLAYER)
            save_current_progress(&a, false);
        if (a.screen == SCREEN_PLAYER && a.test_autoback_delay_ms > 0 && !a.test_autoback_done &&
            now - a.player_open_ms >= a.test_autoback_delay_ms) {
            leave_player(&a);
            a.test_autoback_done = true;
            fprintf(stderr, "[test] voltar do player OK\n");
        }
        if (a.test_exit_at_ms > 0 && now >= a.test_exit_at_ms)
            a.quit = true;
        if (now >= next_draw || ui_animating) {
            redraw(&a);
            next_draw = now + (a.screen == SCREEN_PLAYER ? 33 : (ui_animating ? 16 : 100));
        }
#ifdef VIPTV_USE_SDL2
        vip_sdl2_wait_events(a.dpy, 16);
#else
        if (xfd >= 0) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(xfd, &rfds);
            struct timeval tv = {.tv_sec = 0, .tv_usec = 16000};
            (void)select(xfd + 1, &rfds, NULL, NULL, &tv);
        }
#endif
    }
    destroy_app(&a);
    return 0;
}
