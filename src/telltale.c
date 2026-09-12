/*
 * telltale — ESP-IDF implementation.
 *
 * This file owns the hardware: it reads the heap, the reset reason, the radio
 * and the clock, converts them into the portable registry's vocabulary, and
 * runs the background tasks. All formatting lives in the portable core, which
 * is why this file has no string handling to speak of.
 *
 * Allocation happens once, in telltale_init. The collector, the HTTP handler
 * and the OTLP pusher allocate nothing.
 *
 * Copyright (c) 2026 Opsvex SpA. SPDX-License-Identifier: Apache-2.0
 */

#include "telltale.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_TELLTALE_ENABLE_WIFI_COLLECTOR
#include "esp_wifi.h"
#endif

#if CONFIG_TELLTALE_ENABLE_HTTP_ENDPOINT
#include "esp_http_server.h"
#endif

#if CONFIG_TELLTALE_ENABLE_OTLP
#include "esp_http_client.h"
#endif

static const char *TAG = "telltale";

#define TELLTALE_NVS_NAMESPACE "telltale"
#define TELLTALE_NVS_BOOT_KEY "boots"
#define TELLTALE_DEFAULT_RENDER_BUFFER 4096

/* Kconfig supplies these; the fallbacks keep the file compilable in an editor
 * or a static analyser that has no sdkconfig.h. */
#ifndef CONFIG_TELLTALE_MAX_WATCHED_TASKS
#define CONFIG_TELLTALE_MAX_WATCHED_TASKS 4
#endif
#ifndef CONFIG_TELLTALE_TASK_STACK_SIZE
#define CONFIG_TELLTALE_TASK_STACK_SIZE 3072
#endif

typedef struct {
    TaskHandle_t task;
    char name[TELLTALE_MAX_LABEL_VALUE_LEN];
    bool used;
} watched_task_t;

struct telltale_ctx_s {
    telltale_registry_t registry;
    telltale_config_t config;

    char device_id[TELLTALE_MAX_RESOURCE_LEN];
    telltale_reset_reason_t reset_reason;
    uint32_t boot_count;
    uint32_t disconnects;

    char *render_buffer;
    size_t render_buffer_size;

    watched_task_t watched[CONFIG_TELLTALE_MAX_WATCHED_TASKS];

    SemaphoreHandle_t lock;
    TaskHandle_t collector_task;
    volatile bool running;

#if CONFIG_TELLTALE_ENABLE_HTTP_ENDPOINT
    httpd_handle_t http_server;
#endif
};

/* --- Helpers ------------------------------------------------------------ */

static telltale_reset_reason_t map_reset_reason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return TELLTALE_RESET_POWER_ON;
        case ESP_RST_EXT:
            return TELLTALE_RESET_EXTERNAL;
        case ESP_RST_SW:
            return TELLTALE_RESET_SOFTWARE;
        case ESP_RST_PANIC:
            return TELLTALE_RESET_PANIC;
        case ESP_RST_INT_WDT:
            return TELLTALE_RESET_INT_WATCHDOG;
        case ESP_RST_TASK_WDT:
            return TELLTALE_RESET_TASK_WATCHDOG;
        case ESP_RST_WDT:
            return TELLTALE_RESET_OTHER_WATCHDOG;
        case ESP_RST_DEEPSLEEP:
            return TELLTALE_RESET_DEEP_SLEEP;
        case ESP_RST_BROWNOUT:
            return TELLTALE_RESET_BROWNOUT;
        case ESP_RST_SDIO:
            return TELLTALE_RESET_SDIO;
        default:
            return TELLTALE_RESET_UNKNOWN;
    }
}

