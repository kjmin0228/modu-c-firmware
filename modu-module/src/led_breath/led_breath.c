/*
 * Copyright (c) 2026 EKS Inc.
 * Created by Ryu.
 * SPDX-License-Identifier: LicenseRef-EKS-NonCommercial-1.0
 *
 * LED Bluetooth status effect using PWM.
 * Modified for reduced idle/steady-state power consumption.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include <zmk/activity.h>
#include <zmk/split/transport/central.h>
#include <zmk/split/transport/peripheral.h>
#include <zmk/split/transport/types.h>

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#endif

LOG_MODULE_REGISTER(led_breath, CONFIG_LOG_DEFAULT_LEVEL);

#define BREATH_SCALE       10000u
#define ACTIVE_SCALE       4000u   /* 40% */
#define IDLE_SCALE          500u   /*  5% */

#define SLOW_BLINK_MS      1200u
#define FAST_BLINK_MS       250u
#define BOOT_RED_MS        1500u

/*
 * Fixed LED states do not need a 20 ms update.
 * Poll once a second only to notice BLE/split/profile changes.
 */
#define STATUS_POLL_MS     1000u

__weak int zmk_ble_active_profile_index(void) { return 0; }
__weak bool zmk_ble_active_profile_is_open(void) { return true; }
__weak bool zmk_split_bt_peripheral_is_connected(void) { return false; }
__weak bool zmk_split_bt_peripheral_is_bonded(void) { return false; }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
extern const struct zmk_split_transport_central *active_transport;
#else
extern const struct zmk_split_transport_peripheral *active_transport;
#endif

#define MODU_IS_LEFT_SHIELD  IS_ENABLED(CONFIG_SHIELD_MODU_LEFT)
#define MODU_IS_RIGHT_SHIELD IS_ENABLED(CONFIG_SHIELD_MODU_RIGHT)

/* LED PWM device nodes */
static const struct pwm_dt_spec leds[] = {
#if DT_NODE_EXISTS(DT_ALIAS(led_status1))
    PWM_DT_SPEC_GET(DT_ALIAS(led_status1)),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led_status2))
    PWM_DT_SPEC_GET(DT_ALIAS(led_status2)),
#endif
#if DT_NODE_EXISTS(DT_ALIAS(led_status3))
    PWM_DT_SPEC_GET(DT_ALIAS(led_status3)),
#endif
};

#define NUM_LEDS ARRAY_SIZE(leds)

static struct k_work_delayable breath_work;
static int64_t boot_deadline_ms;

#if MODU_IS_RIGHT_SHIELD

/* Right channel order: blue, green, red. */
static const uint16_t idle_color[3]               = {BREATH_SCALE, 0, 0};
static const uint16_t right_unset_color[3]        = {BREATH_SCALE, 0, 0};
static const uint16_t right_connected_color[3]    = {0, BREATH_SCALE, 0};
static const uint16_t right_disconnected_color[3] = {0, 0, BREATH_SCALE};

#else

/* Left channel order: blue, green, red. */
static const uint16_t idle_color[3] = {BREATH_SCALE, 0, 0};

#if MODU_IS_LEFT_SHIELD
static const uint16_t white_color[3] = {
    BREATH_SCALE, BREATH_SCALE, BREATH_SCALE
};
#endif

static const uint16_t profile_colors[][3] = {
    {7200, BREATH_SCALE, 0},    /* Profile 1: mint */
    {BREATH_SCALE, 3000, 8500}, /* Profile 2: light purple */
    {0, 3600, BREATH_SCALE},    /* Profile 3: orange */
};

static const uint16_t split_unset_color[3] = {
    BREATH_SCALE, 0, 0
};

#endif

static const uint16_t boot_color[3] = {
    0, 0, BREATH_SCALE
};

#if MODU_IS_LEFT_SHIELD
static bool left_usb_connected(void)
{
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}
#endif

#if MODU_IS_RIGHT_SHIELD
static bool split_transport_connected(void)
{
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

    if (!active_transport || !active_transport->api) {
        return false;
    }

    if (!active_transport->api->get_status) {
        return true;
    }

    struct zmk_split_transport_status status =
        active_transport->api->get_status();

    return status.available &&
           status.enabled &&
           status.connections ==
               ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_ALL_CONNECTED;

#else

    if (!active_transport || !active_transport->api) {
        return zmk_split_bt_peripheral_is_connected();
    }

    if (!active_transport->api->get_status) {
        return true;
    }

    struct zmk_split_transport_status status =
        active_transport->api->get_status();

    return status.available &&
           status.enabled &&
           status.connections ==
               ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_ALL_CONNECTED;

#endif
}
#endif

static bool split_setup_done(void)
{
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

    if (!active_transport || !active_transport->api) {
        return false;
    }

    if (!active_transport->api->get_status) {
        return true;
    }

    struct zmk_split_transport_status status =
        active_transport->api->get_status();

    return status.available;

#else

    return zmk_split_bt_peripheral_is_bonded();

#endif
}

