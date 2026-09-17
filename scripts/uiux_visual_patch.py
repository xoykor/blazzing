from pathlib import Path

PATH = Path('src/ui_x11/x11_app.c')
s = PATH.read_text()


def replace_once(old: str, new: str) -> None:
    global s
    if old not in s:
        raise SystemExit('missing pattern: ' + old[:160].replace('\n', '\\n'))
    s = s.replace(old, new, 1)


def function_span(text: str, signature: str):
    start = text.find(signature)
    if start < 0:
        raise SystemExit('missing function: ' + signature)
    brace = text.find('{', start)
    depth = 0
    i = brace
    state = 'code'
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ''
        if state == 'code':
            if c == '"': state = 'string'
            elif c == "'": state = 'char'
            elif c == '/' and n == '/': state = 'line'; i += 1
            elif c == '/' and n == '*': state = 'block'; i += 1
            elif c == '{': depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0: return start, i + 1
        elif state == 'string':
            if c == '\\': i += 1
            elif c == '"': state = 'code'
        elif state == 'char':
            if c == '\\': i += 1
            elif c == "'": state = 'code'
        elif state == 'line':
            if c == '\n': state = 'code'
        elif state == 'block':
            if c == '*' and n == '/': state = 'code'; i += 1
        i += 1
    raise SystemExit('unterminated function: ' + signature)


def replace_function(signature: str, replacement: str) -> None:
    global s
    a, b = function_span(s, signature)
    s = s[:a] + replacement.rstrip() + s[b:]


replace_once('#define APP_TITLE "Visual IPTV"', '#define APP_TITLE "Blazzing"')
replace_once('''    GC gc;
    XFontStruct *font;
    Atom wm_delete;''', '''    GC gc;
    XFontStruct *font;
    XFontStruct *font_title;
    XFontStruct *font_heading;
    XFontStruct *font_small;
    Atom wm_delete;''')

replace_function('static void init_palette(app_t *a)', r'''static void init_palette(app_t *a) {
    a->colors.bg = alloc_color(a, "#070A12");
    a->colors.panel = alloc_color(a, "#0E1420");
    a->colors.panel2 = alloc_color(a, "#151E2D");
    a->colors.border = alloc_color(a, "#2B3950");
    a->colors.text = alloc_color(a, "#F6F8FC");
    a->colors.muted = alloc_color(a, "#91A0B7");
    a->colors.accent = alloc_color(a, "#62A9FF");
    a->colors.accent2 = alloc_color(a, "#183E6B");
    a->colors.danger = alloc_color(a, "#FF7185");
    a->colors.black = alloc_color(a, "#030509");
}''')

round_helpers = r'''
static void fill_round_rect(app_t *a, int x, int y, int w, int h, int r, unsigned long color) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r == 0) { fill_rect(a, x, y, (unsigned)w, (unsigned)h, color); return; }
    set_fg(a, color);
    XFillRectangle(a->dpy, draw_target(a), a->gc, x + r, y, (unsigned)(w - 2*r), (unsigned)h);
    XFillRectangle(a->dpy, draw_target(a), a->gc, x, y + r, (unsigned)w, (unsigned)(h - 2*r));
    XFillArc(a->dpy, draw_target(a), a->gc, x, y, (unsigned)(2*r), (unsigned)(2*r), 90*64, 90*64);
    XFillArc(a->dpy, draw_target(a), a->gc, x+w-2*r, y, (unsigned)(2*r), (unsigned)(2*r), 0, 90*64);
    XFillArc(a->dpy, draw_target(a), a->gc, x, y+h-2*r, (unsigned)(2*r), (unsigned)(2*r), 180*64, 90*64);
    XFillArc(a->dpy, draw_target(a), a->gc, x+w-2*r, y+h-2*r, (unsigned)(2*r), (unsigned)(2*r), 270*64, 90*64);
}

static void stroke_round_rect(app_t *a, int x, int y, int w, int h, int r, unsigned long color) {
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r == 0) { stroke_rect(a, x, y, (unsigned)w, (unsigned)h, color); return; }
    set_fg(a, color);
    XDrawLine(a->dpy, draw_target(a), a->gc, x+r, y, x+w-r, y);
    XDrawLine(a->dpy, draw_target(a), a->gc, x+r, y+h, x+w-r, y+h);
    XDrawLine(a->dpy, draw_target(a), a->gc, x, y+r, x, y+h-r);
    XDrawLine(a->dpy, draw_target(a), a->gc, x+w, y+r, x+w, y+h-r);
    XDrawArc(a->dpy, draw_target(a), a->gc, x, y, (unsigned)(2*r), (unsigned)(2*r), 90*64, 90*64);
    XDrawArc(a->dpy, draw_target(a), a->gc, x+w-2*r, y, (unsigned)(2*r), (unsigned)(2*r), 0, 90*64);
    XDrawArc(a->dpy, draw_target(a), a->gc, x, y+h-2*r, (unsigned)(2*r), (unsigned)(2*r), 180*64, 90*64);
    XDrawArc(a->dpy, draw_target(a), a->gc, x+w-2*r, y+h-2*r, (unsigned)(2*r), (unsigned)(2*r), 270*64, 90*64);
}

static void draw_surface(app_t *a, int x, int y, int w, int h, int r, bool focused) {
    fill_round_rect(a, x + 3, y + 5, w, h, r, a->colors.black);
    fill_round_rect(a, x, y, w, h, r, focused ? a->colors.accent2 : a->colors.panel2);
    stroke_round_rect(a, x, y, w, h, r, focused ? a->colors.accent : a->colors.border);
}
'''
needle = '''static void stroke_rect(app_t *a, int x, int y, unsigned w, unsigned h, unsigned long color) {
    set_fg(a, color); XDrawRectangle(a->dpy, draw_target(a), a->gc, x, y, w, h);
}
'''
replace_once(needle, needle + round_helpers)

