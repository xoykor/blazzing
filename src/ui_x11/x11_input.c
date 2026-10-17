/* SPDX-License-Identifier: MIT */
#include "x11_app_internal.h"

/* Handle the browse card at operation. */
static bool browse_point_over_details(app_t *a, int x, int y) {
    if (!details_panel_active(a))
        return false;
    int px, py, pw, ph;
    details_panel_geometry(a, &px, &py, &pw, &ph);
    return point_in(x, y, px, py, pw, ph) || x >= px - 12;
}

static bool browse_grid_position(app_t *a, int x, int y, card_layout_t layout, int *row, int *col) {
    int content_x = SIDEBAR_W + 20;
    int content_y = TOPBAR_H + 18;
    if (x < content_x || y < content_y || x >= a->width || y >= a->height)
        return false;
    int relx = x - content_x;
    int rely = y - content_y + a->grid_scroll;
    if (relx < 0 || rely < 0)
        return false;
    *col = relx / (layout.card_w + GRID_GAP);
    *row = rely / layout.row_step;
    return *col >= 0 && *col < layout.cols &&
           relx % (layout.card_w + GRID_GAP) < layout.card_w &&
           rely % layout.row_step < layout.card_h;
}

static bool browse_card_at(app_t *a, int x, int y, size_t *fidx_out) {
    if (!a || a->screen != SCREEN_BROWSE || a->filtered_len == 0u || browse_point_over_details(a, x, y))
        return false;

    card_layout_t layout = browse_layout(a);
    int row = 0, col = 0;
    if (!browse_grid_position(a, x, y, layout, &row, &col))
        return false;

    size_t fidx = (size_t)row * (size_t)layout.cols + (size_t)col;
    if (fidx >= a->filtered_len)
        return false;
    if (fidx_out)
        *fidx_out = fidx;
    return true;
}

/* Handle the browse control at operation. */
static int browse_top_control_at(app_t *a, int x, int y) {
    const int tab_x[3] = {8, 88, 174};
    const int tab_w[3] = {74, 80, 88};
    if (y >= 12 && y < 58 && x < SIDEBAR_W) {
        for (int k = 0; k < 3; ++k)
            if (point_in(x, y, tab_x[k], 12, tab_w[k], 46))
                return HOVER_TAB_BASE + k;
    }

    int list_w = 94, fav_w = 174;
    int list_x = a->width - list_w - 18;
    int fav_x = list_x - fav_w - 10;
    int search_w = fav_x - (SIDEBAR_W + 18) - 10;
    if (search_w < 180)
        search_w = 180;
    if (point_in(x, y, SIDEBAR_W + 18, 12, search_w, 46))
        return HOVER_SEARCH;
    if (point_in(x, y, fav_x, 12, fav_w, 46))
        return HOVER_FAVORITES;
    if (point_in(x, y, list_x, 12, list_w, 46))
        return HOVER_LISTS;
    return HOVER_NONE;
}

static int browse_sidebar_control_at(app_t *a, int x, int y) {
    if (x >= SIDEBAR_W || y < TOPBAR_H)
        return HOVER_NONE;
    int base = TOPBAR_H + 12;
    if (a->series_episode_mode && point_in(x, y, 8, base, SIDEBAR_W - 16, 38))
        return HOVER_BACK;
    if (a->series_episode_mode)
        base += 48;
    if (point_in(x, y, 8, base, SIDEBAR_W - 16, 36))
        return HOVER_CATEGORY_ALL;

    int local = y - (base + 42);
    if (local < 0 || local % 42 >= 36)
        return HOVER_NONE;
    int idx = a->category_scroll + local / 42;
    if (idx < 0 || (size_t)idx >= ACTIVE_CATEGORIES(a).len)
        return HOVER_NONE;
    return HOVER_CATEGORY_BASE + idx;
}

static int browse_control_at(app_t *a, int x, int y) {
    if (!a || a->screen != SCREEN_BROWSE)
        return HOVER_NONE;
    int control = browse_top_control_at(a, x, y);
    return control != HOVER_NONE ? control : browse_sidebar_control_at(a, x, y);
}

