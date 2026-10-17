/* SPDX-License-Identifier: MIT */
/*
 * X11 Workers: background threading, catalog loading, Xtream & M3U parsing,
 * media metadata fetch, phone pairing, and async state publishing.
 */
#include "x11_app_internal.h"

void cancel_browse_async_workers(app_t *a) {
    if (!a)
        return;
    if (a->thumbs)
        vip_thumbnail_scheduler_cancel_pending(a->thumbs);
    if (a->series_thread_started) {
        pthread_join(a->series_thread, NULL);
        a->series_thread_started = false;
        atomic_store(&a->series_done, false);
    }
    if (a->details_thread_started) {
        pthread_join(a->details_thread, NULL);
        a->details_thread_started = false;
        atomic_store(&a->details_done, false);
    }
}

/* Handle the login one server operation. */
static void load_optional_vod_catalog(vip_xtream_client_t *client, catalog_t *vod) {
    if (!vod)
        return;
    vip_error_t error = {0};
    vip_category_list_init(&vod->categories);
    vip_channel_list_init(&vod->channels);
    vip_status_t st = vip_xtream_vod_categories(client, &vod->categories, &error);
    if (st == VIP_OK)
        st = vip_xtream_vod_streams(client, &vod->channels, &error);
    if (st == VIP_OK) {
        vod->loaded = true;
        return;
    }
    fprintf(stderr, "[catalog] filmes indisponíveis: %s\n", error.message);
    vip_category_list_clear(&vod->categories);
    vip_channel_list_clear(&vod->channels);
    vod->loaded = false;
}

static void load_optional_series_catalog(vip_xtream_client_t *client, catalog_t *series) {
    if (!series)
        return;
    vip_error_t error = {0};
    vip_category_list_init(&series->categories);
    vip_channel_list_init(&series->channels);
    vip_status_t st = vip_xtream_series_categories(client, &series->categories, &error);
    if (st == VIP_OK)
        st = vip_xtream_series(client, &series->channels, &error);
    if (st == VIP_OK) {
        series->loaded = true;
        return;
    }
    fprintf(stderr, "[catalog] séries indisponíveis: %s\n", error.message);
    vip_category_list_clear(&series->categories);
    vip_channel_list_clear(&series->channels);
    series->loaded = false;
}

static vip_status_t login_one_server(const char *server, const char *username, const char *password,
                                     vip_credentials_t *credentials, vip_category_list_t *cats,
                                     vip_channel_list_t *channels, catalog_t *vod, catalog_t *series,
                                     vip_error_t *error) {
    vip_xtream_client_t *client = NULL;
    vip_status_t st = vip_credentials_init(credentials, server, username, password, error);
    if (st == VIP_OK)
        st = vip_xtream_client_create(&client, credentials, error);
    if (st == VIP_OK)
        st = vip_xtream_authenticate(client, error);
    if (st == VIP_OK)
        st = vip_xtream_live_categories(client, cats, error);
    if (st == VIP_OK)
        st = vip_xtream_live_streams(client, channels, error);

    if (st == VIP_OK) {
        load_optional_vod_catalog(client, vod);
        load_optional_series_catalog(client, series);
    }
    if (client)
        vip_xtream_client_destroy(client);
    return st;
}

/* Handle the remap catalog provider id operation. */
static void remap_catalog_provider_id(vip_category_list_t *cats, vip_channel_list_t *channels,
                                      const char provider_id[17]) {
    if (!provider_id || !provider_id[0])
        return;
    for (size_t i = 0; i < cats->len; ++i) {
        if (cats->items[i].provider_id) {
            free(cats->items[i].provider_id);
            cats->items[i].provider_id = vip_strdup(provider_id);
        }
    }
    for (size_t i = 0; i < channels->len; ++i) {
        if (channels->items[i].provider_id) {
            free(channels->items[i].provider_id);
            channels->items[i].provider_id = vip_strdup(provider_id);
        }
    }
}

/* Network authentication/catalog fetch runs off-thread; Xlib must not be
 * called from this worker.  Results are transferred back through app state. */