replace_function('static void draw_text(app_t *a, int x, int y, const char *text, unsigned long color)', r'''static void draw_text(app_t *a, int x, int y, const char *text, unsigned long color) {
    if (!text) return;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    if (a->font) XSetFont(a->dpy, a->gc, a->font->fid);
    set_fg(a, color);
    XDrawString(a->dpy, draw_target(a), a->gc, x, y, latin, (int)len);
}''')

font_helpers = r'''
static int text_width_font(app_t *a, XFontStruct *font, const char *text) {
    if (!text) return 0;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    XFontStruct *f = font ? font : a->font;
    return f ? XTextWidth(f, latin, (int)len) : (int)len * 8;
}

static void draw_text_font(app_t *a, XFontStruct *font, int x, int y, const char *text, unsigned long color) {
    if (!text) return;
    char latin[2048];
    size_t len = utf8_to_latin1(latin, sizeof(latin), text);
    XFontStruct *f = font ? font : a->font;
    if (f) XSetFont(a->dpy, a->gc, f->fid);
    set_fg(a, color);
    XDrawString(a->dpy, draw_target(a), a->gc, x, y, latin, (int)len);
}

static void draw_centered_font(app_t *a, XFontStruct *font, int x, int y, int w, const char *text, unsigned long color) {
    int tw = text_width_font(a, font, text);
    draw_text_font(a, font, x + (w - tw) / 2, y, text, color);
}
'''
center_sig = 'static void draw_centered(app_t *a, int x, int y, int w, const char *text, unsigned long color)'
_, center_end = function_span(s, center_sig)
s = s[:center_end] + '\n' + font_helpers + s[center_end:]

replace_function('static void draw_input(app_t *a, int x, int y, int w, int h, const char *value,', r'''static void draw_input(app_t *a, int x, int y, int w, int h, const char *value,
                       const char *placeholder, int focus_id, bool password) {
    bool focused = a->input_focus == focus_id;
    fill_round_rect(a, x + 2, y + 3, w, h, 12, a->colors.black);
    fill_round_rect(a, x, y, w, h, 12, a->colors.panel2);
    stroke_round_rect(a, x, y, w, h, 12, focused ? a->colors.accent : a->colors.border);
    if (focused) stroke_round_rect(a, x + 2, y + 2, w - 4, h - 4, 10, a->colors.accent2);
    const char *text = value && value[0] ? value : placeholder;
    char masked[256];
    if (password && value && value[0]) {
        size_t n = strlen(value); if (n > sizeof(masked)-1) n = sizeof(masked)-1;
        memset(masked, '*', n); masked[n] = '\0'; text = masked;
    }
    draw_text(a, x + 14, y + h/2 + 6, text, value && value[0] ? a->colors.text : a->colors.muted);
}''')

