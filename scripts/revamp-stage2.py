#!/usr/bin/env python3
from pathlib import Path

path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

# 1. Persistent interaction state.
old = '''    bool mouse_down;\n\n    login_mode_t login_mode;'''
new = '''    bool mouse_down;\n    int mouse_x;\n    int mouse_y;\n    bool mouse_inside;\n    bool hovered_card_valid;\n    size_t hovered_filtered;\n    vip_ui_motion_t hover_motion;\n    bool ui_motion_active;\n    int grid_scroll_target;\n    bool grid_scroll_animating;\n    int64_t grid_scroll_last_ms;\n\n    login_mode_t login_mode;'''
assert old in s
s = s.replace(old, new, 1)

# 2. Pointer hit-test + animation engine hooks, colocated with browse geometry.
marker = 'static bool contains_ascii_case(const char *haystack, const char *needle) {'
assert marker in s
helpers = r'''static bool browse_card_at(app_t *a, int x, int y, size_t *fidx_out) {
    if (!a || a->screen != SCREEN_BROWSE || a->filtered_len == 0u) return false;
    card_layout_t layout = browse_layout(a);
    int content_x = SIDEBAR_W + 20;
    int content_y = TOPBAR_H + 18;
    if (x < content_x || y < content_y || x >= a->width || y >= a->height) return false;
    if (details_panel_active(a)) {
        int px, py, pw, ph;
        details_panel_geometry(a, &px, &py, &pw, &ph);
        if (point_in(x, y, px, py, pw, ph) || x >= px - 12) return false;
    }
    int relx = x - content_x;
    int rely = y - content_y + a->grid_scroll;
    if (relx < 0 || rely < 0) return false;
    int col = relx / (layout.card_w + GRID_GAP);
    int row = rely / layout.row_step;
    if (col < 0 || col >= layout.cols ||
        relx % (layout.card_w + GRID_GAP) >= layout.card_w ||
        rely % layout.row_step >= layout.card_h) return false;
    size_t fidx = (size_t)row * (size_t)layout.cols + (size_t)col;
    if (fidx >= a->filtered_len) return false;
    if (fidx_out) *fidx_out = fidx;
    return true;
}

static void update_browse_hover(app_t *a, int x, int y) {
    if (!a) return;
    int64_t now = monotonic_ms();
    a->mouse_x = x;
    a->mouse_y = y;
    a->mouse_inside = true;
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

static bool step_browse_animations(app_t *a, int64_t now) {
    if (!a || a->screen != SCREEN_BROWSE) return false;
    bool active = false;
    bool scroll_changed = false;
    if (a->grid_scroll_animating) {
        if (a->grid_scroll_last_ms <= 0) a->grid_scroll_last_ms = now;
        int64_t elapsed = now - a->grid_scroll_last_ms;
        if (elapsed < 1) elapsed = 1;
        if (elapsed > 32) elapsed = 32;
        a->grid_scroll_last_ms = now;
        int diff = a->grid_scroll_target - a->grid_scroll;
        if (diff == 0) {
            a->grid_scroll_animating = false;
        } else {
            int step = (int)((int64_t)diff * elapsed / 85LL);
            if (step == 0) step = diff > 0 ? 1 : -1;
            if ((diff > 0 && step > diff) || (diff < 0 && step < diff)) step = diff;
            a->grid_scroll += step;
            scroll_changed = true;
            if (a->grid_scroll == a->grid_scroll_target) a->grid_scroll_animating = false;
            else active = true;
        }
    }
    if (scroll_changed && a->mouse_inside)
        update_browse_hover(a, a->mouse_x, a->mouse_y);
    if (a->hovered_card_valid) {
        if (vip_ui_motion_step(&a->hover_motion, now, 140)) active = true;
        if (a->hover_motion.value <= 0.0f && a->hover_motion.target <= 0.0f)
            a->hovered_card_valid = false;
    }
    a->ui_motion_active = active;
    return active;
}

'''
s = s.replace(marker, helpers + marker, 1)

