#!/usr/bin/env python3
from pathlib import Path

path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

def replace_once(old, new, label):
    global s
    if old not in s:
        raise SystemExit(f'missing patch anchor: {label}')
    s = s.replace(old, new, 1)

# 1. M3U login: split the flat playlist into live/VOD/series catalogs.
old = '''    if (job->mode == LOGIN_M3U) {
        fprintf(stderr, "[login] carregando playlist M3U\\n");
        st = vip_m3u_load(job->server, &cats, &channels, provider_id, &error);
    } else {
        fprintf(stderr, "[login] conectando ao servidor primário\\n");
'''
new = '''    if (job->mode == LOGIN_M3U) {
        fprintf(stderr, "[login] carregando playlist M3U\\n");
        st = vip_m3u_load(job->server, &cats, &channels, provider_id, &error);
        if (st == VIP_OK) {
            vip_category_list_t live_cats; vip_category_list_init(&live_cats);
            vip_channel_list_t live_channels; vip_channel_list_init(&live_channels);
            vip_category_list_init(&vod.categories); vip_channel_list_init(&vod.channels);
            vip_category_list_init(&series.categories); vip_channel_list_init(&series.channels);
            st = vip_m3u_split_catalog(&cats, &channels,
                                       &live_cats, &live_channels,
                                       &vod.categories, &vod.channels,
                                       &series.categories, &series.channels,
                                       &error);
            if (st == VIP_OK) {
                vip_category_list_clear(&cats);
                vip_channel_list_clear(&channels);
                cats = live_cats; memset(&live_cats, 0, sizeof(live_cats));
                channels = live_channels; memset(&live_channels, 0, sizeof(live_channels));
                vod.loaded = vod.channels.len > 0u;
                series.loaded = series.channels.len > 0u;
                fprintf(stderr, "[catalog] M3U separado: TV=%zu Filmes=%zu Séries=%zu\\n",
                        channels.len, vod.channels.len, series.channels.len);
            }
            vip_category_list_clear(&live_cats);
            vip_channel_list_clear(&live_channels);
        }
    } else {
        fprintf(stderr, "[login] conectando ao servidor primário\\n");
'''
replace_once(old, new, 'M3U login split')

old = '''        if (job->mode == LOGIN_XTREAM) {
            a->catalogs[CONTENT_VOD] = vod; memset(&vod, 0, sizeof(vod));
            a->catalogs[CONTENT_SERIES] = series; memset(&series, 0, sizeof(series));
        } else {
            memset(&a->catalogs[CONTENT_VOD], 0, sizeof(a->catalogs[CONTENT_VOD]));
            memset(&a->catalogs[CONTENT_SERIES], 0, sizeof(a->catalogs[CONTENT_SERIES]));
            vip_category_list_init(&a->catalogs[CONTENT_VOD].categories);
            vip_channel_list_init(&a->catalogs[CONTENT_VOD].channels);
            vip_category_list_init(&a->catalogs[CONTENT_SERIES].categories);
            vip_channel_list_init(&a->catalogs[CONTENT_SERIES].channels);
        }
'''
new = '''        a->catalogs[CONTENT_VOD] = vod; memset(&vod, 0, sizeof(vod));
        a->catalogs[CONTENT_SERIES] = series; memset(&series, 0, sizeof(series));
'''
replace_once(old, new, 'catalog assignment')

# 2. Series root for M3U: deduplicate episodes into one visible card per series.
start = s.index('static void rebuild_filter(app_t *a) {')
end = s.index('\nstatic void recalc_category_counts', start)
new_filter = r'''static bool m3u_series_root(const app_t *a) {
    return a && a->login_mode == LOGIN_M3U && a->content_kind == CONTENT_SERIES &&
           !a->series_episode_mode;
}

static uint64_t folded_name_hash(const char *text) {
    uint64_t h = UINT64_C(14695981039346656037);
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        unsigned char c = *p++;
        if (c < 0x80u) c = (unsigned char)tolower(c);
        h ^= c;
        h *= UINT64_C(1099511628211);
    }
    return h ? h : UINT64_C(1);
}

static const char *m3u_series_display_name(const app_t *a, const vip_channel_t *ch,
                                           char *buffer, size_t cap) {
    if (m3u_series_root(a) && ch &&
        vip_m3u_parse_episode_label(ch->name, buffer, cap, NULL, NULL))
        return buffer;
    return ch && ch->name ? ch->name : "";
}

static void rebuild_filter(app_t *a) {
    pthread_mutex_lock(&a->data_mutex);
    size_t need = ACTIVE_CHANNELS(a).len;
    if (need > a->filtered_cap) {
        size_t cap = need ? need : 1;
        size_t *p = realloc(a->filtered, cap * sizeof(*p));
        if (p) { a->filtered = p; a->filtered_cap = cap; }
    }
    a->filtered_len = 0;
    const char *cat = NULL;
    if (a->selected_category >= 0 && (size_t)a->selected_category < ACTIVE_CATEGORIES(a).len)
        cat = ACTIVE_CATEGORIES(a).items[a->selected_category].id;

    bool group_series = m3u_series_root(a);
    uint64_t *seen = NULL;
    size_t seen_cap = 0u;
    if (group_series && need > 0u) {
        seen_cap = 16u;
        while (seen_cap < need * 2u && seen_cap < (SIZE_MAX / 2u)) seen_cap <<= 1u;
        seen = calloc(seen_cap, sizeof(*seen));
    }

    if (a->filtered) {
        for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i) {
            vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[i];
            if (cat && (!ch->category_id || strcmp(ch->category_id, cat) != 0)) continue;
            if (a->favorites_only && (!a->favorite_flags || !a->favorite_flags[i])) continue;
            char grouped_name[256];
            const char *name = m3u_series_display_name(a, ch, grouped_name, sizeof(grouped_name));
            if (!contains_ascii_case(name, a->search)) continue;
            if (group_series && seen && seen_cap > 0u) {
                uint64_t hash = folded_name_hash(name);
                size_t slot = (size_t)hash & (seen_cap - 1u);
                while (seen[slot] != 0u && seen[slot] != hash)
                    slot = (slot + 1u) & (seen_cap - 1u);
                if (seen[slot] == hash) continue;
                seen[slot] = hash;
            }
            a->filtered[a->filtered_len++] = i;
        }
    }
    free(seen);
    pthread_mutex_unlock(&a->data_mutex);
    a->grid_scroll = 0;
    a->focused_filtered = 0;
}
'''
s = s[:start] + new_filter + s[end:]