replace_function('static void draw_login(app_t *a)', r'''static void draw_login(app_t *a) {
    fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);
    fill_rect(a, 0, 0, (unsigned)a->width, 7, a->colors.accent);
    int w = a->width > 1120 ? 1080 : a->width - 40;
    if (w < 720) w = 720;
    int h = 620;
    int x = (a->width - w) / 2, y = (a->height - h) / 2;
    if (y < 18) y = 18;
    fill_round_rect(a, x + 8, y + 10, w, h, 24, a->colors.black);
    fill_round_rect(a, x, y, w, h, 24, a->colors.panel);
    stroke_round_rect(a, x, y, w, h, 24, a->colors.border);

    fill_round_rect(a, x + 30, y + 25, 42, 42, 13, a->colors.accent);
    draw_centered_font(a, a->font_heading, x + 30, y + 53, 42, "B", a->colors.bg);
    draw_text_font(a, a->font_title, x + 86, y + 49, "Blazzing", a->colors.text);
    draw_text(a, x + 86, y + 69, "Streaming, listas e biblioteca em um só lugar", a->colors.muted);

    int form_x = x + 34, form_w = (w * 58) / 100 - 50;
    int list_x = x + (w * 60) / 100, list_w = w - (list_x - x) - 34;
    int mode_y = y + 88;
    int mode_w = (form_w - 10) / 2;
    for (int i = 0; i < 2; ++i) {
        bool selected = (int)a->login_mode == i;
        int bx = form_x + i * (mode_w + 10);
        fill_round_rect(a, bx, mode_y, mode_w, 42, 12, selected ? a->colors.accent2 : a->colors.panel2);
        stroke_round_rect(a, bx, mode_y, mode_w, 42, 12, selected ? a->colors.accent : a->colors.border);
        draw_centered(a, bx, mode_y + 27, mode_w, i == LOGIN_XTREAM ? "Xtream" : "M3U",
                      selected ? a->colors.text : a->colors.muted);
    }

    draw_input(a, form_x, y+142, form_w, 44, a->profile_name, "Nome da lista (opcional)", INPUT_PROFILE_NAME, false);
    if (a->login_mode == LOGIN_XTREAM) {
        draw_input(a, form_x, y+196, form_w, 44, a->server, "Servidor primário (https://...)", INPUT_SERVER, false);
        draw_input(a, form_x, y+250, form_w, 44, a->server_alt, "Servidor alternativo (opcional)", INPUT_SERVER_ALT, false);
        draw_input(a, form_x, y+304, form_w, 44, a->username, "Usuário", INPUT_USERNAME, false);
        draw_input(a, form_x, y+358, form_w, 44, a->password, "Senha", INPUT_PASSWORD, true);
    } else {
        draw_input(a, form_x, y+196, form_w, 44, a->server, "URL ou caminho de playlist .m3u/.m3u8", INPUT_SERVER, false);
        draw_text(a, form_x, y+266, "M3U remoto (HTTP/HTTPS) ou arquivo local.", a->colors.muted);
        draw_text(a, form_x, y+292, "A playlist é processada diretamente pelo Blazzing.", a->colors.muted);
    }
    bool ready = a->server[0] && !atomic_load(&a->login_running) &&
                 (a->login_mode == LOGIN_M3U || (a->username[0] && a->password[0]));
    int connect_y = y + 430;
    fill_round_rect(a, form_x + 2, connect_y + 4, form_w, 50, 14, a->colors.black);
    fill_round_rect(a, form_x, connect_y, form_w, 50, 14, ready ? a->colors.accent : a->colors.panel2);
    stroke_round_rect(a, form_x, connect_y, form_w, 50, 14, ready ? a->colors.accent : a->colors.border);
    draw_centered_font(a, a->font_heading, form_x, connect_y+32, form_w,
                       atomic_load(&a->login_running) ? "Conectando..." : "Conectar",
                       ready ? a->colors.bg : a->colors.muted);

    fill_round_rect(a, list_x - 14, y + 88, list_w + 28, 438, 18, a->colors.panel2);
    stroke_round_rect(a, list_x - 14, y + 88, list_w + 28, 438, 18, a->colors.border);
    draw_text_font(a, a->font_heading, list_x, y+116, "Suas listas", a->colors.text);
    draw_text(a, list_x, y+137, "Acesso rápido aos perfis salvos", a->colors.muted);
    int row_y = y + 154;
    int visible = 6;
    for (int r = 0; r < visible; ++r) {
        int idx = a->profile_scroll + r;
        if (idx < 0 || (size_t)idx >= a->profiles.len) break;
        vip_profile_t *p = &a->profiles.items[idx];
        draw_surface(a, list_x, row_y, list_w, 52, 12, false);
        char label[220];
        snprintf(label, sizeof(label), "%s  ·  %s", p->name ? p->name : "Lista", p->type == VIP_PROFILE_M3U ? "M3U" : "Xtream");
        bounded_text(label, sizeof(label), label, 44);
        draw_text(a, list_x+13, row_y+22, label, a->colors.text);
        char sub[220]; bounded_text(sub, sizeof(sub), p->server ? p->server : "", 46);
        draw_text_font(a, a->font_small, list_x+13, row_y+42, sub, a->colors.muted);
        row_y += 60;
    }
    if (a->profiles.len == 0u) draw_text(a, list_x, y+182, "Nenhuma lista salva ainda.", a->colors.muted);

    char status_copy[512];
    pthread_mutex_lock(&a->data_mutex);
    snprintf(status_copy, sizeof(status_copy), "%s", a->status);
    pthread_mutex_unlock(&a->data_mutex);
    draw_text(a, form_x, y+h-42, status_copy,
              (strstr(status_copy, "falha") || strstr(status_copy, "Erro")) ? a->colors.danger : a->colors.muted);
}''')

