/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Right (peripheral) HUD: 2P badge, link state, own HP, and a cat that
 * idles while linked and sleeps when the link drops.
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>

#include "status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static uint32_t frame;

static const char *const moon[9] = {"...####..", ".####....", ".###.....", "###......", "###......",
                                    "###......", ".###.....", ".####....", "...####.."};
static const char *const cat[14] = {
    ".#.......#......", ".##.....##......", ".#########......", ".#########......",
    ".##.###.##......", ".##.###.##......", ".####.####......", "..#######.......",
    "..########......", ".##########....#", ".##########...##", ".###########..#.",
    ".###############", ".##.##.#######..",
};
static const char *const cat_flick[14] = {
    ".#.......#......", ".##.....##......", ".#########......", ".#########......",
    ".##.###.##......", ".##.###.##......", ".####.####......", "..#######.....#.",
    "..########....#.", ".##########...#.", ".##########..##.", ".############...",
    ".#############..", ".##.##.#######..",
};
static const char *const cat_blink[14] = {
    ".#.......#......", ".##.....##......", ".#########......", ".#########......",
    ".#########......", ".##.###.##......", ".####.####......", "..#######.......",
    "..########......", ".##########....#", ".##########...##", ".###########..#.",
    ".###############", ".##.##.#######..",
};
static const char *const cat_sleep[9] = {
    ".#...#..........", ".##.##..........", ".#####.#######..", "################",
    "#..#..##########", "################", "################", "################",
    ".##############.",
};
static const uint8_t stars[][2] = {{6, 31},  {14, 47}, {31, 35}, {58, 58}, {8, 74},   {40, 50},
                                   {24, 88}, {62, 92}, {47, 78}, {36, 66}, {12, 104}, {55, 112}};

static void draw(struct zmk_widget_status *widget) {
    const struct status_state *s = &widget->state;
    struct hud h;
    char t[4];
    hud_begin(&h, widget);

    // header band: player badge + link
    hud_rect(&h, 0, 0, HUD_W, 11, true);
    hud_rect(&h, 2, 2, 13, 7, false);
    hud_text(&h, "2P", 3, 3, 1, true);
    if (s->connected || frame % 2) {
        hud_text(&h, s->connected ? "LINK" : "LOST", 66 - hud_text_w("LINK", 1), 3, 1, false);
    }

    // HP row
    hud_text(&h, "HP", 2, 15, 1, true);
    hud_hearts(&h, 10, 14, s->battery);
    if (s->charging) {
        hud_sprite(&h, hud_bolt, 7, 51, 13, 1, true, false);
    }
    snprintf(t, sizeof(t), "%d", s->battery);
    hud_text(&h, t, 66 - hud_text_w(t, 1), 15, 1, true);
    for (int x = 0; x < HUD_W; x += 2) {
        hud_px(&h, x, 24, true);
    }

    // night sky
    hud_sprite(&h, moon, 9, 50, 32, 1, true, false);
    for (int i = 0; i < ARRAY_SIZE(stars); i++) {
        int x = stars[i][0], y = stars[i][1];
        hud_px(&h, x, y, true);
        if ((i + frame) % 5 == 0) {
            hud_px(&h, x - 1, y, true);
            hud_px(&h, x + 1, y, true);
            hud_px(&h, x, y - 1, true);
            hud_px(&h, x, y + 1, true);
        }
    }

    // ground
    hud_rect(&h, 0, 136, HUD_W, 1, true);
    static const uint8_t tufts[] = {4, 26, 44, 60};
    for (int i = 0; i < ARRAY_SIZE(tufts); i++) {
        hud_px(&h, tufts[i], 135, true);
        hud_px(&h, tufts[i] + 1, 134, true);
        hud_px(&h, tufts[i] + 2, 135, true);
    }
    hud_dither(&h, 138);

    // cat
    if (s->connected) {
        const char *const *f = (frame % 4 == 1) ? cat_flick : (frame % 4 == 3) ? cat_blink : cat;
        hud_sprite(&h, f, 14, 18, 136 - 28, 2, true, false);
    } else {
        int z = frame % 3;
        hud_sprite(&h, cat_sleep, 9, 18, 136 - 18, 2, true, false);
        hud_text(&h, "Z", 44, 104 - z * 4, 1, true);
        if (z > 0) {
            hud_text(&h, "Z", 50, 96 - z * 4, 2, true);
        }
    }

    hud_end(&h);
}

static void redraw_all(void) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { draw(widget); }
}

// ponytail: animates forever like the old animimg did; pause on idle if battery life suffers
static void tick(lv_timer_t *timer) {
    frame++;
    redraw_all();
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        widget->state.charging = state.usb_present;
#endif
        widget->state.battery = state.level;
    }
    redraw_all();
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

struct peripheral_status_state {
    bool connected;
};

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.connected = state.connected;
    }
    redraw_all();
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    hud_create(widget, parent);
    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
    lv_timer_create(tick, CONFIG_CUSTOM_ANIMATION_SPEED / 4, NULL);
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
