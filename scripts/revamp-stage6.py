#!/usr/bin/env python3
from pathlib import Path

path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

# Login card: switch the structural surface to Cairo while keeping the Xlib fallback.
old = '''    fill_round_rect(a, x + 8, y + 10, w, h, 24, a->colors.black);\n    fill_round_rect(a, x, y, w, h, 24, a->colors.panel);\n    stroke_round_rect(a, x, y, w, h, 24, a->colors.border);'''
new = '''    if (a->renderer.active) {\n        vip_ui_render_round_rect(&a->renderer, x + 8, y + 10, w, h, 26, 0x000000u, 0.58);\n        vip_ui_render_round_rect(&a->renderer, x, y, w, h, 26, 0x0E1420u, 0.97);\n        vip_ui_render_round_stroke(&a->renderer, x, y, w, h, 26, 0x2B3950u, 1.0, 1.0);\n    } else {\n        fill_round_rect(a, x + 8, y + 10, w, h, 24, a->colors.black);\n        fill_round_rect(a, x, y, w, h, 24, a->colors.panel);\n        stroke_round_rect(a, x, y, w, h, 24, a->colors.border);\n    }'''
assert old in s
s = s.replace(old, new, 1)

# Brand tile gets antialiased geometry and Pango type.
old = '''    fill_round_rect(a, x + 30, y + 25, 42, 42, 13, a->colors.accent);\n    draw_centered_font(a, a->font_heading, x + 30, y + 53, 42, "B", a->colors.bg);'''
new = '''    if (a->renderer.active) {\n        vip_ui_render_round_rect(&a->renderer, x + 30, y + 25, 42, 42, 13, 0x62A9FFu, 1.0);\n        vip_ui_render_text(&a->renderer, x + 30, y + 35, 42, "B", "Sans Bold 13", 0x050811u, 1.0, true);\n    } else {\n        fill_round_rect(a, x + 30, y + 25, 42, 42, 13, a->colors.accent);\n        draw_centered_font(a, a->font_heading, x + 30, y + 53, 42, "B", a->colors.bg);\n    }'''
assert old in s
s = s.replace(old, new, 1)

# Login mode selector: modern pill surfaces and proportional typography.
old = '''        fill_round_rect(a, bx, mode_y, mode_w, 42, 12, selected ? a->colors.accent2 : a->colors.panel2);\n        stroke_round_rect(a, bx, mode_y, mode_w, 42, 12, selected ? a->colors.accent : a->colors.border);\n        draw_centered(a, bx, mode_y + 27, mode_w, i == LOGIN_XTREAM ? "Xtream" : "M3U",\n                      selected ? a->colors.text : a->colors.muted);'''
new = '''        if (a->renderer.active) {\n            vip_ui_render_round_rect(&a->renderer, bx, mode_y, mode_w, 42, 12,\n                                     selected ? 0x183E6Bu : 0x151E2Du, 1.0);\n            vip_ui_render_round_stroke(&a->renderer, bx, mode_y, mode_w, 42, 12,\n                                       selected ? 0x62A9FFu : 0x2B3950u, 1.0, selected ? 1.5 : 1.0);\n            vip_ui_render_text(&a->renderer, bx, mode_y + 12, mode_w,\n                               i == LOGIN_XTREAM ? "Xtream" : "M3U",\n                               selected ? "Sans Bold 10" : "Sans 10",\n                               selected ? 0xF6F8FCu : 0x91A0B7u, 1.0, true);\n        } else {\n            fill_round_rect(a, bx, mode_y, mode_w, 42, 12, selected ? a->colors.accent2 : a->colors.panel2);\n            stroke_round_rect(a, bx, mode_y, mode_w, 42, 12, selected ? a->colors.accent : a->colors.border);\n            draw_centered(a, bx, mode_y + 27, mode_w, i == LOGIN_XTREAM ? "Xtream" : "M3U",\n                          selected ? a->colors.text : a->colors.muted);\n        }'''
assert old in s
s = s.replace(old, new, 1)

