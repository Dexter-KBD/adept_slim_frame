#include QMK_KEYBOARD_H
#include <math.h>
#include "wait.h"

#define PLOOPY_DRAGSCROLL_DIVISOR_H 3.0
#define PLOOPY_DRAGSCROLL_DIVISOR_V 3.0
#define VOLUME_DIVIDER 5
#define MAX_VOLUME_DELTA 40
#define MAX_SCROLL_EVENTS_PER_CYCLE 1
#define DRAG_SCROLL_MODE 2
#define FAST_H_THRESHOLD_LAYER0 30
#define FAST_H_DISTANCE_TRIGGER 1000
#define FAST_H_TIME_WINDOW 400
#define FAST_H_COOLDOWN 400
#define LAYER1_EXIT_COOLDOWN 1000
#define DPI_LAYER_1 100
#define DPI_LAYER_2 100

static inline int clamp(int value, int min, int max) {
    return (value < min) ? min : (value > max) ? max : value;
}

typedef struct {
    int volume_accumulator;
    float scroll_accumulated_h;
    float scroll_accumulated_v;
} trackball_state_t;

static trackball_state_t tb_state = {0};
static int fast_h_distance = 0;
static uint32_t fast_h_start_time = 0;
static uint32_t last_fast_h_scroll_time = 0;

static void reset_tb_state(void) {
    tb_state = (trackball_state_t){0};
    fast_h_distance = 0;
    fast_h_start_time = 0;
}

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(KC_BTN4, KC_BTN5, DRAG_SCROLL, KC_BTN2, KC_BTN1, KC_BTN3),
    [1] = LAYOUT(KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS),
    [4] = LAYOUT(KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS)
};

static uint32_t layer1_exit_time = 0;
static uint32_t layer4_exit_time = 0;
static bool wheel_click_active = false;
static uint32_t last_tab_time = 0;

layer_state_t layer_state_set_user(layer_state_t state) {
    static bool was_layer1 = false;
    static bool was_layer4 = false;

    if (was_layer1 && !layer_state_cmp(state, 1)) layer1_exit_time = timer_read();
    was_layer1 = layer_state_cmp(state, 1);

    if (was_layer4 && !layer_state_cmp(state, 4)) layer4_exit_time = timer_read();
    was_layer4 = layer_state_cmp(state, 4);

    switch (get_highest_layer(state)) {
        case 2: pointing_device_set_cpi(DPI_LAYER_2); break;
        case 1: pointing_device_set_cpi(DPI_LAYER_1); break;
        default: pointing_device_set_cpi(dpi_array[keyboard_config.dpi_config]); break;
    }

    if (layer_state_cmp(state, 4)) {
        wheel_click_active = true;
        register_code(KC_BTN3);
    } else if (wheel_click_active) {
        wheel_click_active = false;
        unregister_code(KC_BTN3);
    }

    return state;
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    uint32_t now = timer_read();
    uint8_t highest_layer = get_highest_layer(layer_state);

    switch (highest_layer) {
        case 0: {
            if ((now - layer1_exit_time < LAYER1_EXIT_COOLDOWN) ||
                (now - layer4_exit_time < LAYER1_EXIT_COOLDOWN)) break;

            if (abs(mouse_report.x) >= FAST_H_THRESHOLD_LAYER0) {
                if (fast_h_distance == 0) {
                    fast_h_start_time = now;
                }
                fast_h_distance += mouse_report.x;
            }

            if ((now - fast_h_start_time <= FAST_H_TIME_WINDOW) &&
                (abs(fast_h_distance) >= FAST_H_DISTANCE_TRIGGER) &&
                (now - last_fast_h_scroll_time > FAST_H_COOLDOWN)) {

                if (fast_h_distance > 0) tap_code16(LGUI(LCTL(KC_RGHT)));
                else tap_code16(LGUI(LCTL(KC_LEFT)));

                fast_h_distance = 0;
                fast_h_start_time = now;
                last_fast_h_scroll_time = now;
            }

            if (now - fast_h_start_time > FAST_H_TIME_WINDOW) {
                fast_h_distance = 0;
            }

            break;
        }

        case 1: {
            tb_state.scroll_accumulated_h += (float)mouse_report.x / PLOOPY_DRAGSCROLL_DIVISOR_H;
            tb_state.scroll_accumulated_v += (float)mouse_report.y / PLOOPY_DRAGSCROLL_DIVISOR_V;

            int h_scrolls = (int)truncf(tb_state.scroll_accumulated_h);
            int v_scrolls = (int)truncf(tb_state.scroll_accumulated_v);

            int h_count = 0;
            while (h_scrolls != 0 && h_count < MAX_SCROLL_EVENTS_PER_CYCLE) {
                report_mouse_t scroll_report = {0};
                scroll_report.h = (h_scrolls > 0) ? 1 : -1;
                host_mouse_send(&scroll_report);
                tb_state.scroll_accumulated_h -= (h_scrolls > 0) ? 1.0f : -1.0f;
                h_scrolls += (h_scrolls > 0) ? -1 : 1;
                h_count++;
            }

            int v_count = 0;
            while (v_scrolls != 0 && v_count < MAX_SCROLL_EVENTS_PER_CYCLE) {
                report_mouse_t scroll_report = {0};
                scroll_report.v = (v_scrolls > 0) ? -1 : 1;
                host_mouse_send(&scroll_report);
                tb_state.scroll_accumulated_v -= (v_scrolls > 0) ? 1.0f : -1.0f;
                v_scrolls += (v_scrolls > 0) ? -1 : 1;
                v_count++;
            }

            mouse_report.x = 0;
            mouse_report.y = 0;
            break;
        }

        case 2:
            tb_state.volume_accumulator = clamp(tb_state.volume_accumulator - mouse_report.y, -MAX_VOLUME_DELTA, MAX_VOLUME_DELTA);
            while (tb_state.volume_accumulator >= VOLUME_DIVIDER) { tap_code(KC_AUDIO_VOL_UP); tb_state.volume_accumulator -= VOLUME_DIVIDER; }
            while (tb_state.volume_accumulator <= -VOLUME_DIVIDER) { tap_code(KC_AUDIO_VOL_DOWN); tb_state.volume_accumulator += VOLUME_DIVIDER; }
            mouse_report.x = 0;
            mouse_report.y = 0;
            break;

        case 4:
            if (now - last_tab_time > 200) {
                if (mouse_report.y >= 2) { tap_code16(LCTL(KC_PGDN)); reset_tb_state(); last_tab_time = now; mouse_report.y = 0; }
                else if (mouse_report.y <= -2) { tap_code16(LCTL(KC_PGUP)); reset_tb_state(); last_tab_time = now; mouse_report.y = 0; }
            }
            break;
    }

    return mouse_report;
}
