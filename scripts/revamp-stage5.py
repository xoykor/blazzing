#!/usr/bin/env python3
from pathlib import Path

path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

# Responsive card sizing: fit the available width rather than leaving fixed-width gaps.
old = '''static card_layout_t browse_layout(app_t *a) {\n    card_layout_t layout = {0};\n    layout.mode = detect_artwork_mode(a);\n    if (layout.mode == ART_PORTRAIT) {\n        layout.card_w = 196;\n        layout.art_h = 294;\n    } else if (layout.mode == ART_SQUARE) {\n        layout.card_w = 224;\n        layout.art_h = 224;\n    } else {\n        layout.card_w = 320;\n        layout.art_h = 180;\n    }\n    layout.card_h = layout.art_h + 48;\n    int content_x = SIDEBAR_W + 20;\n    int avail_w = a->width - content_x - 18;\n    if (details_panel_active(a)) {\n        int px, py, pw, ph;\n        details_panel_geometry(a, &px, &py, &pw, &ph);\n        (void)py; (void)ph;\n        avail_w = px - content_x - 12;\n    }\n    layout.cols = (avail_w + GRID_GAP) / (layout.card_w + GRID_GAP);\n    if (layout.cols < 1) layout.cols = 1;\n    layout.row_step = layout.card_h + GRID_GAP;\n    return layout;\n}'''
new = '''static card_layout_t browse_layout(app_t *a) {\n    card_layout_t layout = {0};\n    layout.mode = detect_artwork_mode(a);\n    int ideal_w = 196, min_w = 168, max_w = 224;\n    if (layout.mode == ART_SQUARE) { ideal_w = 224; min_w = 184; max_w = 260; }\n    else if (layout.mode == ART_LANDSCAPE) { ideal_w = 320; min_w = 260; max_w = 380; }\n\n    int content_x = SIDEBAR_W + 20;\n    int avail_w = a->width - content_x - 18;\n    if (details_panel_active(a)) {\n        int px, py, pw, ph;\n        details_panel_geometry(a, &px, &py, &pw, &ph);\n        (void)py; (void)ph;\n        avail_w = px - content_x - 12;\n    }\n    if (avail_w < min_w) avail_w = min_w;\n    layout.cols = (avail_w + GRID_GAP) / (ideal_w + GRID_GAP);\n    if (layout.cols < 1) layout.cols = 1;\n    int fitted = (avail_w - (layout.cols - 1) * GRID_GAP) / layout.cols;\n    while (fitted > max_w && layout.cols < 16) {\n        ++layout.cols;\n        fitted = (avail_w - (layout.cols - 1) * GRID_GAP) / layout.cols;\n    }\n    while (fitted < min_w && layout.cols > 1) {\n        --layout.cols;\n        fitted = (avail_w - (layout.cols - 1) * GRID_GAP) / layout.cols;\n    }\n    if (fitted < min_w) fitted = min_w;\n    if (fitted > max_w) fitted = max_w;\n    layout.card_w = fitted;\n    if (layout.mode == ART_PORTRAIT) layout.art_h = (layout.card_w * 3) / 2;\n    else if (layout.mode == ART_SQUARE) layout.art_h = layout.card_w;\n    else layout.art_h = (layout.card_w * 9) / 16;\n    layout.card_h = layout.art_h + 54;\n    layout.row_step = layout.card_h + GRID_GAP;\n    return layout;\n}'''
assert old in s
s = s.replace(old, new, 1)

# Pango-backed input text and exact caret position, fallback to Xlib.
old = '''    draw_text(a, x + 16, y + h/2 + 6, text,\n              search_focused || (value && value[0]) ? a->colors.text : a->colors.muted);\n    if (search_focused) {\n        int caret_x = x + 16 + text_width(a, value && value[0] ? value : "");\n        if (caret_x < x + 16) caret_x = x + 16;\n        if (caret_x > x + w - 18) caret_x = x + w - 18;\n        set_fg(a, a->colors.accent);\n        XDrawLine(a->dpy, draw_target(a), a->gc, caret_x, y + 12, caret_x, y + h - 12);\n    }'''
new = '''    bool bright = search_focused || (value && value[0]);\n    if (a->renderer.active)\n        vip_ui_render_text(&a->renderer, x + 16, y + (h - 16) / 2, w - 32, text, "Sans 10",\n                           bright ? 0xF6F8FCu : 0x91A0B7u, 1.0, false);\n    else\n        draw_text(a, x + 16, y + h/2 + 6, text, bright ? a->colors.text : a->colors.muted);\n    if (search_focused) {\n        const char *caret_text = value && value[0] ? value : "";\n        int measured = a->renderer.active ? vip_ui_render_text_width(&a->renderer, caret_text, "Sans 10")\n                                          : text_width(a, caret_text);\n        int caret_x = x + 16 + measured;\n        if (caret_x < x + 16) caret_x = x + 16;\n        if (caret_x > x + w - 18) caret_x = x + w - 18;\n        set_fg(a, a->colors.accent);\n        XDrawLine(a->dpy, draw_target(a), a->gc, caret_x, y + 11, caret_x, y + h - 11);\n    }'''
assert old in s
s = s.replace(old, new, 1)

# Modern top tabs.
old = '''        draw_centered(a, tab_x[k], 42, tab_w[k], content_label((content_kind_t)k),\n                      (selected || hovered) ? a->colors.text : a->colors.muted);'''
new = '''        if (a->renderer.active)\n            vip_ui_render_text(&a->renderer, tab_x[k], tab_y + 14, tab_w[k], content_label((content_kind_t)k),\n                               selected ? "Sans Bold 10" : "Sans 10",\n                               (selected || hovered) ? 0xF6F8FCu : 0x91A0B7u, 1.0, true);\n        else\n            draw_centered(a, tab_x[k], 42, tab_w[k], content_label((content_kind_t)k),\n                          (selected || hovered) ? a->colors.text : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)

# Favorites and Lists labels.
old = '''    draw_centered(a, fav_x, 42, fav_w, fav_label, a->favorites_only ? a->colors.text : a->colors.muted);'''
new = '''    if (a->renderer.active)\n        vip_ui_render_text(&a->renderer, fav_x, 27, fav_w, fav_label, a->favorites_only ? "Sans Bold 10" : "Sans 10",\n                           (a->favorites_only || fav_hover) ? 0xF6F8FCu : 0x91A0B7u, 1.0, true);\n    else\n        draw_centered(a, fav_x, 42, fav_w, fav_label, a->favorites_only ? a->colors.text : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)
old = '''    draw_centered(a, list_x, 42, list_w, "Listas", list_hover ? a->colors.text : a->colors.muted);'''
new = '''    if (a->renderer.active)\n        vip_ui_render_text(&a->renderer, list_x, 27, list_w, "Listas", "Sans 10",\n                           list_hover ? 0xF6F8FCu : 0x91A0B7u, 1.0, true);\n    else\n        draw_centered(a, list_x, 42, list_w, "Listas", list_hover ? a->colors.text : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)

# Sidebar navigation uses Pango and keeps Xlib fallback.
old = '''        draw_text_font(a, a->font_heading, 18, y+26, browse_back_label(a), a->colors.text);'''
new = '''        if (a->renderer.active)\n            vip_ui_render_text(&a->renderer, 18, y + 10, SIDEBAR_W - 36, browse_back_label(a), "Sans Bold 10", 0xF6F8FCu, 1.0, false);\n        else\n            draw_text_font(a, a->font_heading, 18, y+26, browse_back_label(a), a->colors.text);'''
assert old in s
s = s.replace(old, new, 1)
old = '''    draw_text(a, 18, y+24, all_label, (all_sel || all_hover) ? a->colors.text : a->colors.muted);'''
new = '''    if (a->renderer.active)\n        vip_ui_render_text(&a->renderer, 18, y + 9, SIDEBAR_W - 36, all_label, all_sel ? "Sans Bold 9" : "Sans 9",\n                           (all_sel || all_hover) ? 0xF6F8FCu : 0x91A0B7u, 1.0, false);\n    else\n        draw_text(a, 18, y+24, all_label, (all_sel || all_hover) ? a->colors.text : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)
old = '''        draw_text(a, 18, y+24, label, (selected || hovered) ? a->colors.text : a->colors.muted);'''
new = '''        if (a->renderer.active)\n            vip_ui_render_text(&a->renderer, 18, y + 9, SIDEBAR_W - 36, label, selected ? "Sans Bold 9" : "Sans 9",\n                               (selected || hovered) ? 0xF6F8FCu : 0x91A0B7u, 1.0, false);\n        else\n            draw_text(a, 18, y+24, label, (selected || hovered) ? a->colors.text : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)

# Loading text and card action/badges/card title/meta use modern type.
old = '''                draw_centered(a, cx, cy + layout.art_h/2 + 5, layout.card_w, "carregando imagem...", a->colors.muted);'''
new = '''                if (a->renderer.active)\n                    vip_ui_render_text(&a->renderer, cx + 8, cy + layout.art_h/2 - 7, layout.card_w - 16, "carregando imagem...", "Sans 9", 0x91A0B7u, 1.0, true);\n                else\n                    draw_centered(a, cx, cy + layout.art_h/2 + 5, layout.card_w, "carregando imagem...", a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)
old = '''                draw_centered_font(a, a->font_small, open_x, open_y + 20, open_w, "ABRIR", a->colors.text);'''
new = '''                if (a->renderer.active)\n                    vip_ui_render_text(&a->renderer, open_x, open_y + 7, open_w, "ABRIR", "Sans Bold 8", 0xF6F8FCu, 1.0, true);\n                else\n                    draw_centered_font(a, a->font_small, open_x, open_y + 20, open_w, "ABRIR", a->colors.text);'''
assert old in s
s = s.replace(old, new, 1)

