/*
 * &disp_anim: turn the nice!view animation mode on or off.
 *
 * On by default. Off shows the normal battery / connection / profile panels all
 * the time. The choice is saved to flash (key "disp_anim/on") so it survives a
 * restart. The animation itself is drawn by nice_view/widgets/anim.c, which
 * reads display_anim_enabled() a few times a second.
 */

#define DT_DRV_COMPAT zmk_behavior_display_anim

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>

#include <display_anim.h>

static atomic_t enabled = ATOMIC_INIT(1);

bool display_anim_enabled(void) { return atomic_get(&enabled) != 0; }

#if IS_ENABLED(CONFIG_SETTINGS)
static int disp_anim_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                  void *cb_arg) {
    uint8_t v;
    if (!settings_name_steq(name, "on", NULL) || len != sizeof(v)) {
        return -ENOENT;
    }
    int rc = read_cb(cb_arg, &v, sizeof(v));
    if (rc < 0) {
        return rc;
    }
    atomic_set(&enabled, v ? 1 : 0);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(disp_anim, "disp_anim", NULL, disp_anim_settings_set, NULL, NULL);
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_disp_anim_pressed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event) {
    uint8_t v = atomic_get(&enabled) ? 0 : 1;
    atomic_set(&enabled, v);
#if IS_ENABLED(CONFIG_SETTINGS)
    settings_save_one("disp_anim/on", &v, sizeof(v));
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_disp_anim_released(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api disp_anim_driver_api = {
    .binding_pressed = on_disp_anim_pressed,
    .binding_released = on_disp_anim_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &disp_anim_driver_api);

#endif