static void *login_worker(void *userdata) {
    login_job_t *job = userdata;
    app_t *a = job->app;
    vip_error_t error = {0};
    vip_credentials_t credentials = {0};
    vip_category_list_t cats;
    vip_category_list_init(&cats);
    vip_channel_list_t channels;
    vip_channel_list_init(&channels);
    catalog_t vod = {0};
    catalog_t series = {0};
    bool success = false;
    bool used_alternate = false;
    bool saw_http_404 = false;
    char *resolved_primary = NULL;
    char *resolved_alternate = NULL;
    char provider_id[17] = {0};
    vip_status_t st = VIP_ERR_INVALID_ARGUMENT;

    if (job->mode == LOGIN_M3U) {
        fprintf(stderr, "[login] carregando playlist M3U\n");
        st = vip_m3u_load(job->server, &cats, &channels, provider_id, &error);
        if (st == VIP_OK) {
            vip_category_list_t live_cats;
            vip_category_list_init(&live_cats);
            vip_channel_list_t live_channels;
            vip_channel_list_init(&live_channels);
            vip_category_list_init(&vod.categories);
            vip_channel_list_init(&vod.channels);
            vip_category_list_init(&series.categories);
            vip_channel_list_init(&series.channels);
            st = vip_m3u_split_catalog(&cats, &channels, &live_cats, &live_channels, &vod.categories,
                                       &vod.channels, &series.categories, &series.channels, &error);
            if (st == VIP_OK) {
                vip_category_list_clear(&cats);
                vip_channel_list_clear(&channels);
                cats = live_cats;
                memset(&live_cats, 0, sizeof(live_cats));
                channels = live_channels;
                memset(&live_channels, 0, sizeof(live_channels));
                vod.loaded = vod.channels.len > 0u;
                series.loaded = series.channels.len > 0u;
                fprintf(stderr, "[catalog] M3U separado: TV=%zu Filmes=%zu Séries=%zu\n", channels.len,
                        vod.channels.len, series.channels.len);
            }
            vip_category_list_clear(&live_cats);
            vip_channel_list_clear(&live_channels);
        }
    } else {
        fprintf(stderr, "[login] conectando ao servidor primário\n");
        st = login_one_server(job->server, job->username, job->password, &credentials, &cats, &channels, &vod,
                              &series, &error);
        saw_http_404 = st != VIP_OK && strstr(error.message, "HTTP 404") != NULL;
        if (st != VIP_OK && job->server_alt && job->server_alt[0]) {
            fprintf(stderr, "[login] primário falhou; tentando servidor alternativo\n");
            pthread_mutex_lock(&a->data_mutex);
            snprintf(a->status, sizeof(a->status), "Primário indisponível; tentando servidor alternativo...");
            pthread_mutex_unlock(&a->data_mutex);
            vip_category_list_clear(&cats);
            vip_category_list_init(&cats);
            vip_channel_list_clear(&channels);
            vip_channel_list_init(&channels);
            vip_category_list_clear(&vod.categories);
            vip_channel_list_clear(&vod.channels);
            memset(&vod, 0, sizeof(vod));
            vip_category_list_clear(&series.categories);
            vip_channel_list_clear(&series.channels);
            memset(&series, 0, sizeof(series));
            vip_credentials_clear(&credentials);
            vip_error_clear(&error);
            st = login_one_server(job->server_alt, job->username, job->password, &credentials, &cats,
                                  &channels, &vod, &series, &error);
            if (st != VIP_OK && strstr(error.message, "HTTP 404") != NULL)
                saw_http_404 = true;
            used_alternate = st == VIP_OK;
            if (used_alternate) {
                vip_credentials_t identity = {0};
                vip_error_t identity_error = {0};
                if (vip_credentials_init(&identity, job->server, job->username, job->password,
                                         &identity_error) == VIP_OK) {
                    snprintf(credentials.provider_id, sizeof(credentials.provider_id), "%s",
                             identity.provider_id);
                    remap_catalog_provider_id(&cats, &channels, identity.provider_id);
                    if (vod.loaded)
                        remap_catalog_provider_id(&vod.categories, &vod.channels, identity.provider_id);
                    if (series.loaded)
                        remap_catalog_provider_id(&series.categories, &series.channels, identity.provider_id);
                }
                vip_credentials_clear(&identity);
            }
        }

        /* StreamFire/Spark-compatible fallback. Only an HTTP 404 from the
         * regular Xtream endpoint activates it. Every candidate returned by
         * the resolver API is verified through player_api.php before use. */
        const char *resolver_opt = getenv("VIPTV_ALLOW_EXTERNAL_RESOLVER");
        bool allow_external_resolver = resolver_opt && resolver_opt[0] && strcmp(resolver_opt, "0") != 0;
        if (st != VIP_OK && saw_http_404 && allow_external_resolver) {
            fprintf(stderr,
                    "[login] endpoint Xtream retornou 404; resolvedor externo autorizado pelo usuário\n");
            pthread_mutex_lock(&a->data_mutex);
            snprintf(a->status, sizeof(a->status),
                     "Servidor retornou 404; procurando endpoint automaticamente...");
            pthread_mutex_unlock(&a->data_mutex);

            vip_server_resolution_t resolution = {0};
            vip_error_t resolve_error = {0};
            if (vip_streamfire_resolve_servers(job->username, job->password, &resolution, &resolve_error) ==
                VIP_OK) {
                resolved_primary = vip_strdup(resolution.primary);
                resolved_alternate = vip_strdup_nullable(resolution.alternate);
                if (!resolved_primary) {
                    vip_error_set(&error, VIP_ERR_NOMEM, "sem memória para servidor resolvido");
                } else {
                    vip_category_list_clear(&cats);
                    vip_category_list_init(&cats);
                    vip_channel_list_clear(&channels);
                    vip_channel_list_init(&channels);
                    vip_category_list_clear(&vod.categories);
                    vip_channel_list_clear(&vod.channels);
                    memset(&vod, 0, sizeof(vod));
                    vip_category_list_clear(&series.categories);
                    vip_channel_list_clear(&series.channels);
                    memset(&series, 0, sizeof(series));
                    vip_credentials_clear(&credentials);
                    vip_error_clear(&error);
                    used_alternate = false;
                    st = login_one_server(resolved_primary, job->username, job->password, &credentials, &cats,
                                          &channels, &vod, &series, &error);
                    if (st != VIP_OK && resolved_alternate && resolved_alternate[0]) {
                        vip_category_list_clear(&cats);
                        vip_category_list_init(&cats);
                        vip_channel_list_clear(&channels);
                        vip_channel_list_init(&channels);
                        vip_category_list_clear(&vod.categories);
                        vip_channel_list_clear(&vod.channels);
                        memset(&vod, 0, sizeof(vod));
                        vip_category_list_clear(&series.categories);
                        vip_channel_list_clear(&series.channels);
                        memset(&series, 0, sizeof(series));
                        vip_credentials_clear(&credentials);
                        vip_error_clear(&error);
                        st = login_one_server(resolved_alternate, job->username, job->password, &credentials,
                                              &cats, &channels, &vod, &series, &error);
                        used_alternate = st == VIP_OK;
                    }
                }
            } else {
                error = resolve_error;
            }
            vip_server_resolution_clear(&resolution);
        } else if (st != VIP_OK && saw_http_404 && !allow_external_resolver) {
            vip_error_set(&error, VIP_ERR_NETWORK,
                          "servidor retornou HTTP 404; descoberta externa desativada por privacidade "
                          "(VIPTV_ALLOW_EXTERNAL_RESOLVER=1 para autorizar)");
        }
        if (st == VIP_OK)
            snprintf(provider_id, sizeof(provider_id), "%s", credentials.provider_id);
    }

    if (st == VIP_OK) {
        if (a->db)
            (void)vip_database_replace_catalog(a->db, provider_id, &cats, &channels, &error);
        pthread_mutex_lock(&a->data_mutex);
        vip_category_list_clear(&a->catalogs[CONTENT_LIVE].categories);
        vip_channel_list_clear(&a->catalogs[CONTENT_LIVE].channels);
        a->catalogs[CONTENT_LIVE].categories = cats;
        memset(&cats, 0, sizeof(cats));
        a->catalogs[CONTENT_LIVE].channels = channels;
        memset(&channels, 0, sizeof(channels));
        a->catalogs[CONTENT_LIVE].loaded = true;
        vip_category_list_clear(&a->catalogs[CONTENT_VOD].categories);
        vip_channel_list_clear(&a->catalogs[CONTENT_VOD].channels);
        vip_category_list_clear(&a->catalogs[CONTENT_SERIES].categories);
        vip_channel_list_clear(&a->catalogs[CONTENT_SERIES].channels);
        a->catalogs[CONTENT_VOD] = vod;
        memset(&vod, 0, sizeof(vod));
        a->catalogs[CONTENT_SERIES] = series;
        memset(&series, 0, sizeof(series));
        if (resolved_primary && resolved_primary[0]) {
            snprintf(a->server, sizeof(a->server), "%s", resolved_primary);
            snprintf(a->server_alt, sizeof(a->server_alt), "%s",
                     resolved_alternate ? resolved_alternate : "");
        }
        a->active_server_alt = used_alternate;
        a->content_kind = CONTENT_LIVE;
        a->series_episode_mode = false;
        a->series_season_select = false;
        snprintf(a->active_profile_id, sizeof(a->active_profile_id), "%s", provider_id);
        if (job->mode == LOGIN_M3U) {
            snprintf(a->cached_m3u_server, sizeof(a->cached_m3u_server), "%s", job->server);
            a->cached_m3u_valid = true;
        } else {
            a->cached_m3u_server[0] = '\0';
            a->cached_m3u_valid = false;
        }
        if (job->mode == LOGIN_XTREAM) {
            fprintf(stderr, "[catalog] filmes: %zu itens/%zu categorias; séries: %zu itens/%zu categorias\n",
                    a->catalogs[CONTENT_VOD].channels.len, a->catalogs[CONTENT_VOD].categories.len,
                    a->catalogs[CONTENT_SERIES].channels.len, a->catalogs[CONTENT_SERIES].categories.len);
        }
        snprintf(a->status, sizeof(a->status), "%zu canais em %zu categorias%s",
                 a->catalogs[CONTENT_LIVE].channels.len, a->catalogs[CONTENT_LIVE].categories.len,
                 used_alternate ? " (servidor alternativo)" : "");
        pthread_mutex_unlock(&a->data_mutex);
        success = true;
    } else {
        fprintf(stderr, "[login] falhou: %s\n", error.message);
        pthread_mutex_lock(&a->data_mutex);
        snprintf(a->status, sizeof(a->status), "%s", error.message[0] ? error.message : "falha ao conectar");
        pthread_mutex_unlock(&a->data_mutex);
    }

    vip_category_list_clear(&cats);
    vip_channel_list_clear(&channels);
    vip_category_list_clear(&vod.categories);
    vip_channel_list_clear(&vod.channels);
    vip_category_list_clear(&series.categories);
    vip_channel_list_clear(&series.channels);
    vip_credentials_clear(&credentials);
    free(resolved_primary);
    free(resolved_alternate);
    if (job->password) {
        volatile char *p = job->password;
        size_t n = strlen(job->password);
        while (n--)
            *p++ = 0;
    }
    free(job->server);
    free(job->server_alt);
    free(job->username);
    free(job->password);
    free(job->profile_name);
    free(job);
    atomic_store(&a->login_success, success);
    atomic_store(&a->login_running, false);
    atomic_store(&a->login_done, true);
    return NULL;
}