/** Derives a stable device id from the station MAC: "esp32-a1b2c3". */
static void derive_device_id(char *out, size_t size) {
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        snprintf(out, size, "esp32-unknown");
        return;
    }
    snprintf(out, size, "esp32-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

/**
 * Reads and increments the boot counter in NVS.
 *
 * A device that has never been provisioned starts at 1 rather than failing:
 * the counter is diagnostic, and refusing to boot over it would be absurd.
 */
static uint32_t bump_boot_counter(void) {
    nvs_handle_t nvs;
    uint32_t boots = 0;

    esp_err_t err = nvs_open(TELLTALE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable (%s); boot count will not persist", esp_err_to_name(err));
        return 1;
    }

    err = nvs_get_u32(nvs, TELLTALE_NVS_BOOT_KEY, &boots);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "could not read boot count: %s", esp_err_to_name(err));
    }

    boots += 1;
    err = nvs_set_u32(nvs, TELLTALE_NVS_BOOT_KEY, boots);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not persist boot count: %s", esp_err_to_name(err));
    }
    nvs_close(nvs);
    return boots;
}

static void lock(telltale_handle_t handle) {
    if (handle->lock != NULL) {
        xSemaphoreTake(handle->lock, portMAX_DELAY);
    }
}

static void unlock(telltale_handle_t handle) {
    if (handle->lock != NULL) {
        xSemaphoreGive(handle->lock);
    }
}

/* --- Collectors --------------------------------------------------------- */

static void collect_heap(telltale_handle_t handle) {
    const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    const size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

    telltale_set(&handle->registry, "device.heap.free", TELLTALE_GAUGE, TELLTALE_UNIT_BYTES,
                 "Free heap right now", (double)free_bytes);
    telltale_set(&handle->registry, "device.heap.total", TELLTALE_GAUGE, TELLTALE_UNIT_BYTES,
                 "Total heap on this device", (double)total);
    telltale_set(&handle->registry, "device.heap.min_free", TELLTALE_GAUGE, TELLTALE_UNIT_BYTES,
                 "Lowest free heap since boot", (double)min_free);
    telltale_set(&handle->registry, "device.heap.largest_free_block", TELLTALE_GAUGE,
                 TELLTALE_UNIT_BYTES, "Largest allocation that can currently succeed",
                 (double)largest);
    telltale_set(&handle->registry, "device.heap.fragmentation", TELLTALE_GAUGE,
                 TELLTALE_UNIT_RATIO, "Heap fragmentation, 0 contiguous to 1 shattered",
                 telltale_heap_fragmentation(free_bytes, largest));
}

static void collect_uptime(telltale_handle_t handle) {
    const double seconds = (double)esp_timer_get_time() / 1000000.0;
    telltale_set(&handle->registry, "device.uptime", TELLTALE_GAUGE, TELLTALE_UNIT_SECONDS,
                 "Seconds since boot", seconds);
}

static void collect_tasks(telltale_handle_t handle) {
    for (size_t i = 0; i < CONFIG_TELLTALE_MAX_WATCHED_TASKS; ++i) {
        const watched_task_t *watched = &handle->watched[i];
        if (!watched->used) {
            continue;
        }
        /* The high-water mark is the smallest free stack this task has ever
         * had. The current value would tell you nothing about the worst case,
         * which is the only value that predicts an overflow. */
        const UBaseType_t words = uxTaskGetStackHighWaterMark(watched->task);
        telltale_label_t labels[1];
        snprintf(labels[0].key, sizeof(labels[0].key), "task");
        snprintf(labels[0].value, sizeof(labels[0].value), "%s", watched->name);

        telltale_set_labeled(&handle->registry, "device.task.stack.free", TELLTALE_GAUGE,
                             TELLTALE_UNIT_BYTES, "Smallest free stack this task has ever had",
                             (double)(words * sizeof(StackType_t)), labels, 1);
    }
}

#if CONFIG_TELLTALE_ENABLE_WIFI_COLLECTOR
static void collect_link(telltale_handle_t handle) {
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        /* Not connected, or Wi-Fi not started. Report the fact rather than a
         * zero RSSI, which reads as an extremely good signal. */
        telltale_set(&handle->registry, "link.connected", TELLTALE_GAUGE, TELLTALE_UNIT_NONE,
                     "1 when the station is associated", 0);
        return;
    }

    telltale_set(&handle->registry, "link.connected", TELLTALE_GAUGE, TELLTALE_UNIT_NONE,
                 "1 when the station is associated", 1);
    telltale_set(&handle->registry, "link.rssi", TELLTALE_GAUGE, TELLTALE_UNIT_DBM,
                 "Received signal strength of the associated AP", (double)ap.rssi);
    telltale_set(&handle->registry, "link.channel", TELLTALE_GAUGE, TELLTALE_UNIT_NONE,
                 "Wi-Fi channel in use", (double)ap.primary);
}
#endif

