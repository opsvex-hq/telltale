/*
 * telltale — basic example.
 *
 * Starts telltale with the defaults and prints the Prometheus exposition to
 * the console. No Wi-Fi and no network: the point is to see what the device
 * reports about itself, including why it restarted.
 *
 * Copyright (c) 2026 Opsvex SpA. SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "telltale.h"

static const char *TAG = "example";

/* A render buffer sized for the default metric set. The library never grows
 * it behind your back: if it is too small you get an explicit error, not a
 * truncated scrape that a dashboard would happily plot. */
static char metrics[3072];

void app_main(void) {
    /* telltale keeps its boot counter in NVS, so NVS has to be up first. A
     * fresh or resized partition needs an erase before it will mount. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    telltale_config_t config = TELLTALE_DEFAULT_CONFIG();
    config.service_name = "example";
    config.hardware_model = "esp32-devkit-v1";
    config.sample_interval_ms = 5000;

    telltale_handle_t telltale = NULL;
    ESP_ERROR_CHECK(telltale_init(&config, &telltale));

    /* Watch this task's stack so the exposition shows a second series. */
    ESP_ERROR_CHECK(telltale_watch_task(telltale, NULL, "app_main"));

    ESP_LOGI(TAG, "boot %u, last reset was %s", (unsigned)telltale_boot_count(telltale),
             telltale_reset_reason_name(telltale_last_reset_reason(telltale)));

    while (true) {
        size_t needed = 0;
        err = telltale_render_prometheus(telltale, metrics, sizeof(metrics), &needed);
        if (err == ESP_ERR_INVALID_SIZE) {
            ESP_LOGE(TAG, "buffer too small: %u bytes needed", (unsigned)needed);
        } else {
            printf("\n%s", metrics);
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
