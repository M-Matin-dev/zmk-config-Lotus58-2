/*
 * Battery test logger (temporary, battery-logger branch only).
 *
 * Records, on each half, into flash so the log survives the battery running flat:
 *   boot      every start-up (the boot number goes up by one each time)
 *   volt      battery voltage in mV every CONFIG_LOTUS58_BATTERY_LOG_INTERVAL_MIN
 *             minutes, and whenever USB power comes or goes. Each reading is
 *             the mean of SAMPLE_READS ADC reads; spread_mv is max - min.
 *   usb       USB power connected / disconnected
 *   link_up   the split link between the halves connected
 *   link_down the split link dropped (with the Bluetooth reason code)
 *   rbatt     left half only: a right-half battery level arrived (0 = ZMK's
 *             "disconnected" value), and which split slot ZMK says sent it
 *   host_up / host_down
 *             left half only: a computer's Bluetooth link connected / dropped
 *
 * Times are seconds since that boot. Records are kept in RAM and written to
 * flash (settings keys "blog/...") every sample, on USB changes, and when a
 * block fills, so at most one interval of link events is lost at power-off.
 *
 * Extra load (CONFIG_LOTUS58_BATTERY_LOG_LOAD): the blue LED stays on and a
 * lowest-priority thread keeps the CPU busy, so the chip never sleeps. This
 * gives a steady, higher draw that shortens the test while staying a small load
 * for the battery. The load can be toggled with the b key and is noted in
 * every volt record.
 *
 * Reading it: plug the half into USB and open its serial port (PuTTY, any
 * speed). The log prints as CSV when the port opens. Commands (type, then Enter):
 *   d  print the log again     s  take a voltage reading now
 *   b  turn the extra load off/on   clear  clear the log
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/events/battery_state_changed.h>
#endif

#define HALF_NAME (IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) ? "left" : "right")

/* The split link is the one connection where this half plays this role: the
 * left half is the Bluetooth central towards the right half; the right half is
 * a peripheral and only ever connects to the left half. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define SPLIT_LINK_ROLE BT_CONN_ROLE_CENTRAL
#else
#define SPLIT_LINK_ROLE BT_CONN_ROLE_PERIPHERAL
#endif

enum rec_type {
    REC_BOOT = 1,
    REC_VOLT,
    REC_USB,
    REC_LINK_UP,
    REC_LINK_DOWN,
    REC_RBATT,
    REC_HOST_UP,
    REC_HOST_DOWN,
};

struct rec {
    uint32_t t;     /* seconds since this boot */
    uint16_t boot;  /* boot number */
    uint8_t type;   /* enum rec_type */
    uint8_t pct;    /* volt: ZMK's percentage; rbatt: level received */
    uint16_t mv;    /* volt: millivolts */
    uint16_t info;  /* volt: VOLT_* flags; usb: zmk_usb_conn_state; link_down: reason */
    uint16_t spread; /* volt: max - min mV across the averaged reads */
} __packed;

#define VOLT_ON_USB BIT(0)
#define VOLT_LOAD_ON BIT(1)

/* ADC reads averaged into one volt record, and the gap between them. */
#define SAMPLE_READS 16
#define SAMPLE_GAP_MS 10

#define PER_BLOCK 20
#define MAX_BLOCKS 50
#define MAX_RECS (PER_BLOCK * MAX_BLOCKS)

struct meta {
    uint16_t boot;
    uint16_t dropped; /* records pushed out of the last block once the log was full */
};

static struct rec recs[MAX_RECS];
static size_t count;
static struct meta meta;
static int dirty_from = -1; /* first block not yet written to flash, -1 if none */
static bool loaded;

/* Records made before saved settings have loaded wait here. */
static struct rec pending[16];
static size_t pending_count;

static K_MUTEX_DEFINE(lock);

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zmk_battery_log_uart));
static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

