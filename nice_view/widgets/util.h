/*
 *
 * Copyright (c) 2025 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Modified: removed wpm[10] from status_state, added right_battery.
 */

#include <lvgl.h>
#include <zmk/endpoints.h>

#define NICEVIEW_PROFILE_COUNT 5

#define CANVAS_SIZE 68

#define LVGL_BACKGROUND                                                                            \
    IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_black() : lv_color_white()
#define LVGL_FOREGROUND                                                                            \
    IS_ENABLED(CONFIG_NICE_VIEW_WIDGET_INVERTED) ? lv_color_white() : lv_color_black()

struct status_state {
    uint8_t battery;
    bool charging;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_connected[NICEVIEW_PROFILE_COUNT];
    bool profiles_bonded[NICEVIEW_PROFILE_COUNT];
    uint8_t layer_index;
    const char *layer_label;
    uint8_t right_battery;
    bool right_connected; /* false until the right half's first reading, and after it disconnects */
    bool led_known;       /* LED fields below have been read at least once */
    bool led_on;
    uint8_t led_effect; /* ZMK underglow effect index, 0-based */
    uint8_t led_brightness; /* underglow brightness, 0-100 percent */
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

void rotate_canvas(lv_obj_t *canvas, lv_color_t cbuf[]);
/* Draw a battery bar with its top-left corner at (x, y). level is 0-100. */
void draw_battery(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, uint8_t level, bool charging);
/* Draw an empty battery outline crossed out with an X: "not connected". */
void draw_battery_disconnected(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y);
void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align);
void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color);
void init_line_dsc(lv_draw_line_dsc_t *line_dsc, lv_color_t color, uint8_t width);
void init_arc_dsc(lv_draw_arc_dsc_t *arc_dsc, lv_color_t color, uint8_t width);
