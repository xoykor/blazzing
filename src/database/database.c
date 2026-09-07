/* SPDX-License-Identifier: MIT */
/*
 * SQLite persistence layer.
 *
 * The database stores non-secret profile metadata, catalog cache, favorites,
 * thumbnail metadata and playback progress.  Xtream passwords never enter this
 * module; the UI delegates them to Secret Service when available.
 */
#include "visual_iptv/database.h"

#include <pthread.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

struct vip_database {
    sqlite3 *conn;
    pthread_mutex_t mutex;
};

static vip_status_t db_error(vip_database_t *db, vip_error_t *error, const char *prefix) {
    vip_error_set(error, VIP_ERR_DATABASE, "%s: %s", prefix, sqlite3_errmsg(db->conn));
    return VIP_ERR_DATABASE;
}

static vip_status_t exec_sql(vip_database_t *db, const char *sql, vip_error_t *error) {
    char *msg = NULL;
    int rc = sqlite3_exec(db->conn, sql, NULL, NULL, &msg);
    if (rc != SQLITE_OK) {
        vip_error_set(error, VIP_ERR_DATABASE, "sqlite: %s", msg ? msg : sqlite3_errmsg(db->conn));
        sqlite3_free(msg);
        return VIP_ERR_DATABASE;
    }
    return VIP_OK;
}

/* Schema creation is idempotent.  Existing installations are upgraded in
 * place so favorites, profiles and watch progress survive application updates. */