/* Return whether the form points at the M3U playlist already parsed in RAM. */
bool cached_m3u_matches(const app_t *a) {
    if (!a || a->login_mode != LOGIN_M3U || !a->cached_m3u_valid ||
        !a->server[0] || !a->cached_m3u_server[0])
        return false;
    return strcmp(a->server, a->cached_m3u_server) == 0;
}

/* Reopen the in-memory catalog without network or reparsing. */
static void resume_cached_m3u(app_t *a) {
    if (!a)
        return;
    recalc_category_counts(a);
    load_media_state(a);
    a->favorites_only = false;
    a->selected_category = -1;
    a->category_scroll = 0;
    a->search[0] = '\0';
    rebuild_filter(a);
    a->screen = SCREEN_BROWSE;
    a->browse_top_focus = (int)a->content_kind;
    a->browse_sidebar_focus = -1;
    browse_focus_grid(a);
    snprintf(a->status, sizeof(a->status), "Playlist já carregada; usando sessão atual");
    fprintf(stderr, "[catalog] reutilizando M3U já carregado em memória\n");
}

/* Start login. force_reload is used only by an explicit Connect action. */
static void free_login_job(login_job_t *job) {
    if (!job)
        return;
    if (job->password) {
        volatile char *wipe = job->password;
        size_t n = strlen(job->password);
        while (n-- > 0u)
            *wipe++ = 0;
    }
    free(job->server);
    free(job->server_alt);
    free(job->username);
    free(job->password);
    free(job->profile_name);
    free(job);
}

static bool login_inputs_valid(app_t *a) {
    if (!a->server[0]) {
        snprintf(a->status, sizeof(a->status),
                 a->login_mode == LOGIN_M3U ? "Informe uma URL ou caminho de playlist M3U"
                                            : "Preencha servidor, usuário e senha");
        return false;
    }
    if (a->login_mode == LOGIN_XTREAM && (!a->username[0] || !a->password[0])) {
        snprintf(a->status, sizeof(a->status), "Preencha servidor, usuário e senha");
        return false;
    }
    return true;
}

static login_job_t *create_login_job(app_t *a) {
    login_job_t *job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;
    job->app = a;
    job->mode = a->login_mode;
    job->server = vip_strdup(a->server);
    job->server_alt = vip_strdup(a->server_alt);
    job->username = vip_strdup(a->username);
    job->password = vip_strdup(a->password);
    job->profile_name = vip_strdup(a->profile_name);
    if (!job->server || !job->server_alt || !job->username || !job->password || !job->profile_name) {
        free_login_job(job);
        return NULL;
    }
    return job;
}

static bool launch_login_worker(app_t *a, login_job_t *job) {
    atomic_store(&a->login_done, false);
    atomic_store(&a->login_success, false);
    atomic_store(&a->login_running, true);
    if (pthread_create(&a->login_thread, NULL, login_worker, job) == 0) {
        a->login_thread_started = true;
        return true;
    }
    atomic_store(&a->login_running, false);
    free_login_job(job);
    snprintf(a->status, sizeof(a->status), "Falha ao iniciar conexão");
    return false;
}

void start_login(app_t *a, bool force_reload) {
    if (atomic_load(&a->login_running) || a->login_thread_started || !login_inputs_valid(a))
        return;
    if (!force_reload && cached_m3u_matches(a)) {
        resume_cached_m3u(a);
        return;
    }

    login_job_t *job = create_login_job(a);
    if (!job) {
        snprintf(a->status, sizeof(a->status), "Sem memória");
        return;
    }
    snprintf(a->status, sizeof(a->status),
             a->login_mode == LOGIN_M3U ? "Carregando M3U..." : "Conectando...");
    (void)launch_login_worker(a, job);
}

