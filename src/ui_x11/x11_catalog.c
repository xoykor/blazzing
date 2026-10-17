/* SPDX-License-Identifier: MIT */
#include "x11_app_internal.h"

static bool contains_ascii_case(const char *haystack, const char *needle) {
    if (!needle || !needle[0])
        return true;
    if (!haystack)
        return false;
    size_t nn = strlen(needle);
    for (const char *h = haystack; *h; ++h) {
        size_t i = 0;
        while (i < nn && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i]))
            ++i;
        if (i == nn)
            return true;
    }
    return false;
}

/* Handle the m3u series root operation. */
bool m3u_series_root(const app_t *a) {
    return a && a->login_mode == LOGIN_M3U && a->content_kind == CONTENT_SERIES && !a->series_episode_mode;
}

/* Return the name of hash in the folded. */
uint64_t folded_name_hash(const char *text) {
    uint64_t h = UINT64_C(14695981039346656037);
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        unsigned char c = *p++;
        if (c < 0x80u)
            c = (unsigned char)tolower(c);
        h ^= c;
        h *= UINT64_C(1099511628211);
    }
    return h ? h : UINT64_C(1);
}

/* Return the name of the requested state in the m3u series display. */
const char *m3u_series_display_name(const app_t *a, const vip_channel_t *ch, char *buffer,
                                           size_t cap) {
    if (m3u_series_root(a) && ch && vip_m3u_parse_episode_label(ch->name, buffer, cap, NULL, NULL))
        return buffer;
    return ch && ch->name ? ch->name : "";
}

/* Extract a numeric season/episode key from either M3U labels or Xtream fields. */
static void episode_sort_key(const vip_channel_t *ch, int *season_out, int *episode_out) {
    int season = INT_MAX;
    int episode = INT_MAX;
    if (ch) {
        char series_name[256];
        int parsed_season = 0;
        int parsed_episode = 0;
        if (vip_m3u_parse_episode_label(ch->name, series_name, sizeof(series_name), &parsed_season,
                                        &parsed_episode)) {
            season = parsed_season;
            episode = parsed_episode;
        } else {
            if (ch->category_id && ch->category_id[0]) {
                char *end = NULL;
                long parsed = strtol(ch->category_id, &end, 10);
                if (end != ch->category_id && end && *end == '\0' && parsed >= 0 && parsed <= INT_MAX)
                    season = (int)parsed;
            }
            if (ch->position >= 0)
                episode = ch->position;
        }
    }
    if (season_out)
        *season_out = season;
    if (episode_out)
        *episode_out = episode;
}

/* Sort episodes naturally as T1E1, T1E2, ... instead of provider insertion order. */
static int compare_episode_channels(const void *lhs, const void *rhs) {
    const vip_channel_t *a = lhs;
    const vip_channel_t *b = rhs;
    int as = INT_MAX, ae = INT_MAX, bs = INT_MAX, be = INT_MAX;
    episode_sort_key(a, &as, &ae);
    episode_sort_key(b, &bs, &be);
    if (as != bs)
        return as < bs ? -1 : 1;
    if (ae != be)
        return ae < be ? -1 : 1;
    const char *an = a && a->name ? a->name : "";
    const char *bn = b && b->name ? b->name : "";
    return strcasecmp(an, bn);
}

/* Sort season cards numerically while preserving their original category index in position. */
static long season_category_number(const vip_channel_t *channel) {
    if (!channel || !channel->category_id || !channel->category_id[0])
        return LONG_MAX;
    char *end = NULL;
    long parsed = strtol(channel->category_id, &end, 10);
    return end != channel->category_id && end && *end == '\0' ? parsed : LONG_MAX;
}

static int compare_long_values(long a, long b) {
    if (a == b)
        return 0;
    return a < b ? -1 : 1;
}

static int compare_int_values(int a, int b) {
    if (a == b)
        return 0;
    return a < b ? -1 : 1;
}

static int compare_season_channels(const void *lhs, const void *rhs) {
    const vip_channel_t *a = lhs;
    const vip_channel_t *b = rhs;
    int by_category = compare_long_values(season_category_number(a), season_category_number(b));
    if (by_category)
        return by_category;
    int by_position = compare_int_values(a ? a->position : INT_MAX, b ? b->position : INT_MAX);
    if (by_position)
        return by_position;
    return strcasecmp(a && a->name ? a->name : "", b && b->name ? b->name : "");
}

void sort_episode_channels(vip_channel_list_t *channels) {
    if (channels && channels->len > 1u)
        qsort(channels->items, channels->len, sizeof(channels->items[0]), compare_episode_channels);
}

void sort_season_channels(vip_channel_list_t *channels) {
    if (channels && channels->len > 1u)
        qsort(channels->items, channels->len, sizeof(channels->items[0]), compare_season_channels);
}