static vip_status_t migrate(vip_database_t *db, vip_error_t *error) {
    const char *sql =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS categories("
        " provider_id TEXT NOT NULL, category_id TEXT NOT NULL, name TEXT NOT NULL,"
        " position INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(provider_id, category_id));"
        "CREATE TABLE IF NOT EXISTS channels("
        " provider_id TEXT NOT NULL, channel_id TEXT NOT NULL, category_id TEXT, name TEXT NOT NULL,"
        " logo_url TEXT, stream_url TEXT NOT NULL, epg_channel_id TEXT, position INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY(provider_id, channel_id));"
        "CREATE INDEX IF NOT EXISTS channels_category_idx ON channels(provider_id, category_id, position);"
        "CREATE TABLE IF NOT EXISTS favorites("
        " provider_id TEXT NOT NULL, channel_id TEXT NOT NULL, created_at INTEGER NOT NULL DEFAULT(unixepoch()),"
        " PRIMARY KEY(provider_id, channel_id));"
        "CREATE TABLE IF NOT EXISTS history("
        " provider_id TEXT NOT NULL, channel_id TEXT NOT NULL, watched_at INTEGER NOT NULL DEFAULT(unixepoch()),"
        " PRIMARY KEY(provider_id, channel_id));"
        "CREATE TABLE IF NOT EXISTS thumbnail_metadata("
        " provider_id TEXT NOT NULL, channel_id TEXT NOT NULL, path TEXT NOT NULL, source INTEGER NOT NULL,"
        " generated_at INTEGER NOT NULL, last_verified INTEGER NOT NULL,"
        " PRIMARY KEY(provider_id, channel_id));"
        "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS profiles("
        " profile_id TEXT PRIMARY KEY, name TEXT NOT NULL, type INTEGER NOT NULL,"
        " server TEXT NOT NULL, server_alt TEXT, username TEXT,"
        " created_at INTEGER NOT NULL DEFAULT(unixepoch()), last_used INTEGER NOT NULL DEFAULT(unixepoch()));"
        "CREATE INDEX IF NOT EXISTS profiles_last_used_idx ON profiles(last_used DESC);"
        "CREATE TABLE IF NOT EXISTS watch_progress("
        " provider_id TEXT NOT NULL, channel_id TEXT NOT NULL, position_seconds REAL NOT NULL DEFAULT 0,"
        " duration_seconds REAL NOT NULL DEFAULT 0, completed INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL DEFAULT(unixepoch()), PRIMARY KEY(provider_id, channel_id));"
        "CREATE INDEX IF NOT EXISTS watch_progress_updated_idx ON watch_progress(provider_id, updated_at DESC);"
        "CREATE TABLE IF NOT EXISTS series_progress("
        " provider_id TEXT NOT NULL, series_id TEXT NOT NULL, last_episode_id TEXT,"
        " watched_count INTEGER NOT NULL DEFAULT 0, total_count INTEGER NOT NULL DEFAULT 0,"
        " updated_at INTEGER NOT NULL DEFAULT(unixepoch()), PRIMARY KEY(provider_id, series_id));"
        "CREATE TABLE IF NOT EXISTS media_metadata("
        " provider_id TEXT NOT NULL, media_id TEXT NOT NULL, plot TEXT, cover_url TEXT, backdrop_url TEXT,"
        " genre TEXT, release_date TEXT, rating TEXT, duration TEXT, cast TEXT, director TEXT, youtube_trailer TEXT,"
        " updated_at INTEGER NOT NULL DEFAULT(unixepoch()), PRIMARY KEY(provider_id, media_id));";
    return exec_sql(db, sql, error);
}

static vip_status_t open_common(vip_database_t **out, const char *path, vip_error_t *error) {
    *out = NULL;
    vip_database_t *db = calloc(1, sizeof(*db));
    if (!db) {
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para banco");
        return VIP_ERR_NOMEM;
    }
    pthread_mutex_init(&db->mutex, NULL);
    int rc = sqlite3_open_v2(path, &db->conn,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                             NULL);
    if (rc != SQLITE_OK) {
        vip_error_set(error, VIP_ERR_DATABASE, "não foi possível abrir SQLite: %s",
                      db->conn ? sqlite3_errmsg(db->conn) : "erro desconhecido");
        if (db->conn) sqlite3_close(db->conn);
        pthread_mutex_destroy(&db->mutex);
        free(db);
        return VIP_ERR_DATABASE;
    }
    vip_status_t st = migrate(db, error);
    if (st != VIP_OK) {
        vip_database_close(db);
        return st;
    }
    *out = db;
    return VIP_OK;
}

vip_status_t vip_database_open(vip_database_t **out, const char *path, vip_error_t *error) {
    return open_common(out, path, error);
}

vip_status_t vip_database_open_memory(vip_database_t **out, vip_error_t *error) {
    return open_common(out, ":memory:", error);
}

void vip_database_close(vip_database_t *db) {
    if (!db) return;
    if (db->conn) sqlite3_close(db->conn);
    pthread_mutex_destroy(&db->mutex);
    free(db);
}

static void bind_nullable(sqlite3_stmt *stmt, int index, const char *value) {
    if (value && value[0]) sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
    else sqlite3_bind_null(stmt, index);
}

vip_status_t vip_database_replace_catalog(vip_database_t *db,
                                          const char *provider_id,
                                          const vip_category_list_t *categories,
                                          const vip_channel_list_t *channels,
                                          vip_error_t *error) {
    if (!db || !provider_id || !categories || !channels) return VIP_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&db->mutex);
    vip_status_t st = exec_sql(db, "BEGIN IMMEDIATE", error);
    sqlite3_stmt *delcat = NULL, *delch = NULL, *inscat = NULL, *insch = NULL;
    if (st != VIP_OK) goto done;
    if (sqlite3_prepare_v2(db->conn, "DELETE FROM categories WHERE provider_id=?1", -1, &delcat, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db->conn, "DELETE FROM channels WHERE provider_id=?1", -1, &delch, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db->conn,
            "INSERT INTO categories(provider_id,category_id,name,position) VALUES(?1,?2,?3,?4)",
            -1, &inscat, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db->conn,
            "INSERT INTO channels(provider_id,channel_id,category_id,name,logo_url,stream_url,epg_channel_id,position)"
            " VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", -1, &insch, NULL) != SQLITE_OK) {
        st = db_error(db, error, "falha ao preparar catálogo");
        goto rollback;
    }
    sqlite3_bind_text(delcat, 1, provider_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(delcat) != SQLITE_DONE) { st = db_error(db, error, "falha ao limpar categorias"); goto rollback; }
    sqlite3_bind_text(delch, 1, provider_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(delch) != SQLITE_DONE) { st = db_error(db, error, "falha ao limpar canais"); goto rollback; }

    for (size_t i = 0; i < categories->len; ++i) {
        const vip_category_t *c = &categories->items[i];
        sqlite3_reset(inscat); sqlite3_clear_bindings(inscat);
        sqlite3_bind_text(inscat, 1, provider_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(inscat, 2, c->id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(inscat, 3, c->name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(inscat, 4, c->position);
        if (sqlite3_step(inscat) != SQLITE_DONE) { st = db_error(db, error, "falha ao inserir categoria"); goto rollback; }
    }
    for (size_t i = 0; i < channels->len; ++i) {
        const vip_channel_t *c = &channels->items[i];
        sqlite3_reset(insch); sqlite3_clear_bindings(insch);
        sqlite3_bind_text(insch, 1, provider_id, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insch, 2, c->id, -1, SQLITE_TRANSIENT);
        bind_nullable(insch, 3, c->category_id);
        sqlite3_bind_text(insch, 4, c->name, -1, SQLITE_TRANSIENT);
        bind_nullable(insch, 5, c->logo_url);
        sqlite3_bind_text(insch, 6, c->stream_url, -1, SQLITE_TRANSIENT);
        bind_nullable(insch, 7, c->epg_channel_id);
        sqlite3_bind_int(insch, 8, c->position);
        if (sqlite3_step(insch) != SQLITE_DONE) { st = db_error(db, error, "falha ao inserir canal"); goto rollback; }
    }
    st = exec_sql(db, "COMMIT", error);
    goto done;

rollback:
    exec_sql(db, "ROLLBACK", NULL);
done:
    sqlite3_finalize(delcat); sqlite3_finalize(delch); sqlite3_finalize(inscat); sqlite3_finalize(insch);
    pthread_mutex_unlock(&db->mutex);
    return st;
}

vip_status_t vip_database_load_channels(vip_database_t *db,
                                        const char *provider_id,
                                        vip_channel_list_t *out,
                                        vip_error_t *error) {
    if (!db || !provider_id || !out) return VIP_ERR_INVALID_ARGUMENT;
    vip_channel_list_init(out);
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT channel_id,category_id,name,logo_url,stream_url,epg_channel_id,position"
                      " FROM channels WHERE provider_id=?1 ORDER BY position";
    if (sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao ler canais");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    vip_status_t st = VIP_OK;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        vip_channel_t c = {
            .provider_id = (char *)provider_id,
            .id = (char *)sqlite3_column_text(stmt, 0),
            .category_id = (char *)sqlite3_column_text(stmt, 1),
            .name = (char *)sqlite3_column_text(stmt, 2),
            .logo_url = (char *)sqlite3_column_text(stmt, 3),
            .stream_url = (char *)sqlite3_column_text(stmt, 4),
            .epg_channel_id = (char *)sqlite3_column_text(stmt, 5),
            .position = sqlite3_column_int(stmt, 6),
        };
        st = vip_channel_list_push(out, &c, error);
        if (st != VIP_OK) break;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (st != VIP_OK) vip_channel_list_clear(out);
    return st;
}

vip_status_t vip_database_set_favorite(vip_database_t *db,
                                       const char *provider_id,
                                       const char *channel_id,
                                       bool favorite,
                                       vip_error_t *error) {
    if (!db || !provider_id || !channel_id) return VIP_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    const char *sql = favorite
        ? "INSERT INTO favorites(provider_id,channel_id) VALUES(?1,?2) ON CONFLICT DO NOTHING"
        : "DELETE FROM favorites WHERE provider_id=?1 AND channel_id=?2";
    if (sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao preparar favorito");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channel_id, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_DONE) return db_error(db, error, "falha ao atualizar favorito");
    return VIP_OK;
}

vip_status_t vip_database_set_thumbnail(vip_database_t *db,
                                        const char *provider_id,
                                        const char *channel_id,
                                        const char *path,
                                        vip_thumbnail_source_t source,
                                        int64_t generated_at,
                                        int64_t last_verified,
                                        vip_error_t *error) {
    if (!db || !provider_id || !channel_id || !path) return VIP_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO thumbnail_metadata(provider_id,channel_id,path,source,generated_at,last_verified)"
        " VALUES(?1,?2,?3,?4,?5,?6)"
        " ON CONFLICT(provider_id,channel_id) DO UPDATE SET path=excluded.path,source=excluded.source,"
        " generated_at=excluded.generated_at,last_verified=excluded.last_verified";
    if (sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao preparar thumbnail");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channel_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, (int)source);
    sqlite3_bind_int64(stmt, 5, generated_at);
    sqlite3_bind_int64(stmt, 6, last_verified);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_DONE) return db_error(db, error, "falha ao salvar thumbnail");
    return VIP_OK;
}

vip_status_t vip_database_load_favorite_flags(vip_database_t *db,
                                               const char *provider_id,
                                               const vip_channel_list_t *channels,
                                               bool *flags,
                                               size_t flags_len,
                                               vip_error_t *error) {
    if (!db || !provider_id || !channels || !flags || flags_len < channels->len)
        return VIP_ERR_INVALID_ARGUMENT;
    memset(flags, 0, flags_len * sizeof(*flags));
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->conn,
            "SELECT channel_id FROM favorites WHERE provider_id=?1",
            -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao ler favoritos");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        if (!id) continue;
        for (size_t i = 0; i < channels->len; ++i) {
            if (channels->items[i].id && strcmp(channels->items[i].id, id) == 0) {
                flags[i] = true;
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    vip_error_clear(error);
    return VIP_OK;
}

char *vip_database_thumbnail_path(vip_database_t *db,
                                  const char *provider_id,
                                  const char *channel_id,
                                  vip_error_t *error) {
    if (!db || !provider_id || !channel_id) return NULL;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->conn,
            "SELECT path FROM thumbnail_metadata WHERE provider_id=?1 AND channel_id=?2",
            -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        db_error(db, error, "falha ao consultar thumbnail");
        return NULL;
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channel_id, -1, SQLITE_TRANSIENT);
    char *path = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *text = sqlite3_column_text(stmt, 0);
        if (text) path = vip_strdup((const char *)text);
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    return path;
}

void vip_profile_list_init(vip_profile_list_t *list) {
    if (list) memset(list, 0, sizeof(*list));
}

static void profile_clear(vip_profile_t *profile) {
    if (!profile) return;
    free(profile->profile_id);
    free(profile->name);
    free(profile->server);
    free(profile->server_alt);
    free(profile->username);
    memset(profile, 0, sizeof(*profile));
}

void vip_profile_list_clear(vip_profile_list_t *list) {
    if (!list) return;
    for (size_t i = 0; i < list->len; ++i) profile_clear(&list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static vip_status_t profile_list_push(vip_profile_list_t *list,
                                      const vip_profile_t *profile,
                                      vip_error_t *error) {
    if (list->len == list->cap) {
        size_t cap = list->cap ? list->cap * 2u : 8u;
        vip_profile_t *items = realloc(list->items, cap * sizeof(*items));
        if (!items) {
            vip_error_set(error, VIP_ERR_NOMEM, "sem memória para perfis salvos");
            return VIP_ERR_NOMEM;
        }
        list->items = items;
        list->cap = cap;
    }
    vip_profile_t copy = {
        .profile_id = vip_strdup(profile->profile_id),
        .name = vip_strdup(profile->name),
        .type = profile->type,
        .server = vip_strdup(profile->server),
        .server_alt = vip_strdup_nullable(profile->server_alt),
        .username = vip_strdup_nullable(profile->username),
        .last_used = profile->last_used,
    };
    if (!copy.profile_id || !copy.name || !copy.server ||
        (profile->server_alt && profile->server_alt[0] && !copy.server_alt) ||
        (profile->username && profile->username[0] && !copy.username)) {
        profile_clear(&copy);
        vip_error_set(error, VIP_ERR_NOMEM, "sem memória para perfil salvo");
        return VIP_ERR_NOMEM;
    }
    list->items[list->len++] = copy;
    return VIP_OK;
}

vip_status_t vip_database_save_profile(vip_database_t *db,
                                       const vip_profile_t *profile,
                                       vip_error_t *error) {
    if (!db || !profile || !profile->profile_id || !profile->profile_id[0] ||
        !profile->name || !profile->name[0] || !profile->server || !profile->server[0])
        return VIP_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO profiles(profile_id,name,type,server,server_alt,username,last_used)"
        " VALUES(?1,?2,?3,?4,?5,?6,unixepoch())"
        " ON CONFLICT(profile_id) DO UPDATE SET name=excluded.name,type=excluded.type,"
        " server=excluded.server,server_alt=excluded.server_alt,username=excluded.username,last_used=unixepoch()";
    if (sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao preparar perfil");
    }
    sqlite3_bind_text(stmt, 1, profile->profile_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, profile->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, (int)profile->type);
    sqlite3_bind_text(stmt, 4, profile->server, -1, SQLITE_TRANSIENT);
    bind_nullable(stmt, 5, profile->server_alt);
    bind_nullable(stmt, 6, profile->username);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_DONE) return db_error(db, error, "falha ao salvar perfil");
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_database_list_profiles(vip_database_t *db,
                                        vip_profile_list_t *out,
                                        vip_error_t *error) {
    if (!db || !out) return VIP_ERR_INVALID_ARGUMENT;
    vip_profile_list_init(out);
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->conn,
            "SELECT profile_id,name,type,server,server_alt,username,last_used FROM profiles ORDER BY last_used DESC,name",
            -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao listar perfis");
    }
    vip_status_t st = VIP_OK;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *pid = sqlite3_column_text(stmt, 0);
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        const unsigned char *server = sqlite3_column_text(stmt, 3);
        if (!pid || !name || !server) continue;
        vip_profile_t profile = {
            .profile_id = (char *)pid,
            .name = (char *)name,
            .type = (vip_profile_type_t)sqlite3_column_int(stmt, 2),
            .server = (char *)server,
            .server_alt = (char *)sqlite3_column_text(stmt, 4),
            .username = (char *)sqlite3_column_text(stmt, 5),
            .last_used = sqlite3_column_int64(stmt, 6),
        };
        st = profile_list_push(out, &profile, error);
        if (st != VIP_OK) break;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (st != VIP_OK) { vip_profile_list_clear(out); return st; }
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_database_touch_profile(vip_database_t *db,
                                        const char *profile_id,
                                        vip_error_t *error) {
    if (!db || !profile_id || !profile_id[0]) return VIP_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->conn, "UPDATE profiles SET last_used=unixepoch() WHERE profile_id=?1", -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao atualizar perfil");
    }
    sqlite3_bind_text(stmt, 1, profile_id, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_DONE) return db_error(db, error, "falha ao atualizar perfil");
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_database_set_progress(vip_database_t *db,
                                       const char *provider_id,
                                       const char *channel_id,
                                       double position_seconds,
                                       double duration_seconds,
                                       bool completed,
                                       vip_error_t *error) {
    if (!db || !provider_id || !channel_id) return VIP_ERR_INVALID_ARGUMENT;
    if (position_seconds < 0.0) position_seconds = 0.0;
    if (duration_seconds < 0.0) duration_seconds = 0.0;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO watch_progress(provider_id,channel_id,position_seconds,duration_seconds,completed,updated_at)"
        " VALUES(?1,?2,?3,?4,?5,unixepoch())"
        " ON CONFLICT(provider_id,channel_id) DO UPDATE SET position_seconds=excluded.position_seconds,"
        " duration_seconds=excluded.duration_seconds,completed=excluded.completed,updated_at=unixepoch()";
    if (sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao preparar progresso");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channel_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, position_seconds);
    sqlite3_bind_double(stmt, 4, duration_seconds);
    sqlite3_bind_int(stmt, 5, completed ? 1 : 0);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_DONE) return db_error(db, error, "falha ao salvar progresso");
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_database_get_progress(vip_database_t *db,
                                       const char *provider_id,
                                       const char *channel_id,
                                       vip_watch_progress_t *out,
                                       vip_error_t *error) {
    if (!db || !provider_id || !channel_id || !out) return VIP_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->conn,
            "SELECT position_seconds,duration_seconds,completed,updated_at FROM watch_progress WHERE provider_id=?1 AND channel_id=?2",
            -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao consultar progresso");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channel_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->position_seconds = sqlite3_column_double(stmt, 0);
        out->duration_seconds = sqlite3_column_double(stmt, 1);
        out->completed = sqlite3_column_int(stmt, 2) != 0;
        out->updated_at = sqlite3_column_int64(stmt, 3);
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_database_load_progress(vip_database_t *db,
                                        const char *provider_id,
                                        const vip_channel_list_t *channels,
                                        vip_watch_progress_t *progress,
                                        size_t progress_len,
                                        vip_error_t *error) {
    if (!db || !provider_id || !channels || !progress || progress_len < channels->len)
        return VIP_ERR_INVALID_ARGUMENT;
    memset(progress, 0, progress_len * sizeof(*progress));
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->conn,
            "SELECT channel_id,position_seconds,duration_seconds,completed,updated_at FROM watch_progress WHERE provider_id=?1",
            -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao carregar progresso");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        if (!id) continue;
        for (size_t i = 0; i < channels->len; ++i) {
            if (channels->items[i].id && strcmp(channels->items[i].id, id) == 0) {
                progress[i].position_seconds = sqlite3_column_double(stmt, 1);
                progress[i].duration_seconds = sqlite3_column_double(stmt, 2);
                progress[i].completed = sqlite3_column_int(stmt, 3) != 0;
                progress[i].updated_at = sqlite3_column_int64(stmt, 4);
                break;
            }
        }
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_database_set_series_progress(vip_database_t *db,
                                              const char *provider_id,
                                              const char *series_id,
                                              const char *last_episode_id,
                                              int watched_count,
                                              int total_count,
                                              vip_error_t *error) {
    if (!db || !provider_id || !series_id) return VIP_ERR_INVALID_ARGUMENT;
    if (watched_count < 0) watched_count = 0;
    if (total_count < 0) total_count = 0;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO series_progress(provider_id,series_id,last_episode_id,watched_count,total_count,updated_at)"
        " VALUES(?1,?2,?3,?4,?5,unixepoch())"
        " ON CONFLICT(provider_id,series_id) DO UPDATE SET last_episode_id=excluded.last_episode_id,"
        " watched_count=excluded.watched_count,total_count=excluded.total_count,updated_at=unixepoch()";
    if (sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao preparar progresso da série");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, series_id, -1, SQLITE_TRANSIENT);
    bind_nullable(stmt, 3, last_episode_id);
    sqlite3_bind_int(stmt, 4, watched_count);
    sqlite3_bind_int(stmt, 5, total_count);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_DONE) return db_error(db, error, "falha ao salvar progresso da série");
    vip_error_clear(error);
    return VIP_OK;
}

void vip_series_progress_clear(vip_series_progress_t *progress) {
    if (!progress) return;
    free(progress->last_episode_id);
    memset(progress, 0, sizeof(*progress));
}

vip_status_t vip_database_get_series_progress(vip_database_t *db,
                                              const char *provider_id,
                                              const char *series_id,
                                              vip_series_progress_t *out,
                                              vip_error_t *error) {
    if (!db || !provider_id || !series_id || !out) return VIP_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->conn,
            "SELECT last_episode_id,watched_count,total_count,updated_at FROM series_progress WHERE provider_id=?1 AND series_id=?2",
            -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao consultar progresso da série");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, series_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *last = sqlite3_column_text(stmt, 0);
        if (last) out->last_episode_id = vip_strdup((const char *)last);
        out->watched_count = sqlite3_column_int(stmt, 1);
        out->total_count = sqlite3_column_int(stmt, 2);
        out->updated_at = sqlite3_column_int64(stmt, 3);
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    vip_error_clear(error);
    return VIP_OK;
}

vip_status_t vip_database_set_media_metadata(vip_database_t *db,
                                             const char *provider_id,
                                             const char *media_id,
                                             const vip_media_metadata_t *metadata,
                                             vip_error_t *error) {
    if (!db || !provider_id || !provider_id[0] || !media_id || !media_id[0] || !metadata)
        return VIP_ERR_INVALID_ARGUMENT;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO media_metadata(provider_id,media_id,plot,cover_url,backdrop_url,genre,release_date,rating,duration,[cast],director,youtube_trailer,updated_at)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,unixepoch())"
        " ON CONFLICT(provider_id,media_id) DO UPDATE SET plot=excluded.plot,cover_url=excluded.cover_url,"
        " backdrop_url=excluded.backdrop_url,genre=excluded.genre,release_date=excluded.release_date,"
        " rating=excluded.rating,duration=excluded.duration,[cast]=excluded.[cast],director=excluded.director,"
        " youtube_trailer=excluded.youtube_trailer,updated_at=unixepoch()";
    if (sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao preparar cache de metadados");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, media_id, -1, SQLITE_TRANSIENT);
    bind_nullable(stmt, 3, metadata->plot);
    bind_nullable(stmt, 4, metadata->cover_url);
    bind_nullable(stmt, 5, metadata->backdrop_url);
    bind_nullable(stmt, 6, metadata->genre);
    bind_nullable(stmt, 7, metadata->release_date);
    bind_nullable(stmt, 8, metadata->rating);
    bind_nullable(stmt, 9, metadata->duration);
    bind_nullable(stmt, 10, metadata->cast);
    bind_nullable(stmt, 11, metadata->director);
    bind_nullable(stmt, 12, metadata->youtube_trailer);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_DONE) return db_error(db, error, "falha ao salvar cache de metadados");
    vip_error_clear(error);
    return VIP_OK;
}

static char *column_strdup(sqlite3_stmt *stmt, int column) {
    const unsigned char *text = sqlite3_column_text(stmt, column);
    return text && text[0] ? vip_strdup((const char *)text) : NULL;
}

vip_status_t vip_database_get_media_metadata(vip_database_t *db,
                                             const char *provider_id,
                                             const char *media_id,
                                             vip_media_metadata_t *metadata_out,
                                             int64_t *updated_at_out,
                                             bool *found_out,
                                             vip_error_t *error) {
    if (!db || !provider_id || !provider_id[0] || !media_id || !media_id[0] || !metadata_out)
        return VIP_ERR_INVALID_ARGUMENT;
    vip_media_metadata_init(metadata_out);
    if (updated_at_out) *updated_at_out = 0;
    if (found_out) *found_out = false;
    pthread_mutex_lock(&db->mutex);
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT plot,cover_url,backdrop_url,genre,release_date,rating,duration,[cast],director,youtube_trailer,updated_at"
        " FROM media_metadata WHERE provider_id=?1 AND media_id=?2";
    if (sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&db->mutex);
        return db_error(db, error, "falha ao consultar cache de metadados");
    }
    sqlite3_bind_text(stmt, 1, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, media_id, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        metadata_out->plot = column_strdup(stmt, 0);
        metadata_out->cover_url = column_strdup(stmt, 1);
        metadata_out->backdrop_url = column_strdup(stmt, 2);
        metadata_out->genre = column_strdup(stmt, 3);
        metadata_out->release_date = column_strdup(stmt, 4);
        metadata_out->rating = column_strdup(stmt, 5);
        metadata_out->duration = column_strdup(stmt, 6);
        metadata_out->cast = column_strdup(stmt, 7);
        metadata_out->director = column_strdup(stmt, 8);
        metadata_out->youtube_trailer = column_strdup(stmt, 9);
        if (updated_at_out) *updated_at_out = sqlite3_column_int64(stmt, 10);
        if (found_out) *found_out = true;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db->mutex);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        vip_media_metadata_clear(metadata_out);
        return db_error(db, error, "falha ao consultar cache de metadados");
    }
    vip_error_clear(error);
    return VIP_OK;
}
