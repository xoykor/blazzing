#!/usr/bin/env python3
from pathlib import Path

path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

# Hover IDs for non-card browse controls.
old = '''#define INPUT_PROFILE_NAME 6\n\ntypedef enum { SCREEN_LOGIN = 0, SCREEN_BROWSE, SCREEN_PLAYER } screen_t;'''
new = '''#define INPUT_PROFILE_NAME 6\n#define HOVER_NONE 0\n#define HOVER_TAB_BASE 10\n#define HOVER_SEARCH 20\n#define HOVER_FAVORITES 21\n#define HOVER_LISTS 22\n#define HOVER_BACK 23\n#define HOVER_CATEGORY_ALL 30\n#define HOVER_CATEGORY_BASE 1000\n\ntypedef enum { SCREEN_LOGIN = 0, SCREEN_BROWSE, SCREEN_PLAYER } screen_t;'''
assert old in s
s = s.replace(old, new, 1)

# Give the visual language one more surface tone.
old = '''    unsigned long panel2;\n    unsigned long border;'''
new = '''    unsigned long panel2;\n    unsigned long hover;\n    unsigned long border;'''
assert old in s
s = s.replace(old, new, 1)
old = '''    a->colors.panel2 = alloc_color(a, "#151E2D");\n    a->colors.border = alloc_color(a, "#2B3950");'''
new = '''    a->colors.panel2 = alloc_color(a, "#151E2D");\n    a->colors.hover = alloc_color(a, "#1D2C43");\n    a->colors.border = alloc_color(a, "#2B3950");'''
assert old in s
s = s.replace(old, new, 1)

# Persistent control hover state.
old = '''    bool hovered_card_valid;\n    size_t hovered_filtered;\n    vip_ui_motion_t hover_motion;\n    bool ui_motion_active;'''
new = '''    bool hovered_card_valid;\n    size_t hovered_filtered;\n    vip_ui_motion_t hover_motion;\n    int hovered_control;\n    vip_ui_motion_t control_motion;\n    bool ui_motion_active;'''
assert old in s
s = s.replace(old, new, 1)

# Add control hit testing immediately before the card hover updater.
marker = 'static void update_browse_hover(app_t *a, int x, int y) {'
assert marker in s
helper = r'''static int browse_control_at(app_t *a, int x, int y) {
    if (!a || a->screen != SCREEN_BROWSE) return HOVER_NONE;
    const int tab_x[3] = {8, 88, 174};
    const int tab_w[3] = {74, 80, 88};
    if (y >= 12 && y < 58 && x < SIDEBAR_W) {
        for (int k = 0; k < 3; ++k)
            if (point_in(x, y, tab_x[k], 12, tab_w[k], 46)) return HOVER_TAB_BASE + k;
    }
    int list_w = 94, fav_w = 174;
    int list_x = a->width - list_w - 18;
    int fav_x = list_x - fav_w - 10;
    int search_w = fav_x - (SIDEBAR_W + 18) - 10;
    if (search_w < 180) search_w = 180;
    if (point_in(x, y, SIDEBAR_W + 18, 12, search_w, 46)) return HOVER_SEARCH;
    if (point_in(x, y, fav_x, 12, fav_w, 46)) return HOVER_FAVORITES;
    if (point_in(x, y, list_x, 12, list_w, 46)) return HOVER_LISTS;
    if (x < SIDEBAR_W && y >= TOPBAR_H) {
        int base = TOPBAR_H + 12;
        if (a->series_episode_mode) {
            if (point_in(x, y, 8, base, SIDEBAR_W - 16, 38)) return HOVER_BACK;
            base += 48;
        }
        if (point_in(x, y, 8, base, SIDEBAR_W - 16, 36)) return HOVER_CATEGORY_ALL;
        int local = y - (base + 42);
        if (local >= 0) {
            int row = local / 42;
            if (local % 42 < 36) {
                int idx = a->category_scroll + row;
                if (idx >= 0 && (size_t)idx < ACTIVE_CATEGORIES(a).len)
                    return HOVER_CATEGORY_BASE + idx;
            }
        }
    }
    return HOVER_NONE;
}

'''
s = s.replace(marker, helper + marker, 1)

