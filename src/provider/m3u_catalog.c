/* SPDX-License-Identifier: MIT */
/*
 * M3U content classification plus inferred series/season/episode grouping.
 *
 * Comments intentionally cover straightforward helpers as well as subtle
 * behavior so a maintainer can follow intent without reverse-engineering it.
 */
#define _POSIX_C_SOURCE 200809L
#include "visual_iptv/provider_m3u.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Implement the fold_accent helper. */
static char fold_accent(unsigned char second) {
    switch (second) {
    case 0x80:
    case 0x81:
    case 0x82:
    case 0x83:
    case 0x84:
    case 0xa0:
    case 0xa1:
    case 0xa2:
    case 0xa3:
    case 0xa4:
        return 'a';
    case 0x88:
    case 0x89:
    case 0x8a:
    case 0x8b:
    case 0xa8:
    case 0xa9:
    case 0xaa:
    case 0xab:
        return 'e';
    case 0x8c:
    case 0x8d:
    case 0x8e:
    case 0x8f:
    case 0xac:
    case 0xad:
    case 0xae:
    case 0xaf:
        return 'i';
    case 0x92:
    case 0x93:
    case 0x94:
    case 0x95:
    case 0x96:
    case 0xb2:
    case 0xb3:
    case 0xb4:
    case 0xb5:
    case 0xb6:
        return 'o';
    case 0x99:
    case 0x9a:
    case 0x9b:
    case 0x9c:
    case 0xb9:
    case 0xba:
    case 0xbb:
    case 0xbc:
        return 'u';
    case 0x87:
    case 0xa7:
        return 'c';
    default:
        return 0;
    }
}

/* Normalize ascii. */
static void normalize_ascii(const char *src, char *dst, size_t cap) {
    if (!dst || cap == 0u)
        return;
    size_t out = 0u;
    bool last_space = true;
    const unsigned char *p = (const unsigned char *)(src ? src : "");
    while (*p && out + 1u < cap) {
        char c = 0;
        if (*p < 0x80u) {
            c = (char)tolower(*p++);
        } else if (*p == 0xc3u && p[1]) {
            c = fold_accent(p[1]);
            p += 2;
        } else {
            ++p;
        }
        if (c && isalnum((unsigned char)c)) {
            dst[out++] = c;
            last_space = false;
        } else if (!last_space && out + 1u < cap) {
            dst[out++] = ' ';
            last_space = true;
        }
    }
    while (out > 0u && dst[out - 1u] == ' ')
        --out;
    dst[out] = '\0';
}

/* Return whether word. */
static bool has_word(const char *text, const char *word) {
    if (!text || !word || !word[0])
        return false;
    size_t wn = strlen(word);
    const char *p = text;
    while ((p = strstr(p, word)) != NULL) {
        bool left = p == text || p[-1] == ' ';
        bool right = p[wn] == '\0' || p[wn] == ' ';
        if (left && right)
            return true;
        ++p;
    }
    return false;
}

/* Implement the vip_m3u_classify_group helper. */
vip_m3u_content_kind_t vip_m3u_classify_group(const char *group_name) {
    char norm[512];
    normalize_ascii(group_name, norm, sizeof(norm));
    if (has_word(norm, "series") || has_word(norm, "serie") || has_word(norm, "seriados") ||
        has_word(norm, "seriado") || has_word(norm, "anime") || has_word(norm, "animes") ||
        has_word(norm, "novela") || has_word(norm, "novelas") || has_word(norm, "dorama") ||
        has_word(norm, "doramas"))
        return VIP_M3U_CONTENT_SERIES;
    if (has_word(norm, "filmes") || has_word(norm, "filme") || has_word(norm, "movies") ||
        has_word(norm, "movie") || has_word(norm, "vod") || has_word(norm, "cinema"))
        return VIP_M3U_CONTENT_VOD;
    return VIP_M3U_CONTENT_LIVE;
}