# M3U helper copy no longer falls back to the bitmap font when Cairo is available.
old = '''        draw_text(a, form_x, y+266, "M3U remoto (HTTP/HTTPS) ou arquivo local.", a->colors.muted);\n        draw_text(a, form_x, y+292, "A playlist é processada diretamente pelo Blazzing.", a->colors.muted);'''
new = '''        if (a->renderer.active) {\n            vip_ui_render_text(&a->renderer, form_x, y + 252, form_w, "M3U remoto (HTTP/HTTPS) ou arquivo local.", "Sans 9", 0x91A0B7u, 1.0, false);\n            vip_ui_render_text(&a->renderer, form_x, y + 278, form_w, "A playlist é processada diretamente pelo Blazzing.", "Sans 9", 0x91A0B7u, 1.0, false);\n        } else {\n            draw_text(a, form_x, y+266, "M3U remoto (HTTP/HTTPS) ou arquivo local.", a->colors.muted);\n            draw_text(a, form_x, y+292, "A playlist é processada diretamente pelo Blazzing.", a->colors.muted);\n        }'''
assert old in s
s = s.replace(old, new, 1)

# Primary connect CTA: Cairo shape, subtle shadow, Pango label.
old = '''    fill_round_rect(a, form_x + 2, connect_y + 4, form_w, 50, 14, a->colors.black);\n    fill_round_rect(a, form_x, connect_y, form_w, 50, 14, ready ? a->colors.accent : a->colors.panel2);\n    stroke_round_rect(a, form_x, connect_y, form_w, 50, 14, ready ? a->colors.accent : a->colors.border);\n    draw_centered_font(a, a->font_heading, form_x, connect_y+32, form_w,\n                       atomic_load(&a->login_running) ? "Conectando..." : "Conectar",\n                       ready ? a->colors.bg : a->colors.muted);'''
new = '''    if (a->renderer.active) {\n        vip_ui_render_round_rect(&a->renderer, form_x + 3, connect_y + 5, form_w, 50, 15, 0x000000u, 0.48);\n        vip_ui_render_round_rect(&a->renderer, form_x, connect_y, form_w, 50, 15,\n                                 ready ? 0x62A9FFu : 0x151E2Du, 1.0);\n        vip_ui_render_round_stroke(&a->renderer, form_x, connect_y, form_w, 50, 15,\n                                   ready ? 0x8BC1FFu : 0x2B3950u, 1.0, 1.0);\n        vip_ui_render_text(&a->renderer, form_x, connect_y + 15, form_w,\n                           atomic_load(&a->login_running) ? "Conectando..." : "Conectar",\n                           "Sans Bold 11", ready ? 0x050811u : 0x91A0B7u, 1.0, true);\n    } else {\n        fill_round_rect(a, form_x + 2, connect_y + 4, form_w, 50, 14, a->colors.black);\n        fill_round_rect(a, form_x, connect_y, form_w, 50, 14, ready ? a->colors.accent : a->colors.panel2);\n        stroke_round_rect(a, form_x, connect_y, form_w, 50, 14, ready ? a->colors.accent : a->colors.border);\n        draw_centered_font(a, a->font_heading, form_x, connect_y+32, form_w,\n                           atomic_load(&a->login_running) ? "Conectando..." : "Conectar",\n                           ready ? a->colors.bg : a->colors.muted);\n    }'''
assert old in s
s = s.replace(old, new, 1)

# Saved profiles panel and heading.
old = '''    fill_round_rect(a, list_x - 14, y + 88, list_w + 28, 438, 18, a->colors.panel2);\n    stroke_round_rect(a, list_x - 14, y + 88, list_w + 28, 438, 18, a->colors.border);\n    draw_text_font(a, a->font_heading, list_x, y+116, "Suas listas", a->colors.text);\n    draw_text(a, list_x, y+137, "Acesso rápido aos perfis salvos", a->colors.muted);'''
new = '''    if (a->renderer.active) {\n        vip_ui_render_round_rect(&a->renderer, list_x - 14, y + 88, list_w + 28, 438, 18, 0x111A28u, 0.98);\n        vip_ui_render_round_stroke(&a->renderer, list_x - 14, y + 88, list_w + 28, 438, 18, 0x2B3950u, 1.0, 1.0);\n        vip_ui_render_text(&a->renderer, list_x, y + 102, list_w, "Suas listas", "Sans Bold 12", 0xF6F8FCu, 1.0, false);\n        vip_ui_render_text(&a->renderer, list_x, y + 126, list_w, "Acesso rápido aos perfis salvos", "Sans 9", 0x91A0B7u, 1.0, false);\n    } else {\n        fill_round_rect(a, list_x - 14, y + 88, list_w + 28, 438, 18, a->colors.panel2);\n        stroke_round_rect(a, list_x - 14, y + 88, list_w + 28, 438, 18, a->colors.border);\n        draw_text_font(a, a->font_heading, list_x, y+116, "Suas listas", a->colors.text);\n        draw_text(a, list_x, y+137, "Acesso rápido aos perfis salvos", a->colors.muted);\n    }'''
assert old in s
s = s.replace(old, new, 1)