static void collect_battery(telltale_handle_t handle) {
    if (handle->config.battery_provider == NULL) {
        return;
    }
    float volts = 0.0f;
    if (!handle->config.battery_provider(handle->config.battery_user_data, &volts)) {
        return; /* absent is honest; zero would not be */
    }
    telltale_set(&handle->registry, "device.battery.voltage", TELLTALE_GAUGE, TELLTALE_UNIT_VOLTS,
                 "Battery voltage as reported by the application", (double)volts);
}

static void collect_static(telltale_handle_t handle) {
    telltale_label_t reason_label[1];
    snprintf(reason_label[0].key, sizeof(reason_label[0].key), "reason");
    snprintf(reason_label[0].value, sizeof(reason_label[0].value), "%s",
             telltale_reset_reason_name(handle->reset_reason));

    telltale_set_labeled(&handle->registry, "device.reset", TELLTALE_INFO, TELLTALE_UNIT_NONE,
                         "Cause of the reset that started this boot", 1, reason_label, 1);
    telltale_set(&handle->registry, "device.reset.fault", TELLTALE_GAUGE, TELLTALE_UNIT_NONE,
                 "1 when the last reset was a fault rather than an intended restart",
                 telltale_reset_reason_is_fault(handle->reset_reason) ? 1 : 0);
    telltale_set(&handle->registry, "device.boot.count", TELLTALE_COUNTER, TELLTALE_UNIT_NONE,
                 "Boots recorded in NVS, including this one", (double)handle->boot_count);
    telltale_set(&handle->registry, "link.disconnect.count", TELLTALE_COUNTER, TELLTALE_UNIT_NONE,
                 "Disconnections reported by the application since boot",
                 (double)handle->disconnects);
}

esp_err_t telltale_collect(telltale_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lock(handle);
    collect_static(handle);
    collect_heap(handle);
    collect_uptime(handle);
    collect_tasks(handle);
    collect_battery(handle);
#if CONFIG_TELLTALE_ENABLE_WIFI_COLLECTOR
    collect_link(handle);
#endif
    unlock(handle);

    return ESP_OK;
}

/* --- Rendering ---------------------------------------------------------- */