/* Parse uint at. */
static bool parse_uint_at(const char *s, size_t len, size_t *pos, int *value) {
    if (!s || !pos || *pos >= len || !isdigit((unsigned char)s[*pos]))
        return false;
    int v = 0;
    size_t i = *pos;
    int digits = 0;
    while (i < len && isdigit((unsigned char)s[i]) && digits < 4) {
        v = v * 10 + (s[i] - '0');
        ++i;
        ++digits;
    }
    if (digits == 0)
        return false;
    *pos = i;
    *value = v;
    return true;
}

/* Implement the episode_marker_at helper. */
static bool episode_marker_at(const char *name, size_t len, size_t pos, size_t *end_out, int *season_out,
                              int *episode_out) {
    if (!name || pos >= len)
        return false;
    unsigned char c = (unsigned char)name[pos];
    if (c == 's' || c == 'S' || c == 't' || c == 'T') {
        size_t p = pos + 1u;
        int season = 0, episode = 0;
        if (!parse_uint_at(name, len, &p, &season))
            return false;
        while (p < len && (name[p] == ' ' || name[p] == '.' || name[p] == '-' || name[p] == '_'))
            ++p;
        if (p >= len || (name[p] != 'e' && name[p] != 'E'))
            return false;
        ++p;
        if (!parse_uint_at(name, len, &p, &episode))
            return false;
        if (end_out)
            *end_out = p;
        if (season_out)
            *season_out = season;
        if (episode_out)
            *episode_out = episode;
        return true;
    }
    if (isdigit(c)) {
        size_t p = pos;
        int season = 0, episode = 0;
        if (!parse_uint_at(name, len, &p, &season))
            return false;
        if (p >= len || (name[p] != 'x' && name[p] != 'X'))
            return false;
        ++p;
        if (!parse_uint_at(name, len, &p, &episode))
            return false;
        if (end_out)
            *end_out = p;
        if (season_out)
            *season_out = season;
        if (episode_out)
            *episode_out = episode;
        return true;
    }
    return false;
}

/* Implement the vip_m3u_parse_episode_label helper. */
bool vip_m3u_parse_episode_label(const char *name, char *series_out, size_t series_cap, int *season_out,
                                 int *episode_out) {
    if (series_out && series_cap > 0u)
        series_out[0] = '\0';
    if (!name || !name[0] || !series_out || series_cap == 0u)
        return false;
    size_t len = strlen(name);
    size_t marker = len;
    int season = 0, episode = 0;
    for (size_t i = 0u; i < len; ++i) {
        if (i > 0u && isalnum((unsigned char)name[i - 1u]) &&
            (name[i] == 's' || name[i] == 'S' || name[i] == 't' || name[i] == 'T'))
            continue;
        size_t end = 0u;
        int s = 0, e = 0;
        if (episode_marker_at(name, len, i, &end, &s, &e)) {
            marker = i;
            season = s;
            episode = e;
            break;
        }
    }
    if (marker == len)
        return false;
    size_t title_end = marker;
    while (title_end > 0u) {
        unsigned char c = (unsigned char)name[title_end - 1u];
        if (isspace(c) || c == '-' || c == '_' || c == '|' || c == '.' || c == ':')
            --title_end;
        else
            break;
    }
    if (title_end == 0u)
        return false;
    size_t copy = title_end < series_cap - 1u ? title_end : series_cap - 1u;
    memcpy(series_out, name, copy);
    series_out[copy] = '\0';
    if (season_out)
        *season_out = season;
    if (episode_out)
        *episode_out = episode;
    return true;
}

/* Find category. */
static const vip_category_t *find_category(const vip_category_list_t *cats, const char *id) {
    if (!cats || !id)
        return NULL;
    for (size_t i = 0u; i < cats->len; ++i)
        if (cats->items[i].id && strcmp(cats->items[i].id, id) == 0)
            return &cats->items[i];
    return NULL;
}

