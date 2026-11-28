/*
 * LED brightness keys with finer steps near the bottom (Aurora Lily58).
 *
 *   &led_brt LED_BRI   up:   +1% below 2%, otherwise +2%   (0, 1, 2, 4, 6, ...)
 *   &led_brt LED_BRD   down: -2% down to 2%, then -1%      (..., 6, 4, 2, 1, 0)
 *
 * ZMK's &rgb_ug brightness keys use one fixed step (CONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP).
 * This behavior works the same way they do: on the left (central) half the
 * relative step is turned into an absolute colour, and that absolute colour is
 * then applied on both halves (global locality), so the halves never drift.
 * The new state is saved to flash, so the brightness survives a restart.
 */

#define DT_DRV_COMPAT zmk_behavior_led_brightness

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/rgb_underglow.h>

#include <dt-bindings/led_brightness.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Defined in ZMK's rgb_underglow.c but not in its header. */
int zmk_rgb_underglow_save_state(void);

#define HSB_VAL(h, s, b) (((uint32_t)(h) << 16) | ((uint32_t)(s) << 8) | (uint32_t)(b))

static uint8_t next_brightness(uint8_t b, bool up) {
    if (up) {
        return b < 2 ? b + 1 : MIN(b + 2, CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX);
    }
    if (b > 2) {
        return MAX(b - 2, 2);
    }
    return b > 0 ? b - 1 : 0;
}

/* Central only: relative step -> absolute colour, before it goes to both halves. */
static int on_led_brt_convert(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    if (binding->param1 != LED_BRT_UP && binding->param1 != LED_BRT_DOWN) {
        return 0;
    }

    struct zmk_led_hsb color = zmk_rgb_underglow_calc_brt(0); /* current colour */
    color.b = next_brightness(color.b, binding->param1 == LED_BRT_UP);

    binding->param1 = LED_BRT_SET;
    binding->param2 = HSB_VAL(color.h, color.s, color.b);
    return 0;
}

static int on_led_brt_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    struct zmk_led_hsb color;

    if (binding->param1 == LED_BRT_SET) {
        color.h = (binding->param2 >> 16) & 0xFFFF;
        color.s = (binding->param2 >> 8) & 0xFF;
        color.b = binding->param2 & 0xFF;
    } else {
        /* Not converted (should not happen): step from this half's own colour. */
        color = zmk_rgb_underglow_calc_brt(0);
        color.b = next_brightness(color.b, binding->param1 == LED_BRT_UP);
    }

    int err = zmk_rgb_underglow_set_hsb(color);
    if (err) {
        LOG_ERR("LED brightness: set_hsb failed (%d)", err);
        return err;
    }
    zmk_rgb_underglow_save_state();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_led_brt_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata param_values[] = {
    {.value = LED_BRT_UP, .display_name = "Brightness up", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.value = LED_BRT_DOWN, .display_name = "Brightness down", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
};

static const struct behavior_parameter_metadata_set param_set = {
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = 1,
    .sets = &param_set,
};
#endif

static const struct behavior_driver_api led_brt_driver_api = {
    .binding_convert_central_state_dependent_params = on_led_brt_convert,
    .binding_pressed = on_led_brt_pressed,
    .binding_released = on_led_brt_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &led_brt_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