esp_err_t telltale_render_prometheus(telltale_handle_t handle, char *out, size_t size,
                                     size_t *out_needed) {
    if (handle == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    lock(handle);
    const size_t needed = telltale_core_render_prometheus(&handle->registry, out, size);
    unlock(handle);
    if (out_needed != NULL) {
        *out_needed = needed;
    }
    return needed >= size ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

esp_err_t telltale_render_otlp(telltale_handle_t handle, char *out, size_t size,
                               size_t *out_needed) {
    if (handle == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Without SNTP the device has no wall clock; 0 tells the collector to
     * stamp arrival time, which beats sending a timestamp from 1970. */
    const int64_t now_us = esp_timer_get_time();
    uint64_t unix_nanos = 0;
    time_t wall = time(NULL);
    if (wall > 1600000000) { /* a plausible wall clock, i.e. SNTP has run */
        unix_nanos = (uint64_t)wall * 1000000000ULL + (uint64_t)(now_us % 1000000) * 1000ULL;
    }

    lock(handle);
    const size_t needed = telltale_core_render_otlp_json(&handle->registry, unix_nanos, out, size);
    unlock(handle);
    if (out_needed != NULL) {
        *out_needed = needed;
    }
    return needed >= size ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

/* --- HTTP endpoint ------------------------------------------------------ */

#if CONFIG_TELLTALE_ENABLE_HTTP_ENDPOINT
static esp_err_t metrics_get_handler(httpd_req_t *req) {
    telltale_handle_t handle = (telltale_handle_t)req->user_ctx;

    telltale_collect(handle);

    size_t needed = 0;
    esp_err_t err = telltale_render_prometheus(handle, handle->render_buffer,
                                               handle->render_buffer_size, &needed);
    if (err == ESP_ERR_INVALID_SIZE) {
        /* Truncated output would be silently wrong for a scraper, so say so
         * with a 500 and log the size the buffer should have been. */
        ESP_LOGE(TAG, "render buffer too small: %u bytes needed, %u available",
                 (unsigned)needed, (unsigned)handle->render_buffer_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "telltale render buffer too small");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain; version=0.0.4; charset=utf-8");
    return httpd_resp_send(req, handle->render_buffer, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_http_endpoint(telltale_handle_t handle) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = handle->config.http_port != 0 ? handle->config.http_port : 80;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&handle->http_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not start metrics endpoint: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t metrics_uri = {
        .uri = "/metrics",
        .method = HTTP_GET,
        .handler = metrics_get_handler,
        .user_ctx = handle,
    };
    err = httpd_register_uri_handler(handle->http_server, &metrics_uri);
    if (err != ESP_OK) {
        httpd_stop(handle->http_server);
        handle->http_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "metrics endpoint on :%d/metrics", config.server_port);
    return ESP_OK;
}
#endif /* CONFIG_TELLTALE_ENABLE_HTTP_ENDPOINT */

/* --- OTLP push ---------------------------------------------------------- */

esp_err_t telltale_push_otlp(telltale_handle_t handle) {
#if CONFIG_TELLTALE_ENABLE_OTLP
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->config.otlp_endpoint == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t needed = 0;
    esp_err_t err =
        telltale_render_otlp(handle, handle->render_buffer, handle->render_buffer_size, &needed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTLP payload needs %u bytes, buffer is %u", (unsigned)needed,
                 (unsigned)handle->render_buffer_size);
        return err;
    }

    esp_http_client_config_t config = {
        .url = handle->config.otlp_endpoint,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (handle->config.otlp_bearer_token != NULL) {
        char auth[192];
        snprintf(auth, sizeof(auth), "Bearer %s", handle->config.otlp_bearer_token);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_http_client_set_post_field(client, handle->render_buffer, (int)strlen(handle->render_buffer));

    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        const int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "OTLP endpoint answered %d", status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "OTLP push failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
#else
    (void)handle;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* --- Background task ---------------------------------------------------- */

static void collector_task(void *arg) {
    telltale_handle_t handle = (telltale_handle_t)arg;
    const uint32_t sample_ms = handle->config.sample_interval_ms;
    const uint32_t push_ms = handle->config.otlp_push_interval_ms != 0
                                 ? handle->config.otlp_push_interval_ms
                                 : sample_ms;
    uint32_t since_push = 0;

    while (handle->running) {
        telltale_collect(handle);

        if (handle->config.otlp_endpoint != NULL) {
            since_push += sample_ms;
            if (since_push >= push_ms) {
                since_push = 0;
                telltale_push_otlp(handle);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(sample_ms));
    }

    handle->collector_task = NULL;
    vTaskDelete(NULL);
}

/* --- Lifecycle ---------------------------------------------------------- */

esp_err_t telltale_init(const telltale_config_t *config, telltale_handle_t *out_handle) {
    if (config == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    telltale_handle_t handle = calloc(1, sizeof(struct telltale_ctx_s));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->config = *config;
    handle->render_buffer_size =
        config->render_buffer_size != 0 ? config->render_buffer_size : TELLTALE_DEFAULT_RENDER_BUFFER;
    handle->render_buffer = calloc(1, handle->render_buffer_size);
    if (handle->render_buffer == NULL) {
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    handle->lock = xSemaphoreCreateMutex();
    if (handle->lock == NULL) {
        free(handle->render_buffer);
        free(handle);
        return ESP_ERR_NO_MEM;
    }

    if (config->device_id != NULL) {
        snprintf(handle->device_id, sizeof(handle->device_id), "%s", config->device_id);
    } else {
        derive_device_id(handle->device_id, sizeof(handle->device_id));
    }

    telltale_registry_init(&handle->registry, handle->device_id, config->service_name);

    const esp_app_desc_t *app = esp_app_get_description();
    telltale_registry_set_build(&handle->registry, app != NULL ? app->version : NULL,
                                config->hardware_model);

    handle->reset_reason = map_reset_reason(esp_reset_reason());
    handle->boot_count = bump_boot_counter();

    ESP_LOGI(TAG, "boot %u, last reset: %s", (unsigned)handle->boot_count,
             telltale_reset_reason_name(handle->reset_reason));

    telltale_collect(handle);

#if CONFIG_TELLTALE_ENABLE_HTTP_ENDPOINT
    if (config->enable_http_endpoint) {
        esp_err_t err = start_http_endpoint(handle);
        if (err != ESP_OK) {
            telltale_deinit(handle);
            return err;
        }
    }
#endif

    if (config->sample_interval_ms > 0) {
        handle->running = true;
        if (xTaskCreate(collector_task, "telltale", CONFIG_TELLTALE_TASK_STACK_SIZE, handle,
                        tskIDLE_PRIORITY + 1, &handle->collector_task) != pdPASS) {
            handle->running = false;
            telltale_deinit(handle);
            return ESP_ERR_NO_MEM;
        }
    }

    *out_handle = handle;
    return ESP_OK;
}

esp_err_t telltale_deinit(telltale_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    handle->running = false;
    /* Give the collector one period plus a margin to observe the flag and
     * delete itself, rather than deleting a task mid-sample. */
    if (handle->collector_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(handle->config.sample_interval_ms + 50));
    }

#if CONFIG_TELLTALE_ENABLE_HTTP_ENDPOINT
    if (handle->http_server != NULL) {
        httpd_stop(handle->http_server);
        handle->http_server = NULL;
    }
#endif

    if (handle->lock != NULL) {
        vSemaphoreDelete(handle->lock);
        handle->lock = NULL;
    }
    free(handle->render_buffer);
    free(handle);
    return ESP_OK;
}

/* --- Small public accessors --------------------------------------------- */

esp_err_t telltale_watch_task(telltale_handle_t handle, TaskHandle_t task, const char *name) {
    if (handle == NULL || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    lock(handle);
    for (size_t i = 0; i < CONFIG_TELLTALE_MAX_WATCHED_TASKS; ++i) {
        if (!handle->watched[i].used) {
            handle->watched[i].task = task != NULL ? task : xTaskGetCurrentTaskHandle();
            snprintf(handle->watched[i].name, sizeof(handle->watched[i].name), "%s", name);
            handle->watched[i].used = true;
            unlock(handle);
            return ESP_OK;
        }
    }
    unlock(handle);
    return ESP_ERR_NO_MEM;
}

esp_err_t telltale_note_disconnect(telltale_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    lock(handle);
    handle->disconnects += 1;
    unlock(handle);
    return ESP_OK;
}

esp_err_t telltale_set_metric(telltale_handle_t handle, const char *name,
                              telltale_metric_type_t type, telltale_unit_t unit,
                              const char *help, double value) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    lock(handle);
    const telltale_result_t result = telltale_set(&handle->registry, name, type, unit, help, value);
    unlock(handle);

    switch (result) {
        case TELLTALE_OK:
            return ESP_OK;
        case TELLTALE_ERR_NO_SPACE:
            return ESP_ERR_NO_MEM;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

telltale_registry_t *telltale_registry_of(telltale_handle_t handle) {
    return handle == NULL ? NULL : &handle->registry;
}

telltale_reset_reason_t telltale_last_reset_reason(telltale_handle_t handle) {
    return handle == NULL ? TELLTALE_RESET_UNKNOWN : handle->reset_reason;
}

uint32_t telltale_boot_count(telltale_handle_t handle) {
    return handle == NULL ? 0 : handle->boot_count;
}