static void flush_work_cb(struct k_work *work);
static K_WORK_DEFINE(flush_work, flush_work_cb);

/* ── Recording ─────────────────────────────────────────────────────────────── */

static void mark_dirty(size_t index) {
    int block = index / PER_BLOCK;
    if (dirty_from < 0 || block < dirty_from) {
        dirty_from = block;
    }
}

static void append_locked(struct rec r) {
    if (!loaded) {
        if (pending_count < ARRAY_SIZE(pending)) {
            pending[pending_count++] = r;
        }
        return;
    }
    if (count < MAX_RECS) {
        recs[count] = r;
        mark_dirty(count);
        count++;
        if (count % PER_BLOCK == 0) {
            k_work_submit(&flush_work);
        }
        return;
    }
    /* Full: keep the start of the test and slide the newest records through the
     * last block, so both the beginning and the end survive. */
    size_t first = MAX_RECS - PER_BLOCK;
    memmove(&recs[first], &recs[first + 1], (PER_BLOCK - 1) * sizeof(struct rec));
    recs[MAX_RECS - 1] = r;
    meta.dropped++;
    mark_dirty(first);
}

static void record_spread(uint8_t type, uint8_t pct, uint16_t mv, uint16_t info,
                          uint16_t spread) {
    struct rec r = {
        .t = (uint32_t)(k_uptime_get() / 1000),
        .type = type,
        .pct = pct,
        .mv = mv,
        .info = info,
        .spread = spread,
    };
    k_mutex_lock(&lock, K_FOREVER);
    r.boot = meta.boot;
    append_locked(r);
    k_mutex_unlock(&lock);
}

static void record(uint8_t type, uint8_t pct, uint16_t mv, uint16_t info) {
    record_spread(type, pct, mv, info, 0);
}

/* ── Flash ─────────────────────────────────────────────────────────────────── */

static void flush_work_cb(struct k_work *work) {
    char key[16];

    k_mutex_lock(&lock, K_FOREVER);
    if (!loaded) {
        k_mutex_unlock(&lock);
        return;
    }
    settings_save_one("blog/m", &meta, sizeof(meta));
    if (dirty_from >= 0) {
        int last = count == 0 ? -1 : (int)((count - 1) / PER_BLOCK);
        for (int b = dirty_from; b <= last; b++) {
            size_t start = b * PER_BLOCK;
            size_t n = MIN(count - start, PER_BLOCK);
            snprintf(key, sizeof(key), "blog/b%d", b);
            settings_save_one(key, &recs[start], n * sizeof(struct rec));
        }
        dirty_from = -1;
    }
    k_mutex_unlock(&lock);
}

static void clear_log(void) {
    char key[16];

    k_mutex_lock(&lock, K_FOREVER);
    int blocks = (count + PER_BLOCK - 1) / PER_BLOCK;
    for (int b = 0; b < blocks; b++) {
        snprintf(key, sizeof(key), "blog/b%d", b);
        settings_delete(key);
    }
    count = 0;
    dirty_from = -1;
    meta.dropped = 0;
    /* Keep the boot number so the cleared log does not reuse old numbers. */
    settings_save_one("blog/m", &meta, sizeof(meta));
    k_mutex_unlock(&lock);
}

static int blog_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "m", &next) && !next) {
        if (len != sizeof(meta)) {
            return -EINVAL;
        }
        return MIN(read_cb(cb_arg, &meta, sizeof(meta)), 0);
    }

    if (name[0] == 'b') {
        int b = atoi(name + 1);
        if (b < 0 || b >= MAX_BLOCKS || len % sizeof(struct rec) != 0 ||
            len > PER_BLOCK * sizeof(struct rec)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, &recs[b * PER_BLOCK], len);
        if (rc < 0) {
            return rc;
        }
        count = MAX(count, b * PER_BLOCK + len / sizeof(struct rec));
        return 0;
    }

    return -ENOENT;
}

static void sample_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_work_cb);

