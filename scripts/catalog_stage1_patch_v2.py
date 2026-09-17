from pathlib import Path
import re

p = Path('src/ui_x11/x11_app.c')
s = p.read_text()


def sub_once(pattern: str, replacement: str, flags=0):
    global s
    s2, n = re.subn(pattern, lambda m: replacement, s, count=1, flags=flags)
    if n != 1:
        raise SystemExit(f'pattern matched {n} times: {pattern[:100]}')
    s = s2


sub_once(r'(\s+bool series_episode_mode;\n)', r'\1    bool series_season_select;\n')

sub_once(
    r'static void rebuild_filter\(app_t \*a\) \{.*?\n\}\n\nstatic void recalc_category_counts',
    r'''static void rebuild_filter(app_t *a) {
    pthread_mutex_lock(&a->data_mutex);
    size_t need = a->series_season_select ? ACTIVE_CATEGORIES(a).len : ACTIVE_CHANNELS(a).len;
    if (need > a->filtered_cap) {
        size_t cap = need ? need : 1;
        size_t *p = realloc(a->filtered, cap * sizeof(*p));
        if (p) { a->filtered = p; a->filtered_cap = cap; }
    }
    a->filtered_len = 0;
    if (a->filtered && a->series_season_select) {
        for (size_t i = 0; i < ACTIVE_CATEGORIES(a).len; ++i) {
            vip_category_t *season = &ACTIVE_CATEGORIES(a).items[i];
            if (!contains_ascii_case(season->name, a->search)) continue;
            a->filtered[a->filtered_len++] = i;
        }
    } else {
        const char *cat = NULL;
        if (a->selected_category >= 0 && (size_t)a->selected_category < ACTIVE_CATEGORIES(a).len)
            cat = ACTIVE_CATEGORIES(a).items[a->selected_category].id;
        if (a->filtered) {
            for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i) {
                vip_channel_t *ch = &ACTIVE_CHANNELS(a).items[i];
                if (cat && (!ch->category_id || strcmp(ch->category_id, cat) != 0)) continue;
                if (a->favorites_only && (!a->favorite_flags || !a->favorite_flags[i])) continue;
                if (!contains_ascii_case(ch->name, a->search)) continue;
                a->filtered[a->filtered_len++] = i;
            }
        }
    }
    pthread_mutex_unlock(&a->data_mutex);
    a->grid_scroll = 0;
    a->focused_filtered = 0;
}

static void recalc_category_counts''',
    re.S)

sub_once(r'(static const char \*content_plural\(app_t \*a\) \{\n)', r'\1    if (a->series_season_select) return "temporadas";\n')
sub_once(r'(static const char \*all_content_label\(app_t \*a\) \{\n)', r'\1    if (a->series_season_select) return "Todas as temporadas";\n')

sub_once(
    r'(static void switch_content\(app_t \*a, content_kind_t kind\) \{.*?a->series_episode_mode = false;\n)',
    r'\1    a->series_season_select = false;\n',
    re.S)

sub_once(
    r'static void return_from_episode_list\(app_t \*a\) \{.*?\n\}\n\nstatic void thumbnail_ready',
    r'''static void return_from_episode_list(app_t *a) {
    if (!a || !a->series_episode_mode) return;
    if (!a->series_season_select) {
        a->series_season_select = true;
        a->selected_category = -1;
        a->category_scroll = 0;
        a->grid_scroll = 0;
        a->focused_filtered = 0;
        a->search[0] = '\0';
        a->input_focus = INPUT_SEARCH;
        snprintf(a->status, sizeof(a->status), "Escolha uma temporada de %s", a->series_title);
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
    a->input_focus = INPUT_SEARCH;
    recalc_category_counts(a);
    load_media_state(a);
    rebuild_filter(a);
}

static void thumbnail_ready''',
    re.S)

sub_once(
    r'(a->content_kind = CONTENT_LIVE;\n\s*a->series_episode_mode = false;\n)',
    r'\1        a->series_season_select = false;\n')

sub_once(
    r'a->series_episode_mode=true;a->favorites_only=false;',
    r'a->series_episode_mode=true;a->series_season_select=true;a->favorites_only=false;')

helper_marker = 'static int category_visible_rows(app_t *a) { int n = (a->height - TOPBAR_H - 50) / 40; return n > 1 ? n : 1; }\n\n'
if helper_marker not in s:
    raise SystemExit('category_visible_rows marker missing')
