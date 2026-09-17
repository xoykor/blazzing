#!/usr/bin/env python3
from pathlib import Path

path = Path('src/app/pluto_app.c')
s = path.read_text()

old = '#include "visual_iptv/thumbnails.h"\n'
new = '#include "visual_iptv/thumbnails.h"\n#include "visual_iptv/ui_render.h"\n'
assert old in s
s = s.replace(old, new, 1)

old = '''    Window video_win;\n    GC gc;\n    XFontStruct *font;'''
new = '''    Window video_win;\n    GC gc;\n    vip_ui_renderer_t renderer;\n    XFontStruct *font;'''
assert old in s
s = s.replace(old, new, 1)

# Keep UTF-8 truncation on codepoint boundaries.
old = '''    if (n >= cap) n = cap - 1u;\n    memcpy(dst, src, n);'''
new = '''    if (n >= cap) n = cap - 1u;\n    while (n > 0u && (((unsigned char)src[n] & 0xc0u) == 0x80u)) --n;\n    memcpy(dst, src, n);'''
assert old in s
s = s.replace(old, new, 1)

old = '''static void draw_header(pluto_app_t *a) {\n    fill_rect(a, 0, 0, a->width, PLUTO_HEADER_H, a->panel);\n    fill_rect(a, 12, 11, 112, 44, a->panel2);\n    stroke_rect(a, 12, 11, 112, 44, a->border);\n    draw_center(a, 12, 39, 112, a->playing ? "< Voltar" : "< Hub", a->text);\n    draw_text(a, 146, 34, "Pluto TV", a->text);\n    if (!a->playing) {\n        char count[96];\n        snprintf(count, sizeof(count), "%zu canais | sem login", a->channels.len);\n        draw_text(a, 146, 54, count, a->muted);\n    } else if (a->selected < a->channels.len) {\n        char title[220];\n        bounded_text(title, sizeof(title), a->channels.items[a->selected].name, 44u);\n        draw_text(a, 250, 34, title, a->muted);\n    }\n}'''
new = '''static void draw_header(pluto_app_t *a) {\n    if (a->renderer.active) {\n        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, PLUTO_HEADER_H, 0x121C2Cu, 0x0C1420u);\n        vip_ui_render_round_rect(&a->renderer, 12, 11, 112, 44, 13, 0x151E2Du, 1.0);\n        vip_ui_render_round_stroke(&a->renderer, 12, 11, 112, 44, 13, 0x2B3950u, 1.0, 1.0);\n        vip_ui_render_text(&a->renderer, 12, 25, 112, a->playing ? "<  Voltar" : "<  Hub",\n                           "Sans SemiBold 9", 0xF6F8FCu, 1.0, true);\n        vip_ui_render_text(&a->renderer, 146, 18, 180, "Pluto TV", "Sans Bold 13", 0xF6F8FCu, 1.0, false);\n        if (!a->playing) {\n            char count[96];\n            snprintf(count, sizeof(count), "%zu canais  ·  sem login", a->channels.len);\n            vip_ui_render_text(&a->renderer, 146, 43, 240, count, "Sans 8", 0x91A0B7u, 1.0, false);\n        } else if (a->selected < a->channels.len) {\n            char title[220];\n            bounded_text(title, sizeof(title), a->channels.items[a->selected].name, 80u);\n            vip_ui_render_text(&a->renderer, 300, 20, a->width - 320, title, "Sans 10", 0x91A0B7u, 1.0, false);\n        }\n        return;\n    }\n    fill_rect(a, 0, 0, a->width, PLUTO_HEADER_H, a->panel);\n    fill_rect(a, 12, 11, 112, 44, a->panel2);\n    stroke_rect(a, 12, 11, 112, 44, a->border);\n    draw_center(a, 12, 39, 112, a->playing ? "< Voltar" : "< Hub", a->text);\n    draw_text(a, 146, 34, "Pluto TV", a->text);\n    if (!a->playing) {\n        char count[96];\n        snprintf(count, sizeof(count), "%zu canais | sem login", a->channels.len);\n        draw_text(a, 146, 54, count, a->muted);\n    } else if (a->selected < a->channels.len) {\n        char title[220];\n        bounded_text(title, sizeof(title), a->channels.items[a->selected].name, 44u);\n        draw_text(a, 250, 34, title, a->muted);\n    }\n}'''
assert old in s
s = s.replace(old, new, 1)