/* Implement the category_is_used helper. */
static bool category_is_used(const vip_channel_list_t *channels, const char *category_id) {
    if (!channels || !category_id)
        return false;
    for (size_t i = 0u; i < channels->len; ++i)
        if (channels->items[i].category_id && strcmp(channels->items[i].category_id, category_id) == 0)
            return true;
    return false;
}

/* Append channel for kind. */
static vip_status_t push_channel_for_kind(vip_m3u_content_kind_t kind, const vip_channel_t *channel,
                                          vip_channel_list_t *live_channels, vip_channel_list_t *vod_channels,
                                          vip_channel_list_t *series_channels, vip_error_t *error) {
    if (kind == VIP_M3U_CONTENT_VOD)
        return vip_channel_list_push(vod_channels, channel, error);
    if (kind == VIP_M3U_CONTENT_SERIES)
        return vip_channel_list_push(series_channels, channel, error);
    return vip_channel_list_push(live_channels, channel, error);
}

/* Implement the vip_m3u_split_catalog helper. */
vip_status_t vip_m3u_split_catalog(const vip_category_list_t *source_categories,
                                   const vip_channel_list_t *source_channels,
                                   vip_category_list_t *live_categories, vip_channel_list_t *live_channels,
                                   vip_category_list_t *vod_categories, vip_channel_list_t *vod_channels,
                                   vip_category_list_t *series_categories,
                                   vip_channel_list_t *series_channels, vip_error_t *error) {
    if (!source_categories || !source_channels || !live_categories || !live_channels || !vod_categories ||
        !vod_channels || !series_categories || !series_channels) {
        vip_error_set(error, VIP_ERR_INVALID_ARGUMENT, "listas M3U inválidas para classificação");
        return VIP_ERR_INVALID_ARGUMENT;
    }
    vip_status_t st = VIP_OK;
    for (size_t i = 0u; i < source_channels->len; ++i) {
        const vip_channel_t *ch = &source_channels->items[i];
        const vip_category_t *cat = find_category(source_categories, ch->category_id);
        vip_m3u_content_kind_t kind = cat ? vip_m3u_classify_group(cat->name) : VIP_M3U_CONTENT_LIVE;
        if (!cat) {
            char series_name[256];
            if (vip_m3u_parse_episode_label(ch->name, series_name, sizeof(series_name), NULL, NULL))
                kind = VIP_M3U_CONTENT_SERIES;
        }
        st = push_channel_for_kind(kind, ch, live_channels, vod_channels, series_channels, error);
        if (st != VIP_OK)
            goto fail;
    }
    for (size_t i = 0u; i < source_categories->len; ++i) {
        const vip_category_t *cat = &source_categories->items[i];
        vip_m3u_content_kind_t kind = vip_m3u_classify_group(cat->name);
        if (kind == VIP_M3U_CONTENT_SERIES && category_is_used(series_channels, cat->id))
            st = vip_category_list_push(series_categories, cat, error);
        else if (kind == VIP_M3U_CONTENT_VOD && category_is_used(vod_channels, cat->id))
            st = vip_category_list_push(vod_categories, cat, error);
        else if (kind == VIP_M3U_CONTENT_LIVE && category_is_used(live_channels, cat->id))
            st = vip_category_list_push(live_categories, cat, error);
        if (st != VIP_OK)
            goto fail;
    }
    vip_error_clear(error);
    return VIP_OK;

fail:
    vip_category_list_clear(live_categories);
    vip_category_list_init(live_categories);
    vip_channel_list_clear(live_channels);
    vip_channel_list_init(live_channels);
    vip_category_list_clear(vod_categories);
    vip_category_list_init(vod_categories);
    vip_channel_list_clear(vod_channels);
    vip_channel_list_init(vod_channels);
    vip_category_list_clear(series_categories);
    vip_category_list_init(series_categories);
    vip_channel_list_clear(series_channels);
    vip_channel_list_init(series_channels);
    return st;
}
