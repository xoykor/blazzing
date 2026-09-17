#!/usr/bin/env python3
from pathlib import Path

# CMake: wire Cairo/Pango renderer into the UI library.
cmake = Path('CMakeLists.txt')
s = cmake.read_text()
old = '''pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)\nfind_program(FFMPEG_EXECUTABLE NAMES ffmpeg REQUIRED)'''
new = '''pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)\npkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)\npkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)\nfind_program(FFMPEG_EXECUTABLE NAMES ffmpeg REQUIRED)'''
assert old in s
s = s.replace(old, new, 1)
old = '''add_library(vip_ui_x11\n    src/ui_x11/x11_app.c\n    src/ui_x11/ui_motion.c)'''
new = '''add_library(vip_ui_x11\n    src/ui_x11/x11_app.c\n    src/ui_x11/ui_motion.c\n    src/ui_x11/ui_render.c)'''
assert old in s
s = s.replace(old, new, 1)
old = '''    vip_core vip_provider vip_database vip_decoder vip_thumbnails vip_player_mpv\n    PkgConfig::X11 PkgConfig::JPEG PkgConfig::CURL Threads::Threads)'''
new = '''    vip_core vip_provider vip_database vip_decoder vip_thumbnails vip_player_mpv\n    PkgConfig::X11 PkgConfig::JPEG PkgConfig::CURL PkgConfig::CAIRO PkgConfig::PANGOCAIRO Threads::Threads)'''
assert old in s
s = s.replace(old, new, 1)
cmake.write_text(s)

# CI: compile the modern renderer in both normal and sanitizer builds.
ci = Path('.github/workflows/ci.yml')
s = ci.read_text()
old = '''            libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev \\\n            libjpeg-dev libpng-dev libwebp-dev libssl-dev ffmpeg mpv'''
new = '''            libx11-dev libcurl4-openssl-dev libjson-c-dev libsqlite3-dev \\\n            libjpeg-dev libpng-dev libwebp-dev libssl-dev libcairo2-dev libpango1.0-dev ffmpeg mpv'''
assert old in s
s = s.replace(old, new, 1)
ci.write_text(s)

# X11 shell: renderer lifecycle + first migrated surfaces/text.
path = Path('src/ui_x11/x11_app.c')
s = path.read_text()
old = '#include "visual_iptv/ui_motion.h"\n'
new = '#include "visual_iptv/ui_motion.h"\n#include "visual_iptv/ui_render.h"\n'
assert old in s
s = s.replace(old, new, 1)

old = '''    GC gc;\n    XFontStruct *font;'''
new = '''    GC gc;\n    vip_ui_renderer_t renderer;\n    XFontStruct *font;'''
assert old in s
s = s.replace(old, new, 1)

# Modern gradient foundation for login.
old = '''static void draw_login(app_t *a) {\n    fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);\n    fill_rect(a, 0, 0, (unsigned)a->width, 7, a->colors.accent);'''
new = '''static void draw_login(app_t *a) {\n    if (a->renderer.active)\n        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, a->height, 0x050811u, 0x0B1220u);\n    else\n        fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);\n    fill_rect(a, 0, 0, (unsigned)a->width, 7, a->colors.accent);'''
assert old in s
s = s.replace(old, new, 1)

# Pango for the primary brand heading and subtitle, with Xlib fallback.
old = '''    draw_text_font(a, a->font_title, x + 86, y + 49, "Blazzing", a->colors.text);\n    draw_text(a, x + 86, y + 69, "Streaming, listas e biblioteca em um só lugar", a->colors.muted);'''
new = '''    if (a->renderer.active) {\n        vip_ui_render_text(&a->renderer, x + 86, y + 27, 360, "Blazzing", "Sans Bold 22", 0xF6F8FCu, 1.0, false);\n        vip_ui_render_text(&a->renderer, x + 86, y + 55, 420, "Streaming, listas e biblioteca em um só lugar", "Sans 10", 0x91A0B7u, 1.0, false);\n    } else {\n        draw_text_font(a, a->font_title, x + 86, y + 49, "Blazzing", a->colors.text);\n        draw_text(a, x + 86, y + 69, "Streaming, listas e biblioteca em um só lugar", a->colors.muted);\n    }'''
assert old in s
s = s.replace(old, new, 1)