static void pairing_submission_cb(const char *profile_name,
                                  const char *playlist_url,
                                  void *userdata) {
    app_t *a = userdata;
    if (!a || !playlist_url || !playlist_url[0])
        return;
    pthread_mutex_lock(&a->data_mutex);
    snprintf(a->pairing_pending_url, sizeof(a->pairing_pending_url), "%s", playlist_url);
    snprintf(a->pairing_pending_name, sizeof(a->pairing_pending_name), "%s",
             profile_name ? profile_name : "");
    pthread_mutex_unlock(&a->data_mutex);
    atomic_store(&a->pairing_submission, true);
}

static const char *pairing_base_url(void) {
    const char *configured = getenv("VIPTV_PAIRING_URL");
    if (configured && configured[0])
        return configured;
#ifdef VIPTV_PAIRING_DEFAULT_URL
    return VIPTV_PAIRING_DEFAULT_URL;
#else
    return "";
#endif
}

void stop_phone_pairing(app_t *a) {
    if (!a)
        return;
    if (a->pairing_relay) {
        vip_pairing_relay_stop(a->pairing_relay);
        a->pairing_relay = NULL;
    }
    if (a->pairing_qr) {
        QRcode_free(a->pairing_qr);
        a->pairing_qr = NULL;
    }
    a->pairing_page_url[0] = '\0';
}

void start_phone_pairing(app_t *a) {
    if (!a || a->login_mode != LOGIN_M3U)
        return;
    if (a->pairing_relay) {
        snprintf(a->status, sizeof(a->status), "Aguardando envio pelo celular...");
        return;
    }

    a->pairing_retry_ready = false;

    const char *base_url = pairing_base_url();
    if (!base_url[0]) {
        snprintf(a->status, sizeof(a->status),
                 "Relay HTTPS não configurado (VIPTV_PAIRING_URL)");
        return;
    }

    vip_error_t error = {0};
    if (vip_pairing_relay_start(&a->pairing_relay, base_url,
                                pairing_submission_cb, a, &error) != VIP_OK) {
        snprintf(a->status, sizeof(a->status), "%s",
                 error.message[0] ? error.message : "Falha ao iniciar pareamento HTTPS");
        return;
    }

    const char *page_url = vip_pairing_relay_page_url(a->pairing_relay);
    int copied = snprintf(a->pairing_page_url, sizeof(a->pairing_page_url), "%s", page_url);
    if (copied <= 0 || (size_t)copied >= sizeof(a->pairing_page_url)) {
        stop_phone_pairing(a);
        snprintf(a->status, sizeof(a->status), "URL do pareamento é longa demais");
        return;
    }

    a->pairing_qr = QRcode_encodeString8bit(a->pairing_page_url, 0, QR_ECLEVEL_M);
    if (!a->pairing_qr) {
        stop_phone_pairing(a);
        snprintf(a->status, sizeof(a->status), "Falha ao gerar QR Code do pareamento");
        return;
    }

    snprintf(a->status, sizeof(a->status),
             "Escaneie o QR Code; a sessão expira em 45 segundos");
    fprintf(stderr, "[pairing] sessão HTTPS criada; aguardando celular\n");
}

/* Run the series background worker. */
static void *series_worker(void *userdata) {
    series_job_t *job = userdata;
    app_t *a = job->app;
    vip_credentials_t credentials = {0};
    vip_xtream_client_t *client = NULL;
    vip_category_list_t seasons;
    vip_category_list_init(&seasons);
    vip_channel_list_t episodes;
    vip_channel_list_init(&episodes);
    vip_channel_list_t season_cards;
    vip_channel_list_init(&season_cards);
    vip_media_metadata_t series_metadata;
    vip_media_metadata_init(&series_metadata);
    vip_error_t error = {0};

    vip_status_t st = vip_credentials_init(&credentials, job->server, job->username, job->password, &error);
    if (st == VIP_OK)
        st = vip_xtream_client_create(&client, &credentials, &error);
    if (st == VIP_OK)
        st = vip_xtream_series_info(client, job->series_id, &series_metadata, &seasons, &episodes, &error);

    if (st == VIP_OK) {
        char *series_art = series_metadata.cover_url && series_metadata.cover_url[0]
                               ? series_metadata.cover_url
                               : (job->logo_url && job->logo_url[0] ? job->logo_url : NULL);
        if (series_art) {
            for (size_t i = 0; i < episodes.len; ++i) {
                char *inherited = vip_strdup(series_art);
                if (!inherited) {
                    vip_error_set(&error, VIP_ERR_NOMEM, "sem memória para capa dos episódios");
                    st = VIP_ERR_NOMEM;
                    break;
                }
                free(episodes.items[i].logo_url);
                episodes.items[i].logo_url = inherited;
            }
        }
        const char *fallback_provider =
            episodes.len > 0u ? episodes.items[0].provider_id : credentials.provider_id;
        for (size_t i = 0; i < seasons.len; ++i) {
            vip_category_t *season = &seasons.items[i];
            char season_id[512];
            snprintf(season_id, sizeof(season_id), "season:%s:%s", job->series_id,
                     season->id ? season->id : "0");
            vip_channel_t card = {
                .provider_id = season->provider_id && season->provider_id[0] ? season->provider_id
                                                                             : (char *)fallback_provider,
                .id = season_id,
                .category_id = season->id,
                .name = season->name ? season->name : "Temporada",
                .logo_url = series_art,
                .stream_url = "series://season",
                .epg_channel_id = NULL,
                .position = (int)i,
            };
            st = vip_channel_list_push(&season_cards, &card, &error);
            if (st != VIP_OK)
                break;
        }
        if (st == VIP_OK) {
            sort_episode_channels(&episodes);
            sort_season_channels(&season_cards);
        }
    }

    if (st == VIP_OK && season_cards.len > 0u) {
        pthread_mutex_lock(&a->data_mutex);
        vip_category_list_clear(&a->episode_categories);
        vip_channel_list_clear(&a->episode_channels);
        vip_channel_list_clear(&a->season_channels);
        a->episode_categories = seasons;
        memset(&seasons, 0, sizeof(seasons));
        a->episode_channels = episodes;
        memset(&episodes, 0, sizeof(episodes));
        a->season_channels = season_cards;
        memset(&season_cards, 0, sizeof(season_cards));
        snprintf(a->series_title, sizeof(a->series_title), "%s", job->title ? job->title : "Série");
        snprintf(a->status, sizeof(a->status), "%zu temporadas • %zu episódios", a->season_channels.len,
                 a->episode_channels.len);
        pthread_mutex_unlock(&a->data_mutex);
        atomic_store(&a->series_success, true);
    } else {
        if (st == VIP_OK)
            vip_error_set(&error, VIP_ERR_MALFORMED, "nenhuma temporada encontrada");
        pthread_mutex_lock(&a->data_mutex);
        snprintf(a->status, sizeof(a->status), "Falha ao carregar episódios: %s",
                 error.message[0] ? error.message : "erro desconhecido");
        pthread_mutex_unlock(&a->data_mutex);
        atomic_store(&a->series_success, false);
    }

    if (client)
        vip_xtream_client_destroy(client);
    vip_credentials_clear(&credentials);
    vip_category_list_clear(&seasons);
    vip_channel_list_clear(&episodes);
    vip_channel_list_clear(&season_cards);
    vip_media_metadata_clear(&series_metadata);
    if (job->password) {
        volatile char *wipe = job->password;
        size_t n = strlen(job->password);
        while (n-- > 0u)
            *wipe++ = 0;
    }
    free(job->server);
    free(job->username);
    free(job->password);
    free(job->series_id);
    free(job->title);
    free(job->logo_url);
    free(job);
    atomic_store(&a->series_running, false);
    atomic_store(&a->series_done, true);
    return NULL;
}

/* Start series load. */
static void free_series_job(series_job_t *job) {
    if (!job)
        return;
    if (job->password) {
        volatile char *wipe = job->password;
        size_t n = strlen(job->password);
        while (n-- > 0u)
            *wipe++ = 0;
    }
    free(job->server);
    free(job->username);
    free(job->password);
    free(job->series_id);
    free(job->title);
    free(job->logo_url);
    free(job);
}

static series_job_t *create_series_job(app_t *a, const vip_channel_t *series, const char *server) {
    series_job_t *job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;
    job->app = a;
    job->server = vip_strdup(server);
    job->username = vip_strdup(a->username);
    job->password = vip_strdup(a->password);
    job->series_id = vip_strdup(series->id);
    job->title = vip_strdup(series->name);
    job->logo_url = vip_strdup(series->logo_url ? series->logo_url : "");
    if (!job->server || !job->username || !job->password || !job->series_id || !job->title ||
        !job->logo_url) {
        free_series_job(job);
        return NULL;
    }
    return job;
}

static bool series_load_allowed(app_t *a, size_t channel_index) {
    return a && a->content_kind == CONTENT_SERIES && !a->series_episode_mode &&
           !atomic_load(&a->series_running) && !a->series_thread_started &&
           channel_index < ACTIVE_CHANNELS(a).len;
}

void start_series_load(app_t *a, size_t channel_index) {
    if (!series_load_allowed(a, channel_index))
        return;

    vip_channel_t *series = &ACTIVE_CHANNELS(a).items[channel_index];
    if (!series->id || strncmp(series->id, "series:", 7u) != 0)
        return;

    snprintf(a->series_parent_id, sizeof(a->series_parent_id), "%s", series->id);
    const char *server = a->active_server_alt && a->server_alt[0] ? a->server_alt : a->server;
    series_job_t *job = create_series_job(a, series, server);
    if (!job)
        return;

    pthread_mutex_lock(&a->data_mutex);
    snprintf(a->status, sizeof(a->status), "Carregando temporadas de %s...", series->name);
    pthread_mutex_unlock(&a->data_mutex);
    atomic_store(&a->series_done, false);
    atomic_store(&a->series_success, false);
    atomic_store(&a->series_running, true);

    if (pthread_create(&a->series_thread, NULL, series_worker, job) != 0) {
        atomic_store(&a->series_running, false);
        free_series_job(job);
        return;
    }
    a->series_thread_started = true;
}

/* Return whether content for the metadata. */
static bool metadata_has_content(const vip_media_metadata_t *metadata) {
    return metadata &&
           ((metadata->plot && metadata->plot[0]) || (metadata->cover_url && metadata->cover_url[0]) ||
            (metadata->backdrop_url && metadata->backdrop_url[0]) ||
            (metadata->genre && metadata->genre[0]) ||
            (metadata->release_date && metadata->release_date[0]) ||
            (metadata->rating && metadata->rating[0]) || (metadata->duration && metadata->duration[0]) ||
            (metadata->cast && metadata->cast[0]) || (metadata->director && metadata->director[0]));
}

/* Release the requested state in the details job. */
static void details_job_free(details_job_t *job) {
    if (!job)
        return;
    if (job->password) {
        volatile char *wipe = job->password;
        size_t n = strlen(job->password);
        while (n-- > 0u)
            *wipe++ = 0;
    }
    free(job->server);
    free(job->username);
    free(job->password);
    free(job->provider_id);
    free(job->media_id);
    free(job->title);
    free(job);
}

/* Run the details background worker. */
static bool details_cache_is_fresh(int64_t updated_at) {
    if (updated_at <= 0)
        return false;
    int64_t age = (int64_t)time(NULL) - updated_at;
    return age >= 0 && age < 7LL * 24LL * 60LL * 60LL;
}

static vip_status_t load_details_from_cache(app_t *a, const details_job_t *job, vip_media_metadata_t *metadata,
                                            vip_media_metadata_t *cached, bool *cache_found, bool *used_cache) {
    if (!a->db)
        return VIP_ERR_NETWORK;

    int64_t updated_at = 0;
    vip_error_t cache_error = {0};
    vip_status_t st = vip_database_get_media_metadata(a->db, job->provider_id, job->media_id, cached,
                                                      &updated_at, cache_found, &cache_error);
    if (st != VIP_OK || !*cache_found || !metadata_has_content(cached) || !details_cache_is_fresh(updated_at))
        return VIP_ERR_NETWORK;

    *metadata = *cached;
    memset(cached, 0, sizeof(*cached));
    *used_cache = true;
    return VIP_OK;
}