static bool ensure_filtered_capacity(app_t *a, size_t need) {
    if (need <= a->filtered_cap)
        return true;
    size_t cap = need ? need : 1u;
    size_t *items = realloc(a->filtered, cap * sizeof(*items));
    if (!items)
        return false;
    a->filtered = items;
    a->filtered_cap = cap;
    return true;
}

/* Return the currently selected category id, or NULL for all categories. */
static const char *selected_filter_category(const app_t *a) {
    if (a->selected_category < 0 ||
        (size_t)a->selected_category >= ACTIVE_CATEGORIES(a).len)
        return NULL;
    return ACTIVE_CATEGORIES(a).items[a->selected_category].id;
}

/* Allocate the open-addressed hash table used to collapse M3U series rows. */
static uint64_t *create_series_seen_table(size_t need, size_t *capacity) {
    *capacity = 0u;
    if (need == 0u)
        return NULL;

    size_t cap = 16u;
    while (cap < need * 2u && cap < (SIZE_MAX / 2u))
        cap <<= 1u;
    uint64_t *seen = calloc(cap, sizeof(*seen));
    if (!seen)
        return NULL;
    *capacity = cap;
    return seen;
}

/* Return whether a channel passes category, favorites and text filters. */
static bool channel_matches_filter(const app_t *a, const vip_channel_t *ch,
                                   size_t index, const char *category,
                                   const char *display_name) {
    if (category && (!ch->category_id || strcmp(ch->category_id, category) != 0))
        return false;
    if (a->favorites_only && (!a->favorite_flags || !a->favorite_flags[index]))
        return false;
    return contains_ascii_case(display_name, a->search);
}

/* Record one grouped series name and return whether it was already present. */
static bool grouped_series_seen(uint64_t *seen, size_t capacity, const char *name) {
    if (!seen || capacity == 0u)
        return false;
    uint64_t hash = folded_name_hash(name);
    size_t slot = (size_t)hash & (capacity - 1u);
    while (seen[slot] != 0u && seen[slot] != hash)
        slot = (slot + 1u) & (capacity - 1u);
    if (seen[slot] == hash)
        return true;
    seen[slot] = hash;
    return false;
}

/* Populate filtered indexes while data_mutex is held. */
static void rebuild_filter_locked(app_t *a, const char *category,
                                  uint64_t *seen, size_t seen_cap) {
    for (size_t i = 0u; i < ACTIVE_CHANNELS(a).len; ++i) {
        vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[i];
        char grouped_name[256];
        const char *name = m3u_series_display_name(a, ch, grouped_name, sizeof(grouped_name));
        if (!channel_matches_filter(a, ch, i, category, name))
            continue;
        if (grouped_series_seen(seen, seen_cap, name))
            continue;
        a->filtered[a->filtered_len++] = i;
    }
}

/* Reset scroll, hover and focus after the filtered collection changes. */
static void reset_filter_view_state(app_t *a) {
    a->grid_scroll = 0;
    a->grid_scroll_target = 0;
    a->grid_scroll_animating = false;
    a->grid_scroll_last_ms = monotonic_ms();
    a->hovered_card_valid = false;
    a->hovered_control = HOVER_NONE;
    vip_ui_motion_init(&a->hover_motion, 0.0f, a->grid_scroll_last_ms);
    vip_ui_motion_init(&a->control_motion, 0.0f, a->grid_scroll_last_ms);
    a->ui_motion_active = false;
    a->focused_filtered = 0;
}

/* Rebuild filter. */
void rebuild_filter(app_t *a) {
    pthread_mutex_lock(&a->data_mutex);
    size_t need = ACTIVE_CHANNELS(a).len;
    a->filtered_len = 0u;

    if (ensure_filtered_capacity(a, need)) {
        const char *category = selected_filter_category(a);
        size_t seen_cap = 0u;
        uint64_t *seen = m3u_series_root(a) ?
            create_series_seen_table(need, &seen_cap) : NULL;
        rebuild_filter_locked(a, category, seen, seen_cap);
        free(seen);
    }

    pthread_mutex_unlock(&a->data_mutex);
    reset_filter_view_state(a);
}

/* Handle the recalc category counts operation. */
void recalc_category_counts(app_t *a) {
    free(a->category_counts);
    a->category_counts = NULL;
    if (ACTIVE_CATEGORIES(a).len == 0)
        return;
    a->category_counts = calloc(ACTIVE_CATEGORIES(a).len, sizeof(*a->category_counts));
    if (!a->category_counts)
        return;
    for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i) {
        const char *cid = ACTIVE_CHANNELS(a).items[i].category_id;
        if (!cid)
            continue;
        for (size_t j = 0; j < ACTIVE_CATEGORIES(a).len; ++j) {
            if (strcmp(cid, ACTIVE_CATEGORIES(a).items[j].id) == 0) {
                ++a->category_counts[j];
                break;
            }
        }
    }
}

