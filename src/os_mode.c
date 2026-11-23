/*
 * Per-connection OS mode for ZMK.
 *
 * Each connection (every BLE profile, plus USB) remembers whether it talks to a
 * Mac or a Windows/Linux machine. The Mac layer is turned on or off to match
 * whenever the connection changes and once at boot, after saved settings load.
 *
 *   &os_mode OS_TOG   flip the current connection between Mac and Win
 *   &os_mode OS_MAC   set the current connection to Mac
 *   &os_mode OS_WIN   set the current connection to Win
 *
 * The choice is saved to flash (key "os_mode/modes") only when one of these keys
 * is pressed, so it survives reboots. Flashing settings_reset clears it.
 */

#define DT_DRV_COMPAT zmk_behavior_os_mode

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/keymap.h>

#include <dt-bindings/os_mode.h>
#include <os_mode.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define MAC_LAYER DT_INST_PROP(0, mac_layer)

/* One byte per connection: 0 = Win, 1 = Mac. Indexed like zmk_endpoint_instance_to_index(). */
static uint8_t modes[ZMK_ENDPOINT_COUNT];

/* Index of the connection that is in use right now. */
static int current_slot(void) {
    struct zmk_endpoint_instance ep = zmk_endpoints_selected();
#if IS_ENABLED(CONFIG_ZMK_BLE)
    /* Read the profile straight from the BLE code: at boot the endpoint code can
     * still hold the profile it saw before saved settings were loaded. */
    if (ep.transport == ZMK_TRANSPORT_BLE) {
        ep.ble.profile_index = zmk_ble_active_profile_index();
    }
#endif
    int slot = zmk_endpoint_instance_to_index(ep);
    return (slot >= 0 && slot < ZMK_ENDPOINT_COUNT) ? slot : 0;
}

bool os_mode_is_mac(void) { return modes[current_slot()] != 0; }

/* Turn the Mac layer on or off to match the current connection's mode. */
static void apply_mode(void) {
    zmk_keymap_layer_id_t mac = zmk_keymap_layer_index_to_id(MAC_LAYER);
    if (mac == ZMK_KEYMAP_LAYER_ID_INVAL) {
        LOG_ERR("os_mode: mac-layer %d does not exist", MAC_LAYER);
        return;
    }

    bool want_mac = os_mode_is_mac();
    if (want_mac == zmk_keymap_layer_active(mac)) {
        return;
    }

    LOG_INF("os_mode: connection %d uses %s mods", current_slot(), want_mac ? "Mac" : "Win");
    if (want_mac) {
        zmk_keymap_layer_activate(mac);
    } else {
        zmk_keymap_layer_deactivate(mac);
    }
}

/* ── Saving to flash ───────────────────────────────────────────────────────── */

#if IS_ENABLED(CONFIG_SETTINGS)
static void save_work_cb(struct k_work *work) {
    int err = settings_save_one("os_mode/modes", modes, sizeof(modes));
    if (err < 0) {
        LOG_ERR("os_mode: failed to save modes (%d)", err);
    }
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_work_cb);

static int os_mode_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                void *cb_arg) {
    if (!settings_name_steq(name, "modes", NULL)) {
        return -ENOENT;
    }

    /* Read what fits, so a build with a different profile count still loads. */
    uint8_t buf[ZMK_ENDPOINT_COUNT + 8] = {0};
    int n = read_cb(cb_arg, buf, MIN(len, sizeof(buf)));
    if (n < 0) {
        LOG_ERR("os_mode: failed to read saved modes (%d)", n);
        return n;
    }
    memcpy(modes, buf, MIN((size_t)n, sizeof(modes)));
    return 0;
}

/* Runs once all saved settings (including the active BLE profile) are loaded. */
static int os_mode_settings_commit(void) {
    apply_mode();
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(os_mode, "os_mode", NULL, os_mode_settings_set,
                               os_mode_settings_commit, NULL);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

static void save_modes(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#endif
}

/* ── Follow connection changes ─────────────────────────────────────────────── */

static int os_mode_listener(const zmk_event_t *eh) {
    apply_mode();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(os_mode, os_mode_listener);
ZMK_SUBSCRIPTION(os_mode, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(os_mode, zmk_ble_active_profile_changed);
#endif

/* ── The &os_mode key ──────────────────────────────────────────────────────── */

static int on_os_mode_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    int slot = current_slot();

    switch (binding->param1) {
    case OS_WIN:
        modes[slot] = 0;
        break;
    case OS_MAC:
        modes[slot] = 1;
        break;
    case OS_TOG:
        modes[slot] = !modes[slot];
        break;
    default:
        LOG_ERR("os_mode: unknown parameter %d", binding->param1);
        return -ENOTSUP;
    }

    save_modes();
    apply_mode();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_os_mode_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata param_values[] = {
    {.value = OS_TOG, .display_name = "Toggle Mac/Win", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.value = OS_MAC, .display_name = "Mac", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
    {.value = OS_WIN, .display_name = "Windows", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE},
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

static const struct behavior_driver_api os_mode_driver_api = {
    .binding_pressed = on_os_mode_pressed,
    .binding_released = on_os_mode_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &os_mode_driver_api);

#else /* no &os_mode node in the keymap */

bool os_mode_is_mac(void) { return false; }

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