/* Update browse hover. */
void update_browse_hover(app_t *a, int x, int y) {
    if (!a)
        return;
    int64_t now = monotonic_ms();
    a->mouse_x = x;
    a->mouse_y = y;
    a->mouse_inside = true;

    int control = browse_control_at(a, x, y);
    if (control != a->hovered_control) {
        a->hovered_control = control;
        vip_ui_motion_init(&a->control_motion, 0.0f, now);
        if (control != HOVER_NONE)
            vip_ui_motion_set_target(&a->control_motion, 1.0f, now);
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


void browse_sync_input_focus(app_t *a) {
    if (!a)
        return;
    a->input_focus =
        (a->browse_focus == BROWSE_FOCUS_TOP && a->browse_top_focus == BROWSE_TOP_SEARCH) ? INPUT_SEARCH : 0;
}

void browse_focus_top(app_t *a, int item) {
    if (!a)
        return;
    if (item < BROWSE_TOP_TV)
        item = BROWSE_TOP_TV;
    if (item > BROWSE_TOP_LISTS)
        item = BROWSE_TOP_LISTS;
    a->browse_focus = BROWSE_FOCUS_TOP;
    a->browse_top_focus = item;
    browse_sync_input_focus(a);
}

void browse_focus_grid(app_t *a) {
    if (!a)
        return;
    a->browse_focus = BROWSE_FOCUS_GRID;
    browse_sync_input_focus(a);
}

void browse_sidebar_ensure_visible(app_t *a) {
    if (!a || a->browse_sidebar_focus < 0)
        return;
    int rows = category_visible_rows(a) - 1;
    if (rows < 1)
        rows = 1;
    if (a->browse_sidebar_focus < a->category_scroll)
        a->category_scroll = a->browse_sidebar_focus;
    if (a->browse_sidebar_focus >= a->category_scroll + rows)
        a->category_scroll = a->browse_sidebar_focus - rows + 1;
    int max_scroll = (int)ACTIVE_CATEGORIES(a).len - rows;
    if (max_scroll < 0)
        max_scroll = 0;
    if (a->category_scroll > max_scroll)
        a->category_scroll = max_scroll;
}

void browse_focus_sidebar(app_t *a, int item) {
    if (!a)
        return;
    int min_item = a->series_episode_mode ? -2 : -1;
    int max_item = ACTIVE_CATEGORIES(a).len > 0u ? (int)ACTIVE_CATEGORIES(a).len - 1 : -1;
    if (item < min_item)
        item = min_item;
    if (item > max_item)
        item = max_item;
    a->browse_focus = BROWSE_FOCUS_SIDEBAR;
    a->browse_sidebar_focus = item;
    browse_sidebar_ensure_visible(a);
    browse_sync_input_focus(a);
}



static void browse_activate_top(app_t *a) {
    if (!a)
        return;
    switch (a->browse_top_focus) {
    case BROWSE_TOP_TV:
    case BROWSE_TOP_MOVIES:
    case BROWSE_TOP_SERIES: {
        int item = a->browse_top_focus;
        switch_content(a, (content_kind_t)item);
        browse_focus_top(a, item);
        break;
    }
    case BROWSE_TOP_SEARCH:
        a->input_focus = INPUT_SEARCH;
        break;
    case BROWSE_TOP_FAVORITES:
        a->favorites_only = !a->favorites_only;
        rebuild_filter(a);
        browse_focus_top(a, BROWSE_TOP_FAVORITES);
        break;
    case BROWSE_TOP_LISTS:
        cancel_browse_async_workers(a);
        clear_details_view(a);
        refresh_profiles(a);
        a->screen = SCREEN_LOGIN;
        a->input_focus = INPUT_MODE;
        snprintf(a->status, sizeof(a->status), "Escolha uma lista salva ou conecte outra");
        break;
    default:
        break;
    }
}

static void browse_activate_sidebar(app_t *a) {
    if (!a)
        return;
    if (a->browse_sidebar_focus == -2 && a->series_episode_mode) {
        return_from_episode_list(a);
        browse_focus_sidebar(a, -1);
        return;
    }
    if (a->browse_sidebar_focus == -1) {
        choose_category(a, -1);
        return;
    }
    if (a->browse_sidebar_focus >= 0 &&
        (size_t)a->browse_sidebar_focus < ACTIVE_CATEGORIES(a).len)
        choose_category(a, a->browse_sidebar_focus);
}


void login_move_focus(app_t *a, int direction) {
    static const int xtream_order[] = {
        INPUT_MODE, INPUT_PROFILE_NAME, INPUT_SERVER, INPUT_SERVER_ALT,
        INPUT_USERNAME, INPUT_PASSWORD, INPUT_CONNECT
    };
    static const int m3u_order[] = {
        INPUT_MODE, INPUT_PROFILE_NAME, INPUT_SERVER, INPUT_PHONE, INPUT_CONNECT
    };
    const int *order = a->login_mode == LOGIN_M3U ? m3u_order : xtream_order;
    size_t len = a->login_mode == LOGIN_M3U ? sizeof(m3u_order) / sizeof(m3u_order[0])
                                             : sizeof(xtream_order) / sizeof(xtream_order[0]);
    size_t current = 0u;
    for (size_t i = 0; i < len; ++i) {
        if (order[i] == a->input_focus) {
            current = i;
            break;
        }
    }
    long next = (long)current + (direction < 0 ? -1L : 1L);
    if (next < 0)
        next = (long)len - 1L;
    if ((size_t)next >= len)
        next = 0L;
    a->input_focus = order[(size_t)next];
}

void login_select_mode(app_t *a, login_mode_t mode) {
    if (!a)
        return;
    if (mode != LOGIN_M3U && a->pairing_relay)
        stop_phone_pairing(a);
    a->login_mode = mode;
    a->input_focus = INPUT_MODE;
}

void login_profile_ensure_visible(app_t *a) {
    if (!a || a->profiles.len == 0u) {
        if (a) {
            a->profile_focus = 0;
            a->profile_scroll = 0;
        }
        return;
    }
    if (a->profile_focus < 0)
        a->profile_focus = 0;
    if ((size_t)a->profile_focus >= a->profiles.len)
        a->profile_focus = (int)a->profiles.len - 1;
    const int visible = 6;
    if (a->profile_focus < a->profile_scroll)
        a->profile_scroll = a->profile_focus;
    if (a->profile_focus >= a->profile_scroll + visible)
        a->profile_scroll = a->profile_focus - visible + 1;
    int max_scroll = (int)a->profiles.len - visible;
    if (max_scroll < 0)
        max_scroll = 0;
    if (a->profile_scroll < 0)
        a->profile_scroll = 0;
    if (a->profile_scroll > max_scroll)
        a->profile_scroll = max_scroll;
}

void login_focus_saved_profiles(app_t *a) {
    if (!a || a->pairing_relay || a->profiles.len == 0u)
        return;
    login_profile_ensure_visible(a);
    a->input_focus = INPUT_SAVED_PROFILE;
}


/* Handle the request paste operation. */
static void request_paste(app_t *a, Atom selection) {
    if (a->input_focus == 0)
        return;
    a->paste_target = a->input_focus;
    XConvertSelection(a->dpy, selection, a->utf8, a->paste_property, a->win, CurrentTime);
}

/* Handle the active input operation. */
static char *active_input(app_t *a, size_t *cap) {
    switch (a->input_focus) {
    case INPUT_SERVER:
        *cap = sizeof(a->server);
        return a->server;
    case INPUT_SERVER_ALT:
        *cap = sizeof(a->server_alt);
        return a->server_alt;
    case INPUT_USERNAME:
        *cap = sizeof(a->username);
        return a->username;
    case INPUT_PASSWORD:
        *cap = sizeof(a->password);
        return a->password;
    case INPUT_SEARCH:
        *cap = sizeof(a->search);
        return a->search;
    case INPUT_PROFILE_NAME:
        *cap = sizeof(a->profile_name);
        return a->profile_name;
    default:
        *cap = 0;
        return NULL;
    }
}

/* Append input. */
static void append_input(app_t *a, const char *text, size_t n) {
    size_t cap = 0;
    char *dst = active_input(a, &cap);
    if (!dst || cap == 0 || !text)
        return;
    size_t cur = strlen(dst);
    if (n > cap - 1 - cur)
        n = cap - 1 - cur;
    memcpy(dst + cur, text, n);
    dst[cur + n] = '\0';
    if (a->input_focus == INPUT_SEARCH)
        rebuild_filter(a);
}

/* Handle the backspace input operation. */
static void backspace_input(app_t *a) {
    size_t cap = 0;
    char *dst = active_input(a, &cap);
    (void)cap;
    if (!dst)
        return;
    size_t n = strlen(dst);
    if (n == 0)
        return;
    do {
        --n;
    } while (n > 0 && (((unsigned char)dst[n] & 0xc0u) == 0x80u));
    dst[n] = '\0';
    if (a->input_focus == INPUT_SEARCH)
        rebuild_filter(a);
}


/* Handle browse click. */
static bool handle_browse_top_click(app_t *a, int x, int y) {
    const int tab_x[3] = {8, 88, 174};
    const int tab_w[3] = {74, 80, 88};
    if (y >= 12 && y < 58 && x < SIDEBAR_W) {
        for (int k = 0; k < 3; ++k) {
            if (!point_in(x, y, tab_x[k], 12, tab_w[k], 46))
                continue;
            switch_content(a, (content_kind_t)k);
            browse_focus_top(a, k);
            return true;
        }
    }

    int list_w = 94, fav_w = 174;
    int list_x = a->width - list_w - 18;
    int fav_x = list_x - fav_w - 10;
    int search_w = fav_x - (SIDEBAR_W + 18) - 10;
    if (search_w < 180)
        search_w = 180;

    if (point_in(x, y, SIDEBAR_W + 18, 12, search_w, 46)) {
        browse_focus_top(a, BROWSE_TOP_SEARCH);
        return true;
    }
    if (point_in(x, y, fav_x, 12, fav_w, 46)) {
        browse_focus_top(a, BROWSE_TOP_FAVORITES);
        a->favorites_only = !a->favorites_only;
        rebuild_filter(a);
        browse_focus_top(a, BROWSE_TOP_FAVORITES);
        return true;
    }
    if (!point_in(x, y, list_x, 12, list_w, 46))
        return false;

    browse_focus_top(a, BROWSE_TOP_LISTS);
    cancel_browse_async_workers(a);
    clear_details_view(a);
    refresh_profiles(a);
    a->screen = SCREEN_LOGIN;
    a->input_focus = INPUT_SERVER;
    snprintf(a->status, sizeof(a->status), "Escolha uma lista salva ou conecte outra");
    return true;
}

static bool handle_browse_sidebar_click(app_t *a, int x, int y) {
    if (x >= SIDEBAR_W || y < TOPBAR_H)
        return false;

    int base = TOPBAR_H + 12;
    if (a->series_episode_mode && point_in(x, y, 8, base, SIDEBAR_W - 16, 38)) {
        browse_focus_sidebar(a, -2);
        return_from_episode_list(a);
        browse_focus_sidebar(a, -1);
        return true;
    }

    int local = y - browse_sidebar_category_y(a);
    if (local >= 0 && local < 36) {
        browse_focus_sidebar(a, -1);
        choose_category(a, -1);
        return true;
    }

    local -= 42;
    if (local >= 0 && local % 42 < 36) {
        int idx = a->category_scroll + local / 42;
        if (idx >= 0 && (size_t)idx < ACTIVE_CATEGORIES(a).len) {
            browse_focus_sidebar(a, idx);
            choose_category(a, idx);
        }
    }
    return true;
}

static bool handle_details_panel_click(app_t *a, int x, int y) {
    if (!details_panel_active(a))
        return false;

    int px, py, pw, ph;
    details_panel_geometry(a, &px, &py, &pw, &ph);
    if (!point_in(x, y, px, py, pw, ph))
        return false;

    if (a->filtered_len > 0u) {
        if (a->focused_filtered >= a->filtered_len)
            a->focused_filtered = a->filtered_len - 1u;
        size_t chidx = a->filtered[a->focused_filtered];
        int fav_w = 92, fav_h = 32;
        int fav_x = px + pw - fav_w - 14, fav_y = py + 10;
        if (point_in(x, y, fav_x, fav_y, fav_w, fav_h))
            toggle_favorite(a, chidx);
    }
    return true;
}

static void handle_browse_card_click(app_t *a, int x, int y) {
    card_layout_t layout = browse_layout(a);
    int content_x = SIDEBAR_W + 20, content_y = TOPBAR_H + 18;
    if (x < content_x || y < content_y)
        return;

    int relx = x - content_x;
    int rely = y - content_y + a->grid_scroll;
    int col = relx / (layout.card_w + GRID_GAP);
    int row = rely / layout.row_step;
    if (col < 0 || col >= layout.cols || relx % (layout.card_w + GRID_GAP) >= layout.card_w ||
        rely % layout.row_step >= layout.card_h)
        return;

    size_t fidx = (size_t)row * (size_t)layout.cols + (size_t)col;
    if (fidx >= a->filtered_len)
        return;

    browse_focus_grid(a);
    a->focused_filtered = fidx;
    size_t chidx = a->filtered[fidx];
    int cx = content_x + col * (layout.card_w + GRID_GAP);
    int cy = content_y - a->grid_scroll + row * layout.row_step;
    if (point_in(x, y, cx + layout.card_w - 66, cy + 2, 64, 38))
        toggle_favorite(a, chidx);
    else
        activate_item(a, chidx);
}

static void handle_browse_click(app_t *a, int x, int y) {
    if (handle_browse_top_click(a, x, y) ||
        handle_browse_sidebar_click(a, x, y) ||
        handle_details_panel_click(a, x, y))
        return;
    handle_browse_card_click(a, x, y);
}

/* Handle click. */
static void handle_saved_profile_click(app_t *a, int x, int y, int list_x, int list_w, int py) {
    int row_y = py + 154;
    for (int r = 0; r < 6; ++r) {
        int idx = a->profile_scroll + r;
        if (idx < 0 || (size_t)idx >= a->profiles.len)
            break;
        if (point_in(x, y, list_x, row_y, list_w, 52)) {
            a->profile_focus = idx;
            load_profile_into_form(a, (size_t)idx);
            if (cached_m3u_matches(a))
                start_login(a, false);
            return;
        }
        row_y += 60;
    }
    a->input_focus = 0;
}

static bool handle_login_mode_click(app_t *a, int x, int y, int form_x, int form_w, int py) {
    int mode_w = (form_w - 10) / 2;
    if (point_in(x, y, form_x, py + 88, mode_w, 42)) {
        login_select_mode(a, LOGIN_XTREAM);
        return true;
    }
    if (point_in(x, y, form_x + mode_w + 10, py + 88, mode_w, 42)) {
        login_select_mode(a, LOGIN_M3U);
        return true;
    }
    return false;
}

static bool handle_login_field_click(app_t *a, int x, int y, int form_x, int form_w, int py) {
    if (point_in(x, y, form_x, py + 142, form_w, 44))
        a->input_focus = INPUT_PROFILE_NAME;
    else if (point_in(x, y, form_x, py + 196, form_w, 44))
        a->input_focus = INPUT_SERVER;
    else if (a->login_mode == LOGIN_XTREAM && point_in(x, y, form_x, py + 250, form_w, 44))
        a->input_focus = INPUT_SERVER_ALT;
    else if (a->login_mode == LOGIN_XTREAM && point_in(x, y, form_x, py + 304, form_w, 44))
        a->input_focus = INPUT_USERNAME;
    else if (a->login_mode == LOGIN_XTREAM && point_in(x, y, form_x, py + 358, form_w, 44))
        a->input_focus = INPUT_PASSWORD;
    else
        return false;
    return true;
}

static bool handle_login_action_click(app_t *a, int x, int y, int form_x, int form_w, int py) {
    if (a->login_mode == LOGIN_M3U && point_in(x, y, form_x, py + 314, form_w, 46)) {
        a->input_focus = INPUT_PHONE;
        start_phone_pairing(a);
        return true;
    }
    if (point_in(x, y, form_x, py + 430, form_w, 50)) {
        a->input_focus = INPUT_CONNECT;
        start_login(a, true);
        return true;
    }
    return false;
}

static void handle_login_click(app_t *a, int x, int y) {
    int w = a->width > 1120 ? 1080 : a->width - 40;
    if (w < 720)
        w = 720;
    int h = 620, px = (a->width - w) / 2, py = (a->height - h) / 2;
    if (py < 18)
        py = 18;
    int form_x = px + 34;
    int form_w = (w * 58) / 100 - 50;
    int list_x = px + (w * 60) / 100;
    int list_w = w - (list_x - px) - 34;

    if (handle_login_mode_click(a, x, y, form_x, form_w, py) ||
        handle_login_field_click(a, x, y, form_x, form_w, py) ||
        handle_login_action_click(a, x, y, form_x, form_w, py))
        return;
    handle_saved_profile_click(a, x, y, list_x, list_w, py);
}



static void handle_click(app_t *a, int x, int y) {
    if (a->screen == SCREEN_LOGIN)
        handle_login_click(a, x, y);
    else if (a->screen == SCREEN_BROWSE)
        handle_browse_click(a, x, y);
    else
        handle_player_click(a, x, y);
}

/* Handle wheel. */
static void handle_login_wheel(app_t *a, int direction) {
    if (a->input_focus == INPUT_SAVED_PROFILE && a->profiles.len > 0u) {
        a->profile_focus += direction;
        login_profile_ensure_visible(a);
        return;
    }
    int max_scroll = (int)a->profiles.len - 6;
    if (max_scroll < 0)
        max_scroll = 0;
    a->profile_scroll += direction;
    if (a->profile_scroll < 0)
        a->profile_scroll = 0;
    if (a->profile_scroll > max_scroll)
        a->profile_scroll = max_scroll;
}

static void handle_category_wheel(app_t *a, int direction) {
    int max_scroll = (int)ACTIVE_CATEGORIES(a).len - (category_visible_rows(a) - 1);
    if (max_scroll < 0)
        max_scroll = 0;
    a->category_scroll += direction * 3;
    if (a->category_scroll < 0)
        a->category_scroll = 0;
    if (a->category_scroll > max_scroll)
        a->category_scroll = max_scroll;
}

static void handle_grid_wheel(app_t *a, int direction) {
    card_layout_t layout = browse_layout(a);
    int rows = (int)((a->filtered_len + (size_t)layout.cols - 1u) / (size_t)layout.cols);
    int max_scroll = rows * layout.row_step - (a->height - TOPBAR_H - 18);
    if (max_scroll < 0)
        max_scroll = 0;

    int base = a->grid_scroll_animating ? a->grid_scroll_target : a->grid_scroll;
    int step = layout.mode == ART_PORTRAIT ? 220 : 180;
    int target = base + direction * step;
    if (target < 0)
        target = 0;
    if (target > max_scroll)
        target = max_scroll;

    a->grid_scroll_target = target;
    a->grid_scroll_last_ms = monotonic_ms();
    a->grid_scroll_animating = target != a->grid_scroll;
    if (a->grid_scroll_animating)
        a->ui_motion_active = true;
}

static void handle_wheel(app_t *a, int x, int y, int direction) {
    if (a->screen == SCREEN_LOGIN) {
        handle_login_wheel(a, direction);
        return;
    }
    if (a->screen != SCREEN_BROWSE)
        return;
    if (x < SIDEBAR_W)
        handle_category_wheel(a, direction);
    else
        handle_grid_wheel(a, direction);
    (void)y;
}

/* Handle the browse columns operation. */
static int browse_columns(app_t *a) {
    return browse_layout(a).cols;
}

/* Ensure grid focus visible. */
static void ensure_grid_focus_visible(app_t *a) {
    if (a->filtered_len == 0) {
        a->grid_scroll = 0;
        return;
    }
    if (a->focused_filtered >= a->filtered_len)
        a->focused_filtered = a->filtered_len - 1u;
    card_layout_t layout = browse_layout(a);
    int row = (int)(a->focused_filtered / (size_t)layout.cols);
    int row_top = row * layout.row_step;
    int content_y = TOPBAR_H + 18;
    int viewport_h = a->height - content_y;
    if (viewport_h < layout.card_h)
        viewport_h = layout.card_h;
    if (row_top < a->grid_scroll)
        a->grid_scroll = row_top;
    else if (row_top + layout.card_h > a->grid_scroll + viewport_h)
        a->grid_scroll = row_top + layout.card_h - viewport_h;
    if (a->grid_scroll < 0)
        a->grid_scroll = 0;
    a->grid_scroll_target = a->grid_scroll;
    a->grid_scroll_animating = false;
}

/* Handle the move grid focus operation. */
static void move_grid_focus(app_t *a, int dx, int dy) {
    if (a->filtered_len == 0)
        return;
    int cols = browse_columns(a);
    long current = (long)a->focused_filtered;
    long next = current + (long)dx + (long)dy * (long)cols;
    if (next < 0)
        next = 0;
    if ((size_t)next >= a->filtered_len)
        next = (long)a->filtered_len - 1;
    a->focused_filtered = (size_t)next;
    ensure_grid_focus_visible(a);
}



/* Handle Ctrl+1/2/3 content switching. */
static bool handle_browse_content_shortcut(app_t *a, KeySym sym) {
    switch (sym) {
    case XK_1:
        switch_content(a, CONTENT_LIVE);
        browse_focus_top(a, BROWSE_TOP_TV);
        return true;
    case XK_2:
        switch_content(a, CONTENT_VOD);
        browse_focus_top(a, BROWSE_TOP_MOVIES);
        return true;
    case XK_3:
        switch_content(a, CONTENT_SERIES);
        browse_focus_top(a, BROWSE_TOP_SERIES);
        return true;
    default:
        return false;
    }
}

/* Handle non-content Ctrl shortcuts on the browse screen. */
static bool handle_browse_command_shortcut(app_t *a, KeySym sym) {
    if (sym == XK_l || sym == XK_L) {
        if (a->thumbs)
            vip_thumbnail_scheduler_cancel_pending(a->thumbs);
        refresh_profiles(a);
        a->screen = SCREEN_LOGIN;
        a->input_focus = INPUT_MODE;
        return true;
    }
    if (sym == XK_f || sym == XK_F) {
        browse_focus_top(a, BROWSE_TOP_SEARCH);
        return true;
    }
    if (sym != XK_d && sym != XK_D)
        return false;
    if (a->filtered_len == 0u)
        return false;
    if (a->focused_filtered >= a->filtered_len)
        a->focused_filtered = a->filtered_len - 1u;
    toggle_favorite(a, a->filtered[a->focused_filtered]);
    return true;
}

/* Handle global browse shortcuts before focus-local navigation. */
static bool handle_browse_shortcut(app_t *a, KeySym sym, bool ctrl) {
    if (!ctrl)
        return false;
    if (handle_browse_content_shortcut(a, sym))
        return true;
    return handle_browse_command_shortcut(a, sym);
}

/* Handle top-bar browse focus. */
static bool handle_browse_top_key(app_t *a, KeySym sym) {
    if (sym == XK_Left) {
        int next = a->browse_top_focus - 1;
        if (next < BROWSE_TOP_TV)
            next = BROWSE_TOP_LISTS;
        browse_focus_top(a, next);
        return true;
    }
    if (sym == XK_Right) {
        int next = a->browse_top_focus + 1;
        if (next > BROWSE_TOP_LISTS)
            next = BROWSE_TOP_TV;
        browse_focus_top(a, next);
        return true;
    }
    if (sym == XK_Down) {
        if (a->browse_top_focus <= BROWSE_TOP_SERIES)
            browse_focus_sidebar(a, a->selected_category >= 0 ? a->selected_category : -1);
        else
            browse_focus_grid(a);
        return true;
    }
    if (sym == XK_Up)
        return true;
    if (is_activate_key(sym)) {
        browse_activate_top(a);
        return true;
    }
    return false;
}

/* Handle sidebar browse focus. */
static bool handle_browse_sidebar_key(app_t *a, KeySym sym) {
    int min_item = a->series_episode_mode ? -2 : -1;
    int max_item = ACTIVE_CATEGORIES(a).len > 0u ? (int)ACTIVE_CATEGORIES(a).len - 1 : -1;
    if (sym == XK_Up) {
        if (a->browse_sidebar_focus <= min_item)
            browse_focus_top(a, (int)a->content_kind);
        else
            browse_focus_sidebar(a, a->browse_sidebar_focus - 1);
        return true;
    }
    if (sym == XK_Down) {
        if (a->browse_sidebar_focus < max_item)
            browse_focus_sidebar(a, a->browse_sidebar_focus + 1);
        return true;
    }
    if (sym == XK_Right) {
        browse_focus_grid(a);
        return true;
    }
    if (sym == XK_Left)
        return true;
    if (is_activate_key(sym)) {
        browse_activate_sidebar(a);
        return true;
    }
    return false;
}

/* Handle grid browse focus. */
static bool handle_browse_grid_key(app_t *a, KeySym sym) {
    int cols = browse_columns(a);
    size_t row = cols > 0 ? a->focused_filtered / (size_t)cols : 0u;
    size_t col = cols > 0 ? a->focused_filtered % (size_t)cols : 0u;

    if (sym == XK_Left) {
        if (col == 0u)
            browse_focus_sidebar(a, a->selected_category >= 0 ? a->selected_category : -1);
        else
            move_grid_focus(a, -1, 0);
        return true;
    }
    if (sym == XK_Right) {
        move_grid_focus(a, 1, 0);
        return true;
    }
    if (sym == XK_Up) {
        if (row == 0u)
            browse_focus_top(a, BROWSE_TOP_SEARCH);
        else
            move_grid_focus(a, 0, -1);
        return true;
    }
    if (sym == XK_Down) {
        move_grid_focus(a, 0, 1);
        return true;
    }
    if (is_activate_key(sym) && a->filtered_len > 0u) {
        if (a->focused_filtered >= a->filtered_len)
            a->focused_filtered = a->filtered_len - 1u;
        activate_item(a, a->filtered[a->focused_filtered]);
        return true;
    }
    return false;
}

/* Dispatch navigation within the currently focused browse region. */
static bool handle_browse_focus_key(app_t *a, KeySym sym) {
    if (a->browse_focus == BROWSE_FOCUS_TOP)
        return handle_browse_top_key(a, sym);
    if (a->browse_focus == BROWSE_FOCUS_SIDEBAR)
        return handle_browse_sidebar_key(a, sym);
    return handle_browse_grid_key(a, sym);
}

/* Backspace the active browse search field when it contains text. */
static bool handle_browse_search_backspace(app_t *a, KeySym sym) {
    if (sym != XK_BackSpace || a->browse_focus != BROWSE_FOCUS_TOP ||
        a->browse_top_focus != BROWSE_TOP_SEARCH || !a->search[0])
        return false;
    backspace_input(a);
    return true;
}

/* Handle Back/Escape semantics on the browse screen. */
static bool handle_browse_back_key(app_t *a, KeySym sym) {
    if (!is_navigation_back(sym) && sym != XK_BackSpace)
        return false;
    if (a->series_episode_mode) {
        return_from_episode_list(a);
        browse_focus_grid(a);
        return true;
    }
    if (a->search[0]) {
        a->search[0] = '\0';
        rebuild_filter(a);
        browse_focus_top(a, BROWSE_TOP_SEARCH);
        return true;
    }
    cancel_browse_async_workers(a);
    refresh_profiles(a);
    a->screen = SCREEN_LOGIN;
    a->input_focus = INPUT_MODE;
    snprintf(a->status, sizeof(a->status), "Escolha uma lista ou adicione outra");
    return true;
}

/* Handle paste, tab and printable browse-search input. */
static bool handle_browse_search_input(app_t *a, KeySym sym, bool ctrl, bool shift,
                                       bool printable, const char *buf, int n) {
    if (ctrl && (sym == XK_v || sym == XK_V)) {
        browse_focus_top(a, BROWSE_TOP_SEARCH);
        request_paste(a, a->clipboard);
        return true;
    }
    if (shift && sym == XK_Insert) {
        browse_focus_top(a, BROWSE_TOP_SEARCH);
        request_paste(a, XA_PRIMARY);
        return true;
    }
    if (sym == XK_Tab) {
        browse_focus_top(a, BROWSE_TOP_SEARCH);
        return true;
    }
    if (!printable)
        return false;
    browse_focus_top(a, BROWSE_TOP_SEARCH);
    append_input(a, buf, (size_t)n);
    return true;
}

/* Handle browse back/search/paste input after local focus navigation. */
static bool handle_browse_text_key(app_t *a, KeySym sym, bool ctrl, bool shift,
                                   bool printable, const char *buf, int n) {
    if (handle_browse_search_backspace(a, sym))
        return true;
    if (handle_browse_back_key(a, sym))
        return true;
    return handle_browse_search_input(a, sym, ctrl, shift, printable, buf, n);
}

/* Handle browse-screen key input. */
static void handle_browse_key(app_t *a, KeySym sym, bool ctrl, bool shift,
                              bool printable, const char *buf, int n) {
    if (handle_browse_shortcut(a, sym, ctrl))
        return;
    if (handle_browse_focus_key(a, sym))
        return;
    (void)handle_browse_text_key(a, sym, ctrl, shift, printable, buf, n);
}

/* Activate the selected saved profile. */
static void activate_saved_profile(app_t *a) {
    if (a->profiles.len == 0u)
        return;
    size_t index = (size_t)a->profile_focus;
    if (index >= a->profiles.len)
        index = a->profiles.len - 1u;
    load_profile_into_form(a, index);
    if (a->server[0] &&
        (a->login_mode == LOGIN_M3U || (a->username[0] && a->password[0])))
        start_login(a, false);
}

/* Handle movement within the saved-profile picker. */
static bool handle_saved_profile_navigation(app_t *a, KeySym sym) {
    if (is_navigation_back(sym) || sym == XK_BackSpace || sym == XK_Left) {
        a->input_focus = INPUT_PROFILE_NAME;
        return true;
    }
    if (sym == XK_Right)
        return true;
    if (sym == XK_Up) {
        if (a->profile_focus > 0)
            --a->profile_focus;
        login_profile_ensure_visible(a);
        return true;
    }
    if (sym == XK_Down) {
        if ((size_t)(a->profile_focus + 1) < a->profiles.len)
            ++a->profile_focus;
        login_profile_ensure_visible(a);
        return true;
    }
    return false;
}

/* Handle the saved-profile picker on the login screen. */
static bool handle_saved_profile_key(app_t *a, KeySym sym) {
    if (a->input_focus != INPUT_SAVED_PROFILE)
        return false;
    if (handle_saved_profile_navigation(a, sym))
        return true;
    if (is_activate_key(sym))
        activate_saved_profile(a);
    return true;
}

/* Handle pairing-specific login keys before ordinary field navigation. */
static bool handle_login_pairing_key(app_t *a, KeySym sym) {
    if (a->login_mode == LOGIN_M3U && sym == XK_F2) {
        a->input_focus = INPUT_PHONE;
        start_phone_pairing(a);
        return true;
    }
    if (!a->pairing_relay || (!is_navigation_back(sym) && sym != XK_BackSpace))
        return false;
    stop_phone_pairing(a);
    a->pairing_retry_ready = false;
    snprintf(a->status, sizeof(a->status), "Pareamento cancelado");
    a->input_focus = INPUT_PHONE;
    return true;
}

/* Handle arrows and Back on ordinary login fields. */
static bool handle_login_focus_key(app_t *a, KeySym sym) {
    if (sym == XK_Up) {
        login_move_focus(a, -1);
        return true;
    }
    if (sym == XK_Down) {
        login_move_focus(a, 1);
        return true;
    }
    if (a->input_focus == INPUT_MODE && (sym == XK_Left || sym == XK_Right)) {
        login_select_mode(a, a->login_mode == LOGIN_XTREAM ? LOGIN_M3U : LOGIN_XTREAM);
        return true;
    }
    if (sym == XK_Right && a->profiles.len > 0u && !a->pairing_relay) {
        login_focus_saved_profiles(a);
        return true;
    }
    return is_navigation_back(sym);
}

/* Handle paste and tab navigation on the login form. */
static bool handle_login_edit_shortcut(app_t *a, KeySym sym, bool ctrl, bool shift) {
    if (ctrl && (sym == XK_v || sym == XK_V)) {
        request_paste(a, a->clipboard);
        return true;
    }
    if (shift && sym == XK_Insert) {
        request_paste(a, XA_PRIMARY);
        return true;
    }
    if (sym != XK_Tab)
        return false;
    login_move_focus(a, shift ? -1 : 1);
    return true;
}

/* Activate the focused login control. */
static bool handle_login_activate_key(app_t *a, KeySym sym) {
    if (!is_activate_key(sym))
        return false;
    if (a->input_focus == INPUT_PHONE && a->login_mode == LOGIN_M3U)
        start_phone_pairing(a);
    else if (a->input_focus != INPUT_MODE)
        start_login(a, true);
    return true;
}

/* Handle direct text editing on the login form. */
static void handle_login_text_key(app_t *a, KeySym sym, bool printable,
                                  const char *buf, int n) {
    if (sym == XK_BackSpace) {
        backspace_input(a);
        return;
    }
    if (printable)
        append_input(a, buf, (size_t)n);
}

/* Handle login-screen key input. */
static void handle_login_key(app_t *a, KeySym sym, bool ctrl, bool shift,
                             bool printable, const char *buf, int n) {
    if (handle_login_pairing_key(a, sym))
        return;
    if (handle_saved_profile_key(a, sym))
        return;
    if (handle_login_focus_key(a, sym))
        return;
    if (handle_login_edit_shortcut(a, sym, ctrl, shift))
        return;
    if (handle_login_activate_key(a, sym))
        return;
    handle_login_text_key(a, sym, printable, buf, n);
}

/* Handle key. */
static void handle_key(app_t *a, XKeyEvent *kev) {
    KeySym sym = NoSymbol;
    char buf[64];
    int n = XLookupString(kev, buf, sizeof(buf), &sym, NULL);
    bool ctrl = (kev->state & ControlMask) != 0;
    bool shift = (kev->state & ShiftMask) != 0;
    bool printable = n > 0 && !ctrl && (unsigned char)buf[0] >= 0x20u;

    if (getenv("VIPTV_INPUT_DEBUG")) {
        const char *name = XKeysymToString(sym);
        fprintf(stderr, "[input] keycode=%u keysym=0x%lx name=%s state=0x%x\n",
                kev->keycode, (unsigned long)sym, name ? name : "?", kev->state);
    }

    if (sym == XK_F11) {
        set_fullscreen(a, !a->fullscreen_requested);
        show_player_hud(a);
        return;
    }
    if (a->screen == SCREEN_PLAYER) {
        handle_player_key(a, sym);
        return;
    }
    if (a->screen == SCREEN_BROWSE) {
        handle_browse_key(a, sym, ctrl, shift, printable, buf, n);
        return;
    }
    handle_login_key(a, sym, ctrl, shift, printable, buf, n);
}

/* Handle selection. */
static void handle_selection(app_t *a, XSelectionEvent *sel) {
    int target = a->paste_target;
    a->paste_target = 0;
    if (sel->property == None)
        return;
    Atom type;
    int format;
    unsigned long nitems, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(a->dpy, a->win, sel->property, 0, 8192, True, AnyPropertyType, &type, &format,
                           &nitems, &after, &data) == Success &&
        data) {
        /* Selection conversion is asynchronous. Discard it if focus/screen
           changed meanwhile instead of pasting into an unrelated field. */
        if (format == 8 && target != 0 && target == a->input_focus)
            append_input(a, (const char *)data, nitems);
        XFree(data);
    }
}

/* Apply ConfigureNotify only for the top-level application window. */
static void handle_configure_event(app_t *a, const XConfigureEvent *event) {
    /*
     * video_win also selects StructureNotifyMask and emits ConfigureNotify
     * events (including its initial 1px geometry). Treating those as main
     * window resizes collapses the player container to 1px high.
     */
    if (event->window != a->win)
        return;
    a->width = event->width;
    a->height = event->height;
    if (a->screen == SCREEN_PLAYER)
        layout_video_window(a);
}

/* Synchronize fullscreen state after the WM changes _NET_WM_STATE. */
static void handle_property_event(app_t *a, const XPropertyEvent *event) {
    if (event->window != a->win ||
        event->atom != XInternAtom(a->dpy, "_NET_WM_STATE", False))
        return;
    bool actual = wm_reports_fullscreen(a);
    if (actual || !a->fullscreen_fallback)
        a->fullscreen = actual || a->fullscreen_fallback;
    if (a->fullscreen)
        layout_video_window(a);
}



/* Handle pointer motion for browse hover or player HUD/timeline. */
static void handle_motion_event(app_t *a, const XMotionEvent *event) {
    if (a->screen == SCREEN_BROWSE && event->window == a->win) {
        update_browse_hover(a, event->x, event->y);
        return;
    }
    if (a->screen != SCREEN_PLAYER)
        return;

    show_player_hud(a);
    if (getenv("VIPTV_MPV_DEBUG"))
        fprintf(stderr, "[mpv-debug] input MotionNotify window=%lu\n",
                (unsigned long)event->window);
    handle_timeline_motion(a, event);
}

/* Clear browse hover state after the pointer leaves the window. */
static void handle_leave_event(app_t *a) {
    if (a->screen != SCREEN_BROWSE)
        return;
    a->mouse_inside = false;
    if (a->hovered_card_valid)
        vip_ui_motion_set_target(&a->hover_motion, 0.0f, monotonic_ms());
    a->hovered_control = HOVER_NONE;
    vip_ui_motion_init(&a->control_motion, 0.0f, monotonic_ms());
    a->ui_motion_active = true;
}

/* Finish mouse interactions after releasing the primary button. */
static void handle_button_release_event(app_t *a, const XButtonEvent *event) {
    if (event->button != Button1)
        return;
    a->mouse_down = false;
    if (a->screen != SCREEN_PLAYER)
        return;
    a->timeline_dragging = false;
    save_current_progress(a, true);
    show_player_hud(a);
}



/* Dispatch a mouse button press after player-surface handling. */
static void handle_button_press_event(app_t *a, const XButtonEvent *event) {
    a->mouse_down = true;
    if (handle_player_surface_press(a, event))
        return;
    if (event->button == Button1)
        handle_click(a, event->x, event->y);
    else if (event->button == Button4)
        handle_wheel(a, event->x, event->y, -1);
    else if (event->button == Button5)
        handle_wheel(a, event->x, event->y, 1);
    else if (event->button == Button2)
        request_paste(a, XA_PRIMARY);
}

/* Log and dispatch an X11 key press. */
static void handle_key_press_event(app_t *a, XKeyEvent *event) {
    if (a->screen == SCREEN_PLAYER && getenv("VIPTV_MPV_DEBUG"))
        fprintf(stderr, "[mpv-debug] input KeyPress window=%lu keycode=%u\n",
                (unsigned long)event->window, event->keycode);
    handle_key(a, event);
}

/* Diagnose focus changes and recover player input focus when necessary. */
static void handle_focus_event(app_t *a, const XFocusChangeEvent *event, int type) {
    if (a->screen == SCREEN_PLAYER && getenv("VIPTV_MPV_DEBUG")) {
        Window focus = None;
        int revert = 0;
        XGetInputFocus(a->dpy, &focus, &revert);
        fprintf(stderr, "[mpv-debug] input %s event-window=%lu current-focus=%lu\n",
                type == FocusIn ? "FocusIn" : "FocusOut",
                (unsigned long)event->window, (unsigned long)focus);
    }
    if (type == FocusOut && a->screen == SCREEN_PLAYER)
        recover_player_focus_if_needed(a, event);
}

/* Single-threaded X11 event dispatch.  Background jobs communicate by
 * state/flags and are observed from the main loop rather than calling Xlib. */
void process_event(app_t *a, XEvent *e) {
    switch (e->type) {
    case Expose:
        break;
    case ConfigureNotify:
        handle_configure_event(a, &e->xconfigure);
        break;
    case ClientMessage:
        if ((Atom)e->xclient.data.l[0] == a->wm_delete)
            a->quit = true;
        break;
    case PropertyNotify:
        handle_property_event(a, &e->xproperty);
        break;
    case MotionNotify:
        handle_motion_event(a, &e->xmotion);
        break;
    case LeaveNotify:
        handle_leave_event(a);
        break;
    case ButtonRelease:
        handle_button_release_event(a, &e->xbutton);
        break;
    case ButtonPress:
        handle_button_press_event(a, &e->xbutton);
        break;
    case KeyPress:
        handle_key_press_event(a, &e->xkey);
        break;
    case FocusIn:
    case FocusOut:
        handle_focus_event(a, &e->xfocus, e->type);
        break;
    case SelectionNotify:
        handle_selection(a, &e->xselection);
        break;
    default:
        break;
    }
}

