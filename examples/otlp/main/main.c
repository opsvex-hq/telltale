/*
 * telltale — OTLP example.
 *
 * Connects to Wi-Fi, serves GET /metrics for a Prometheus scrape, and pushes
 * the same registry to an OTLP/HTTP collector. It also shows the two hooks a
 * real application is expected to provide: a disconnect notification and a
 * battery reading.
 *
 * Configure with: idf.py menuconfig -> Example Configuration
 * (or edit the defines below).
 *
 * Copyright (c) 2026 Opsvex SpA. SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "telltale.h"

#define EXAMPLE_WIFI_SSID "your-ssid"
#define EXAMPLE_WIFI_PASSWORD "your-password"
#define EXAMPLE_OTLP_ENDPOINT "http://collector.lan:4318/v1/metrics"

static const char *TAG = "example";
static telltale_handle_t s_telltale = NULL;

/**
 * Battery provider.
 *
 * telltale does not read an ADC for you: the divider ratio, the channel and
 * the calibration are board decisions, and a wrong guess reports a healthy
 * battery on a device that is about to die. Returning false leaves the metric
 * absent, which is honest.
 */
static bool read_battery(void *user_data, float *out_volts) {
    (void)user_data;
    /* Replace with a real ADC read for your board. */
    *out_volts = 3.92f;
    return true;
}

/**
 * Wi-Fi events.
 *
 * telltale deliberately does not install its own handler — an application
 * that already has one should not get a second, invisible one. Disconnections
 * are reported by calling telltale_note_disconnect().
 */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_telltale != NULL) {
            telltale_note_disconnect(s_telltale);
        }
        ESP_LOGW(TAG, "disconnected, retrying");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "connected; metrics on :8080/metrics");
    }
}

static void wifi_start(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, EXAMPLE_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, EXAMPLE_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    wifi_start();

    telltale_config_t config = TELLTALE_DEFAULT_CONFIG();
    config.service_name = "fleet-gateway";
    config.hardware_model = "esp32-devkit-v1";
    config.sample_interval_ms = 15000;
    config.enable_http_endpoint = true;
    config.http_port = 8080;
    config.otlp_endpoint = EXAMPLE_OTLP_ENDPOINT;
    config.otlp_push_interval_ms = 60000; /* scrape often, push rarely */
    config.battery_provider = read_battery;
    config.render_buffer_size = 4096;

    ESP_ERROR_CHECK(telltale_init(&config, &s_telltale));
    ESP_ERROR_CHECK(telltale_watch_task(s_telltale, NULL, "app_main"));

    /* An application metric rides the same pipeline as the built-in ones. */
    ESP_ERROR_CHECK(telltale_set_metric(s_telltale, "pump.pressure", TELLTALE_GAUGE,
                                        TELLTALE_UNIT_NONE, "Pump pressure in bar", 2.4));

    /* Nothing else to do: the collector task samples, serves and pushes. */
}