static vip_status_t fetch_details_remote(app_t *a, const details_job_t *job, vip_media_metadata_t *metadata,
                                         vip_error_t *error) {
    vip_credentials_t credentials = {0};
    vip_xtream_client_t *client = NULL;
    vip_status_t st = vip_credentials_init(&credentials, job->server, job->username, job->password, error);
    if (st == VIP_OK)
        st = vip_xtream_client_create(&client, &credentials, error);
    if (st == VIP_OK) {
        if (job->kind == CONTENT_VOD)
            st = vip_xtream_vod_info(client, job->media_id, metadata, error);
        else
            st = vip_xtream_series_metadata(client, job->media_id, metadata, error);
    }
    if (client)
        vip_xtream_client_destroy(client);
    vip_credentials_clear(&credentials);

    if (st == VIP_OK && metadata_has_content(metadata) && a->db) {
        vip_error_t db_error = {0};
        (void)vip_database_set_media_metadata(a->db, job->provider_id, job->media_id, metadata, &db_error);
    }
    return st;
}

static vip_status_t fallback_to_cached_details(vip_status_t st, bool cache_found,
                                               vip_media_metadata_t *metadata, vip_media_metadata_t *cached,
                                               bool *used_cache) {
    if (st == VIP_OK || !cache_found || !metadata_has_content(cached))
        return st;
    vip_media_metadata_clear(metadata);
    *metadata = *cached;
    memset(cached, 0, sizeof(*cached));
    *used_cache = true;
    return VIP_OK;
}

static bool publish_details_result(app_t *a, const details_job_t *job, vip_media_metadata_t *metadata,
                                   vip_status_t st, const vip_error_t *error, bool used_cache) {
    pthread_mutex_lock(&a->data_mutex);
    bool current = strcmp(a->details_media_id, job->media_id) == 0;
    if (current) {
        vip_media_metadata_clear(&a->details_metadata);
        if (st == VIP_OK) {
            a->details_metadata = *metadata;
            memset(metadata, 0, sizeof(*metadata));
            snprintf(a->details_status, sizeof(a->details_status), "%s",
                     used_cache ? "Detalhes carregados do cache" : "Detalhes carregados");
        } else {
            char bounded_error[220];
            bounded_text(bounded_error, sizeof(bounded_error),
                         error->message[0] ? error->message : "provider não retornou metadados", 90);
            snprintf(a->details_status, sizeof(a->details_status), "Sem detalhes: %.220s", bounded_error);
        }
    }
    pthread_mutex_unlock(&a->data_mutex);
    return current;
}

static void *details_worker(void *userdata) {
    details_job_t *job = userdata;
    app_t *a = job->app;
    vip_media_metadata_t metadata;
    vip_media_metadata_init(&metadata);
    vip_media_metadata_t cached;
    vip_media_metadata_init(&cached);
    vip_error_t error = {0};
    bool cache_found = false;
    bool used_cache = false;

    vip_status_t st = load_details_from_cache(a, job, &metadata, &cached, &cache_found, &used_cache);
    if (st != VIP_OK) {
        st = fetch_details_remote(a, job, &metadata, &error);
        st = fallback_to_cached_details(st, cache_found, &metadata, &cached, &used_cache);
    }

    bool current = publish_details_result(a, job, &metadata, st, &error, used_cache);
    vip_media_metadata_clear(&metadata);
    vip_media_metadata_clear(&cached);
    atomic_store(&a->details_success, current && st == VIP_OK);
    atomic_store(&a->details_running, false);
    atomic_store(&a->details_done, true);
    details_job_free(job);
    return NULL;
}

/* Clear details view. */
void clear_details_view(app_t *a) {
    if (!a)
        return;
    pthread_mutex_lock(&a->data_mutex);
    vip_media_metadata_clear(&a->details_metadata);
    a->details_media_id[0] = '\0';
    a->details_title[0] = '\0';
    a->details_status[0] = '\0';
    pthread_mutex_unlock(&a->data_mutex);
}

/* Return whether a details request may start for this catalog item. */
static bool details_load_allowed(const app_t *a, size_t channel_index) {
    if (!a || atomic_load(&a->details_running) || a->details_thread_started)
        return false;
    if (a->login_mode != LOGIN_XTREAM || a->series_episode_mode)
        return false;
    if (a->content_kind != CONTENT_VOD && a->content_kind != CONTENT_SERIES)
        return false;
    return channel_index < ACTIVE_CHANNELS(a).len;
}

/* Return the active Xtream server used by metadata requests. */
static const char *details_active_server(const app_t *a) {
    if (a->active_server_alt && a->server_alt[0])
        return a->server_alt;
    return a->server;
}

/* Allocate and copy all data needed by the background details worker. */
static details_job_t *create_details_job(app_t *a, const vip_channel_t *media,
                                         const char *server) {
    details_job_t *job = calloc(1, sizeof(*job));
    if (!job)
        return NULL;

    job->app = a;
    job->kind = a->content_kind;
    job->server = vip_strdup(server);
    job->username = vip_strdup(a->username);
    job->password = vip_strdup(a->password);
    job->provider_id = vip_strdup(media->provider_id);
    job->media_id = vip_strdup(media->id);
    job->title = vip_strdup(media->name);
    if (!job->server || !job->username || !job->password ||
        !job->provider_id || !job->media_id || !job->title) {
        details_job_free(job);
        return NULL;
    }
    return job;
}

/* Initialize the visible details state before launching the worker. */
static void prepare_details_load(app_t *a, const vip_channel_t *media) {
    pthread_mutex_lock(&a->data_mutex);
    vip_media_metadata_clear(&a->details_metadata);
    snprintf(a->details_media_id, sizeof(a->details_media_id), "%s", media->id);
    snprintf(a->details_title, sizeof(a->details_title), "%s", media->name);
    snprintf(a->details_status, sizeof(a->details_status), "Carregando detalhes...");
    pthread_mutex_unlock(&a->data_mutex);
    atomic_store(&a->details_done, false);
    atomic_store(&a->details_success, false);
    atomic_store(&a->details_running, true);
}

/* Launch the details worker and restore state if pthread_create fails. */
static bool launch_details_worker(app_t *a, details_job_t *job) {
    if (pthread_create(&a->details_thread, NULL, details_worker, job) == 0) {
        a->details_thread_started = true;
        return true;
    }

    atomic_store(&a->details_running, false);
    pthread_mutex_lock(&a->data_mutex);
    snprintf(a->details_status, sizeof(a->details_status),
             "Falha ao iniciar carregamento de detalhes");
    pthread_mutex_unlock(&a->data_mutex);
    details_job_free(job);
    return false;
}