static void set_led_channel(int channel, uint32_t level)
{
    if (channel >= NUM_LEDS ||
        !device_is_ready(leds[channel].dev)) {
        return;
    }

    if (level > BREATH_SCALE) {
        level = BREATH_SCALE;
    }

    uint32_t pulse =
        (uint32_t)((uint64_t)leds[channel].period *
                   (BREATH_SCALE - level) /
                   BREATH_SCALE);

    pwm_set_pulse_dt(&leds[channel], pulse);
}

static void set_color(const uint16_t color[3], uint32_t brightness)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t channel =
            i < 3 ? color[i] : BREATH_SCALE;

        uint32_t level =
            (uint32_t)((uint64_t)channel *
                       brightness /
                       BREATH_SCALE);

        set_led_channel(i, level);
    }
}

static void leds_off(void)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        set_led_channel(i, 0);
    }
}

/*
 * Square blink using uptime instead of waking every 20 ms.
 * FAST: edge every 125 ms
 * SLOW: edge every 600 ms
 */
static uint32_t blink_level(uint32_t period_ms)
{
    uint32_t half = period_ms / 2;
    uint32_t phase = k_uptime_get_32() % period_ms;

    return phase < half ? ACTIVE_SCALE : 0;
}

static uint32_t next_blink_delay(uint32_t period_ms)
{
    uint32_t half = period_ms / 2;
    uint32_t phase = k_uptime_get_32() % half;
    uint32_t remaining = half - phase;

    return remaining ? remaining : 1;
}

static uint32_t boot_remaining_ms(void)
{
    int64_t remaining =
        boot_deadline_ms - k_uptime_get();

    return remaining > 0 ?
        (uint32_t)remaining : 0;
}

static void schedule_next(uint32_t delay_ms)
{
    k_work_schedule(
        &breath_work,
        K_MSEC(delay_ms ? delay_ms : 1)
    );
}

static void breath_work_handler(struct k_work *work)
{
    enum zmk_activity_state activity =
        zmk_activity_get_state();

    /*
     * IDLE:
     * fixed dim blue at 5%.
     * No software breath.
     */
    if (activity == ZMK_ACTIVITY_IDLE) {
        set_color(idle_color, IDLE_SCALE);
        schedule_next(STATUS_POLL_MS);
        return;
    }

    /*
     * Deep sleep:
     * LED off. Normally system-off follows immediately.
     */
    if (activity == ZMK_ACTIVITY_SLEEP) {
        leds_off();
        return;
    }

    uint32_t boot_remaining = boot_remaining_ms();

#if MODU_IS_RIGHT_SHIELD

    bool split_setup = split_setup_done();

    if (!split_setup) {
        set_color(
            right_unset_color,
            blink_level(FAST_BLINK_MS)
        );

        schedule_next(
            next_blink_delay(FAST_BLINK_MS)
        );
        return;
    }

    if (boot_remaining) {
        set_color(boot_color, ACTIVE_SCALE);
        schedule_next(boot_remaining);
        return;
    }

    bool split_ready = split_transport_connected();

    set_color(
        split_ready ?
            right_connected_color :
            right_disconnected_color,
        ACTIVE_SCALE
    );

    schedule_next(STATUS_POLL_MS);

#else

    bool force_white = false;

#if MODU_IS_LEFT_SHIELD
    force_white = left_usb_connected();
#endif

    if (force_white) {
#if MODU_IS_LEFT_SHIELD
        set_color(white_color, ACTIVE_SCALE);
#endif
        schedule_next(STATUS_POLL_MS);
        return;
    }

    bool split_setup = split_setup_done();

    if (!split_setup) {
        set_color(
            split_unset_color,
            blink_level(FAST_BLINK_MS)
        );

        schedule_next(
            next_blink_delay(FAST_BLINK_MS)
        );
        return;
    }

    if (boot_remaining) {
        set_color(boot_color, ACTIVE_SCALE);
        schedule_next(boot_remaining);
        return;
    }

    uint8_t profile =
        zmk_ble_active_profile_index() %
        ARRAY_SIZE(profile_colors);

    if (zmk_ble_active_profile_is_open()) {
        set_color(
            profile_colors[profile],
            blink_level(SLOW_BLINK_MS)
        );

        schedule_next(
            next_blink_delay(SLOW_BLINK_MS)
        );
        return;
    }

    set_color(
        profile_colors[profile],
        ACTIVE_SCALE
    );

    schedule_next(STATUS_POLL_MS);

#endif
}

static int led_breath_init(void)
{
    for (int i = 0; i < NUM_LEDS; i++) {
        if (!device_is_ready(leds[i].dev)) {
            LOG_WRN(
                "LED %d PWM device not ready",
                i
            );
        }
    }

    boot_deadline_ms =
        k_uptime_get() + BOOT_RED_MS;

    k_work_init_delayable(
        &breath_work,
        breath_work_handler
    );

    k_work_schedule(
        &breath_work,
        K_NO_WAIT
    );

    LOG_INF(
        "LED status started: active=40%% idle=5%%"
    );

    return 0;
}

SYS_INIT(led_breath_init, APPLICATION, 99);