static int blog_commit(void) {
    k_mutex_lock(&lock, K_FOREVER);
    if (loaded) {
        k_mutex_unlock(&lock);
        return 0;
    }
    loaded = true;
    meta.boot++;
    uint32_t now = (uint32_t)(k_uptime_get() / 1000);
    append_locked((struct rec){.t = now, .boot = meta.boot, .type = REC_BOOT});
    for (size_t i = 0; i < pending_count; i++) {
        pending[i].boot = meta.boot;
        append_locked(pending[i]);
    }
    pending_count = 0;
    k_mutex_unlock(&lock);

    /* First reading shortly after boot, then every interval. */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &sample_work, K_SECONDS(5));
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(blog, "blog", NULL, blog_set, blog_commit, NULL);

/* ── Voltage samples ───────────────────────────────────────────────────────── */

static bool load_on;

/* Same formula as ZMK's lithium_ion_mv_to_pct() (not on our include path). */
static uint8_t zmk_linear_pct(uint16_t mv) {
    if (mv >= 4200) {
        return 100;
    }
    if (mv <= 3450) {
        return 0;
    }
    return mv * 2 / 15 - 459;
}

static void take_sample(void) {
    struct sensor_value v;
    uint32_t sum = 0;
    uint16_t lo = UINT16_MAX, hi = 0;
    int n = 0;

    if (!device_is_ready(battery)) {
        return;
    }
    for (int i = 0; i < SAMPLE_READS; i++) {
        if (i > 0) {
            k_msleep(SAMPLE_GAP_MS);
        }
        if (sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_VOLTAGE) != 0 ||
            sensor_channel_get(battery, SENSOR_CHAN_GAUGE_VOLTAGE, &v) != 0) {
            continue;
        }
        uint16_t mv = v.val1 * 1000 + v.val2 / 1000;
        sum += mv;
        lo = MIN(lo, mv);
        hi = MAX(hi, mv);
        n++;
    }
    if (n == 0) {
        return;
    }
    uint16_t mv = (sum + n / 2) / n;
    uint16_t info = (zmk_usb_is_powered() ? VOLT_ON_USB : 0) | (load_on ? VOLT_LOAD_ON : 0);
    /* pct is what stock ZMK would show for this voltage, for comparison. */
    record_spread(REC_VOLT, zmk_linear_pct(mv), mv, info, hi - lo);
    k_work_submit(&flush_work);
}

static void sample_work_cb(struct k_work *work) {
    take_sample();
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &sample_work,
                                K_MINUTES(CONFIG_LOTUS58_BATTERY_LOG_INTERVAL_MIN));
}

static void usb_sample_work_cb(struct k_work *work) { take_sample(); }
static K_WORK_DELAYABLE_DEFINE(usb_sample_work, usb_sample_work_cb);

/* ── Extra load ──────────────────────────────────────────────────────────── */

#if IS_ENABLED(CONFIG_LOTUS58_BATTERY_LOG_LOAD)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led), gpios);

static void set_load(bool on) {
    load_on = on;
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_set_dt(&led, on ? 1 : 0);
    }
}

/* Lowest priority, never sleeps: every other thread still runs first, but the
 * idle thread never does, so the CPU stays awake. */
static void load_thread(void) {
    if (gpio_is_ready_dt(&led)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }
    set_load(true);
    while (true) {
        if (load_on) {
            k_busy_wait(1000);
        } else {
            k_msleep(500);
        }
    }
}

K_THREAD_DEFINE(battery_log_load, 512, load_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 1000);
#else
static void set_load(bool on) { ARG_UNUSED(on); }
#endif

/* ── Events ────────────────────────────────────────────────────────────────── */