old = '''static void draw_grid(pluto_app_t *a) {\n    fill_rect(a, 0, 0, a->width, a->height, a->bg);\n    draw_header(a);'''
new = '''static void draw_grid(pluto_app_t *a) {\n    if (a->renderer.active)\n        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, a->height, 0x050811u, 0x090F1Au);\n    else\n        fill_rect(a, 0, 0, a->width, a->height, a->bg);\n    draw_header(a);'''
assert old in s
s = s.replace(old, new, 1)

old = '''        draw_center(a, 0, a->height / 2, a->width,\n                    a->status[0] ? a->status : "Nenhum canal recebido do Pluto TV", a->muted);'''
new = '''        if (a->renderer.active)\n            vip_ui_render_text(&a->renderer, 40, a->height / 2 - 10, a->width - 80,\n                               a->status[0] ? a->status : "Nenhum canal recebido do Pluto TV",\n                               "Sans 10", 0x91A0B7u, 1.0, true);\n        else\n            draw_center(a, 0, a->height / 2, a->width,\n                        a->status[0] ? a->status : "Nenhum canal recebido do Pluto TV", a->muted);'''
assert old in s
s = s.replace(old, new, 1)

old = '''        bool selected = i == a->selected;\n        if (selected) {\n            stroke_rect(a, x - 3, y - 3, PLUTO_CARD_W + 6, PLUTO_CARD_H + 6, a->accent);\n            stroke_rect(a, x - 2, y - 2, PLUTO_CARD_W + 4, PLUTO_CARD_H + 4, a->accent);\n        }\n        fill_rect(a, x, y, PLUTO_CARD_W, PLUTO_ART_H, a->black);'''
new = '''        bool selected = i == a->selected;\n        if (a->renderer.active) {\n            vip_ui_render_round_rect(&a->renderer, x + 4, y + 6, PLUTO_CARD_W, PLUTO_CARD_H, 16, 0x000000u, 0.46);\n            vip_ui_render_round_rect(&a->renderer, x, y, PLUTO_CARD_W, PLUTO_CARD_H, 16,\n                                     selected ? 0x173B67u : 0x151E2Du, 1.0);\n            vip_ui_render_round_stroke(&a->renderer, x, y, PLUTO_CARD_W, PLUTO_CARD_H, 16,\n                                       selected ? 0x62A9FFu : 0x2B3950u, 1.0, selected ? 1.8 : 1.0);\n        } else if (selected) {\n            stroke_rect(a, x - 3, y - 3, PLUTO_CARD_W + 6, PLUTO_CARD_H + 6, a->accent);\n            stroke_rect(a, x - 2, y - 2, PLUTO_CARD_W + 4, PLUTO_CARD_H + 4, a->accent);\n        }\n        fill_rect(a, x, y, PLUTO_CARD_W, PLUTO_ART_H, a->black);'''
assert old in s
s = s.replace(old, new, 1)

old = '''            draw_center(a, x, y + PLUTO_ART_H / 2 + 5, PLUTO_CARD_W, "carregando imagem...", a->muted);\n            enqueue_thumbnail(a, channel, 1000000LL - (int64_t)i);'''
new = '''            if (a->renderer.active)\n                vip_ui_render_text(&a->renderer, x + 8, y + PLUTO_ART_H / 2 - 7, PLUTO_CARD_W - 16,\n                                   "carregando imagem...", "Sans 9", 0x91A0B7u, 1.0, true);\n            else\n                draw_center(a, x, y + PLUTO_ART_H / 2 + 5, PLUTO_CARD_W, "carregando imagem...", a->muted);\n            enqueue_thumbnail(a, channel, 1000000LL - (int64_t)i);'''
assert old in s
s = s.replace(old, new, 1)

old = '''        stroke_rect(a, x, y, PLUTO_CARD_W, PLUTO_ART_H, a->border);\n        char title[160];\n        bounded_text(title, sizeof(title), channel->name, 38u);\n        draw_text(a, x, y + PLUTO_ART_H + 24, title, a->text);'''
new = '''        stroke_rect(a, x, y, PLUTO_CARD_W, PLUTO_ART_H, selected ? a->accent : a->border);\n        char title[192];\n        bounded_text(title, sizeof(title), channel->name, 72u);\n        if (a->renderer.active)\n            vip_ui_render_text(&a->renderer, x + 10, y + PLUTO_ART_H + 12, PLUTO_CARD_W - 20, title,\n                               selected ? "Sans SemiBold 10" : "Sans 10", 0xF6F8FCu, 1.0, false);\n        else\n            draw_text(a, x, y + PLUTO_ART_H + 24, title, a->text);'''
assert old in s
s = s.replace(old, new, 1)

