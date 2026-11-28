/*
 * Per-key reactive LEDs for ZMK's RGB underglow (Aurora Lily58).
 *
 * This is a small LED strip device that sits between ZMK's underglow code and
 * the real ws2812 strip. ZMK draws its normal colour or effect and sends the
 * frame here every 50 ms. When a key on this half is pressed, the LED under it
 * is drawn at up to twice its normal brightness, fading back to normal over
 * fade-ms. The frame then goes to the real strip unchanged otherwise.
 *
 * Because the boost multiplies the underglow colour, it follows the colour and
 * brightness keys, and does nothing while the LEDs are dark or off.
 *
 * Devicetree: a "zmk,reactive-led-strip" node chosen as zmk,underglow (see the
 * end of config/splitkb_aurora_lily58.keymap).
 */

#define DT_DRV_COMPAT zmk_reactive_led_strip

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define NUM_LEDS DT_INST_PROP(0, chain_length)
#define FADE_MS DT_INST_PROP(0, fade_ms)

static const struct device *const real_strip = DEVICE_DT_GET(DT_INST_PHANDLE(0, led_strip));

/* LED index for each key position (255 = none). */
static const uint8_t key_leds[] = DT_INST_PROP(0, key_leds);

/*
 * Time of the last press for each LED, in ms since boot, with bit 0 forced on
 * so that 0 can mean "not pressed". Written by the key event listener, read by
 * the frame update; a 32-bit store is atomic on the Cortex-M4.
 */
static volatile uint32_t pressed_at[NUM_LEDS];

static uint8_t boost_channel(uint8_t value, uint32_t scale) {
    /* scale is 256 for normal brightness, up to 512 for double. */
    uint32_t boosted = ((uint32_t)value * scale) >> 8;
    return boosted > 255 ? 255 : (uint8_t)boosted;
}

static int reactive_update_rgb(const struct device *dev, struct led_rgb *pixels,
                               size_t num_pixels) {
    uint32_t now = k_uptime_get_32();
    size_t count = MIN(num_pixels, (size_t)NUM_LEDS);

    for (size_t i = 0; i < count; i++) {
        uint32_t t = pressed_at[i];
        if (t == 0) {
            continue;
        }

        uint32_t age = now - t;
        if (age >= FADE_MS) {
            pressed_at[i] = 0;
            continue;
        }

        /* 512 right after the press (2x), falling linearly to 256 (1x). */
        uint32_t scale = 256 + (256 * (FADE_MS - age)) / FADE_MS;
        pixels[i].r = boost_channel(pixels[i].r, scale);
        pixels[i].g = boost_channel(pixels[i].g, scale);
        pixels[i].b = boost_channel(pixels[i].b, scale);
    }

    return led_strip_update_rgb(real_strip, pixels, num_pixels);
}

static int reactive_update_channels(const struct device *dev, uint8_t *channels,
                                    size_t num_channels) {
    return led_strip_update_channels(real_strip, channels, num_channels);
}

static const struct led_strip_driver_api reactive_api = {
    .update_rgb = reactive_update_rgb,
    .update_channels = reactive_update_channels,
};

static int reactive_init(const struct device *dev) {
    if (!device_is_ready(real_strip)) {
        LOG_ERR("Reactive LEDs: real LED strip is not ready");
        return -ENODEV;
    }
    return 0;
}

/* After the ws2812 driver (LED_STRIP_INIT_PRIORITY, 90 by default). */
DEVICE_DT_INST_DEFINE(0, reactive_init, NULL, NULL, NULL, POST_KERNEL, 99, &reactive_api);

static int reactive_key_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    /* Only presses of keys on this half; the other half lights its own LEDs. */
    if (ev == NULL || !ev->state || ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (ev->position >= ARRAY_SIZE(key_leds)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint8_t led = key_leds[ev->position];
    if (led < NUM_LEDS) {
        pressed_at[led] = k_uptime_get_32() | 1;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(reactive_led, reactive_key_listener);
ZMK_SUBSCRIPTION(reactive_led, zmk_position_state_changed);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
