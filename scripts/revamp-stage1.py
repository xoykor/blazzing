#!/usr/bin/env python3
from pathlib import Path

cmake = Path('CMakeLists.txt')
s = cmake.read_text()
old = 'add_library(vip_ui_x11 src/ui_x11/x11_app.c)'
new = 'add_library(vip_ui_x11\n    src/ui_x11/x11_app.c\n    src/ui_x11/ui_motion.c)'
assert old in s
s = s.replace(old, new, 1)
old = '''    add_executable(test_player_mpv tests/test_player_mpv.c)\n    target_link_libraries(test_player_mpv PRIVATE vip_player_mpv)\n    vip_warnings(test_player_mpv)\n    add_test(NAME player_mpv COMMAND test_player_mpv)\nendif()'''
new = '''    add_executable(test_player_mpv tests/test_player_mpv.c)\n    target_link_libraries(test_player_mpv PRIVATE vip_player_mpv)\n    vip_warnings(test_player_mpv)\n    add_test(NAME player_mpv COMMAND test_player_mpv)\n\n    add_executable(test_ui_motion tests/test_ui_motion.c src/ui_x11/ui_motion.c)\n    target_include_directories(test_ui_motion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)\n    vip_warnings(test_ui_motion)\n    add_test(NAME ui_motion COMMAND test_ui_motion)\nendif()'''
assert old in s
s = s.replace(old, new, 1)
cmake.write_text(s)

path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

old = '#include "visual_iptv/thumbnails.h"\n'
new = '#include "visual_iptv/thumbnails.h"\n#include "visual_iptv/ui_motion.h"\n'
assert old in s
s = s.replace(old, new, 1)

old = '''static int category_visible_rows(app_t *a) { int n = (a->height - TOPBAR_H - 50) / 40; return n > 1 ? n : 1; }'''
new = '''static int browse_sidebar_category_y(const app_t *a) {\n    return TOPBAR_H + 12 + (a && a->series_episode_mode ? 48 : 0);\n}\n\nstatic int category_visible_rows(app_t *a) {\n    int top = browse_sidebar_category_y(a);\n    int n = (a->height - top - 8) / 40;\n    return n > 1 ? n : 1;\n}\n\nstatic const char *browse_back_label(const app_t *a) {\n    if (!a || !a->series_episode_mode) return \"Voltar\";\n    return a->series_season_select ? \"< Séries\" : \"< Temporadas\";\n}'''
assert old in s
s = s.replace(old, new, 1)

old = '''    int y = TOPBAR_H + 12;\n    bool all_sel = a->selected_category < 0;'''
new = '''    int y = TOPBAR_H + 12;\n    if (a->series_episode_mode) {\n        fill_round_rect(a, 8, y, SIDEBAR_W-16, 38, 11, a->colors.panel);\n        stroke_round_rect(a, 8, y, SIDEBAR_W-16, 38, 11, a->colors.accent);\n        draw_text_font(a, a->font_heading, 18, y+26, browse_back_label(a), a->colors.text);\n        y += 48;\n    }\n    bool all_sel = a->selected_category < 0;'''
assert old in s
s = s.replace(old, new, 1)

old = '''    if (x < SIDEBAR_W && y >= TOPBAR_H) {\n        int local=y-(TOPBAR_H+12); if (local>=0 && local<36) { choose_category(a,-1); return; }\n        local-=42; if (local>=0) { int row=local/42; if (local%42<36) { int idx=a->category_scroll+row; if (idx>=0 && (size_t)idx<ACTIVE_CATEGORIES(a).len) choose_category(a,idx); } }\n        return;\n    }'''
new = '''    if (x < SIDEBAR_W && y >= TOPBAR_H) {\n        int base = TOPBAR_H + 12;\n        if (a->series_episode_mode && point_in(x, y, 8, base, SIDEBAR_W-16, 38)) {\n            return_from_episode_list(a);\n            return;\n        }\n        int category_y = browse_sidebar_category_y(a);\n        int local=y-category_y; if (local>=0 && local<36) { choose_category(a,-1); return; }\n        local-=42; if (local>=0) { int row=local/42; if (local%42<36) { int idx=a->category_scroll+row; if (idx>=0 && (size_t)idx<ACTIVE_CATEGORIES(a).len) choose_category(a,idx); } }\n        return;\n    }'''
assert old in s
s = s.replace(old, new, 1)