old = '''    if (a->status[0]) {\n        fill_rect(a, 0, a->height - 32, a->width, 32, a->panel);\n        draw_text(a, 30, a->height - 11, a->status, a->muted);\n    }'''
new = '''    if (a->status[0]) {\n        if (a->renderer.active) {\n            vip_ui_render_round_rect(&a->renderer, 0, a->height - 34, a->width, 34, 0, 0x0E1420u, 0.96);\n            vip_ui_render_text(&a->renderer, 30, a->height - 25, a->width - 60, a->status, "Sans 8", 0x91A0B7u, 1.0, false);\n        } else {\n            fill_rect(a, 0, a->height - 32, a->width, 32, a->panel);\n            draw_text(a, 30, a->height - 11, a->status, a->muted);\n        }\n    }'''
assert old in s
s = s.replace(old, new, 1)

old = '''static void draw_player(pluto_app_t *a) {\n    fill_rect(a, 0, 0, a->width, a->height, a->black);\n    draw_header(a);\n    int footer_y = a->height - PLUTO_PLAYER_FOOTER_H;\n    fill_rect(a, 0, footer_y, a->width, PLUTO_PLAYER_FOOTER_H, a->panel);'''
new = '''static void draw_player(pluto_app_t *a) {\n    fill_rect(a, 0, 0, a->width, a->height, a->black);\n    draw_header(a);\n    int footer_y = a->height - PLUTO_PLAYER_FOOTER_H;\n    if (a->renderer.active) {\n        vip_ui_render_round_rect(&a->renderer, 10, footer_y + 6, a->width - 20, PLUTO_PLAYER_FOOTER_H - 10, 18, 0x0E1420u, 0.96);\n        vip_ui_render_round_stroke(&a->renderer, 10, footer_y + 6, a->width - 20, PLUTO_PLAYER_FOOTER_H - 10, 18, 0x2B3950u, 1.0, 1.0);\n    } else {\n        fill_rect(a, 0, footer_y, a->width, PLUTO_PLAYER_FOOTER_H, a->panel);\n    }'''
assert old in s
s = s.replace(old, new, 1)

old = '''    fill_rect(a, 16, footer_y + 14, 54, 44, a->panel2);\n    stroke_rect(a, 16, footer_y + 14, 54, 44, a->border);\n    draw_center(a, 16, footer_y + 42, 54, snapshot.paused ? ">" : "||", a->text);\n    fill_rect(a, 80, footer_y + 14, 88, 44, a->panel2);\n    stroke_rect(a, 80, footer_y + 14, 88, 44, a->border);\n    draw_center(a, 80, footer_y + 42, 88, "< Voltar", a->text);\n    draw_text(a, 190, footer_y + 40, "<- -> troca canal", a->muted);'''
new = '''    if (a->renderer.active) {\n        vip_ui_render_round_rect(&a->renderer, 16, footer_y + 14, 54, 44, 14, 0x151E2Du, 1.0);\n        vip_ui_render_round_stroke(&a->renderer, 16, footer_y + 14, 54, 44, 14, 0x36506Fu, 1.0, 1.0);\n        vip_ui_render_text(&a->renderer, 16, footer_y + 27, 54, snapshot.paused ? ">" : "||", "Sans Bold 11", 0xF6F8FCu, 1.0, true);\n        vip_ui_render_round_rect(&a->renderer, 80, footer_y + 14, 88, 44, 14, 0x151E2Du, 1.0);\n        vip_ui_render_round_stroke(&a->renderer, 80, footer_y + 14, 88, 44, 14, 0x36506Fu, 1.0, 1.0);\n        vip_ui_render_text(&a->renderer, 80, footer_y + 27, 88, "<  Voltar", "Sans SemiBold 9", 0xF6F8FCu, 1.0, true);\n        vip_ui_render_text(&a->renderer, 190, footer_y + 29, 220, "<-  ->  troca canal", "Sans 9", 0x91A0B7u, 1.0, false);\n    } else {\n        fill_rect(a, 16, footer_y + 14, 54, 44, a->panel2);\n        stroke_rect(a, 16, footer_y + 14, 54, 44, a->border);\n        draw_center(a, 16, footer_y + 42, 54, snapshot.paused ? ">" : "||", a->text);\n        fill_rect(a, 80, footer_y + 14, 88, 44, a->panel2);\n        stroke_rect(a, 80, footer_y + 14, 88, 44, a->border);\n        draw_center(a, 80, footer_y + 42, 88, "< Voltar", a->text);\n        draw_text(a, 190, footer_y + 40, "<- -> troca canal", a->muted);\n    }'''
assert old in s
s = s.replace(old, new, 1)