# Modern gradient foundation for catalog/topbar/sidebar.
old = '''static void draw_browse(app_t *a) {\n    fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);\n    fill_rect(a, 0, 0, (unsigned)a->width, TOPBAR_H, a->colors.panel);\n    fill_rect(a, 0, 0, (unsigned)a->width, 4, a->colors.accent);\n    fill_rect(a, 0, TOPBAR_H, SIDEBAR_W, (unsigned)(a->height-TOPBAR_H), a->colors.panel2);'''
new = '''static void draw_browse(app_t *a) {\n    if (a->renderer.active) {\n        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, a->height, 0x050811u, 0x080D17u);\n        vip_ui_render_linear_gradient(&a->renderer, 0, 0, a->width, TOPBAR_H, 0x121C2Cu, 0x0C1420u);\n        vip_ui_render_linear_gradient(&a->renderer, 0, TOPBAR_H, SIDEBAR_W, a->height-TOPBAR_H, 0x121C2Au, 0x0C1320u);\n    } else {\n        fill_rect(a, 0, 0, (unsigned)a->width, (unsigned)a->height, a->colors.bg);\n        fill_rect(a, 0, 0, (unsigned)a->width, TOPBAR_H, a->colors.panel);\n        fill_rect(a, 0, TOPBAR_H, SIDEBAR_W, (unsigned)(a->height-TOPBAR_H), a->colors.panel2);\n    }\n    fill_rect(a, 0, 0, (unsigned)a->width, 4, a->colors.accent);'''
assert old in s
s = s.replace(old, new, 1)

# Details panel gets true alpha shadow/surface when Cairo is available.
old = '''    fill_round_rect(a, px + 4, py + 6, pw, ph, 18, a->colors.black);\n    fill_round_rect(a, px, py, pw, ph, 18, a->colors.panel);\n    stroke_round_rect(a, px, py, pw, ph, 18, a->colors.border);'''
new = '''    if (a->renderer.active) {\n        vip_ui_render_round_rect(&a->renderer, px + 5, py + 8, pw, ph, 20, 0x000000u, 0.55);\n        vip_ui_render_round_rect(&a->renderer, px, py, pw, ph, 20, 0x0E1420u, 0.97);\n        vip_ui_render_round_stroke(&a->renderer, px, py, pw, ph, 20, 0x2B3950u, 1.0, 1.0);\n    } else {\n        fill_round_rect(a, px + 4, py + 6, pw, ph, 18, a->colors.black);\n        fill_round_rect(a, px, py, pw, ph, 18, a->colors.panel);\n        stroke_round_rect(a, px, py, pw, ph, 18, a->colors.border);\n    }'''
assert old in s
s = s.replace(old, new, 1)

# Active card gets a soft Cairo shadow/glow behind the existing Xlib content.
old = '''            bool active_card = focused || hovered;\n            fill_round_rect(a, cx + 4, cy + 6 + (int)(3.0f * hover_eased), layout.card_w, layout.card_h, 16, a->colors.black);'''
new = '''            bool active_card = focused || hovered;\n            if (a->renderer.active && active_card) {\n                vip_ui_render_round_rect(&a->renderer, cx + 5, cy + 8 + (int)(3.0f * hover_eased), layout.card_w, layout.card_h, 18, 0x000000u, 0.62);\n                vip_ui_render_round_stroke(&a->renderer, cx-3, cy-3, layout.card_w+6, layout.card_h+6, 19, 0x62A9FFu, 0.80, 2.0);\n            } else {\n                fill_round_rect(a, cx + 4, cy + 6 + (int)(3.0f * hover_eased), layout.card_w, layout.card_h, 16, a->colors.black);\n            }'''
assert old in s
s = s.replace(old, new, 1)

# One renderer context per frame. The wrapper synchronizes Cairo/Xlib mixing.
old = '''static void redraw(app_t *a) {\n    bool buffered = ensure_backbuffer(a);\n    a->draw = buffered ? a->backbuffer : a->win;\n    if (a->screen == SCREEN_LOGIN) draw_login(a);\n    else if (a->screen == SCREEN_BROWSE) draw_browse(a);\n    else draw_player(a);\n    if (buffered) {'''
new = '''static void redraw(app_t *a) {\n    bool buffered = ensure_backbuffer(a);\n    a->draw = buffered ? a->backbuffer : a->win;\n    (void)vip_ui_renderer_begin(&a->renderer, a->dpy, a->draw, a->visual, a->width, a->height);\n    if (a->screen == SCREEN_LOGIN) draw_login(a);\n    else if (a->screen == SCREEN_BROWSE) draw_browse(a);\n    else draw_player(a);\n    vip_ui_renderer_end(&a->renderer);\n    if (buffered) {'''
assert old in s
s = s.replace(old, new, 1)

path.write_text(s)