# Update both card and chrome hover state from each pointer move.
start = s.index(marker)
end_marker = 'static bool step_browse_animations(app_t *a, int64_t now) {'
end = s.index(end_marker, start)
old_func = s[start:end]
new_func = r'''static void update_browse_hover(app_t *a, int x, int y) {
    if (!a) return;
    int64_t now = monotonic_ms();
    a->mouse_x = x;
    a->mouse_y = y;
    a->mouse_inside = true;

    int control = browse_control_at(a, x, y);
    if (control != a->hovered_control) {
        a->hovered_control = control;
        vip_ui_motion_init(&a->control_motion, 0.0f, now);
        if (control != HOVER_NONE) vip_ui_motion_set_target(&a->control_motion, 1.0f, now);
        a->ui_motion_active = true;
    } else if (control != HOVER_NONE) {
        vip_ui_motion_set_target(&a->control_motion, 1.0f, now);
    }

    size_t hit = 0u;
    if (browse_card_at(a, x, y, &hit)) {
        if (!a->hovered_card_valid || a->hovered_filtered != hit) {
            a->hovered_card_valid = true;
            a->hovered_filtered = hit;
            vip_ui_motion_init(&a->hover_motion, 0.0f, now);
        }
        vip_ui_motion_set_target(&a->hover_motion, 1.0f, now);
        a->ui_motion_active = true;
    } else if (a->hovered_card_valid) {
        vip_ui_motion_set_target(&a->hover_motion, 0.0f, now);
        a->ui_motion_active = true;
    }
}

'''
s = s[:start] + new_func + s[end:]

# Step control animation as part of the same on-demand animation clock.
old = '''    if (a->hovered_card_valid) {\n        if (vip_ui_motion_step(&a->hover_motion, now, 140)) active = true;\n        if (a->hover_motion.value <= 0.0f && a->hover_motion.target <= 0.0f)\n            a->hovered_card_valid = false;\n    }\n    a->ui_motion_active = active;'''
new = '''    if (a->hovered_card_valid) {\n        if (vip_ui_motion_step(&a->hover_motion, now, 140)) active = true;\n        if (a->hover_motion.value <= 0.0f && a->hover_motion.target <= 0.0f)\n            a->hovered_card_valid = false;\n    }\n    if (a->hovered_control != HOVER_NONE) {\n        if (vip_ui_motion_step(&a->control_motion, now, 120)) active = true;\n    }\n    a->ui_motion_active = active;'''
assert old in s
s = s.replace(old, new, 1)

# Reset control hover alongside card hover when the catalog is rebuilt.
old = '''    a->hovered_card_valid = false;\n    vip_ui_motion_init(&a->hover_motion, 0.0f, a->grid_scroll_last_ms);\n    a->ui_motion_active = false;'''
new = '''    a->hovered_card_valid = false;\n    a->hovered_control = HOVER_NONE;\n    vip_ui_motion_init(&a->hover_motion, 0.0f, a->grid_scroll_last_ms);\n    vip_ui_motion_init(&a->control_motion, 0.0f, a->grid_scroll_last_ms);\n    a->ui_motion_active = false;'''
assert old in s
s = s.replace(old, new, 1)

# Tabs respond to pointer hover with a brighter surface and animated underline.
old = '''    for (int k = 0; k < 3; ++k) {\n        bool selected = (int)a->content_kind == k;\n        fill_round_rect(a, tab_x[k], tab_y, tab_w[k], tab_h, 13,\n                        selected ? a->colors.accent2 : a->colors.panel2);\n        stroke_round_rect(a, tab_x[k], tab_y, tab_w[k], tab_h, 13,\n                          selected ? a->colors.accent : a->colors.border);\n        draw_centered(a, tab_x[k], 42, tab_w[k], content_label((content_kind_t)k),\n                      selected ? a->colors.text : a->colors.muted);\n    }'''
new = '''    for (int k = 0; k < 3; ++k) {\n        bool selected = (int)a->content_kind == k;\n        bool hovered = a->hovered_control == HOVER_TAB_BASE + k;\n        float hover_t = hovered ? vip_ui_ease_out_cubic(a->control_motion.value) : 0.0f;\n        fill_round_rect(a, tab_x[k], tab_y, tab_w[k], tab_h, 13,\n                        selected ? a->colors.accent2 : (hovered ? a->colors.hover : a->colors.panel2));\n        stroke_round_rect(a, tab_x[k], tab_y, tab_w[k], tab_h, 13,\n                          (selected || hovered) ? a->colors.accent : a->colors.border);\n        if (hovered && !selected) {\n            int line_w = (int)((float)(tab_w[k] - 24) * hover_t + 0.5f);\n            if (line_w > 0) fill_round_rect(a, tab_x[k] + (tab_w[k] - line_w)/2, tab_y + tab_h - 4, line_w, 3, 1, a->colors.accent);\n        }\n        draw_centered(a, tab_x[k], 42, tab_w[k], content_label((content_kind_t)k),\n                      (selected || hovered) ? a->colors.text : a->colors.muted);\n    }'''
assert old in s
s = s.replace(old, new, 1)

