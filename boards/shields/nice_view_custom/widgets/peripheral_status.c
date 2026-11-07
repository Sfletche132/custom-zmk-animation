/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Right (peripheral) HUD: bond state, own flask gauge, and a sword in a campfire
 * that burns while linked and dies to smoke when the link drops.
 */

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
static const char *const fire[2][13] = {
    {".....#......", ".....##.....", "....###.....", "....###..#..", "...####..#..",
     "...#####.##.", "..###.#####.", "..##..#####.", ".###...####.", ".##.....###.",
     ".##..#..###.", "..#.###.##..", "...#####...."},
    {"......#.....", ".....##.....", ".....###....", "..#..###....", "..#..####...",
     ".##.#####...", ".#####.###..", ".#####..##..", ".####...###.", ".###.....##.",
     ".###..#..##.", "..##.###.#..", "....#####..."},
};
static const char *const logs[4] = {"##..####..##", ".####..####.", "..##....##..", "###......###"};
static const char *const sword[] = {
    "...###...", "..#####..", "..##.##..", "..#####..", "...###...", "....#....", "...###...",
    "....#....", "...###...", "....#....", "...###...", "#########", ".#######.", "...#.#...",
    "...#.#...", "...#.#...", "...#.#...", "...#.#...", "...#.#...", "...#.#...", "...#.#...",
    "...#.#...", "...#.#...", "...#.#...", "...#.#...", "...#.#...", "...#.#...", "...#.#...",
    "...#.#...", "...###...", "...###...", "....#....",
};
static const uint8_t stars[][2] = {{34, 30}, {44, 38}, {8, 44},  {24, 52}, {58, 48}, {12, 66},
                                   {48, 62}, {62, 78}, {18, 84}, {6, 96},  {56, 92}};

/* Sword with a 1px dark halo so it reads against the flames. */
static void draw_sword(struct hud *h, int x, int y) {
    for (int j = 0; j < ARRAY_SIZE(sword); j++) {
        for (int i = 0; sword[j][i]; i++) {
            if (sword[j][i] == '#') {
                hud_rect(h, x + i - 1, y + j - 1, 3, 3, false);
            }
        }
    }
    hud_sprite(h, sword, ARRAY_SIZE(sword), x, y, 1, true, false);
}

static void draw(struct zmk_widget_status *widget) {
    const struct status_state *s = &widget->state;
    struct hud h;
    hud_begin(&h, widget);

    // header: bond state, blinking when severed
    if (s->connected || frame % 2) {
        hud_ctext(&h, s->connected ? "BOUND" : "SEVERED", 2, 1);
    }
    hud_orn(&h, 9, 2, 65);

    // own flask gauge, moon, stars
    hud_gauge(&h, "R", 13, s->battery, s->charging);
    hud_sprite(&h, moon, 9, 52, 28, 1, true, false);
    for (int i = 0; i < ARRAY_SIZE(stars); i++) {
        int x = stars[i][0], y = stars[i][1];
        hud_px(&h, x, y, true);
        if ((i + frame) % 6 == 0) {
            hud_px(&h, x - 1, y, true);
            hud_px(&h, x + 1, y, true);
            hud_px(&h, x, y - 1, true);
            hud_px(&h, x, y + 1, true);
        }
    }

    // ground
    hud_rect(&h, 0, 136, HUD_W, 1, true);
    hud_dither(&h, 138);

    // campfire with a sword driven into it, or cold logs and smoke
    hud_sprite(&h, logs, 4, 22, 128, 2, true, false);
    if (s->connected) {
        hud_sprite(&h, fire[frame % 2], 13, 22, 102, 2, true, false);
        draw_sword(&h, 30, 94);
        for (int i = 0; i < 6; i++) {
            hud_px(&h, 34 + (int)((i * 7 + frame * 3) % 15) - 7,
                   98 - (int)((frame * 4 + i * 9) % 44), true);
        }
        for (int x = 10; x < 58; x += 3) {
            if ((x + frame) % 2) {
                hud_px(&h, x, 137, true);
            }
        }
    } else {
        draw_sword(&h, 30, 94);
        for (int i = 0; i < 7; i++) {
            if ((i + frame) % 2 == 0) {
                hud_px(&h, 40 + (int)((i + frame) % 3) - 1 + (i % 2),
                       120 - i * 5 - (int)(frame % 3) * 2, true);
            }
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
