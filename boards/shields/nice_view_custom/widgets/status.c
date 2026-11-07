/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Left (central) HUD: bond state, rune-stone profiles, the layer in a gothic arch,
 * and a flask gauge for each half.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static const char *const house[16] = {
    ".......##.......", "......####......", ".....######.....", "....###..###....",
    "...###....###...", "..###......###..", ".###........###.", "####........####",
    "..#..........#..", "..#..##..##..#..", "..#..........#..", "..#...####...#..",
    "..#...#..#...#..", "..#...#..#...#..", "..#...#..#...#..", "..############..",
};
static const char *const arrow[16] = {
    "................", ".......##.......", "......####......", ".....######.....",
    "....###..###....", "...###....###...", "..###......###..", ".###........###.",
    ".......##.......", "......####......", ".....######.....", "....###..###....",
    "...###....###...", "..###......###..", ".###........###.", "................",
};
static const char *const gear[16] = {
    "......####......", "..#...####...#..", ".###.######.###.", "..############..",
    "...###....###...", "..###......###..", "####...##...####", "####..####..####",
    "####..####..####", "####...##...####", "..###......###..", "...###....###...",
    "..############..", ".###.######.###.", "..#...####...#..", "......####......",
};
static const char *const rune[9] = {"....#....", "...###...", "..#####..", ".#######.", "#########",
                                    ".#######.", "..#####..", "...###...", "....#...."};
static const char *const rune_empty[9] = {"....#....", "...#.#...", "..#...#..", ".#.....#.", "#.......#",
                                          ".#.....#.", "..#...#..", "...#.#...", "....#...."};
static const char *const roman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII"};

/* Gothic arch curves: y for x = 2..33 (outer, r=38) and x = 4..33 (inner, r=36). Mirrored. */
static const uint8_t arch_outer[] = {66, 57, 53, 51, 49, 47, 45, 44, 42, 41, 40, 39, 38, 37, 36, 35,
                                     35, 34, 33, 33, 32, 32, 31, 31, 30, 30, 29, 29, 29, 29, 28, 28};
static const uint8_t arch_inner[] = {66, 57, 54, 51, 49, 47, 46, 44, 43, 42, 41, 40, 39, 38, 37,
                                     36, 36, 35, 34, 34, 33, 33, 32, 32, 32, 31, 31, 31, 30, 30};

static void arch(struct hud *h, const uint8_t *ys, int n, int xs, int y1) {
    for (int i = 0; i < n; i++) {
        int x = xs + i, y = ys[i], prev = i ? ys[i - 1] : y;
        for (int yy = MIN(y, prev); yy <= MAX(y, prev); yy++) {
            hud_px(h, x, yy, true);
            hud_px(h, 67 - x, yy, true);
        }
    }
    hud_rect(h, xs, 66, 1, y1 - 66, true);
    hud_rect(h, 67 - xs, 66, 1, y1 - 66, true);
    hud_rect(h, xs, y1, HUD_W - 2 * xs, 1, true);
}

