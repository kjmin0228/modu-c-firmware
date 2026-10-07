/*
 * Copyright (c) 2026 EKS Inc.
 * Created by Ryu.
 * SPDX-License-Identifier: LicenseRef-EKS-NonCommercial-1.0
 */

#define DT_DRV_COMPAT modu_kscan_alt_thumb_remap

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(modu_alt_thumb_kscan, CONFIG_LOG_DEFAULT_LEVEL);

#define MODU_THUMB_ROW 5
#define MODU_ALT_SWAP_COL_A 0
#define MODU_ALT_SWAP_COL_B 2
#define MODU_ALT_SWAP_RIGHT_COL_A 6
#define MODU_ALT_SWAP_RIGHT_COL_B 8

struct modu_alt_thumb_kscan_config {
    const struct device *kscan;
    struct gpio_dt_spec orientation_gpio;
};

struct modu_alt_thumb_kscan_data {
    kscan_callback_t callback;
    bool alt_enabled;
};

#define MODU_ALT_THUMB_INST_DEV(n) DEVICE_DT_GET(DT_DRV_INST(n)),

static const struct device *instances[] = {DT_INST_FOREACH_STATUS_OKAY(MODU_ALT_THUMB_INST_DEV)};

static void modu_alt_thumb_inner_callback(const struct device *inner_dev, uint32_t row,
                                          uint32_t column, bool pressed)
{
    for (int i = 0; i < ARRAY_SIZE(instances); i++) {
        const struct device *dev = instances[i];
        const struct modu_alt_thumb_kscan_config *cfg = dev->config;
        struct modu_alt_thumb_kscan_data *data = dev->data;

        if (cfg->kscan != inner_dev || !data->callback) {
            continue;
        }

        if (data->alt_enabled && row == MODU_THUMB_ROW) {
            if (column == MODU_ALT_SWAP_COL_A) {
                column = MODU_ALT_SWAP_COL_B;
            } else if (column == MODU_ALT_SWAP_COL_B) {
                column = MODU_ALT_SWAP_COL_A;
            } else if (column == MODU_ALT_SWAP_RIGHT_COL_A) {
                column = MODU_ALT_SWAP_RIGHT_COL_B;
            } else if (column == MODU_ALT_SWAP_RIGHT_COL_B) {
                column = MODU_ALT_SWAP_RIGHT_COL_A;
            }
        }

        data->callback(dev, row, column, pressed);
    }
}

static int modu_alt_thumb_kscan_configure(const struct device *dev, kscan_callback_t callback)
{
    const struct modu_alt_thumb_kscan_config *cfg = dev->config;
    struct modu_alt_thumb_kscan_data *data = dev->data;

    if (!callback) {
        return -EINVAL;
    }

    kscan_config(cfg->kscan, modu_alt_thumb_inner_callback);
    data->callback = callback;

    return 0;
}

static int modu_alt_thumb_kscan_enable_callback(const struct device *dev)
{
    const struct modu_alt_thumb_kscan_config *cfg = dev->config;


#if IS_ENABLED(CONFIG_PM_DEVICE)
    /* MODU WAKEUP PROPAGATION */

    /*
     * alt_thumb_kscan is the physical-layout kscan.
     * Keep this wrapper enabled as a wake source.
     */
    if (pm_device_wakeup_is_capable(dev) &&
        !pm_device_wakeup_is_enabled(dev)) {
        if (!pm_device_wakeup_enable(dev, true)) {
            LOG_ERR("Failed to enable wakeup for alt thumb kscan");
        }
    }

    /*
     * Pass wakeup capability to the wrapped composite kscan.
     * Its enable callback will then propagate it to matrix/direct.
     */
    if (pm_device_wakeup_is_capable(cfg->kscan) &&
        !pm_device_wakeup_is_enabled(cfg->kscan)) {
        if (!pm_device_wakeup_enable(cfg->kscan, true)) {
            LOG_ERR("Failed to enable wakeup for wrapped kscan %s",
                    cfg->kscan->name);
        }
    }
#endif

#if IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
    if (!pm_device_runtime_is_enabled(dev) && pm_device_runtime_is_enabled(cfg->kscan)) {
        pm_device_runtime_get(cfg->kscan);
    }
#elif IS_ENABLED(CONFIG_PM_DEVICE)
    pm_device_action_run(cfg->kscan, PM_DEVICE_ACTION_RESUME);
#endif

    return kscan_enable_callback(cfg->kscan);
}