# 3. Local M3U series navigator: series -> season -> episode.
marker = 'static void activate_item(app_t *a, size_t channel_index) {'
idx = s.index(marker)
local_series = r'''static bool same_text_case(const char *a, const char *b) {
    return a && b && strcasecmp(a, b) == 0;
}

static bool same_category(const char *a, const char *b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

static bool start_m3u_series_load(app_t *a, size_t channel_index) {
    if (!a || !m3u_series_root(a) || channel_index >= a->catalogs[CONTENT_SERIES].channels.len)
        return false;
    vip_channel_t *selected = &a->catalogs[CONTENT_SERIES].channels.items[channel_index];
    char series_name[256];
    int selected_season = 0, selected_episode = 0;
    if (!vip_m3u_parse_episode_label(selected->name, series_name, sizeof(series_name),
                                     &selected_season, &selected_episode))
        return false;
    (void)selected_season; (void)selected_episode;

    int seasons[256];
    size_t season_count = 0u;
    for (size_t i = 0u; i < a->catalogs[CONTENT_SERIES].channels.len; ++i) {
        vip_channel_t *ch = &a->catalogs[CONTENT_SERIES].channels.items[i];
        char candidate[256]; int season = 0, episode = 0;
        if (!same_category(ch->category_id, selected->category_id) ||
            !vip_m3u_parse_episode_label(ch->name, candidate, sizeof(candidate), &season, &episode) ||
            !same_text_case(candidate, series_name)) continue;
        bool exists = false;
        for (size_t j = 0u; j < season_count; ++j) if (seasons[j] == season) { exists = true; break; }
        if (!exists && season_count < sizeof(seasons)/sizeof(seasons[0])) seasons[season_count++] = season;
    }
    if (season_count == 0u) return false;
    for (size_t i = 1u; i < season_count; ++i) {
        int value = seasons[i]; size_t j = i;
        while (j > 0u && seasons[j-1u] > value) { seasons[j] = seasons[j-1u]; --j; }
        seasons[j] = value;
    }

    vip_category_list_t cats; vip_category_list_init(&cats);
    vip_channel_list_t episodes; vip_channel_list_init(&episodes);
    vip_channel_list_t cards; vip_channel_list_init(&cards);
    vip_error_t error = {0};
    vip_status_t st = VIP_OK;
    uint64_t title_hash = folded_name_hash(series_name);

    for (size_t si = 0u; si < season_count && st == VIP_OK; ++si) {
        char cat_id[96]; char cat_name[96]; char card_id[128];
        snprintf(cat_id, sizeof(cat_id), "m3u-season:%016llx:%d",
                 (unsigned long long)title_hash, seasons[si]);
        snprintf(cat_name, sizeof(cat_name), seasons[si] == 0 ? "Especiais" : "Temporada %d", seasons[si]);
        vip_category_t cat = {
            .provider_id = selected->provider_id,
            .id = cat_id,
            .name = cat_name,
            .position = (int)si,
        };
        st = vip_category_list_push(&cats, &cat, &error);
        if (st != VIP_OK) break;
        snprintf(card_id, sizeof(card_id), "m3u-series-season:%016llx:%d",
                 (unsigned long long)title_hash, seasons[si]);
        vip_channel_t card = {
            .provider_id = selected->provider_id,
            .id = card_id,
            .category_id = cat_id,
            .name = cat_name,
            .logo_url = selected->logo_url,
            .stream_url = "series://season",
            .epg_channel_id = NULL,
            .position = (int)si,
        };
        st = vip_channel_list_push(&cards, &card, &error);
        if (st != VIP_OK) break;

        for (size_t i = 0u; i < a->catalogs[CONTENT_SERIES].channels.len && st == VIP_OK; ++i) {
            vip_channel_t *ch = &a->catalogs[CONTENT_SERIES].channels.items[i];
            char candidate[256]; int season = 0, episode = 0;
            if (!same_category(ch->category_id, selected->category_id) ||
                !vip_m3u_parse_episode_label(ch->name, candidate, sizeof(candidate), &season, &episode) ||
                season != seasons[si] || !same_text_case(candidate, series_name)) continue;
            vip_channel_t copy = *ch;
            copy.category_id = cat_id;
            copy.position = episode;
            st = vip_channel_list_push(&episodes, &copy, &error);
        }
    }

    if (st != VIP_OK || cards.len == 0u || episodes.len == 0u) {
        vip_category_list_clear(&cats); vip_channel_list_clear(&episodes); vip_channel_list_clear(&cards);
        snprintf(a->status, sizeof(a->status), "Falha ao organizar série M3U: %s",
                 error.message[0] ? error.message : "dados insuficientes");
        return false;
    }

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
    a->input_focus = INPUT_SEARCH;
    snprintf(a->series_title, sizeof(a->series_title), "%s", series_name);
    snprintf(a->series_parent_id, sizeof(a->series_parent_id), "%s", selected->id ? selected->id : "");
    snprintf(a->status, sizeof(a->status), "%zu temporadas • %zu episódios", cards.len, episodes.len);
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
    return true;
}

'''
s = s[:idx] + local_series + s[idx:]

