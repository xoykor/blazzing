#!/usr/bin/env python3
from pathlib import Path

path = Path('src/ui_x11/x11_app.c')
s = path.read_text()

# Windowed player header: translucent Cairo chrome and Pango title/back action.
old = '''    if (!a->fullscreen) {\n        fill_rect(a, 0, 0, (unsigned)a->width, PLAYER_HEADER_H, a->colors.panel);\n        fill_round_rect(a, 12, 10, 132, 44, 13, a->colors.panel2);\n        stroke_round_rect(a, 12, 10, 132, 44, 13, a->colors.border);\n        draw_text(a, 28, 39, "< Voltar", a->colors.text);\n        if (a->current_channel < ACTIVE_CHANNELS(a).len)\n            draw_text_font(a, a->font_heading, 168, 39, ACTIVE_CHANNELS(a).items[a->current_channel].name, a->colors.text);\n    }'''
new = '''    if (!a->fullscreen) {\n        if (a->renderer.active) {\n            vip_ui_render_round_rect(&a->renderer, 0, 0, a->width, PLAYER_HEADER_H, 0, 0x0E1420u, 0.97);\n            vip_ui_render_round_rect(&a->renderer, 12, 10, 132, 44, 13, 0x151E2Du, 1.0);\n            vip_ui_render_round_stroke(&a->renderer, 12, 10, 132, 44, 13, 0x2B3950u, 1.0, 1.0);\n            vip_ui_render_text(&a->renderer, 28, 24, 104, "<  Voltar", "Sans SemiBold 10", 0xF6F8FCu, 1.0, false);\n            if (a->current_channel < ACTIVE_CHANNELS(a).len)\n                vip_ui_render_text(&a->renderer, 168, 22, a->width - 190, ACTIVE_CHANNELS(a).items[a->current_channel].name,\n                                   "Sans Bold 12", 0xF6F8FCu, 1.0, false);\n        } else {\n            fill_rect(a, 0, 0, (unsigned)a->width, PLAYER_HEADER_H, a->colors.panel);\n            fill_round_rect(a, 12, 10, 132, 44, 13, a->colors.panel2);\n            stroke_round_rect(a, 12, 10, 132, 44, 13, a->colors.border);\n            draw_text(a, 28, 39, "< Voltar", a->colors.text);\n            if (a->current_channel < ACTIVE_CHANNELS(a).len)\n                draw_text_font(a, a->font_heading, 168, 39, ACTIVE_CHANNELS(a).items[a->current_channel].name, a->colors.text);\n        }\n    }'''
assert old in s
s = s.replace(old, new, 1)

# HUD shell and primary controls.
old = '''        fill_rect(a, 0, y, (unsigned)a->width, PLAYER_CONTROLS_H, a->colors.panel);\n        fill_rect(a, 0, y, (unsigned)a->width, 1, a->colors.border);\n        fill_round_rect(a, 16, y+18, 52, 46, 14, a->colors.panel2);\n        stroke_round_rect(a, 16, y+18, 52, 46, 14, a->colors.border);\n        draw_centered_font(a, a->font_heading, 16, y+48, 52, snap.paused ? ">" : "||", a->colors.text);\n        fill_round_rect(a, 76, y+18, 82, 46, 14, a->colors.panel2);\n        stroke_round_rect(a, 76, y+18, 82, 46, 14, a->colors.border);\n        draw_centered(a, 76, y+47, 82, "Voltar", a->colors.text);'''
new = '''        if (a->renderer.active) {\n            vip_ui_render_round_rect(&a->renderer, 10, y + 7, a->width - 20, PLAYER_CONTROLS_H - 12, 18, 0x0E1420u, 0.96);\n            vip_ui_render_round_stroke(&a->renderer, 10, y + 7, a->width - 20, PLAYER_CONTROLS_H - 12, 18, 0x2B3950u, 0.95, 1.0);\n            vip_ui_render_round_rect(&a->renderer, 16, y+18, 52, 46, 14, 0x151E2Du, 1.0);\n            vip_ui_render_round_stroke(&a->renderer, 16, y+18, 52, 46, 14, 0x36506Fu, 1.0, 1.0);\n            vip_ui_render_text(&a->renderer, 16, y + 31, 52, snap.paused ? ">" : "||", "Sans Bold 12", 0xF6F8FCu, 1.0, true);\n            vip_ui_render_round_rect(&a->renderer, 76, y+18, 82, 46, 14, 0x151E2Du, 1.0);\n            vip_ui_render_round_stroke(&a->renderer, 76, y+18, 82, 46, 14, 0x36506Fu, 1.0, 1.0);\n            vip_ui_render_text(&a->renderer, 76, y + 32, 82, "Voltar", "Sans SemiBold 9", 0xF6F8FCu, 1.0, true);\n        } else {\n            fill_rect(a, 0, y, (unsigned)a->width, PLAYER_CONTROLS_H, a->colors.panel);\n            fill_rect(a, 0, y, (unsigned)a->width, 1, a->colors.border);\n            fill_round_rect(a, 16, y+18, 52, 46, 14, a->colors.panel2);\n            stroke_round_rect(a, 16, y+18, 52, 46, 14, a->colors.border);\n            draw_centered_font(a, a->font_heading, 16, y+48, 52, snap.paused ? ">" : "||", a->colors.text);\n            fill_round_rect(a, 76, y+18, 82, 46, 14, a->colors.panel2);\n            stroke_round_rect(a, 76, y+18, 82, 46, 14, a->colors.border);\n            draw_centered(a, 76, y+47, 82, "Voltar", a->colors.text);\n        }'''
assert old in s
s = s.replace(old, new, 1)