static int modu_alt_thumb_kscan_disable_callback(const struct device *dev)
{
    const struct modu_alt_thumb_kscan_config *cfg = dev->config;
    int err = kscan_disable_callback(cfg->kscan);

#if IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
    if (!pm_device_runtime_is_enabled(dev) && pm_device_runtime_is_enabled(cfg->kscan)) {
        pm_device_runtime_put(cfg->kscan);
    }
#elif IS_ENABLED(CONFIG_PM_DEVICE)
    pm_device_action_run(cfg->kscan, PM_DEVICE_ACTION_SUSPEND);
#endif

    return err;
}

static int modu_alt_thumb_kscan_init(const struct device *dev)
{
    const struct modu_alt_thumb_kscan_config *cfg = dev->config;
    struct modu_alt_thumb_kscan_data *data = dev->data;

    if (!device_is_ready(cfg->kscan)) {
        LOG_ERR("Wrapped kscan %s is not ready", cfg->kscan->name);
        return -ENODEV;
    }

    if (!device_is_ready(cfg->orientation_gpio.port)) {
        LOG_WRN("P0.08 GPIO device not ready; using non-alt thumb map");
        data->alt_enabled = false;
        return 0;
    }

    int err = gpio_pin_configure_dt(&cfg->orientation_gpio, GPIO_INPUT);
    if (err) {
        LOG_WRN("Failed to configure P0.08 (%d); using non-alt thumb map", err);
        data->alt_enabled = false;
        return 0;
    }

    int selected = gpio_pin_get_raw(cfg->orientation_gpio.port, cfg->orientation_gpio.pin);
    if (selected < 0) {
        LOG_WRN("Failed to read P0.08 (%d); using non-alt thumb map", selected);
        data->alt_enabled = false;
        return 0;
    }

    data->alt_enabled = selected == 0;
    LOG_INF("P0.08 raw=%d (%s); using %s thumb map", selected,
            data->alt_enabled ? "grounded" : "open",
            data->alt_enabled ? "alt" : "non-alt");

#if IS_ENABLED(CONFIG_PM_DEVICE)
    pm_device_init_suspended(dev);
#endif

    return 0;
}

static const struct kscan_driver_api modu_alt_thumb_kscan_api = {
    .config = modu_alt_thumb_kscan_configure,
    .enable_callback = modu_alt_thumb_kscan_enable_callback,
    .disable_callback = modu_alt_thumb_kscan_disable_callback,
};

#if IS_ENABLED(CONFIG_PM_DEVICE)
static int modu_alt_thumb_kscan_pm_action(const struct device *dev, enum pm_device_action action)
{
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        return modu_alt_thumb_kscan_disable_callback(dev);
    case PM_DEVICE_ACTION_RESUME:
        return modu_alt_thumb_kscan_enable_callback(dev);
    default:
        return -ENOTSUP;
    }
}
#endif

#define MODU_ALT_THUMB_KSCAN_DEFINE(n)                                                            \
    static const struct modu_alt_thumb_kscan_config modu_alt_thumb_kscan_config_##n = {           \
        .kscan = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan)),                                        \
        .orientation_gpio = GPIO_DT_SPEC_INST_GET(n, orientation_gpios),                          \
    };                                                                                             \
    static struct modu_alt_thumb_kscan_data modu_alt_thumb_kscan_data_##n;                        \
    PM_DEVICE_DT_INST_DEFINE(n, modu_alt_thumb_kscan_pm_action);                                  \
    DEVICE_DT_INST_DEFINE(n, modu_alt_thumb_kscan_init, PM_DEVICE_DT_INST_GET(n),                 \
                          &modu_alt_thumb_kscan_data_##n, &modu_alt_thumb_kscan_config_##n,       \
                          POST_KERNEL, CONFIG_MODU_ALT_THUMB_KSCAN_INIT_PRIORITY,                 \
                          &modu_alt_thumb_kscan_api);

DT_INST_FOREACH_STATUS_OKAY(MODU_ALT_THUMB_KSCAN_DEFINE)