start = s.index('static void activate_item(app_t *a, size_t channel_index) {')
end = s.index('\nstatic void request_paste', start)
new_activate = r'''static void activate_item(app_t *a, size_t channel_index) {
    if (a->content_kind == CONTENT_SERIES) {
        if (a->series_season_select) {
            select_season(a, channel_index);
            return;
        }
        if (!a->series_episode_mode) {
            if (a->login_mode == LOGIN_M3U) {
                if (start_m3u_series_load(a, channel_index)) return;
            } else {
                start_series_load(a, channel_index);
                return;
            }
        }
    }
    enter_player(a, channel_index);
}
'''
s = s[:start] + new_activate + s[end:]

# 4. Series cards show the series name, not SxxExx episode names.
old = '''            char title[160]; bounded_text(title, sizeof(title), ch->name, layout.mode == ART_PORTRAIT ? 28 : 42);
            draw_text_font(a, focused ? a->font_heading : a->font, cx + 8, cy + layout.art_h + 25, title, a->colors.text);
'''
new = '''            char grouped_title[256];
            const char *display_title = m3u_series_display_name(a, ch, grouped_title, sizeof(grouped_title));
            char title[160]; bounded_text(title, sizeof(title), display_title, layout.mode == ART_PORTRAIT ? 28 : 42);
            draw_text_font(a, focused ? a->font_heading : a->font, cx + 8, cy + layout.art_h + 25, title, a->colors.text);
'''
replace_once(old, new, 'series card title')

# 5. Strong search-focus affordance: brighter field, accent rail and caret.
start = s.index('static void draw_input(app_t *a, int x, int y, int w, int h, const char *value,')
end = s.index('\nstatic void draw_login', start)
new_draw_input = r'''static void draw_input(app_t *a, int x, int y, int w, int h, const char *value,
                       const char *placeholder, int focus_id, bool password) {
    bool focused = a->input_focus == focus_id;
    bool search_focused = focused && focus_id == INPUT_SEARCH;
    fill_round_rect(a, x + 2, y + 3, w, h, 12, a->colors.black);
    fill_round_rect(a, x, y, w, h, 12, search_focused ? a->colors.accent2 : a->colors.panel2);
    stroke_round_rect(a, x, y, w, h, 12, focused ? a->colors.accent : a->colors.border);
    if (focused) {
        stroke_round_rect(a, x + 2, y + 2, w - 4, h - 4, 10,
                          search_focused ? a->colors.accent : a->colors.accent2);
        if (search_focused) fill_round_rect(a, x + 5, y + 8, 4, h - 16, 2, a->colors.accent);
    }
    const char *text = value && value[0] ? value : (search_focused ? "Digite para buscar..." : placeholder);
    char masked[256];
    if (password && value && value[0]) {
        size_t n = strlen(value); if (n > sizeof(masked)-1) n = sizeof(masked)-1;
        memset(masked, '*', n); masked[n] = '\0'; text = masked;
    }
    draw_text(a, x + 16, y + h/2 + 6, text,
              search_focused || (value && value[0]) ? a->colors.text : a->colors.muted);
    if (search_focused) {
        int caret_x = x + 16 + text_width(a, value && value[0] ? value : "");
        if (caret_x < x + 16) caret_x = x + 16;
        if (caret_x > x + w - 18) caret_x = x + w - 18;
        set_fg(a, a->colors.accent);
        XDrawLine(a->dpy, draw_target(a), a->gc, caret_x, y + 12, caret_x, y + h - 12);
    }
}
'''
s = s[:start] + new_draw_input + s[end:]

path.write_text(s)
print('patched', path)