static int blog_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *usb = as_zmk_usb_conn_state_changed(eh);
    if (usb) {
        record(REC_USB, 0, 0, usb->conn_state);
        /* Read the voltage a couple of seconds after the charger changes. */
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &usb_sample_work,
                                    K_SECONDS(3));
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    const struct zmk_peripheral_battery_state_changed *rb =
        as_zmk_peripheral_battery_state_changed(eh);
    if (rb) {
        record(REC_RBATT, rb->state_of_charge, 0, rb->source);
    }
#endif
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(battery_log, blog_listener);
ZMK_SUBSCRIPTION(battery_log, zmk_usb_conn_state_changed);
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
ZMK_SUBSCRIPTION(battery_log, zmk_peripheral_battery_state_changed);
#endif

static bool is_split_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == SPLIT_LINK_ROLE;
}

static void blog_connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0) {
        return;
    }
    if (is_split_link(conn)) {
        record(REC_LINK_UP, 0, 0, 0);
    } else if (IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)) {
        record(REC_HOST_UP, 0, 0, 0);
    }
}

static void blog_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (is_split_link(conn)) {
        record(REC_LINK_DOWN, 0, 0, reason);
    } else if (IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)) {
        record(REC_HOST_DOWN, 0, 0, reason);
    }
}

BT_CONN_CB_DEFINE(battery_log_conn_cb) = {
    .connected = blog_connected,
    .disconnected = blog_disconnected,
};

/* ── Serial readout ────────────────────────────────────────────────────────── */

static void out(const char *s) {
    for (; *s; s++) {
        uart_poll_out(uart, *s);
    }
    /* The USB serial buffer drops the oldest bytes when it overflows, so give
     * the host time to collect each line. */
    k_msleep(8);
}

static const char *type_name(uint8_t t) {
    switch (t) {
    case REC_BOOT:
        return "boot";
    case REC_VOLT:
        return "volt";
    case REC_USB:
        return "usb";
    case REC_LINK_UP:
        return "link_up";
    case REC_LINK_DOWN:
        return "link_down";
    case REC_RBATT:
        return "rbatt";
    case REC_HOST_UP:
        return "host_up";
    case REC_HOST_DOWN:
        return "host_down";
    default:
        return "?";
    }
}

static const char *usb_name(uint16_t s) {
    switch (s) {
    case ZMK_USB_CONN_NONE:
        return "unplugged";
    case ZMK_USB_CONN_POWERED:
        return "powered";
    case ZMK_USB_CONN_HID:
        return "connected";
    default:
        return "?";
    }
}

static void format_rec(char *buf, size_t size, const struct rec *r) {
    switch (r->type) {
    case REC_VOLT:
        snprintf(buf, size, "%u,%u,volt,%u,%u,%u,%s%s\r\n", r->boot, r->t, r->mv, r->spread,
                 r->pct, (r->info & VOLT_ON_USB) ? "on_usb" : "on_battery",
                 (r->info & VOLT_LOAD_ON) ? "+load" : "");
        break;
    case REC_USB:
        snprintf(buf, size, "%u,%u,usb,,,,%s\r\n", r->boot, r->t, usb_name(r->info));
        break;
    case REC_LINK_DOWN:
        snprintf(buf, size, "%u,%u,link_down,,,,reason 0x%02x\r\n", r->boot, r->t, r->info);
        break;
    case REC_RBATT:
        /* Slot 0 is the right half; any other number is not a real half. */
        if (r->info == 0) {
            snprintf(buf, size, "%u,%u,rbatt,,,%u,from right half\r\n", r->boot, r->t, r->pct);
        } else {
            snprintf(buf, size, "%u,%u,rbatt,,,%u,from slot %u (not a half)\r\n", r->boot, r->t,
                     r->pct, r->info);
        }
        break;
    case REC_HOST_DOWN:
        snprintf(buf, size, "%u,%u,host_down,,,,reason 0x%02x\r\n", r->boot, r->t, r->info);
        break;
    default:
        snprintf(buf, size, "%u,%u,%s,,,,\r\n", r->boot, r->t, type_name(r->type));
        break;
    }
}

