/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include "util.h"

#define INK (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? 0xFF : 0x00)
#define PAPER (IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? 0x00 : 0xFF)

/* 3x5 pixel font, rows separated by spaces. */
static const char *const font[128] = {
    ['A'] = ".#. #.# ### #.# #.#", ['B'] = "##. #.# ##. #.# ##.", ['C'] = ".## #.. #.. #.. .##",
    ['D'] = "##. #.# #.# #.# ##.", ['E'] = "### #.. ##. #.. ###", ['F'] = "### #.. ##. #.. #..",
    ['G'] = ".## #.. #.# #.# .##", ['H'] = "#.# #.# ### #.# #.#", ['I'] = "### .#. .#. .#. ###",
    ['J'] = "..# ..# ..# #.# .#.", ['K'] = "#.# #.# ##. #.# #.#", ['L'] = "#.. #.. #.. #.. ###",
    ['M'] = "#.# ### ### #.# #.#", ['N'] = "##. #.# #.# #.# #.#", ['O'] = ".#. #.# #.# #.# .#.",
    ['P'] = "##. #.# ##. #.. #..", ['Q'] = ".#. #.# #.# ##. .##", ['R'] = "##. #.# ##. #.# #.#",
    ['S'] = ".## #.. .#. ..# ##.", ['T'] = "### .#. .#. .#. .#.", ['U'] = "#.# #.# #.# #.# ###",
    ['V'] = "#.# #.# #.# #.# .#.", ['W'] = "#.# #.# ### ### #.#", ['X'] = "#.# #.# .#. #.# #.#",
    ['Y'] = "#.# #.# .#. .#. .#.", ['Z'] = "### ..# .#. #.. ###", ['0'] = "### #.# #.# #.# ###",
    ['1'] = ".#. ##. .#. .#. ###", ['2'] = "##. ..# .#. #.. ###", ['3'] = "##. ..# .#. ..# ##.",
    ['4'] = "#.# #.# ### ..# ..#", ['5'] = "### #.. ##. ..# ##.", ['6'] = ".## #.. ### #.# ###",
    ['7'] = "### ..# .#. .#. .#.", ['8'] = "### #.# ### #.# ###", ['9'] = "### #.# ### ..# ##.",
    ['%'] = "#.# ..# .#. #.. #.#", ['-'] = "... ... ### ... ...", ['?'] = "##. ..# .#. ... .#.",
    ['+'] = "... .#. ### .#. ...",
};

static const char *const flask[9] = {"..###..", "..###..", "...#...", "..###..", ".#####.",
                                     "#######", "#######", "#######", ".#####."};
static const char *const flame[7] = {"..#..", "..##.", ".###.", ".####", "##.##", "#...#", ".###."};
static const char *const diamond[3] = {".##.", "####", ".##."};

void hud_create(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_canvas_create(parent);
    lv_canvas_set_buffer(widget->obj, widget->cbuf, HUD_H, HUD_W, HUD_COLOR_FORMAT);
}

void hud_begin(struct hud *h, struct zmk_widget_status *widget) {
    lv_draw_buf_t *db = lv_canvas_get_draw_buf(widget->obj);
    h->canvas = widget->obj;
    h->buf = db->data;
    h->stride = db->header.stride;
    memset(h->buf, PAPER, h->stride * HUD_W);
}

void hud_end(struct hud *h) { lv_obj_invalidate(h->canvas); }

/* Portrait (x, y) -> landscape (HUD_H - 1 - y, x): portrait top sits at the panel's right edge,
 * matching the stock widget's LV_DISPLAY_ROTATION_270 canvases. */
void hud_px(struct hud *h, int x, int y, bool ink) {
    if (x < 0 || x >= HUD_W || y < 0 || y >= HUD_H) {
        return;
    }
    h->buf[x * h->stride + (HUD_H - 1 - y)] = ink ? INK : PAPER;
}

void hud_rect(struct hud *h, int x, int y, int w, int h_, bool ink) {
    for (int j = 0; j < h_; j++) {
        for (int i = 0; i < w; i++) {
            hud_px(h, x + i, y + j, ink);
        }
    }
}

void hud_box(struct hud *h, int x, int y, int w, int h_, bool ink) {
    hud_rect(h, x, y, w, 1, ink);
    hud_rect(h, x, y + h_ - 1, w, 1, ink);
    hud_rect(h, x, y, 1, h_, ink);
    hud_rect(h, x + w - 1, y, 1, h_, ink);
}

void hud_sprite(struct hud *h, const char *const rows[], int nrows, int x, int y, int scale,
                bool ink, bool flip) {
    for (int j = 0; j < nrows; j++) {
        int jj = flip ? nrows - 1 - j : j;
        for (int i = 0; rows[j][i]; i++) {
            if (rows[j][i] == '#') {
                hud_rect(h, x + i * scale, y + jj * scale, scale, scale, ink);
            }
        }
    }
}

int hud_text_w(const char *t, int scale) {
    int n = strlen(t);
    return n ? (n * 4 - 1) * scale : 0;
}

void hud_text(struct hud *h, const char *t, int x, int y, int scale, bool ink) {
    for (; *t; t++, x += 4 * scale) {
        unsigned char c = toupper((unsigned char)*t);
        const char *g = c < 128 ? font[c] : NULL;
        if (!g) {
            continue;
        }
        for (int r = 0; r < 5; r++) {
            for (int col = 0; col < 3; col++) {
                if (g[r * 4 + col] == '#') {
                    hud_rect(h, x + col * scale, y + r * scale, scale, scale, ink);
                }
            }
        }
    }
}

void hud_ctext(struct hud *h, const char *t, int y, int scale) {
    hud_text(h, t, (HUD_W - hud_text_w(t, scale)) / 2, y, scale, true);
}

/* Battery row: label, flask (flame while charging), five 20% segments, number. */
void hud_gauge(struct hud *h, const char *label, int y, int pct, bool charging) {
    char t[4];
    hud_text(h, label, 2, y + 2, 1, true);
    if (charging) {
        hud_sprite(h, flame, 7, 7, y, 1, true, false);
    } else {
        hud_sprite(h, flask, 9, 6, y, 1, true, false);
    }
    for (int k = 0; k < 5; k++) {
        if (pct >= k * 20 + 10) {
            hud_rect(h, 15 + k * 8, y + 1, 7, 7, true);
        } else {
            hud_box(h, 15 + k * 8, y + 1, 7, 7, true);
        }
    }
    snprintf(t, sizeof(t), "%d", pct);
    hud_text(h, t, 66 - hud_text_w(t, 1), y + 2, 1, true);
}

/* Horizontal rule with a diamond in the middle and dots at the ends. */
void hud_orn(struct hud *h, int y, int x0, int x1) {
    for (int x = x0; x <= x1; x++) {
        if (x < 30 || x > 37) {
            hud_px(h, x, y, true);
        }
    }
    hud_sprite(h, diamond, 3, 32, y - 1, 1, true, false);
    hud_px(h, x0 - 2, y, true);
    hud_px(h, x1 + 2, y, true);
}

void hud_dither(struct hud *h, int y0) {
    for (int y = y0; y < HUD_H; y++) {
        for (int x = 0; x < HUD_W; x++) {
            if ((x + y) % 4 == 0 && y % 2 == 0) {
                hud_px(h, x, y, true);
            }
        }
    }
}
