from pathlib import Path

PATH = Path("src/ui_x11/x11_app.c")
s = PATH.read_text()


def replace_once(old: str, new: str) -> None:
    global s
    if old not in s:
        raise SystemExit("missing pattern: " + old[:160].replace("\n", "\\n"))
    s = s.replace(old, new, 1)


def function_span(text: str, signature: str) -> tuple[int, int]:
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"missing function: {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit(f"missing opening brace: {signature}")
    depth = 0
    i = brace
    state = "code"
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if c == '"': state = "string"
            elif c == "'": state = "char"
            elif c == "/" and n == "/": state = "line"; i += 1
            elif c == "/" and n == "*": state = "block"; i += 1
            elif c == "{": depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    return start, i + 1
        elif state == "string":
            if c == "\\": i += 1
            elif c == '"': state = "code"
        elif state == "char":
            if c == "\\": i += 1
            elif c == "'": state = "code"
        elif state == "line":
            if c == "\n": state = "code"
        elif state == "block":
            if c == "*" and n == "/": state = "code"; i += 1
        i += 1
    raise SystemExit(f"unterminated function: {signature}")


def replace_function(signature: str, replacement: str) -> None:
    global s
    start, end = function_span(s, signature)
    s = s[:start] + replacement.rstrip() + s[end:]


replace_once(
'''typedef struct {
    app_t *app;
    char *server;
    char *username;
    char *password;
    char *series_id;
    char *title;
} series_job_t;''',
'''typedef struct {
    app_t *app;
    char *server;
    char *username;
    char *password;
    char *series_id;
    char *title;
    char *logo_url;
} series_job_t;''')

replace_once(
'''    vip_category_list_t episode_categories;
    vip_channel_list_t episode_channels;
    content_kind_t content_kind;
    bool series_episode_mode;''',
'''    vip_category_list_t episode_categories;
    vip_channel_list_t episode_channels;
    vip_channel_list_t season_channels;
    content_kind_t content_kind;
    bool series_episode_mode;
    bool series_season_select;''')

replace_function('static vip_channel_list_t *active_channels(app_t *a)', r'''static vip_channel_list_t *active_channels(app_t *a) {
    if (a->series_season_select) return &a->season_channels;
    if (a->series_episode_mode) return &a->episode_channels;
    return &a->catalogs[(int)a->content_kind].channels;
}''')

replace_function('static artwork_mode_t default_artwork_mode(const app_t *a)', r'''static artwork_mode_t default_artwork_mode(const app_t *a) {
    if (a->series_season_select) return ART_PORTRAIT;
    if (a->series_episode_mode) return ART_LANDSCAPE;
    if (a->content_kind == CONTENT_VOD || a->content_kind == CONTENT_SERIES) return ART_PORTRAIT;
    return ART_LANDSCAPE;
}''')

replace_function('static const char *content_plural(app_t *a)', r'''static const char *content_plural(app_t *a) {
    if (a->series_season_select) return "temporadas";
    if (a->series_episode_mode) return "episódios";
    switch (a->content_kind) {
        case CONTENT_VOD: return "filmes";
        case CONTENT_SERIES: return "séries";
        case CONTENT_LIVE:
        default: return "canais";
    }
}''')

replace_function('static const char *all_content_label(app_t *a)', r'''static const char *all_content_label(app_t *a) {
    if (a->series_season_select) return "Todas as temporadas";
    if (a->series_episode_mode) return "Todos os episódios";
    switch (a->content_kind) {
        case CONTENT_VOD: return "Todos os filmes";
        case CONTENT_SERIES: return "Todas as séries";
        case CONTENT_LIVE:
        default: return "Todos os canais";
    }
}''')

replace_function('static void switch_content(app_t *a, content_kind_t kind)', r'''static void switch_content(app_t *a, content_kind_t kind) {
    if (!a || kind < CONTENT_LIVE || kind > CONTENT_SERIES) return;
    if (atomic_load(&a->series_running) || a->series_thread_started) return;
    a->series_episode_mode = false;
    a->series_season_select = false;
    clear_details_view(a);
    a->content_kind = kind;
    a->favorites_only = false;
    a->selected_category = -1;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    a->input_focus = INPUT_SEARCH;
    free(a->favorite_flags);
    a->favorite_flags = NULL;
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}''')

replace_function('static void return_from_episode_list(app_t *a)', r'''static void return_from_episode_list(app_t *a) {
    if (!a || !a->series_episode_mode) return;
    if (!a->series_season_select) {
        a->series_season_select = true;
        a->selected_category = -1;
        a->category_scroll = 0;
        a->grid_scroll = 0;
        a->focused_filtered = 0;
        a->search[0] = '\0';
        a->input_focus = INPUT_SEARCH;
        snprintf(a->status, sizeof(a->status), "Escolha uma temporada de %s", a->series_title);
        recalc_category_counts(a);
        load_media_state(a);
        rebuild_filter(a);
        return;
    }
    a->series_episode_mode = false;
    a->series_season_select = false;
    clear_details_view(a);
    a->selected_category = -1;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    a->input_focus = INPUT_SEARCH;
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}''')