old = '''    for (int rr = 0; rr < visible_rows; ++rr) {\n        int row = first_row + rr;\n        int cy = content_y + y_offset + rr * layout.row_step;\n        if (cy > a->height || cy + layout.card_h < TOPBAR_H) continue;'''
new = '''    int grid_right = a->width - 18;\n    if (details_panel_active(a)) {\n        int px, py, pw, ph;\n        details_panel_geometry(a, &px, &py, &pw, &ph);\n        (void)py; (void)pw; (void)ph;\n        grid_right = px - 12;\n    }\n    if (grid_right > content_x && a->height > content_y) {\n        XRectangle grid_clip = {\n            .x = (short)content_x,\n            .y = (short)content_y,\n            .width = (unsigned short)(grid_right - content_x),\n            .height = (unsigned short)(a->height - content_y),\n        };\n        XSetClipRectangles(a->dpy, a->gc, 0, 0, &grid_clip, 1, Unsorted);\n    }\n\n    for (int rr = 0; rr < visible_rows; ++rr) {\n        int row = first_row + rr;\n        int cy = content_y + y_offset + rr * layout.row_step;\n        if (cy > a->height || cy + layout.card_h < content_y) continue;'''
assert old in s
s = s.replace(old, new, 1)

old = '''    draw_details_panel(a);\n\n    if (atomic_load(&a->series_running)) {'''
new = '''    XSetClipMask(a->dpy, a->gc, None);\n    draw_details_panel(a);\n\n    if (atomic_load(&a->series_running)) {'''
assert old in s
s = s.replace(old, new, 1)

old = '''    vip_category_list_t seasons; vip_category_list_init(&seasons);\n    vip_channel_list_t episodes; vip_channel_list_init(&episodes);\n    vip_channel_list_t season_cards; vip_channel_list_init(&season_cards);\n    vip_error_t error = {0};\n\n    vip_status_t st = vip_credentials_init(&credentials, job->server, job->username, job->password, &error);\n    if (st == VIP_OK) st = vip_xtream_client_create(&client, &credentials, &error);\n    if (st == VIP_OK) st = vip_xtream_series_episodes(client, job->series_id, &seasons, &episodes, &error);\n\n    if (st == VIP_OK) {\n        const char *fallback_provider = episodes.len > 0u ? episodes.items[0].provider_id : credentials.provider_id;'''
new = '''    vip_category_list_t seasons; vip_category_list_init(&seasons);\n    vip_channel_list_t episodes; vip_channel_list_init(&episodes);\n    vip_channel_list_t season_cards; vip_channel_list_init(&season_cards);\n    vip_media_metadata_t series_metadata; vip_media_metadata_init(&series_metadata);\n    vip_error_t error = {0};\n\n    vip_status_t st = vip_credentials_init(&credentials, job->server, job->username, job->password, &error);\n    if (st == VIP_OK) st = vip_xtream_client_create(&client, &credentials, &error);\n    if (st == VIP_OK) st = vip_xtream_series_info(client, job->series_id, &series_metadata, &seasons, &episodes, &error);\n\n    if (st == VIP_OK) {\n        const char *series_art = series_metadata.cover_url && series_metadata.cover_url[0]\n                                     ? series_metadata.cover_url\n                                     : (job->logo_url && job->logo_url[0] ? job->logo_url : NULL);\n        if (series_art) {\n            for (size_t i = 0; i < episodes.len; ++i) {\n                char *inherited = vip_strdup(series_art);\n                if (!inherited) {\n                    vip_error_set(&error, VIP_ERR_NOMEM, \"sem memória para capa dos episódios\");\n                    st = VIP_ERR_NOMEM;\n                    break;\n                }\n                free(episodes.items[i].logo_url);\n                episodes.items[i].logo_url = inherited;\n            }\n        }\n        const char *fallback_provider = episodes.len > 0u ? episodes.items[0].provider_id : credentials.provider_id;'''
assert old in s
s = s.replace(old, new, 1)

old = '''                .name = season->name ? season->name : "Temporada",\n                .logo_url = job->logo_url && job->logo_url[0] ? job->logo_url : NULL,\n                .stream_url = "series://season",'''
new = '''                .name = season->name ? season->name : "Temporada",\n                .logo_url = series_art,\n                .stream_url = "series://season",'''
assert old in s
s = s.replace(old, new, 1)

old = '''    vip_category_list_clear(&seasons);\n    vip_channel_list_clear(&episodes);\n    vip_channel_list_clear(&season_cards);\n    if (job->password) {'''
new = '''    vip_category_list_clear(&seasons);\n    vip_channel_list_clear(&episodes);\n    vip_channel_list_clear(&season_cards);\n    vip_media_metadata_clear(&series_metadata);\n    if (job->password) {'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