helpers = r'''static size_t season_episode_count(app_t *a, size_t season_index) {
    if (!a || season_index >= ACTIVE_CATEGORIES(a).len) return 0u;
    const char *season_id = ACTIVE_CATEGORIES(a).items[season_index].id;
    if (!season_id) return 0u;
    size_t count = 0u;
    for (size_t i = 0; i < ACTIVE_CHANNELS(a).len; ++i) {
        const char *cid = ACTIVE_CHANNELS(a).items[i].category_id;
        if (cid && strcmp(cid, season_id) == 0) ++count;
    }
    return count;
}

static void select_season(app_t *a, size_t season_index) {
    if (!a || !a->series_episode_mode || !a->series_season_select ||
        season_index >= ACTIVE_CATEGORIES(a).len) return;
    a->series_season_select = false;
    a->selected_category = (int)season_index;
    a->category_scroll = 0;
    a->grid_scroll = 0;
    a->focused_filtered = 0;
    a->search[0] = '\0';
    a->input_focus = INPUT_SEARCH;
    snprintf(a->status, sizeof(a->status), "%s • %s", a->series_title,
             ACTIVE_CATEGORIES(a).items[season_index].name);
    rebuild_filter(a);
}

static void draw_season_selector(app_t *a) {
    int content_x = SIDEBAR_W + 28;
    int content_y = TOPBAR_H + 28;
    int available = a->width - content_x - 28;
    int gap = 18;
    int card_w = available >= 940 ? (available - gap * 2) / 3 :
                 available >= 610 ? (available - gap) / 2 : available;
    if (card_w < 250) card_w = 250;
    int card_h = 118;
    int cols = (available + gap) / (card_w + gap);
    if (cols < 1) cols = 1;
    int row_step = card_h + gap;

    draw_text(a, content_x, content_y, "Escolha a temporada", a->colors.text);
    char subtitle[420];
    snprintf(subtitle, sizeof(subtitle), "%s  •  %zu temporadas  •  %zu episódios",
             a->series_title, ACTIVE_CATEGORIES(a).len, ACTIVE_CHANNELS(a).len);
    draw_text(a, content_x, content_y + 24, subtitle, a->colors.muted);
    content_y += 54;

    if (a->filtered_len == 0u) {
        draw_text(a, content_x, content_y + 28, "Nenhuma temporada encontrada", a->colors.muted);
        return;
    }

    int first_row = a->grid_scroll / row_step;
    int offset = -(a->grid_scroll % row_step);
    int visible_rows = (a->height - content_y) / row_step + 3;
    for (int rr = 0; rr < visible_rows; ++rr) {
        int row = first_row + rr;
        int cy = content_y + offset + rr * row_step;
        for (int col = 0; col < cols; ++col) {
            size_t fidx = (size_t)row * (size_t)cols + (size_t)col;
            if (fidx >= a->filtered_len) break;
            size_t season_index = a->filtered[fidx];
            if (season_index >= ACTIVE_CATEGORIES(a).len) continue;
            vip_category_t *season = &ACTIVE_CATEGORIES(a).items[season_index];
            int cx = content_x + col * (card_w + gap);
            bool focused = fidx == a->focused_filtered;
            fill_rect(a, cx, cy, (unsigned)card_w, (unsigned)card_h,
                      focused ? a->colors.accent2 : a->colors.panel2);
            stroke_rect(a, cx, cy, (unsigned)card_w, (unsigned)card_h,
                        focused ? a->colors.accent : a->colors.border);
            if (focused)
                stroke_rect(a, cx + 2, cy + 2, (unsigned)(card_w - 4), (unsigned)(card_h - 4), a->colors.accent);
            char title[192]; bounded_text(title, sizeof(title), season->name, 42);
            draw_text(a, cx + 18, cy + 34, title, a->colors.text);
            char meta[96];
            size_t episodes = season_episode_count(a, season_index);
            snprintf(meta, sizeof(meta), "%zu episódio%s", episodes, episodes == 1u ? "" : "s");
            draw_text(a, cx + 18, cy + 61, meta, a->colors.muted);
            draw_text(a, cx + 18, cy + 94, "Abrir temporada", focused ? a->colors.accent : a->colors.muted);
        }
    }
}

'''
s = s.replace(helper_marker, helper_marker + helpers, 1)

sub_once(
    r'(\s+int y = TOPBAR_H \+ 12;\n\s+bool all_sel = a->selected_category < 0;)',
    r'''    if (a->series_season_select) {
        draw_season_selector(a);
        draw_toast(a);
        return;
    }

\1''')