# Live badge and channel-switch hint.
old = '''            fill_round_rect(a, 176, y+22, 76, 30, 12, a->colors.danger);\n            draw_centered(a, 176, y+43, 76, "AO VIVO", a->colors.text);\n            draw_text(a, 270, y+44, "<- -> troca canal", a->colors.muted);'''
new = '''            if (a->renderer.active) {\n                vip_ui_render_round_rect(&a->renderer, 176, y+22, 76, 30, 12, 0xFF7185u, 0.96);\n                vip_ui_render_text(&a->renderer, 176, y + 30, 76, "AO VIVO", "Sans Bold 8", 0xF6F8FCu, 1.0, true);\n                vip_ui_render_text(&a->renderer, 270, y + 31, 220, "<-  ->  troca canal", "Sans 9", 0x91A0B7u, 1.0, false);\n            } else {\n                fill_round_rect(a, 176, y+22, 76, 30, 12, a->colors.danger);\n                draw_centered(a, 176, y+43, 76, "AO VIVO", a->colors.text);\n                draw_text(a, 270, y+44, "<- -> troca canal", a->colors.muted);\n            }'''
assert old in s
s = s.replace(old, new, 1)

# Timeline track/fill gains antialiasing without changing its geometry or seek hitbox.
old = '''            fill_round_rect(a, tx, ty, tw, th, th/2, a->colors.panel2);'''
new = '''            if (a->renderer.active)\n                vip_ui_render_round_rect(&a->renderer, tx, ty, tw, th, th/2, 0x26354Au, 1.0);\n            else\n                fill_round_rect(a, tx, ty, tw, th, th/2, a->colors.panel2);'''
assert old in s
s = s.replace(old, new, 1)
old = '''            if (fill > 0) fill_round_rect(a, tx, ty, fill, th, th/2, a->colors.accent);'''
new = '''            if (fill > 0) {\n                if (a->renderer.active)\n                    vip_ui_render_round_rect(&a->renderer, tx, ty, fill, th, th/2, 0x62A9FFu, 1.0);\n                else\n                    fill_round_rect(a, tx, ty, fill, th, th/2, a->colors.accent);\n            }'''
assert old in s
s = s.replace(old, new, 1)

# Time labels and hint use Pango; keep existing fallback.
old = '''            draw_text(a, 166, y+45, pos, a->colors.text);\n            draw_text(a, a->width-150, y+45, dur, a->colors.text);\n            draw_text_font(a, a->font_small, tx, y+70, "Clique/arraste para buscar  ·  <- -> 10s", a->colors.muted);'''
new = '''            if (a->renderer.active) {\n                vip_ui_render_text(&a->renderer, 166, y + 31, 60, pos, "Sans SemiBold 9", 0xF6F8FCu, 1.0, false);\n                vip_ui_render_text(&a->renderer, a->width - 150, y + 31, 132, dur, "Sans SemiBold 9", 0xF6F8FCu, 1.0, false);\n                vip_ui_render_text(&a->renderer, tx, y + 57, tw, "Clique/arraste para buscar  ·  <- -> 10s", "Sans 8", 0x91A0B7u, 1.0, false);\n            } else {\n                draw_text(a, 166, y+45, pos, a->colors.text);\n                draw_text(a, a->width-150, y+45, dur, a->colors.text);\n                draw_text_font(a, a->font_small, tx, y+70, "Clique/arraste para buscar  ·  <- -> 10s", a->colors.muted);\n            }'''
assert old in s
s = s.replace(old, new, 1)

# Player state in the lower-right uses actual Pango metrics when available.
old = '''        int sw = text_width(a, state_text);\n        draw_text_font(a, a->font_small, a->width - sw - 18, y+72, state_text,\n                       player_state == VIP_PLAYER_ERROR ? a->colors.danger : a->colors.muted);'''
new = '''        if (a->renderer.active) {\n            int sw = vip_ui_render_text_width(&a->renderer, state_text, "Sans 8");\n            vip_ui_render_text(&a->renderer, a->width - sw - 18, y + 58, sw + 2, state_text, "Sans 8",\n                               player_state == VIP_PLAYER_ERROR ? 0xFF7185u : 0x91A0B7u, 1.0, false);\n        } else {\n            int sw = text_width(a, state_text);\n            draw_text_font(a, a->font_small, a->width - sw - 18, y+72, state_text,\n                           player_state == VIP_PLAYER_ERROR ? a->colors.danger : a->colors.muted);\n        }'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
