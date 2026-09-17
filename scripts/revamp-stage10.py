#!/usr/bin/env python3
from pathlib import Path

path = Path('src/app/pluto_app.c')
s = path.read_text()

old = '''    int cols = grid_columns(a);
    int start_x = 30;
    int start_y = PLUTO_HEADER_H + 18;
    for (size_t i = 0u; i < a->channels.len; ++i) {
        int row = (int)(i / (size_t)cols);
        int col = (int)(i % (size_t)cols);
        int x = start_x + col * (PLUTO_CARD_W + PLUTO_GAP);
        int y = start_y + row * (PLUTO_CARD_H + PLUTO_GAP) - a->scroll;
        if (y > a->height || y + PLUTO_CARD_H < PLUTO_HEADER_H) continue;'''
new = '''    int cols = grid_columns(a);
    int start_x = 30;
    int start_y = PLUTO_HEADER_H + 18;
    int content_bottom = a->height - (a->status[0] ? 42 : 8);
    for (size_t i = 0u; i < a->channels.len; ++i) {
        int row = (int)(i / (size_t)cols);
        int col = (int)(i % (size_t)cols);
        int x = start_x + col * (PLUTO_CARD_W + PLUTO_GAP);
        int y = start_y + row * (PLUTO_CARD_H + PLUTO_GAP) - a->scroll;
        if (y + PLUTO_CARD_H > content_bottom || y + PLUTO_CARD_H < PLUTO_HEADER_H) continue;'''
assert old in s
s = s.replace(old, new, 1)
path.write_text(s)