# Profile rows: smoother surface and modern two-line type.
old = '''        draw_surface(a, list_x, row_y, list_w, 52, 12, false);'''
new = '''        if (a->renderer.active) {\n            vip_ui_render_round_rect(&a->renderer, list_x, row_y, list_w, 52, 12, 0x151E2Du, 1.0);\n            vip_ui_render_round_stroke(&a->renderer, list_x, row_y, list_w, 52, 12, 0x2B3950u, 1.0, 1.0);\n        } else {\n            draw_surface(a, list_x, row_y, list_w, 52, 12, false);\n        }'''
assert old in s
s = s.replace(old, new, 1)
old = '''        draw_text(a, list_x+13, row_y+22, label, a->colors.text);\n        char sub[220]; bounded_text(sub, sizeof(sub), p->server ? p->server : "", 46);\n        draw_text_font(a, a->font_small, list_x+13, row_y+42, sub, a->colors.muted);'''
new = '''        char sub[220]; bounded_text(sub, sizeof(sub), p->server ? p->server : "", 46);\n        if (a->renderer.active) {\n            vip_ui_render_text(&a->renderer, list_x + 13, row_y + 8, list_w - 26, label, "Sans SemiBold 9", 0xF6F8FCu, 1.0, false);\n            vip_ui_render_text(&a->renderer, list_x + 13, row_y + 29, list_w - 26, sub, "Sans 8", 0x91A0B7u, 1.0, false);\n        } else {\n            draw_text(a, list_x+13, row_y+22, label, a->colors.text);\n            draw_text_font(a, a->font_small, list_x+13, row_y+42, sub, a->colors.muted);\n        }'''
assert old in s
s = s.replace(old, new, 1)

# Details title and favorite action: keep the already-modern panel but finish its typography/chrome.
old = '''    char title[256]; bounded_text(title, sizeof(title), ch->name, 44);\n    draw_text_font(a, a->font_heading, px + 16, py + 31, title, a->colors.text);'''
new = '''    char title[256]; bounded_text(title, sizeof(title), ch->name, 72);\n    if (a->renderer.active)\n        vip_ui_render_text(&a->renderer, px + 16, py + 13, pw - 132, title, "Sans Bold 12", 0xF6F8FCu, 1.0, false);\n    else\n        draw_text_font(a, a->font_heading, px + 16, py + 31, title, a->colors.text);'''
assert old in s
s = s.replace(old, new, 1)
old = '''    fill_round_rect(a, fav_x, fav_y, fav_w, fav_h, 12,\n                    favorite ? a->colors.accent2 : a->colors.panel2);\n    stroke_round_rect(a, fav_x, fav_y, fav_w, fav_h, 12,\n                      favorite ? a->colors.accent : a->colors.border);\n    draw_centered(a, fav_x, fav_y + 21, fav_w, favorite ? "SALVO" : "FAVORITAR",\n                  favorite ? a->colors.text : a->colors.muted);'''
new = '''    if (a->renderer.active) {\n        vip_ui_render_round_rect(&a->renderer, fav_x, fav_y, fav_w, fav_h, 12,\n                                 favorite ? 0x183E6Bu : 0x151E2Du, 1.0);\n        vip_ui_render_round_stroke(&a->renderer, fav_x, fav_y, fav_w, fav_h, 12,\n                                   favorite ? 0x62A9FFu : 0x2B3950u, 1.0, 1.0);\n        vip_ui_render_text(&a->renderer, fav_x, fav_y + 9, fav_w, favorite ? "SALVO" : "FAVORITAR",\n                           favorite ? "Sans Bold 8" : "Sans 8",\n                           favorite ? 0xF6F8FCu : 0x91A0B7u, 1.0, true);\n    } else {\n        fill_round_rect(a, fav_x, fav_y, fav_w, fav_h, 12,\n                        favorite ? a->colors.accent2 : a->colors.panel2);\n        stroke_round_rect(a, fav_x, fav_y, fav_w, fav_h, 12,\n                          favorite ? a->colors.accent : a->colors.border);\n        draw_centered(a, fav_x, fav_y + 21, fav_w, favorite ? "SALVO" : "FAVORITAR",\n                      favorite ? a->colors.text : a->colors.muted);\n    }'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