old = '''    const char *state = a->player ? vip_mpv_player_state_name(snapshot.state) : "sem player";\n    int tw = text_width(a, state);\n    draw_text(a, a->width - tw - 20, footer_y + 42, state,\n              snapshot.state == VIP_PLAYER_ERROR ? a->danger : a->muted);'''
new = '''    const char *state = a->player ? vip_mpv_player_state_name(snapshot.state) : "sem player";\n    if (a->renderer.active) {\n        int tw = vip_ui_render_text_width(&a->renderer, state, "Sans 8");\n        vip_ui_render_text(&a->renderer, a->width - tw - 20, footer_y + 30, tw + 2, state, "Sans 8",\n                           snapshot.state == VIP_PLAYER_ERROR ? 0xFF7185u : 0x91A0B7u, 1.0, false);\n    } else {\n        int tw = text_width(a, state);\n        draw_text(a, a->width - tw - 20, footer_y + 42, state,\n                  snapshot.state == VIP_PLAYER_ERROR ? a->danger : a->muted);\n    }'''
assert old in s
s = s.replace(old, new, 1)

old = '''        draw_center(a, 0, a->height / 2, a->width, bounded, a->danger);'''
new = '''        if (a->renderer.active)\n            vip_ui_render_text(&a->renderer, 40, a->height / 2 - 10, a->width - 80, bounded, "Sans SemiBold 10", 0xFF7185u, 1.0, true);\n        else\n            draw_center(a, 0, a->height / 2, a->width, bounded, a->danger);'''
assert old in s
s = s.replace(old, new, 1)

old = '''static void redraw(pluto_app_t *a) {\n    if (a->playing) draw_player(a);\n    else draw_grid(a);\n    XFlush(a->dpy);\n}'''
new = '''static void redraw(pluto_app_t *a) {\n    (void)vip_ui_renderer_begin(&a->renderer, a->dpy, a->win, a->visual, a->width, a->height);\n    if (a->playing) draw_player(a);\n    else draw_grid(a);\n    vip_ui_renderer_end(&a->renderer);\n    XFlush(a->dpy);\n}'''
assert old in s
s = s.replace(old, new, 1)

old = '''static void draw_loading(pluto_app_t *a, const char *message) {\n    fill_rect(a, 0, 0, a->width, a->height, a->bg);\n    fill_rect(a, 0, 0, a->width, PLUTO_HEADER_H, a->panel);\n    draw_text(a, 30, 40, "Pluto TV", a->text);\n    draw_center(a, 0, a->height / 2, a->width, message, a->muted);\n    XFlush(a->dpy);\n}'''
new = '''static void draw_loading(pluto_app_t *a, const char *message) {\n    bool modern = vip_ui_renderer_begin(&a->renderer, a->dpy, a->win, a->visual, a->width, a->height);\n    if (modern) {\n        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, a->height, 0x050811u, 0x090F1Au);\n        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, PLUTO_HEADER_H, 0x121C2Cu, 0x0C1420u);\n        vip_ui_render_text(&a->renderer, 30, 22, 200, "Pluto TV", "Sans Bold 13", 0xF6F8FCu, 1.0, false);\n        vip_ui_render_text(&a->renderer, 40, a->height / 2 - 10, a->width - 80, message, "Sans 10", 0x91A0B7u, 1.0, true);\n        vip_ui_renderer_end(&a->renderer);\n    } else {\n        fill_rect(a, 0, 0, a->width, a->height, a->bg);\n        fill_rect(a, 0, 0, a->width, PLUTO_HEADER_H, a->panel);\n        draw_text(a, 30, 40, "Pluto TV", a->text);\n        draw_center(a, 0, a->height / 2, a->width, message, a->muted);\n    }\n    XFlush(a->dpy);\n}'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
