/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>
#include "util.h"

LV_IMG_DECLARE(bolt);

void rotate_canvas(lv_obj_t *canvas, lv_color_t cbuf[]) {
    static lv_color_t cbuf_tmp[CANVAS_SIZE * CANVAS_SIZE];
    memcpy(cbuf_tmp, cbuf, sizeof(cbuf_tmp));
    lv_img_dsc_t img;
    img.data = (void *)cbuf_tmp;
    img.header.cf = LV_IMG_CF_TRUE_COLOR;
    img.header.w = CANVAS_SIZE;
    img.header.h = CANVAS_SIZE;

    lv_canvas_fill_bg(canvas, LVGL_BACKGROUND, LV_OPA_COVER);
    lv_canvas_transform(canvas, &img, 900, LV_IMG_ZOOM_NONE, -1, 0, CANVAS_SIZE / 2,
                        CANVAS_SIZE / 2, true);
}

void draw_battery(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, uint8_t level, bool charging) {
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);

    // Same shape as the stock nice!view battery, offset to (x, y).
    lv_canvas_draw_rect(canvas, x + 0, y + 2, 29, 12, &rect_white_dsc);
    lv_canvas_draw_rect(canvas, x + 1, y + 3, 27, 10, &rect_black_dsc);
    lv_canvas_draw_rect(canvas, x + 2, y + 4, (level + 2) / 4, 8, &rect_white_dsc);
    lv_canvas_draw_rect(canvas, x + 30, y + 5, 3, 6, &rect_white_dsc);
    lv_canvas_draw_rect(canvas, x + 31, y + 6, 1, 4, &rect_black_dsc);

    if (charging) {
        lv_draw_img_dsc_t img_dsc;
        lv_draw_img_dsc_init(&img_dsc);
        lv_canvas_draw_img(canvas, x + 9, y - 1, &bolt, &img_dsc);
    }
}

void draw_battery_disconnected(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y) {
    draw_battery(canvas, x, y, 0, false);

    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, 1);

    // X across the inside of the battery body.
    lv_point_t down[] = {{x + 2, y + 4}, {x + 26, y + 11}};
    lv_point_t up[] = {{x + 2, y + 11}, {x + 26, y + 4}};
    lv_canvas_draw_line(canvas, down, 2, &line_dsc);
    lv_canvas_draw_line(canvas, up, 2, &line_dsc);
}

void init_label_dsc(lv_draw_label_dsc_t *label_dsc, lv_color_t color, const lv_font_t *font,
                    lv_text_align_t align) {
    lv_draw_label_dsc_init(label_dsc);
    label_dsc->color = color;
    label_dsc->font = font;
    label_dsc->align = align;
}

void init_rect_dsc(lv_draw_rect_dsc_t *rect_dsc, lv_color_t bg_color) {
    lv_draw_rect_dsc_init(rect_dsc);
    rect_dsc->bg_color = bg_color;
}

void init_line_dsc(lv_draw_line_dsc_t *line_dsc, lv_color_t color, uint8_t width) {
    lv_draw_line_dsc_init(line_dsc);
    line_dsc->color = color;
    line_dsc->width = width;
}

void init_arc_dsc(lv_draw_arc_dsc_t *arc_dsc, lv_color_t color, uint8_t width) {
    lv_draw_arc_dsc_init(arc_dsc);
    arc_dsc->color = color;
    arc_dsc->width = width;
}