static void draw(struct zmk_widget_status *widget) {
    const struct status_state *s = &widget->state;
    struct hud h;
    char buf[12];
    hud_begin(&h, widget);

    // header: transport + bond state over an ornamental rule
    bool usb = s->selected_endpoint.transport == ZMK_TRANSPORT_USB;
    hud_text(&h, usb ? "USB" : "BLE", 2, 2, 1, true);
    const char *st = usb                           ? "WIRED"
                     : !s->active_profile_bonded   ? "OPEN"
                     : s->active_profile_connected ? "BOUND"
                                                   : "SEEK";
    hud_text(&h, st, 66 - hud_text_w(st, 1), 2, 1, true);
    hud_orn(&h, 9, 2, 65);

    // profiles as rune stones
    for (int k = 0; k < NICEVIEW_PROFILE_COUNT; k++) {
        int x = 3 + k * 13, y = 13;
        snprintf(buf, sizeof(buf), "%d", k + 1);
        if (k == s->active_profile_index && !usb) {
            hud_sprite(&h, rune, 9, x, y, 1, true, false);
            hud_text(&h, buf, x + 3, y + 2, 1, false);
        } else if (s->profiles_bonded[k]) {
            hud_sprite(&h, rune_empty, 9, x, y, 1, true, false);
            hud_text(&h, buf, x + 3, y + 2, 1, true);
        } else {
            hud_px(&h, x + 4, y + 4, true);
        }
    }

    // hero: gothic arch with layer icon, name and numeral
    arch(&h, arch_outer, ARRAY_SIZE(arch_outer), 2, 117);
    arch(&h, arch_inner, ARRAY_SIZE(arch_inner), 4, 115);
    hud_rect(&h, 0, 64, 5, 2, true);
    hud_rect(&h, 63, 64, 5, 2, true);

    // ponytail: icon picked by layer index, matches this keymap's default/lower/raise/adjust
    int idx = s->layer_index;
    switch (idx) {
    case 0:
        hud_sprite(&h, house, 16, 18, 40, 2, true, false);
        break;
    case 1:
        hud_sprite(&h, arrow, 16, 18, 40, 2, true, true);
        break;
    case 2:
        hud_sprite(&h, arrow, 16, 18, 40, 2, true, false);
        break;
    case 3:
        hud_sprite(&h, gear, 16, 18, 40, 2, true, false);
        break;
    default:
        hud_text(&h, "?", 28, 40, 6, true);
    }

    if (s->layer_label && s->layer_label[0]) {
        snprintf(buf, 8, "%s", s->layer_label);
    } else {
        snprintf(buf, 8, "L%d", idx);
    }
    hud_ctext(&h, buf, 78, 2);
    hud_orn(&h, 93, 12, 55);
    hud_ctext(&h, idx < ARRAY_SIZE(roman) ? roman[idx] : "?", 98, 1);
    hud_orn(&h, 108, 12, 55);

    // a flask per half
    hud_gauge(&h, "L", 122, s->battery, s->charging);
    hud_gauge(&h, "R", 135, s->peripheral_battery, false);

    // footer ornament + embers
    hud_orn(&h, 151, 6, 61);
    hud_px(&h, 10, 156, true);
    hud_px(&h, 22, 158, true);
    hud_px(&h, 45, 157, true);
    hud_px(&h, 57, 155, true);

    hud_end(&h);
}

static void redraw_all(void) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { draw(widget); }
}

/* battery (this half) */

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
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
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

/* battery (right half, fetched over split BLE) */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
struct peripheral_battery_state {
    uint8_t level;
};

static void peripheral_battery_update_cb(struct peripheral_battery_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.peripheral_battery = state.level;
    }
    redraw_all();
}

static struct peripheral_battery_state peripheral_battery_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    uint8_t level = 0;
    if (ev != NULL) {
        level = ev->state_of_charge;
    } else {
        zmk_split_central_get_peripheral_battery_level(0, &level);
    }
    return (struct peripheral_battery_state){.level = level};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_battery, struct peripheral_battery_state,
                            peripheral_battery_update_cb, peripheral_battery_get_state)
ZMK_SUBSCRIPTION(widget_peripheral_battery, zmk_peripheral_battery_state_changed);
#endif

/* output: endpoint + BLE profiles */

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_bonded[NICEVIEW_PROFILE_COUNT];
};

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.selected_endpoint = state.selected_endpoint;
        widget->state.active_profile_index = state.active_profile_index;
        widget->state.active_profile_connected = state.active_profile_connected;
        widget->state.active_profile_bonded = state.active_profile_bonded;
        memcpy(widget->state.profiles_bonded, state.profiles_bonded,
               sizeof(state.profiles_bonded));
    }
    redraw_all();
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    struct output_status_state state = {
        .selected_endpoint = zmk_endpoint_get_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
    for (int i = 0; i < MIN(NICEVIEW_PROFILE_COUNT, ZMK_BLE_PROFILE_COUNT); ++i) {
        state.profiles_bonded[i] = !zmk_ble_profile_is_open(i);
    }
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

/* layer */

struct layer_status_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.layer_index = state.index;
        widget->state.layer_label = state.label;
    }
    redraw_all();
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)
ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    hud_create(widget, parent);
    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    widget_peripheral_battery_init();
#endif
    widget_output_status_init();
    widget_layer_status_init();
    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }
