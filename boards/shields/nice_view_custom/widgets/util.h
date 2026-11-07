/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <lvgl.h>
#include <zmk/endpoints.h>

#define NICEVIEW_PROFILE_COUNT 5

/* The HUD is drawn in portrait: 68 wide, 160 tall (the panel is 160x68 landscape). */
#define HUD_W 68
#define HUD_H 160
#define HUD_COLOR_FORMAT LV_COLOR_FORMAT_L8
#define HUD_BUF_SIZE LV_CANVAS_BUF_SIZE(HUD_H, HUD_W, 8, LV_DRAW_BUF_STRIDE_ALIGN)

struct status_state {
    uint8_t battery;
    bool charging;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint8_t peripheral_battery;
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_connected[NICEVIEW_PROFILE_COUNT];
    bool profiles_bonded[NICEVIEW_PROFILE_COUNT];
    uint8_t layer_index;
    const char *layer_label;
#else
    bool connected;
#endif
};

struct battery_status_state {
    uint8_t level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool usb_present;
#endif
};

struct zmk_widget_status {
    sys_snode_t node;
    lv_obj_t *obj;
    uint8_t cbuf[HUD_BUF_SIZE] __aligned(LV_DRAW_BUF_ALIGN);
    struct status_state state;
};

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget);

/*
 * Pixel HUD primitives. Coordinates are portrait; sprites are arrays of
 * strings where '#' is an ink pixel - the same bitmaps as the design mockup.
 */
struct hud {
    lv_obj_t *canvas;
    uint8_t *buf;
    uint32_t stride;
};

void hud_create(struct zmk_widget_status *widget, lv_obj_t *parent);
void hud_begin(struct hud *h, struct zmk_widget_status *widget);
void hud_end(struct hud *h);
void hud_px(struct hud *h, int x, int y, bool ink);
void hud_rect(struct hud *h, int x, int y, int w, int h_, bool ink);
void hud_box(struct hud *h, int x, int y, int w, int h_, bool ink);
void hud_sprite(struct hud *h, const char *const rows[], int nrows, int x, int y, int scale,
                bool ink, bool flip);
int hud_text_w(const char *t, int scale);
void hud_text(struct hud *h, const char *t, int x, int y, int scale, bool ink);
void hud_hearts(struct hud *h, int x, int y, int pct);
void hud_dither(struct hud *h, int y0);

extern const char *const hud_bolt[7];