static void dump(void) {
    char line[80];

    k_mutex_lock(&lock, K_FOREVER);
    size_t n = count;
    struct meta m = meta;
    k_mutex_unlock(&lock);

    snprintf(line, sizeof(line), "\r\n# Lotus58 battery log, %s half\r\n", HALF_NAME);
    out(line);
    snprintf(line, sizeof(line), "# %u records, now boot %u at %u s, sample every %d min\r\n",
             (unsigned)n, m.boot, (uint32_t)(k_uptime_get() / 1000),
             CONFIG_LOTUS58_BATTERY_LOG_INTERVAL_MIN);
    out(line);
    snprintf(line, sizeof(line), "# volt = mean of %d reads %d ms apart; extra load now %s\r\n",
             SAMPLE_READS, SAMPLE_GAP_MS, load_on ? "on" : "off");
    out(line);
    out("# pct = stock ZMK linear estimate for that mV (3450..4200 mV)\r\n");
    if (m.dropped) {
        snprintf(line, sizeof(line), "# log full: %u records dropped before the last %d\r\n",
                 m.dropped, PER_BLOCK);
        out(line);
    }
    out("boot,time_s,event,mv,spread_mv,pct,detail\r\n");

    for (size_t i = 0; i < n; i++) {
        k_mutex_lock(&lock, K_FOREVER);
        struct rec r = recs[i];
        k_mutex_unlock(&lock);
        format_rec(line, sizeof(line), &r);
        out(line);
    }
    out("# end. Type a command and press Enter: d = print again, s = read voltage now,\r\n");
    out("#   b = extra load off/on, clear = clear the log\r\n");
}

static void run_command(const char *cmd) {
    if (strcmp(cmd, "d") == 0) {
        dump();
    } else if (strcmp(cmd, "s") == 0) {
        /* Sample on the same queue as ZMK's own battery reads. */
        k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &usb_sample_work, K_NO_WAIT);
        k_msleep(300);
        k_mutex_lock(&lock, K_FOREVER);
        struct rec r = recs[count ? count - 1 : 0];
        k_mutex_unlock(&lock);
        char line[80];
        format_rec(line, sizeof(line), &r);
        out(line);
    } else if (strcmp(cmd, "b") == 0) {
        set_load(!load_on);
        out(load_on ? "# extra load is now on\r\n" : "# extra load is now off\r\n");
    } else if (strcmp(cmd, "clear") == 0) {
        clear_log();
        out("# log cleared\r\n");
    }
    /* Anything else is ignored without a reply: some hosts send our own output
     * back to us, and replying to it could loop forever. */
}

/* Commands are whole lines (a word, then Enter). Our own output echoed back by
 * the host never forms such a line: every line we print starts with # or a
 * digit. */
static void readout_thread(void) {
    bool was_open = false;
    char cmd[8];
    size_t len = 0;
    bool overflow = false;

    if (!device_is_ready(uart)) {
        return;
    }

    while (true) {
        k_msleep(250);

        uint32_t dtr = 0;
        if (uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &dtr) != 0) {
            dtr = 0;
        }
        if (dtr && !was_open) {
            k_msleep(500); /* let the terminal settle */
            unsigned char junk;
            while (uart_poll_in(uart, &junk) == 0) {
            }
            len = 0;
            overflow = false;
            dump();
        }
        was_open = dtr;

        unsigned char c;
        while (uart_poll_in(uart, &c) == 0) {
            if (c == '\r' || c == '\n') {
                if (!overflow && len > 0) {
                    cmd[len] = '\0';
                    run_command(cmd);
                }
                len = 0;
                overflow = false;
            } else if (len < sizeof(cmd) - 1) {
                cmd[len++] = c;
            } else {
                overflow = true;
            }
        }
    }
}

/* One step above the load thread, which never yields. */
K_THREAD_DEFINE(battery_log_readout, 1536, readout_thread, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO - 1, 0, 2000);