/* Start details load. */
void start_details_load(app_t *a, size_t channel_index) {
    if (!details_load_allowed(a, channel_index))
        return;

    vip_channel_t *media = &ACTIVE_CHANNELS(a).items[channel_index];
    if (!media->id || !media->provider_id || !media->id[0])
        return;

    const char *server = details_active_server(a);
    if (!server[0] || !a->username[0] || !a->password[0])
        return;

    details_job_t *job = create_details_job(a, media, server);
    if (!job)
        return;

    prepare_details_load(a, media);
    (void)launch_details_worker(a, job);
}

/* Start details load in the maybe. */
void maybe_start_details_load(app_t *a) {
    if (!details_panel_active(a) || a->screen != SCREEN_BROWSE || atomic_load(&a->details_running) ||
        a->details_thread_started || a->filtered_len == 0u)
        return;
    if (a->focused_filtered >= a->filtered_len)
        a->focused_filtered = a->filtered_len - 1u;
    size_t channel_index = a->filtered[a->focused_filtered];
    if (channel_index >= ACTIVE_CHANNELS(a).len)
        return;
    const char *id = ACTIVE_CHANNELS(a).items[channel_index].id;
    if (!id || !id[0])
        return;
    pthread_mutex_lock(&a->data_mutex);
    bool same = strcmp(a->details_media_id, id) == 0;
    pthread_mutex_unlock(&a->data_mutex);
    if (!same)
        start_details_load(a, channel_index);
}

/* Handle the same text case operation. */
static bool same_text_case(const char *a, const char *b) {
    return a && b && strcasecmp(a, b) == 0;
}

/* Collect the distinct seasons belonging to one parsed M3U series. */
static size_t collect_m3u_series_seasons(app_t *a, const char *series_name,
                                         int *seasons, size_t capacity) {
    size_t count = 0u;
    for (size_t i = 0u; i < a->catalogs[CONTENT_SERIES].channels.len; ++i) {
        vip_channel_t *ch = &a->catalogs[CONTENT_SERIES].channels.items[i];
        char candidate[256];
        int season = 0, episode = 0;
        if (!vip_m3u_parse_episode_label(ch->name, candidate, sizeof(candidate), &season, &episode) ||
            !same_text_case(candidate, series_name))
            continue;

        bool exists = false;
        for (size_t j = 0u; j < count; ++j) {
            if (seasons[j] == season) {
                exists = true;
                break;
            }
        }
        if (!exists && count < capacity)
            seasons[count++] = season;
    }
    return count;
}

/* Sort season numbers without allocating a temporary collection. */
static void sort_m3u_seasons(int *seasons, size_t count) {
    for (size_t i = 1u; i < count; ++i) {
        int value = seasons[i];
        size_t j = i;
        while (j > 0u && seasons[j - 1u] > value) {
            seasons[j] = seasons[j - 1u];
            --j;
        }
        seasons[j] = value;
    }
}

/* Append the category/card and matching episodes for one M3U season. */
static vip_status_t append_m3u_season(app_t *a, const vip_channel_t *selected,
                                      const char *series_name, int season_number,
                                      size_t season_position, uint64_t title_hash,
                                      vip_category_list_t *cats, vip_channel_list_t *episodes,
                                      vip_channel_list_t *cards, vip_error_t *error) {
    char cat_id[96];
    char cat_name[96];
    char card_id[128];
    snprintf(cat_id, sizeof(cat_id), "m3u-season:%016llx:%d",
             (unsigned long long)title_hash, season_number);
    snprintf(cat_name, sizeof(cat_name), season_number == 0 ? "Especiais" : "Temporada %d",
             season_number);

    vip_category_t cat = {
        .provider_id = selected->provider_id,
        .id = cat_id,
        .name = cat_name,
        .position = (int)season_position,
    };
    vip_status_t st = vip_category_list_push(cats, &cat, error);
    if (st != VIP_OK)
        return st;

    snprintf(card_id, sizeof(card_id), "m3u-series-season:%016llx:%d",
             (unsigned long long)title_hash, season_number);
    vip_channel_t card = {
        .provider_id = selected->provider_id,
        .id = card_id,
        .category_id = cat_id,
        .name = cat_name,
        .logo_url = selected->logo_url,
        .stream_url = "series://season",
        .epg_channel_id = NULL,
        .position = (int)season_position,
    };
    st = vip_channel_list_push(cards, &card, error);
    if (st != VIP_OK)
        return st;

    for (size_t i = 0u; i < a->catalogs[CONTENT_SERIES].channels.len; ++i) {
        vip_channel_t *ch = &a->catalogs[CONTENT_SERIES].channels.items[i];
        char candidate[256];
        int season = 0, episode = 0;
        if (!vip_m3u_parse_episode_label(ch->name, candidate, sizeof(candidate), &season, &episode) ||
            season != season_number || !same_text_case(candidate, series_name))
            continue;
        vip_channel_t copy = *ch;
        copy.category_id = cat_id;
        copy.position = episode;
        st = vip_channel_list_push(episodes, &copy, error);
        if (st != VIP_OK)
            return st;
    }
    return VIP_OK;
}

/* Build temporary season/category lists before swapping them into app state. */
static vip_status_t build_m3u_series_lists(app_t *a, const vip_channel_t *selected,
                                           const char *series_name, const int *seasons,
                                           size_t season_count, vip_category_list_t *cats,
                                           vip_channel_list_t *episodes, vip_channel_list_t *cards,
                                           vip_error_t *error) {
    uint64_t title_hash = folded_name_hash(series_name);
    for (size_t i = 0u; i < season_count; ++i) {
        vip_status_t st = append_m3u_season(
            a, selected, series_name, seasons[i], i, title_hash,
            cats, episodes, cards, error);
        if (st != VIP_OK)
            return st;
    }
    sort_episode_channels(episodes);
    sort_season_channels(cards);
    return VIP_OK;
}

/* Release temporary M3U series lists after a failed build. */
static void clear_m3u_series_lists(vip_category_list_t *cats, vip_channel_list_t *episodes,
                                   vip_channel_list_t *cards) {
    vip_category_list_clear(cats);
    vip_channel_list_clear(episodes);
    vip_channel_list_clear(cards);
}

