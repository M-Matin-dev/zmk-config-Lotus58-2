/*
 * Re-sync the right half's LEDs when the split link comes back (Aurora Lily58).
 *
 * The left (central) half forwards each LED change to the right half as a
 * behavior command. A command missed while the link is down is lost, and the
 * right half restores its own saved effect after a power cycle, so the two
 * halves can drift apart. When the right half reconnects, this sends it the
 * current on/off state, colour and effect as absolute commands, so both halves
 * match again.
 *
 * Central only: the right half simply applies what it receives.
 */

#include <zephyr/kernel.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/rgb_underglow.h>
#include <zmk/split/central.h>

#include <dt-bindings/zmk/rgb.h>

/* Wait for the right half's behaviors to be ready after the link comes up. */
#define RESYNC_DELAY_MS 1500

static void send_to_peripheral(uint32_t param1, uint32_t param2) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = "rgb_ug", /* device name of &rgb_ug in dts/behaviors/rgb_underglow.dtsi */
        .param1 = param1,
        .param2 = param2,
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };

    /* An error here means the link is not up; the next reconnect tries again. */
    (void)zmk_split_central_invoke_behavior(event.source, &binding, event, true);
}

static void resync_work_handler(struct k_work *work) {
    bool on = false;
    zmk_rgb_underglow_get_state(&on);

    struct zmk_led_hsb color = zmk_rgb_underglow_calc_brt(0); /* current colour, unchanged */
    uint8_t effect = (uint8_t)zmk_rgb_underglow_calc_effect(0);

    send_to_peripheral(RGB_COLOR_HSB_CMD, RGB_COLOR_HSB_VAL(color.h, color.s, color.b));
    send_to_peripheral(RGB_EFS_CMD, effect);
    send_to_peripheral(on ? RGB_ON_CMD : RGB_OFF_CMD, 0);
}

static K_WORK_DELAYABLE_DEFINE(resync_work, resync_work_handler);

static int split_led_sync_listener(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *ev =
        as_zmk_split_peripheral_status_changed(eh);
    if (ev && ev->connected) {
        k_work_reschedule(&resync_work, K_MSEC(RESYNC_DELAY_MS));
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(split_led_sync, split_led_sync_listener);
ZMK_SUBSCRIPTION(split_led_sync, zmk_split_peripheral_status_changed);