replace_function('static void draw_toast(app_t *a)', r'''static void draw_toast(app_t *a) {
    if (!a || !a->toast[0] || monotonic_ms() >= a->toast_until_ms) return;
    int w = text_width(a, a->toast) + 44;
    if (w < 220) w = 220;
    if (w > a->width - 40) w = a->width - 40;
    int x = (a->width - w) / 2;
    int y = TOPBAR_H + 10;
    fill_round_rect(a, x + 3, y + 4, w, 42, 14, a->colors.black);
    fill_round_rect(a, x, y, w, 42, 14, a->colors.panel2);
    stroke_round_rect(a, x, y, w, 42, 14, a->colors.accent);
    draw_centered(a, x, y + 27, w, a->toast, a->colors.text);
}''')

# Details panel gets a floating surface and pill action without changing hitboxes.
replace_once('''    fill_rect(a, px, py, (unsigned)pw, (unsigned)ph, a->colors.panel);
    stroke_rect(a, px, py, (unsigned)pw, (unsigned)ph, a->colors.border);''',
'''    fill_round_rect(a, px + 4, py + 6, pw, ph, 18, a->colors.black);
    fill_round_rect(a, px, py, pw, ph, 18, a->colors.panel);
    stroke_round_rect(a, px, py, pw, ph, 18, a->colors.border);''')
replace_once('''    draw_text(a, px + 16, py + 30, title, a->colors.text);''',
'''    draw_text_font(a, a->font_heading, px + 16, py + 31, title, a->colors.text);''')
replace_once('''    fill_rect(a, fav_x, fav_y, (unsigned)fav_w, (unsigned)fav_h,
              favorite ? a->colors.accent2 : a->colors.panel2);
    stroke_rect(a, fav_x, fav_y, (unsigned)fav_w, (unsigned)fav_h,
                favorite ? a->colors.accent : a->colors.border);''',
'''    fill_round_rect(a, fav_x, fav_y, fav_w, fav_h, 12,
                    favorite ? a->colors.accent2 : a->colors.panel2);
    stroke_round_rect(a, fav_x, fav_y, fav_w, fav_h, 12,
                      favorite ? a->colors.accent : a->colors.border);''')

# Browse chrome: same geometry, premium rounded surfaces.
replace_once('''    fill_rect(a, 0, 0, (unsigned)a->width, TOPBAR_H, a->colors.panel);
    fill_rect(a, 0, TOPBAR_H, SIDEBAR_W, (unsigned)(a->height-TOPBAR_H), a->colors.panel2);''',
'''    fill_rect(a, 0, 0, (unsigned)a->width, TOPBAR_H, a->colors.panel);
    fill_rect(a, 0, 0, (unsigned)a->width, 4, a->colors.accent);
    fill_rect(a, 0, TOPBAR_H, SIDEBAR_W, (unsigned)(a->height-TOPBAR_H), a->colors.panel2);
    fill_rect(a, SIDEBAR_W-1, TOPBAR_H, 1, (unsigned)(a->height-TOPBAR_H), a->colors.border);''')