replace_function('static void *series_worker(void *userdata)', r'''static void *series_worker(void *userdata) {
    series_job_t *job = userdata;
    app_t *a = job->app;
    vip_credentials_t credentials = {0};
    vip_xtream_client_t *client = NULL;
    vip_category_list_t seasons; vip_category_list_init(&seasons);
    vip_channel_list_t episodes; vip_channel_list_init(&episodes);
    vip_channel_list_t season_cards; vip_channel_list_init(&season_cards);
    vip_error_t error = {0};

    vip_status_t st = vip_credentials_init(&credentials, job->server, job->username, job->password, &error);
    if (st == VIP_OK) st = vip_xtream_client_create(&client, &credentials, &error);
    if (st == VIP_OK) st = vip_xtream_series_episodes(client, job->series_id, &seasons, &episodes, &error);

    if (st == VIP_OK) {
        const char *fallback_provider = episodes.len > 0u ? episodes.items[0].provider_id : credentials.provider_id;
        for (size_t i = 0; i < seasons.len; ++i) {
            vip_category_t *season = &seasons.items[i];
            char season_id[512];
            snprintf(season_id, sizeof(season_id), "season:%s:%s", job->series_id,
                     season->id ? season->id : "0");
            vip_channel_t card = {
                .provider_id = season->provider_id && season->provider_id[0] ? season->provider_id : (char *)fallback_provider,
                .id = season_id,
                .category_id = season->id,
                .name = season->name ? season->name : "Temporada",
                .logo_url = job->logo_url && job->logo_url[0] ? job->logo_url : NULL,
                .stream_url = "series://season",
                .epg_channel_id = NULL,
                .position = (int)i,
            };
            st = vip_channel_list_push(&season_cards, &card, &error);
            if (st != VIP_OK) break;
        }
    }

    if (st == VIP_OK && season_cards.len > 0u) {
        pthread_mutex_lock(&a->data_mutex);
        vip_category_list_clear(&a->episode_categories);
        vip_channel_list_clear(&a->episode_channels);
        vip_channel_list_clear(&a->season_channels);
        a->episode_categories = seasons; memset(&seasons, 0, sizeof(seasons));
        a->episode_channels = episodes; memset(&episodes, 0, sizeof(episodes));
        a->season_channels = season_cards; memset(&season_cards, 0, sizeof(season_cards));
        snprintf(a->series_title, sizeof(a->series_title), "%s", job->title ? job->title : "Série");
        snprintf(a->status, sizeof(a->status), "%zu temporadas • %zu episódios",
                 a->season_channels.len, a->episode_channels.len);
        pthread_mutex_unlock(&a->data_mutex);
        atomic_store(&a->series_success, true);
    } else {
        if (st == VIP_OK) vip_error_set(&error, VIP_ERR_MALFORMED, "nenhuma temporada encontrada");
        pthread_mutex_lock(&a->data_mutex);
        snprintf(a->status, sizeof(a->status), "Falha ao carregar episódios: %s",
                 error.message[0] ? error.message : "erro desconhecido");
        pthread_mutex_unlock(&a->data_mutex);
        atomic_store(&a->series_success, false);
    }

    if (client) vip_xtream_client_destroy(client);
    vip_credentials_clear(&credentials);
    vip_category_list_clear(&seasons);
    vip_channel_list_clear(&episodes);
    vip_channel_list_clear(&season_cards);
    if (job->password) {
        volatile char *wipe = job->password;
        size_t n = strlen(job->password);
        while (n-- > 0u) *wipe++ = 0;
    }
    free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job->logo_url); free(job);
    atomic_store(&a->series_running, false);
    atomic_store(&a->series_done, true);
    return NULL;
}''')

replace_function('static void start_series_load(app_t *a, size_t channel_index)', r'''static void start_series_load(app_t *a, size_t channel_index) {
    if (!a || a->content_kind != CONTENT_SERIES || a->series_episode_mode ||
        atomic_load(&a->series_running) || a->series_thread_started ||
        channel_index >= ACTIVE_CHANNELS(a).len) return;
    vip_channel_t *series = &ACTIVE_CHANNELS(a).items[channel_index];
    if (!series->id || strncmp(series->id, "series:", 7u) != 0) return;
    snprintf(a->series_parent_id, sizeof(a->series_parent_id), "%s", series->id);
    const char *server = a->active_server_alt && a->server_alt[0] ? a->server_alt : a->server;
    series_job_t *job = calloc(1, sizeof(*job));
    if (!job) return;
    job->app = a;
    job->server = vip_strdup(server);
    job->username = vip_strdup(a->username);
    job->password = vip_strdup(a->password);
    job->series_id = vip_strdup(series->id);
    job->title = vip_strdup(series->name);
    job->logo_url = vip_strdup(series->logo_url ? series->logo_url : "");
    if (!job->server || !job->username || !job->password || !job->series_id || !job->title || !job->logo_url) {
        if (job->password) { volatile char *wipe = job->password; size_t n = strlen(job->password); while (n-- > 0u) *wipe++ = 0; }
        free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job->logo_url); free(job);
        return;
    }
    pthread_mutex_lock(&a->data_mutex);
    snprintf(a->status, sizeof(a->status), "Carregando temporadas de %s...", series->name);
    pthread_mutex_unlock(&a->data_mutex);
    atomic_store(&a->series_done, false);
    atomic_store(&a->series_success, false);
    atomic_store(&a->series_running, true);
    if (pthread_create(&a->series_thread, NULL, series_worker, job) != 0) {
        atomic_store(&a->series_running, false);
        if (job->password) { volatile char *wipe = job->password; size_t n = strlen(job->password); while (n-- > 0u) *wipe++ = 0; }
        free(job->server); free(job->username); free(job->password); free(job->series_id); free(job->title); free(job->logo_url); free(job);
        return;
    }
    a->series_thread_started = true;
}''')