# Search/favorites/lists chrome hover.
old = '''    draw_input(a, SIDEBAR_W+18, 12, search_w, 46, a->search, search_hint, INPUT_SEARCH, false);\n    fill_round_rect(a, fav_x, 12, fav_w, 46, 13, a->favorites_only ? a->colors.accent2 : a->colors.panel2);\n    stroke_round_rect(a, fav_x, 12, fav_w, 46, 13, a->favorites_only ? a->colors.accent : a->colors.border);'''
new = '''    draw_input(a, SIDEBAR_W+18, 12, search_w, 46, a->search, search_hint, INPUT_SEARCH, false);\n    if (a->hovered_control == HOVER_SEARCH && a->input_focus != INPUT_SEARCH)\n        stroke_round_rect(a, SIDEBAR_W+18, 12, search_w, 46, 13, a->colors.accent);\n    bool fav_hover = a->hovered_control == HOVER_FAVORITES;\n    fill_round_rect(a, fav_x, 12, fav_w, 46, 13, a->favorites_only ? a->colors.accent2 : (fav_hover ? a->colors.hover : a->colors.panel2));\n    stroke_round_rect(a, fav_x, 12, fav_w, 46, 13, (a->favorites_only || fav_hover) ? a->colors.accent : a->colors.border);'''
assert old in s
s = s.replace(old, new, 1)
old = '''    fill_round_rect(a, list_x, 12, list_w, 46, 13, a->colors.panel2);\n    stroke_round_rect(a, list_x, 12, list_w, 46, 13, a->colors.border);\n    draw_centered(a, list_x, 42, list_w, "Listas", a->colors.muted);'''
new = '''    bool list_hover = a->hovered_control == HOVER_LISTS;\n    fill_round_rect(a, list_x, 12, list_w, 46, 13, list_hover ? a->colors.hover : a->colors.panel2);\n    stroke_round_rect(a, list_x, 12, list_w, 46, 13, list_hover ? a->colors.accent : a->colors.border);\n    draw_centered(a, list_x, 42, list_w, "Listas", list_hover ? a->colors.text : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)

# Back/category hover in the sidebar.
old = '''    if (a->series_episode_mode) {\n        fill_round_rect(a, 8, y, SIDEBAR_W-16, 38, 11, a->colors.panel);\n        stroke_round_rect(a, 8, y, SIDEBAR_W-16, 38, 11, a->colors.accent);\n        draw_text_font(a, a->font_heading, 18, y+26, browse_back_label(a), a->colors.text);\n        y += 48;\n    }\n    bool all_sel = a->selected_category < 0;\n    fill_round_rect(a, 8, y, SIDEBAR_W-16, 36, 11, all_sel ? a->colors.accent2 : a->colors.panel2);'''
new = '''    if (a->series_episode_mode) {\n        bool back_hover = a->hovered_control == HOVER_BACK;\n        fill_round_rect(a, 8, y, SIDEBAR_W-16, 38, 11, back_hover ? a->colors.accent2 : a->colors.panel);\n        stroke_round_rect(a, 8, y, SIDEBAR_W-16, 38, 11, a->colors.accent);\n        draw_text_font(a, a->font_heading, 18, y+26, browse_back_label(a), a->colors.text);\n        y += 48;\n    }\n    bool all_sel = a->selected_category < 0;\n    bool all_hover = a->hovered_control == HOVER_CATEGORY_ALL;\n    fill_round_rect(a, 8, y, SIDEBAR_W-16, 36, 11, all_sel ? a->colors.accent2 : (all_hover ? a->colors.hover : a->colors.panel2));'''
assert old in s
s = s.replace(old, new, 1)
old = '''    draw_text(a, 18, y+24, all_label, all_sel ? a->colors.text : a->colors.muted);'''
new = '''    draw_text(a, 18, y+24, all_label, (all_sel || all_hover) ? a->colors.text : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)
old = '''        bool selected = a->selected_category == idx;\n        fill_round_rect(a, 8, y, SIDEBAR_W-16, 36, 11, selected ? a->colors.accent2 : a->colors.panel2);'''
new = '''        bool selected = a->selected_category == idx;\n        bool hovered = a->hovered_control == HOVER_CATEGORY_BASE + idx;\n        fill_round_rect(a, 8, y, SIDEBAR_W-16, 36, 11, selected ? a->colors.accent2 : (hovered ? a->colors.hover : a->colors.panel2));'''
assert old in s
s = s.replace(old, new, 1)
old = '''        draw_text(a, 18, y+24, label, selected ? a->colors.text : a->colors.muted);'''
new = '''        draw_text(a, 18, y+24, label, (selected || hovered) ? a->colors.text : a->colors.muted);'''
assert old in s
s = s.replace(old, new, 1)

# Fade both hover systems on window leave.
old = '''        case LeaveNotify:\n            if(a->screen==SCREEN_BROWSE && a->hovered_card_valid){\n                a->mouse_inside=false;\n                vip_ui_motion_set_target(&a->hover_motion,0.0f,monotonic_ms());\n                a->ui_motion_active=true;\n            }\n            break;'''
new = '''        case LeaveNotify:\n            if(a->screen==SCREEN_BROWSE){\n                a->mouse_inside=false;\n                if(a->hovered_card_valid) vip_ui_motion_set_target(&a->hover_motion,0.0f,monotonic_ms());\n                a->hovered_control=HOVER_NONE;\n                vip_ui_motion_init(&a->control_motion,0.0f,monotonic_ms());\n                a->ui_motion_active=true;\n            }\n            break;'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