old = '''            char title[160]; bounded_text(title, sizeof(title), display_title, layout.mode == ART_PORTRAIT ? 28 : 42);\n            draw_text_font(a, active_card ? a->font_heading : a->font, cx + 8, cy + layout.art_h + 25, title, a->colors.text);'''
new = '''            char title[256]; bounded_text(title, sizeof(title), display_title, 92);\n            if (a->renderer.active)\n                vip_ui_render_text(&a->renderer, cx + 9, cy + layout.art_h + 11, layout.card_w - 18, title,\n                                   active_card ? "Sans SemiBold 10" : "Sans 10", 0xF6F8FCu, 1.0, false);\n            else\n                draw_text_font(a, active_card ? a->font_heading : a->font, cx + 8, cy + layout.art_h + 25, title, a->colors.text);'''
assert old in s
s = s.replace(old, new, 1)

old = '''                draw_text_font(a, a->font_small, cx + 8, cy + layout.art_h + 44, meta, a->colors.muted);'''
new = '''                if (a->renderer.active)\n                    vip_ui_render_text(&a->renderer, cx + 9, cy + layout.art_h + 33, layout.card_w - 18, meta, "Sans 8", 0x91A0B7u, 1.0, false);\n                else\n                    draw_text_font(a, a->font_small, cx + 8, cy + layout.art_h + 44, meta, a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)
old = '''                draw_text_font(a, a->font_small, cx + 8, cy + layout.art_h + 44, progress, a->colors.muted);'''
new = '''                if (a->renderer.active)\n                    vip_ui_render_text(&a->renderer, cx + 9, cy + layout.art_h + 33, layout.card_w - 18, progress, "Sans 8", 0x91A0B7u, 1.0, false);\n                else\n                    draw_text_font(a, a->font_small, cx + 8, cy + layout.art_h + 44, progress, a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