replace_once('''        fill_rect(a, tab_x[k], tab_y, (unsigned)tab_w[k], (unsigned)tab_h,
                  selected ? a->colors.accent2 : a->colors.panel2);
        stroke_rect(a, tab_x[k], tab_y, (unsigned)tab_w[k], (unsigned)tab_h,
                    selected ? a->colors.accent : a->colors.border);''',
'''        fill_round_rect(a, tab_x[k], tab_y, tab_w[k], tab_h, 13,
                        selected ? a->colors.accent2 : a->colors.panel2);
        stroke_round_rect(a, tab_x[k], tab_y, tab_w[k], tab_h, 13,
                          selected ? a->colors.accent : a->colors.border);''')
replace_once('''    fill_rect(a, fav_x, 12, (unsigned)fav_w, 46, a->favorites_only ? a->colors.accent2 : a->colors.panel2);
    stroke_rect(a, fav_x, 12, (unsigned)fav_w, 46, a->favorites_only ? a->colors.accent : a->colors.border);''',
'''    fill_round_rect(a, fav_x, 12, fav_w, 46, 13, a->favorites_only ? a->colors.accent2 : a->colors.panel2);
    stroke_round_rect(a, fav_x, 12, fav_w, 46, 13, a->favorites_only ? a->colors.accent : a->colors.border);''')
replace_once('''    fill_rect(a, list_x, 12, (unsigned)list_w, 46, a->colors.panel2);
    stroke_rect(a, list_x, 12, (unsigned)list_w, 46, a->colors.border);''',
'''    fill_round_rect(a, list_x, 12, list_w, 46, 13, a->colors.panel2);
    stroke_round_rect(a, list_x, 12, list_w, 46, 13, a->colors.border);''')
replace_once('''    fill_rect(a, 8, y, SIDEBAR_W-16, 36, all_sel ? a->colors.accent2 : a->colors.panel2);''',
'''    fill_round_rect(a, 8, y, SIDEBAR_W-16, 36, 11, all_sel ? a->colors.accent2 : a->colors.panel2);''')
replace_once('''        fill_rect(a, 8, y, SIDEBAR_W-16, 36, selected ? a->colors.accent2 : a->colors.panel2);''',
'''        fill_round_rect(a, 8, y, SIDEBAR_W-16, 36, 11, selected ? a->colors.accent2 : a->colors.panel2);''')

replace_once('''            if (focused) {
                stroke_rect(a, cx-3, cy-3, (unsigned)(layout.card_w+6), (unsigned)(layout.card_h+6), a->colors.accent);
                stroke_rect(a, cx-2, cy-2, (unsigned)(layout.card_w+4), (unsigned)(layout.card_h+4), a->colors.accent);
            }
            fill_rect(a, cx, cy, (unsigned)layout.card_w, (unsigned)layout.art_h, a->colors.black);''',
'''            fill_round_rect(a, cx + 4, cy + 6, layout.card_w, layout.card_h, 16, a->colors.black);
            fill_round_rect(a, cx, cy, layout.card_w, layout.card_h, 16, a->colors.panel2);
            if (focused) {
                stroke_round_rect(a, cx-3, cy-3, layout.card_w+6, layout.card_h+6, 18, a->colors.accent);
                stroke_round_rect(a, cx-1, cy-1, layout.card_w+2, layout.card_h+2, 17, a->colors.accent2);
            }
            fill_round_rect(a, cx, cy, layout.card_w, layout.art_h, 14, a->colors.black);''')
replace_once('''            stroke_rect(a, cx, cy, (unsigned)layout.card_w, (unsigned)layout.art_h, a->colors.border);''',
'''            stroke_round_rect(a, cx, cy, layout.card_w, layout.art_h, 14, focused ? a->colors.accent : a->colors.border);''')
replace_once('''            fill_rect(a, card_fav_x, card_fav_y, (unsigned)card_fav_w, (unsigned)card_fav_h,
                      favorite ? a->colors.accent2 : a->colors.panel2);
            stroke_rect(a, card_fav_x, card_fav_y, (unsigned)card_fav_w, (unsigned)card_fav_h,
                        favorite ? a->colors.accent : a->colors.border);''',
'''            fill_round_rect(a, card_fav_x, card_fav_y, card_fav_w, card_fav_h, 10,
                            favorite ? a->colors.accent2 : a->colors.panel);
            stroke_round_rect(a, card_fav_x, card_fav_y, card_fav_w, card_fav_h, 10,
                              favorite ? a->colors.accent : a->colors.border);''')