helper = r'''static void select_season(app_t *a, size_t season_channel_index) {
    if (!a || !a->series_season_select || season_channel_index >= a->season_channels.len) return;
    vip_channel_t *season = &a->season_channels.items[season_channel_index];
    int season_index = season->position;
    if (season_index < 0 || (size_t)season_index >= a->episode_categories.len) return;
    a->series_season_select = false;
    a->selected_category = season_index;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    a->input_focus = INPUT_SEARCH;
    snprintf(a->status, sizeof(a->status), "%s • %s", a->series_title,
             a->episode_categories.items[season_index].name);
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}

'''
activate_sig = 'static void activate_item(app_t *a, size_t channel_index)'
activate_start, _ = function_span(s, activate_sig)
s = s[:activate_start] + helper + s[activate_start:]

replace_function(activate_sig, r'''static void activate_item(app_t *a, size_t channel_index) {
    if (a->content_kind == CONTENT_SERIES) {
        if (a->series_season_select) {
            select_season(a, channel_index);
            return;
        }
        if (!a->series_episode_mode) {
            start_series_load(a, channel_index);
            return;
        }
    }
    enter_player(a, channel_index);
}''')

replace_once(
'''        a->content_kind = CONTENT_LIVE;
        a->series_episode_mode = false;
        snprintf(a->active_profile_id''',
'''        a->content_kind = CONTENT_LIVE;
        a->series_episode_mode = false;
        a->series_season_select = false;
        snprintf(a->active_profile_id''')

replace_once(
'''            a->series_episode_mode=true;a->favorites_only=false;a->selected_category=-1;a->category_scroll=0;a->grid_scroll=0;a->focused_filtered=0;a->search[0]='\\0';''',
'''            a->series_episode_mode=true;a->series_season_select=true;a->favorites_only=false;a->selected_category=-1;a->category_scroll=0;a->grid_scroll=0;a->focused_filtered=0;a->search[0]='\\0';''')

replace_once(
'''    if (a->progress_flags && (a->content_kind == CONTENT_VOD || a->series_episode_mode)) {''',
'''    if (a->progress_flags &&
        (a->content_kind == CONTENT_VOD || (a->series_episode_mode && !a->series_season_select))) {''')

replace_once(
'''            if (a->progress_flags && (a->content_kind == CONTENT_VOD || a->series_episode_mode)) {''',
'''            if (a->progress_flags &&
                (a->content_kind == CONTENT_VOD || (a->series_episode_mode && !a->series_season_select))) {''')

replace_once(
'''    bool episode_source = a->series_episode_mode && a->episode_channels.len > 0u;
    int active_kind = (int)a->content_kind;''',
'''    vip_channel_list_t *series_source = a->series_season_select ? &a->season_channels : &a->episode_channels;
    bool episode_source = a->series_episode_mode && series_source->len > 0u;
    int active_kind = (int)a->content_kind;''')

replace_once(
'''        size_t used = prefetch_thumbnail_list(a, &a->episode_channels,
                                              &a->episode_prefetch_cursor,''',
'''        size_t used = prefetch_thumbnail_list(a, series_source,
                                              &a->episode_prefetch_cursor,''')

replace_once(
'''    vip_category_list_clear(&a->episode_categories);
    vip_channel_list_clear(&a->episode_channels);
    pthread_mutex_destroy(&a->data_mutex);''',
'''    vip_category_list_clear(&a->episode_categories);
    vip_channel_list_clear(&a->episode_channels);
    vip_channel_list_clear(&a->season_channels);
    pthread_mutex_destroy(&a->data_mutex);''')

replace_once(
'''    vip_category_list_init(&a.episode_categories);
    vip_channel_list_init(&a.episode_channels);
    vip_profile_list_init(&a.profiles);''',
'''    vip_category_list_init(&a.episode_categories);
    vip_channel_list_init(&a.episode_channels);
    vip_channel_list_init(&a.season_channels);
    vip_profile_list_init(&a.profiles);''')

PATH.write_text(s)
print("catalog stage1 structural patch applied")