sub_once(
    r'(\s+card_layout_t layout=browse_layout\(a\);\n\s+int content_x=SIDEBAR_W\+20, content_y=TOPBAR_H\+18;\n\s+if \(x<content_x \|\| y<content_y\) return;)',
    r'''    if (a->series_season_select) {
        int content_x = SIDEBAR_W + 28;
        int content_y = TOPBAR_H + 82;
        int available = a->width - content_x - 28;
        int gap = 18;
        int card_w = available >= 940 ? (available - gap * 2) / 3 :
                     available >= 610 ? (available - gap) / 2 : available;
        if (card_w < 250) card_w = 250;
        int card_h = 118;
        int cols = (available + gap) / (card_w + gap);
        if (cols < 1) cols = 1;
        int row_step = card_h + gap;
        if (x < content_x || y < content_y) return;
        int relx = x - content_x, rely = y - content_y + a->grid_scroll;
        int col = relx / (card_w + gap), row = rely / row_step;
        if (col < 0 || col >= cols || relx % (card_w + gap) >= card_w || rely % row_step >= card_h) return;
        size_t fidx = (size_t)row * (size_t)cols + (size_t)col;
        if (fidx < a->filtered_len) {
            a->focused_filtered = fidx;
            select_season(a, a->filtered[fidx]);
        }
        return;
    }
\1''')

sub_once(
    r'''        if \(\(sym == XK_Return \|\| sym == XK_KP_Enter\) && a->filtered_len > 0u\) \{
            if \(a->focused_filtered >= a->filtered_len\) a->focused_filtered = a->filtered_len - 1u;
            activate_item\(a, a->filtered\[a->focused_filtered\]\);
            return;
        \}''',
    r'''        if ((sym == XK_Return || sym == XK_KP_Enter) && a->filtered_len > 0u) {
            if (a->focused_filtered >= a->filtered_len) a->focused_filtered = a->filtered_len - 1u;
            if (a->series_season_select) select_season(a, a->filtered[a->focused_filtered]);
            else activate_item(a, a->filtered[a->focused_filtered]);
            return;
        }''')

sub_once(
    r'''static int browse_columns\(app_t \*a\) \{
    return browse_layout\(a\)\.cols;
\}''',
    r'''static int browse_columns(app_t *a) {
    if (a->series_season_select) {
        int content_x = SIDEBAR_W + 28;
        int available = a->width - content_x - 28;
        int gap = 18;
        int card_w = available >= 940 ? (available - gap * 2) / 3 :
                     available >= 610 ? (available - gap) / 2 : available;
        if (card_w < 250) card_w = 250;
        int cols = (available + gap) / (card_w + gap);
        return cols < 1 ? 1 : cols;
    }
    return browse_layout(a).cols;
}''')

sub_once(
    r'static void ensure_grid_focus_visible\(app_t \*a\) \{.*?\n\}\n\nstatic void move_grid_focus',
    r'''static void ensure_grid_focus_visible(app_t *a) {
    if (a->filtered_len==0) { a->grid_scroll=0; return; }
    if (a->focused_filtered>=a->filtered_len) a->focused_filtered=a->filtered_len-1u;
    if (a->series_season_select) {
        int cols = browse_columns(a);
        int row_step = 136;
        int card_h = 118;
        int row = (int)(a->focused_filtered/(size_t)cols);
        int row_top = row * row_step;
        int content_y = TOPBAR_H + 82;
        int viewport_h = a->height - content_y;
        if (row_top < a->grid_scroll) a->grid_scroll = row_top;
        else if (row_top + card_h > a->grid_scroll + viewport_h) a->grid_scroll = row_top + card_h - viewport_h;
        if (a->grid_scroll < 0) a->grid_scroll = 0;
        return;
    }
    card_layout_t layout=browse_layout(a); int row=(int)(a->focused_filtered/(size_t)layout.cols); int row_top=row*layout.row_step;
    int content_y=TOPBAR_H+18; int viewport_h=a->height-content_y; if(viewport_h<layout.card_h)viewport_h=layout.card_h;
    if(row_top<a->grid_scroll)a->grid_scroll=row_top;
    else if(row_top+layout.card_h>a->grid_scroll+viewport_h)a->grid_scroll=row_top+layout.card_h-viewport_h;
    if(a->grid_scroll<0)a->grid_scroll=0;
}

static void move_grid_focus''',
    re.S)

p.write_text(s)