/* Count the requested state in the favorite. */
size_t favorite_count(app_t *a) {
    size_t n = 0;
    if (!a->favorite_flags)
        return 0;
    for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i)
        if (a->favorite_flags[i])
            ++n;
    return n;
}

/* Load media state. */
void load_media_state(app_t *a) {
    free(a->favorite_flags);
    free(a->progress_flags);
    free(a->series_watched);
    free(a->series_total);
    a->favorite_flags = NULL;
    a->progress_flags = NULL;
    a->series_watched = NULL;
    a->series_total = NULL;
    size_t count = ACTIVE_CHANNELS(a).len;
    if (count == 0u)
        return;
    a->favorite_flags = calloc(count, sizeof(*a->favorite_flags));
    a->progress_flags = calloc(count, sizeof(*a->progress_flags));
    if (a->content_kind == CONTENT_SERIES && !a->series_episode_mode) {
        a->series_watched = calloc(count, sizeof(*a->series_watched));
        a->series_total = calloc(count, sizeof(*a->series_total));
    }
    if (!a->db || !ACTIVE_CHANNELS(a).items[0].provider_id)
        return;
    vip_error_t error = {0};
    if (a->favorite_flags)
        (void)vip_database_load_favorite_flags(a->db, ACTIVE_CHANNELS(a).items[0].provider_id,
                                               &ACTIVE_CHANNELS(a), a->favorite_flags, count, &error);
    if (a->progress_flags &&
        (a->content_kind == CONTENT_VOD || (a->series_episode_mode && !a->series_season_select))) {
        vip_error_clear(&error);
        (void)vip_database_load_progress(a->db, ACTIVE_CHANNELS(a).items[0].provider_id, &ACTIVE_CHANNELS(a),
                                         a->progress_flags, count, &error);
    }
    if (a->series_watched && a->series_total) {
        vip_error_clear(&error);
        (void)vip_database_load_series_progress(a->db, ACTIVE_CHANNELS(a).items[0].provider_id,
                                                &ACTIVE_CHANNELS(a), a->series_watched, a->series_total,
                                                count, &error);
    }
}

/* Toggle favorite. */
void toggle_favorite(app_t *a, size_t channel_index) {
    if (channel_index >= ACTIVE_CHANNELS(a).len || !a->favorite_flags)
        return;
    bool value = !a->favorite_flags[channel_index];
    a->favorite_flags[channel_index] = value;
    if (a->db) {
        vip_error_t error = {0};
        vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[channel_index];
        (void)vip_database_set_favorite(a->db, ch->provider_id, ch->id, value, &error);
    }
    snprintf(a->toast, sizeof(a->toast), "%s", value ? "Adicionado aos favoritos" : "Removido dos favoritos");
    a->toast_until_ms = monotonic_ms() + 1600;
    if (a->favorites_only)
        rebuild_filter(a);
}

/* Switch content. */
void switch_content(app_t *a, content_kind_t kind) {
    if (!a || kind < CONTENT_LIVE || kind > CONTENT_SERIES)
        return;
    if (atomic_load(&a->series_running) || a->series_thread_started)
        return;
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
    browse_sync_input_focus(a);
    free(a->favorite_flags);
    a->favorite_flags = NULL;
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}

/* List the requested state using the return from episode. */
void return_from_episode_list(app_t *a) {
    if (!a || !a->series_episode_mode)
        return;
    if (!a->series_season_select) {
        a->series_season_select = true;
        a->selected_category = -1;
        a->category_scroll = 0;
        a->grid_scroll = 0;
        a->focused_filtered = 0;
        a->search[0] = '\0';
        browse_focus_grid(a);
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
    browse_focus_grid(a);
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}

/* Handle the thumbnail ready operation. */


void choose_category(app_t *a, int index) {
    a->selected_category = index;
    rebuild_filter(a);
}

/* Initialize x11. */

static void select_season(app_t *a, size_t season_channel_index) {
    if (!a || !a->series_season_select || season_channel_index >= a->season_channels.len)
        return;
    vip_channel_t *season = &a->season_channels.items[season_channel_index];
    int season_index = season->position;
    if (season_index < 0 || (size_t)season_index >= a->episode_categories.len)
        return;
    a->series_season_select = false;
    a->selected_category = season_index;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    browse_focus_grid(a);
    snprintf(a->status, sizeof(a->status), "%s • %s", a->series_title,
             a->episode_categories.items[season_index].name);
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}


/* Handle the activate item operation. */
void activate_item(app_t *a, size_t channel_index) {
    if (a->content_kind == CONTENT_SERIES) {
        if (a->series_season_select) {
            select_season(a, channel_index);
            return;
        }
        if (!a->series_episode_mode) {
            if (a->login_mode == LOGIN_M3U) {
                if (start_m3u_series_load(a, channel_index))
                    return;
            } else {
                start_series_load(a, channel_index);
                return;
            }
        }
    }
    enter_player(a, channel_index);
}

/* Handle the refresh profiles operation. */