# 3. Filtering/navigation invalidates transient motion state.
old = '''    free(seen);\n    pthread_mutex_unlock(&a->data_mutex);\n    a->grid_scroll = 0;\n    a->focused_filtered = 0;\n}'''
new = '''    free(seen);\n    pthread_mutex_unlock(&a->data_mutex);\n    a->grid_scroll = 0;\n    a->grid_scroll_target = 0;\n    a->grid_scroll_animating = false;\n    a->grid_scroll_last_ms = monotonic_ms();\n    a->hovered_card_valid = false;\n    vip_ui_motion_init(&a->hover_motion, 0.0f, a->grid_scroll_last_ms);\n    a->ui_motion_active = false;\n    a->focused_filtered = 0;\n}'''
assert old in s
s = s.replace(old, new, 1)

# 4. Card block: animate the complete card by temporarily offsetting row y.
loop_marker = '        for (int col = 0; col < layout.cols; ++col) {'
loop_start = s.index(loop_marker)
brace_start = s.index('{', loop_start)
depth = 0
loop_end = None
for i in range(brace_start, len(s)):
    if s[i] == '{': depth += 1
    elif s[i] == '}':
        depth -= 1
        if depth == 0:
            loop_end = i
            break
assert loop_end is not None
block = s[loop_start:loop_end+1]
old = '''            int cx = content_x + col * (layout.card_w + GRID_GAP);\n            bool focused = fidx == a->focused_filtered;\n            fill_round_rect(a, cx + 4, cy + 6, layout.card_w, layout.card_h, 16, a->colors.black);'''
new = '''            int cx = content_x + col * (layout.card_w + GRID_GAP);\n            bool focused = fidx == a->focused_filtered;\n            bool hovered = a->hovered_card_valid && fidx == a->hovered_filtered &&\n                           a->hover_motion.value > 0.001f;\n            float hover_eased = hovered ? vip_ui_ease_out_cubic(a->hover_motion.value) : 0.0f;\n            int lift = (int)(8.0f * hover_eased + 0.5f);\n            int base_cy = cy;\n            cy = base_cy - lift;\n            bool active_card = focused || hovered;\n            fill_round_rect(a, cx + 4, cy + 6 + (int)(3.0f * hover_eased), layout.card_w, layout.card_h, 16, a->colors.black);'''
assert old in block
block = block.replace(old, new, 1)
block = block.replace('            if (focused) {', '            if (active_card) {', 1)
block = block.replace('focused ? a->colors.accent : a->colors.border', 'active_card ? a->colors.accent : a->colors.border')
block = block.replace('focused ? a->font_heading : a->font', 'active_card ? a->font_heading : a->font')
# Add a visible mouse action, after artwork border is drawn.
needle = '''            stroke_round_rect(a, cx, cy, layout.card_w, layout.art_h, 14, active_card ? a->colors.accent : a->colors.border);\n'''
assert needle in block
block = block.replace(needle, needle + '''            if (hovered && hover_eased > 0.30f) {\n                int open_w = 78, open_h = 30;\n                int open_x = cx + 8, open_y = cy + layout.art_h - open_h - 8;\n                fill_round_rect(a, open_x, open_y, open_w, open_h, 11, a->colors.accent2);\n                stroke_round_rect(a, open_x, open_y, open_w, open_h, 11, a->colors.accent);\n                draw_centered_font(a, a->font_small, open_x, open_y + 20, open_w, \"ABRIR\", a->colors.text);\n            }\n''', 1)
# Restore row y before the next column.
block = block[:-1] + '            cy = base_cy;\n' + block[-1:]
s = s[:loop_start] + block + s[loop_end+1:]

