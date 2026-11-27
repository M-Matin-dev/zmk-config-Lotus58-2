/*
 * Animation over the top of the nice!view status screen (left half).
 *
 * Covers the battery / connection and profile panels (top 136 px as you look
 * at the keyboard). The bottom 24 px, with the layer name, always stays visible.
 *
 * Shown when all of these hold:
 *   - animation mode is on (Adjust + D or K toggles it, saved in flash;
 *     see src/display_anim.c)
 *   - the keyboard has been on for CONFIG_LOTUS58_DISPLAY_ANIM_BOOT_DELAY_SEC
 *   - Adjust is off, and has been off for CONFIG_LOTUS58_DISPLAY_ANIM_ADJUST_DELAY_SEC
 *     (so the Bluetooth profiles are readable while picking one)
 * While the keyboard is idle the animation holds its current frame, so the
 * display is not redrawn when nobody is typing.
 *
 * Everything here runs from LVGL timers, so on the display thread.
 */

#include <string.h>

#include <lvgl.h>
#include <zephyr/kernel.h>

#include <zmk/activity.h>
#include <zmk/keymap.h>

#include <display_anim.h>
#include "anim.h"
#include "anim_frames.h"

#define CHECK_MS 200
#define BOOT_DELAY_MS (CONFIG_LOTUS58_DISPLAY_ANIM_BOOT_DELAY_SEC * 1000)
#define ADJUST_DELAY_MS (CONFIG_LOTUS58_DISPLAY_ANIM_ADJUST_DELAY_SEC * 1000)

/* 2-colour palette (index 0 = black, 1 = white, as in the source frames),
 * then the bitmap. */
static uint8_t img_buf[8 + ANIM_FRAME_BYTES] __aligned(4) = {
    0x00, 0x00, 0x00, 0xff, /* index 0: black */
    0xff, 0xff, 0xff, 0xff, /* index 1: white */
};
static uint8_t *const pixels = img_buf + 8;

static lv_img_dsc_t img_dsc = {
    .header.cf = LV_IMG_CF_INDEXED_1BIT,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = ANIM_W,
    .header.h = ANIM_H,
    .data_size = sizeof(img_buf),
    .data = img_buf,
};

static lv_obj_t *img;
static lv_timer_t *frame_timer;
static lv_timer_t *check_timer;
static uint16_t frame;
static bool shown;
static bool adjust_was_on;
static int64_t adjust_off_at = INT64_MIN / 2;

static void apply_delta(uint16_t k) {
    const uint8_t *p = anim_deltas + anim_delta_offsets[k];
    const uint8_t *end = anim_deltas + anim_delta_offsets[k + 1];
    size_t i = 0;

    while (p < end) {
        uint8_t skip = *p++;
        uint8_t count = *p++;
        i += skip;
        for (uint8_t j = 0; j < count && i < ANIM_FRAME_BYTES; j++, i++) {
            pixels[i] ^= *p++;
        }
    }
}

static void frame_cb(lv_timer_t *t) {
    frame = (frame + 1) % anim_frame_count;
    apply_delta(frame);
    lv_img_cache_invalidate_src(&img_dsc);
    lv_obj_invalidate(img);
    lv_timer_set_period(t, anim_frame_ms[frame]);
}

static bool want_shown(int64_t now) {
    if (!display_anim_enabled() || now < BOOT_DELAY_MS) {
        return false;
    }
    if (zmk_keymap_layer_active(CONFIG_LOTUS58_DISPLAY_ANIM_HIDE_LAYER)) {
        return false;
    }
    return now - adjust_off_at >= ADJUST_DELAY_MS;
}

void anim_check(void) {
    if (img == NULL) {
        return;
    }
    int64_t now = k_uptime_get();

    bool adjust_on = zmk_keymap_layer_active(CONFIG_LOTUS58_DISPLAY_ANIM_HIDE_LAYER);
    if (adjust_was_on && !adjust_on) {
        adjust_off_at = now;
    }
    adjust_was_on = adjust_on;

    bool want = want_shown(now);
    if (want != shown) {
        shown = want;
        if (shown) {
            lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Only advance frames while shown and someone is using the keyboard. */
    bool run = shown && zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE;
    if (run) {
        lv_timer_resume(frame_timer);
    } else {
        lv_timer_pause(frame_timer);
    }
}

static void check_cb(lv_timer_t *t) { anim_check(); }

void anim_init(lv_obj_t *parent) {
    memcpy(pixels, anim_first_frame, ANIM_FRAME_BYTES);
    frame = 0;

    img = lv_img_create(parent);
    lv_img_set_src(img, &img_dsc);
    lv_obj_align(img, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    shown = false;

    frame_timer = lv_timer_create(frame_cb, anim_frame_ms[0], NULL);
    lv_timer_pause(frame_timer);
    check_timer = lv_timer_create(check_cb, CHECK_MS, NULL);
}