replace_once('''            char title[160]; bounded_text(title, sizeof(title), ch->name, layout.mode == ART_PORTRAIT ? 28 : 42);
            draw_text(a, cx, cy + layout.art_h + 24, title, a->colors.text);
            if (a->content_kind == CONTENT_SERIES && !a->series_episode_mode &&''',
'''            char title[160]; bounded_text(title, sizeof(title), ch->name, layout.mode == ART_PORTRAIT ? 28 : 42);
            draw_text_font(a, focused ? a->font_heading : a->font, cx + 8, cy + layout.art_h + 25, title, a->colors.text);
            if (a->series_season_select) {
                size_t season_count = 0u;
                for (size_t ei = 0; ei < a->episode_channels.len; ++ei) {
                    const char *cid = a->episode_channels.items[ei].category_id;
                    if (cid && ch->category_id && strcmp(cid, ch->category_id) == 0) ++season_count;
                }
                char meta[72]; snprintf(meta, sizeof(meta), "%zu episódio%s", season_count, season_count == 1u ? "" : "s");
                draw_text_font(a, a->font_small, cx + 8, cy + layout.art_h + 44, meta, a->colors.muted);
            } else if (a->content_kind == CONTENT_SERIES && !a->series_episode_mode &&''')
replace_once('''                draw_text(a, cx, cy + layout.art_h + 43, progress, a->colors.muted);''',
'''                draw_text_font(a, a->font_small, cx + 8, cy + layout.art_h + 44, progress, a->colors.muted);''')
replace_once('''        fill_rect(a, box_x, box_y, (unsigned)box_w, 56, a->colors.panel2);
        stroke_rect(a, box_x, box_y, (unsigned)box_w, 56, a->colors.accent);''',
'''        fill_round_rect(a, box_x + 3, box_y + 4, box_w, 56, 16, a->colors.black);
        fill_round_rect(a, box_x, box_y, box_w, 56, 16, a->colors.panel2);
        stroke_round_rect(a, box_x, box_y, box_w, 56, 16, a->colors.accent);''')