# 5. Smooth wheel target instead of direct jumps.
old = '''    } else {\n        card_layout_t layout=browse_layout(a); int rows=(int)((a->filtered_len+(size_t)layout.cols-1u)/(size_t)layout.cols);\n        int content_h=rows*layout.row_step; int maxscroll=content_h-(a->height-TOPBAR_H-18); if(maxscroll<0)maxscroll=0;\n        a->grid_scroll+=direction*(layout.mode==ART_PORTRAIT?220:180); if(a->grid_scroll<0)a->grid_scroll=0; if(a->grid_scroll>maxscroll)a->grid_scroll=maxscroll;\n    }'''
new = '''    } else {\n        card_layout_t layout=browse_layout(a); int rows=(int)((a->filtered_len+(size_t)layout.cols-1u)/(size_t)layout.cols);\n        int content_h=rows*layout.row_step; int maxscroll=content_h-(a->height-TOPBAR_H-18); if(maxscroll<0)maxscroll=0;\n        int base = a->grid_scroll_animating ? a->grid_scroll_target : a->grid_scroll;\n        int target = base + direction*(layout.mode==ART_PORTRAIT?220:180);\n        if(target<0)target=0; if(target>maxscroll)target=maxscroll;\n        a->grid_scroll_target=target;\n        a->grid_scroll_last_ms=monotonic_ms();\n        a->grid_scroll_animating=target!=a->grid_scroll;\n        if(a->grid_scroll_animating)a->ui_motion_active=true;\n    }'''
assert old in s
s = s.replace(old, new, 1)

# 6. Keyboard focus scroll is immediate and becomes the new animation target.
old = '''    if(row_top<a->grid_scroll)a->grid_scroll=row_top;\n    else if(row_top+layout.card_h>a->grid_scroll+viewport_h)a->grid_scroll=row_top+layout.card_h-viewport_h;\n    if(a->grid_scroll<0)a->grid_scroll=0;\n}'''
new = '''    if(row_top<a->grid_scroll)a->grid_scroll=row_top;\n    else if(row_top+layout.card_h>a->grid_scroll+viewport_h)a->grid_scroll=row_top+layout.card_h-viewport_h;\n    if(a->grid_scroll<0)a->grid_scroll=0;\n    a->grid_scroll_target=a->grid_scroll;\n    a->grid_scroll_animating=false;\n}'''
assert old in s
s = s.replace(old, new, 1)

# 7. Browse mouse motion and leave events.
old = '''        case MotionNotify:\n            if(a->screen==SCREEN_PLAYER){'''
new = '''        case MotionNotify:\n            if(a->screen==SCREEN_BROWSE && e->xmotion.window==a->win){\n                update_browse_hover(a,e->xmotion.x,e->xmotion.y);\n            } else if(a->screen==SCREEN_PLAYER){'''
assert old in s
s = s.replace(old, new, 1)

old = '''            }break;\n        case ButtonRelease: if(e->xbutton.button==Button1){'''
new = '''            }break;\n        case LeaveNotify:\n            if(a->screen==SCREEN_BROWSE && a->hovered_card_valid){\n                a->mouse_inside=false;\n                vip_ui_motion_set_target(&a->hover_motion,0.0f,monotonic_ms());\n                a->ui_motion_active=true;\n            }\n            break;\n        case ButtonRelease: if(e->xbutton.button==Button1){'''
assert old in s
s = s.replace(old, new, 1)

old = 'ExposureMask|KeyPressMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|StructureNotifyMask|FocusChangeMask|PropertyChangeMask'
new = 'ExposureMask|KeyPressMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|EnterWindowMask|LeaveWindowMask|StructureNotifyMask|FocusChangeMask|PropertyChangeMask'
assert old in s
s = s.replace(old, new, 1)

# 8. 60 fps only while a browse animation is active.
old = '''        int64_t now=monotonic_ms();\n        if (a.screen==SCREEN_PLAYER) save_current_progress(&a,false);'''
new = '''        int64_t now=monotonic_ms();\n        bool ui_animating=step_browse_animations(&a,now);\n        if (a.screen==SCREEN_PLAYER) save_current_progress(&a,false);'''
assert old in s
s = s.replace(old, new, 1)
old = '''        if (a.test_exit_at_ms>0 && now>=a.test_exit_at_ms) a.quit=true;\n        if (now>=next_draw) { redraw(&a); next_draw=now+(a.screen==SCREEN_PLAYER?33:100); }'''
new = '''        if (a.test_exit_at_ms>0 && now>=a.test_exit_at_ms) a.quit=true;\n        if (now>=next_draw || ui_animating) {\n            redraw(&a);\n            next_draw=now+(a.screen==SCREEN_PLAYER?33:(ui_animating?16:100));\n        }'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