/* Swap a successful M3U series build into the current browse state. */
static void activate_m3u_series_lists(app_t *a, const vip_channel_t *selected,
                                      const char *series_name, vip_category_list_t cats,
                                      vip_channel_list_t episodes, vip_channel_list_t cards) {
    vip_category_list_clear(&a->episode_categories);
    vip_channel_list_clear(&a->episode_channels);
    vip_channel_list_clear(&a->season_channels);
    a->episode_categories = cats;
    a->episode_channels = episodes;
    a->season_channels = cards;
    a->series_episode_mode = true;
    a->series_season_select = true;
    a->favorites_only = false;
    a->selected_category = -1;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    browse_focus_grid(a);
    snprintf(a->series_title, sizeof(a->series_title), "%s", series_name);
    snprintf(a->series_parent_id, sizeof(a->series_parent_id), "%s",
             selected->id ? selected->id : "");
    snprintf(a->status, sizeof(a->status), "%zu temporadas • %zu episódios",
             cards.len, episodes.len);
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}

/* Start m3u series load. */
bool start_m3u_series_load(app_t *a, size_t channel_index) {
    if (!a || !m3u_series_root(a) || channel_index >= a->catalogs[CONTENT_SERIES].channels.len)
        return false;

    vip_channel_t *selected = &a->catalogs[CONTENT_SERIES].channels.items[channel_index];
    char series_name[256];
    int selected_season = 0, selected_episode = 0;
    if (!vip_m3u_parse_episode_label(selected->name, series_name, sizeof(series_name),
                                     &selected_season, &selected_episode))
        return false;
    (void)selected_season;
    (void)selected_episode;

    int seasons[256];
    size_t season_count = collect_m3u_series_seasons(
        a, series_name, seasons, sizeof(seasons) / sizeof(seasons[0]));
    if (season_count == 0u)
        return false;
    sort_m3u_seasons(seasons, season_count);

    vip_category_list_t cats;
    vip_channel_list_t episodes;
    vip_channel_list_t cards;
    vip_category_list_init(&cats);
    vip_channel_list_init(&episodes);
    vip_channel_list_init(&cards);
    vip_error_t error = {0};

    vip_status_t st = build_m3u_series_lists(
        a, selected, series_name, seasons, season_count,
        &cats, &episodes, &cards, &error);
    if (st != VIP_OK || cards.len == 0u || episodes.len == 0u) {
        clear_m3u_series_lists(&cats, &episodes, &cards);
        snprintf(a->status, sizeof(a->status), "Falha ao organizar série M3U: %s",
                 error.message[0] ? error.message : "dados insuficientes");
        return false;
    }

    activate_m3u_series_lists(a, selected, series_name, cats, episodes, cards);
    return true;
}

/* Handle async. */
static void handle_pairing_async(app_t *a) {
    if (atomic_exchange(&a->pairing_submission, false)) {
        char url[sizeof(a->server)];
        char name[sizeof(a->profile_name)];
        pthread_mutex_lock(&a->data_mutex);
        snprintf(url, sizeof(url), "%s", a->pairing_pending_url);
        snprintf(name, sizeof(name), "%s", a->pairing_pending_name);
        a->pairing_pending_url[0] = '\0';
        a->pairing_pending_name[0] = '\0';
        pthread_mutex_unlock(&a->data_mutex);

        stop_phone_pairing(a);
        a->pairing_retry_ready = false;
        a->login_mode = LOGIN_M3U;
        snprintf(a->server, sizeof(a->server), "%s", url);
        snprintf(a->profile_name, sizeof(a->profile_name), "%s", name);
        snprintf(a->status, sizeof(a->status), "Playlist recebida do celular; carregando...");
        start_login(a, false);
        return;
    }
    if (a->pairing_relay && vip_pairing_relay_finished(a->pairing_relay)) {
        stop_phone_pairing(a);
        a->pairing_retry_ready = true;
        a->input_focus = INPUT_PHONE;
        snprintf(a->status, sizeof(a->status),
                 "Sessão expirou após 45 s; selecione Tentar novamente (QR)");
    }
}

static void handle_login_async(app_t *a) {
    if (!atomic_exchange(&a->login_done, false))
        return;
    if (a->login_thread_started) {
        pthread_join(a->login_thread, NULL);
        a->login_thread_started = false;
    }
    if (!atomic_load(&a->login_success))
        return;

    save_active_profile(a);
    recalc_category_counts(a);
    load_media_state(a);
    a->favorites_only = false;
    a->selected_category = -1;
    a->category_scroll = 0;
    a->search[0] = '\0';
    rebuild_filter(a);
    a->screen = SCREEN_BROWSE;
    a->browse_top_focus = (int)a->content_kind;
    a->browse_sidebar_focus = -1;
    browse_focus_grid(a);
    fprintf(stderr, "[catalog] %zu canais, %zu categorias\n", ACTIVE_CHANNELS(a).len,
            ACTIVE_CATEGORIES(a).len);
    if (a->test_series) {
        switch_content(a, CONTENT_SERIES);
        if (ACTIVE_CHANNELS(a).len > 0)
            start_series_load(a, 0);
    } else if (a->test_autoplay && ACTIVE_CHANNELS(a).len > 0) {
        enter_player(a, 0);
    }
}

static void handle_series_async(app_t *a) {
    if (!atomic_exchange(&a->series_done, false))
        return;
    if (a->series_thread_started) {
        pthread_join(a->series_thread, NULL);
        a->series_thread_started = false;
    }
    if (!atomic_load(&a->series_success) || a->screen != SCREEN_BROWSE || a->content_kind != CONTENT_SERIES)
        return;

    clear_details_view(a);
    a->series_episode_mode = true;
    a->series_season_select = true;
    a->favorites_only = false;
    a->selected_category = -1;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
    fprintf(stderr, "[series] %zu episódios carregados\n", ACTIVE_CHANNELS(a).len);
}

static void handle_details_async(app_t *a) {
    if (!atomic_exchange(&a->details_done, false))
        return;
    if (a->details_thread_started) {
        pthread_join(a->details_thread, NULL);
        a->details_thread_started = false;
    }
}

void handle_async(app_t *a) {
    handle_pairing_async(a);
    handle_login_async(a);
    handle_series_async(a);
    handle_details_async(a);
    (void)atomic_exchange(&a->thumbs_dirty, false);
}