replace_function('static void draw_player(app_t *a)', r'''static void draw_player(app_t *a) {
    fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.black);
    vip_mpv_player_snapshot_t snap = {0};
    if (a->player) vip_mpv_player_snapshot(a->player, &snap);
    vip_player_state_t player_state = a->player ? snap.state : VIP_PLAYER_ERROR;
    const char *state_text = a->player ? vip_mpv_player_state_name(player_state) : a->player_status;
    bool hud = player_hud_visible(a);

    if (!a->fullscreen) {
        fill_rect(a, 0, 0, (unsigned)a->width, PLAYER_HEADER_H, a->colors.panel);
        fill_round_rect(a, 12, 10, 132, 44, 13, a->colors.panel2);
        stroke_round_rect(a, 12, 10, 132, 44, 13, a->colors.border);
        draw_text(a, 28, 39, "‹ Voltar", a->colors.text);
        if (a->current_channel < ACTIVE_CHANNELS(a).len)
            draw_text_font(a, a->font_heading, 168, 39, ACTIVE_CHANNELS(a).items[a->current_channel].name, a->colors.text);
    }

    if (hud) {
        int y = a->height - PLAYER_CONTROLS_H;
        fill_rect(a, 0, y, (unsigned)a->width, PLAYER_CONTROLS_H, a->colors.panel);
        fill_rect(a, 0, y, (unsigned)a->width, 1, a->colors.border);
        fill_round_rect(a, 16, y+18, 52, 46, 14, a->colors.panel2);
        stroke_round_rect(a, 16, y+18, 52, 46, 14, a->colors.border);
        draw_centered_font(a, a->font_heading, 16, y+48, 52, snap.paused ? "▶" : "Ⅱ", a->colors.text);
        fill_round_rect(a, 76, y+18, 82, 46, 14, a->colors.panel2);
        stroke_round_rect(a, 76, y+18, 82, 46, 14, a->colors.border);
        draw_centered(a, 76, y+47, 82, "Voltar", a->colors.text);
        if (a->player_item_live) {
            fill_round_rect(a, 176, y+22, 76, 30, 12, a->colors.danger);
            draw_centered(a, 176, y+43, 76, "AO VIVO", a->colors.text);
            draw_text(a, 270, y+44, "← → troca canal", a->colors.muted);
        } else {
            int tx,ty,tw,th; timeline_geometry(a,&tx,&ty,&tw,&th);
            fill_round_rect(a, tx, ty, tw, th, th/2, a->colors.panel2);
            double ratio = snap.duration_seconds > 0.0 ? snap.position_seconds / snap.duration_seconds : 0.0;
            if (ratio < 0.0) ratio = 0.0;
            if (ratio > 1.0) ratio = 1.0;
            int fill = (int)((double)tw * ratio);
            if (fill > 0) fill_round_rect(a, tx, ty, fill, th, th/2, a->colors.accent);
            char pos[32], dur[32]; format_clock(snap.position_seconds,pos); format_clock(snap.duration_seconds,dur);
            draw_text(a, 166, y+45, pos, a->colors.text);
            draw_text(a, a->width-150, y+45, dur, a->colors.text);
            draw_text_font(a, a->font_small, tx, y+70, "Clique/arraste para buscar  ·  ← → 10s", a->colors.muted);
        }
        int sw = text_width(a, state_text);
        draw_text_font(a, a->font_small, a->font_small, a->width - sw - 18, y+72, state_text,
                       player_state == VIP_PLAYER_ERROR ? a->colors.danger : a->colors.muted);
    }

    if (player_state == VIP_PLAYER_ERROR) {
        const char *err = a->player ? vip_mpv_player_last_error(a->player) : a->player_status;
        char bounded[220]; bounded_text(bounded, sizeof(bounded), err && err[0] ? err : "Falha desconhecida no stream", 90);
        draw_centered_font(a, a->font_heading, 0, a->height / 2 - 12, a->width, "Não foi possível reproduzir", a->colors.danger);
        draw_centered(a, 20, a->height / 2 + 22, a->width - 40, bounded, a->colors.muted);
        draw_centered(a, 0, a->height / 2 + 56, a->width, "Pressione Esc para voltar", a->colors.muted);
    } else if (!a->video_mapped) {
        draw_centered_font(a, a->font_heading, 0, a->height / 2, a->width, state_text, a->colors.muted);
    }
}''')

# Fix a deliberate typo-safe replacement in the player helper call after function generation.
s = s.replace('draw_text_font(a, a->font_small, a->font_small, a->width - sw - 18, y+72, state_text,',
              'draw_text_font(a, a->font_small, a->width - sw - 18, y+72, state_text,', 1)

replace_once('''    a->font=XLoadQueryFont(a->dpy,"-misc-fixed-medium-r-normal--15-*-*-*-*-*-iso8859-1");
    if (!a->font) a->font=XLoadQueryFont(a->dpy,"9x15");
    if (!a->font) a->font=XLoadQueryFont(a->dpy,"fixed");
    if (a->font) XSetFont(a->dpy,a->gc,a->font->fid);''',
'''    a->font=XLoadQueryFont(a->dpy,"-misc-fixed-medium-r-normal--15-*-*-*-*-*-iso8859-1");
    if (!a->font) a->font=XLoadQueryFont(a->dpy,"9x15");
    if (!a->font) a->font=XLoadQueryFont(a->dpy,"fixed");
    a->font_title=XLoadQueryFont(a->dpy,"-*-helvetica-bold-r-normal--24-*-*-*-*-*-iso8859-1");
    a->font_heading=XLoadQueryFont(a->dpy,"-*-helvetica-bold-r-normal--18-*-*-*-*-*-iso8859-1");
    a->font_small=XLoadQueryFont(a->dpy,"-*-helvetica-medium-r-normal--13-*-*-*-*-*-iso8859-1");
    if (a->font) XSetFont(a->dpy,a->gc,a->font->fid);''')
replace_once('''        if (a->font) XFreeFont(a->dpy,a->font);''',
'''        if (a->font_title) XFreeFont(a->dpy,a->font_title);
        if (a->font_heading) XFreeFont(a->dpy,a->font_heading);
        if (a->font_small) XFreeFont(a->dpy,a->font_small);
        if (a->font) XFreeFont(a->dpy,a->font);''')

PATH.write_text(s)
print('premium UI patch applied')
