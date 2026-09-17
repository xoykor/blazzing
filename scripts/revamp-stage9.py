#!/usr/bin/env python3
from pathlib import Path

# IPTV login: finish the two remaining bitmap-font fallbacks found by visual QA.
path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

old = '''    if (a->profiles.len == 0u) draw_text(a, list_x, y+182, "Nenhuma lista salva ainda.", a->colors.muted);

    char status_copy[512];
    pthread_mutex_lock(&a->data_mutex);
    snprintf(status_copy, sizeof(status_copy), "%s", a->status);
    pthread_mutex_unlock(&a->data_mutex);
    draw_text(a, form_x, y+h-42, status_copy,
              (strstr(status_copy, "falha") || strstr(status_copy, "Erro")) ? a->colors.danger : a->colors.muted);'''
new = '''    if (a->profiles.len == 0u) {
        if (a->renderer.active)
            vip_ui_render_text(&a->renderer, list_x, y + 168, list_w, "Nenhuma lista salva ainda.",
                               "Sans 9", 0x91A0B7u, 1.0, false);
        else
            draw_text(a, list_x, y+182, "Nenhuma lista salva ainda.", a->colors.muted);
    }

    char status_copy[512];
    pthread_mutex_lock(&a->data_mutex);
    snprintf(status_copy, sizeof(status_copy), "%s", a->status);
    pthread_mutex_unlock(&a->data_mutex);
    bool status_error = strstr(status_copy, "falha") || strstr(status_copy, "Erro");
    if (a->renderer.active)
        vip_ui_render_text(&a->renderer, form_x, y + h - 56, form_w, status_copy,
                           "Sans 8", status_error ? 0xFF7185u : 0x91A0B7u, 1.0, false);
    else
        draw_text(a, form_x, y+h-42, status_copy,
                  status_error ? a->colors.danger : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)
path.write_text(s)

# Pluto: reserve the status-bar area in grid scrolling so the last row can be
# brought fully above the footer instead of ending beneath it.
path = Path('src/app/pluto_app.c')
s = path.read_text()

old = '''static int grid_max_scroll(const pluto_app_t *a) {
    int content_height = grid_rows(a) * (PLUTO_CARD_H + PLUTO_GAP);
    int viewport = a->height - PLUTO_HEADER_H - 24;
    int max_scroll = content_height - viewport;
    return max_scroll > 0 ? max_scroll : 0;
}'''
new = '''static int grid_max_scroll(const pluto_app_t *a) {
    int content_height = grid_rows(a) * (PLUTO_CARD_H + PLUTO_GAP);
    int status_reserve = a->status[0] ? 42 : 8;
    int viewport = a->height - PLUTO_HEADER_H - status_reserve;
    if (viewport < PLUTO_CARD_H) viewport = PLUTO_CARD_H;
    int max_scroll = content_height - viewport;
    return max_scroll > 0 ? max_scroll : 0;
}'''
assert old in s
s = s.replace(old, new, 1)

old = '''    int row_top = row * (PLUTO_CARD_H + PLUTO_GAP);
    int viewport = a->height - PLUTO_HEADER_H - 24;
    if (row_top < a->scroll) a->scroll = row_top;'''
new = '''    int row_top = row * (PLUTO_CARD_H + PLUTO_GAP);
    int status_reserve = a->status[0] ? 42 : 8;
    int viewport = a->height - PLUTO_HEADER_H - status_reserve;
    if (viewport < PLUTO_CARD_H) viewport = PLUTO_CARD_H;
    if (row_top < a->scroll) a->scroll = row_top;'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
