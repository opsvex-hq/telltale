/*
 * telltale — device health metrics for ESP-IDF, in the shape Prometheus and
 * OpenTelemetry already understand.
 *
 * The node_exporter that does not exist for microcontrollers: free heap and
 * fragmentation, reboot count with its cause, link quality, uptime and task
 * stack headroom, exposed on /metrics or pushed to an OTLP endpoint.
 *
 * The public interface is C with a stable ABI and an opaque handle, so the
 * component is consumable from C, C++ and Arduino and its internals stay
 * replaceable. Nothing here allocates after `telltale_init` returns.
 *
 * Copyright (c) 2026 Opsvex SpA. SPDX-License-Identifier: Apache-2.0
 */

#ifndef TELLTALE_H
#define TELLTALE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "telltale_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TELLTALE_VERSION "0.1.0"

/** Opaque handle. The layout is private and may change without a major bump. */
typedef struct telltale_ctx_s *telltale_handle_t;

/**
 * Supplies a battery reading, in volts.
 *
 * Battery sensing is board-specific — the divider ratio, the ADC channel and
 * the calibration all differ — so telltale asks the application rather than
 * guessing. Return false when no reading is available; the metric is then
 * simply absent, which is honest, rather than reported as zero.
 */
typedef bool (*telltale_battery_provider_t)(void *user_data, float *out_volts);

typedef struct {
    /** Stable device identity. NULL derives one from the Wi-Fi station MAC. */
    const char *device_id;
    /** Logical service name, e.g. "gateway". NULL means "telltale". */
    const char *service_name;
    /** Hardware model for the resource attributes, e.g. "esp32-devkit-v1". */
    const char *hardware_model;

    /**
     * Sampling period for the background collector, in milliseconds.
     * 0 disables the task entirely, leaving the application to call
     * `telltale_collect()` on its own schedule.
     */
    uint32_t sample_interval_ms;

    /** Serve GET /metrics in Prometheus text format. */
    bool enable_http_endpoint;
    /** Port for that endpoint. 0 means 80. */
    uint16_t http_port;

    /**
     * OTLP/HTTP endpoint for metrics, e.g.
     * "http://collector.lan:4318/v1/metrics". NULL disables pushing.
     */
    const char *otlp_endpoint;
    /** Push period in milliseconds. 0 means "same as sample_interval_ms". */
    uint32_t otlp_push_interval_ms;
    /** Optional bearer token for the OTLP endpoint. */
    const char *otlp_bearer_token;

    /** Optional battery reading provider. */
    telltale_battery_provider_t battery_provider;
    void *battery_user_data;

    /** Scratch buffer size for rendering, in bytes. 0 means 4096. */
    size_t render_buffer_size;
} telltale_config_t;

/**
 * Defaults that are safe on any board: sample every 30 s, no HTTP endpoint,
 * no OTLP push, identity derived from the MAC.
 */
#define TELLTALE_DEFAULT_CONFIG()                 \
    {                                             \
        .device_id = NULL,                        \
        .service_name = NULL,                     \
        .hardware_model = NULL,                   \
        .sample_interval_ms = 30000,              \
        .enable_http_endpoint = false,            \
        .http_port = 0,                           \
        .otlp_endpoint = NULL,                    \
        .otlp_push_interval_ms = 0,               \
        .otlp_bearer_token = NULL,                \
        .battery_provider = NULL,                 \
        .battery_user_data = NULL,                \
        .render_buffer_size = 0,                  \
    }

/**
 * Initialises telltale: reads the boot counter from NVS, records the reset
 * reason of the boot that just happened, allocates the render buffer, and
 * starts the collector task, the HTTP endpoint and the OTLP pusher if they
 * are enabled.
 *
 * Call once. Returns ESP_ERR_INVALID_STATE on a second call with the same
 * handle, ESP_ERR_NO_MEM if the render buffer cannot be allocated.
 */
esp_err_t telltale_init(const telltale_config_t *config, telltale_handle_t *out_handle);

/** Stops the tasks, closes the endpoint and frees everything. */
esp_err_t telltale_deinit(telltale_handle_t handle);

/**
 * Samples every collector once, updating the registry.
 *
 * Safe to call from any task. Cheap: a few register reads and one NVS read
 * that is cached after the first call.
 */
esp_err_t telltale_collect(telltale_handle_t handle);

/**
 * Renders the current registry as Prometheus text exposition.
 *
 * Follows the snprintf contract through `out_needed`: when the value it
 * reports is greater than or equal to `size`, the output was truncated.
 */
esp_err_t telltale_render_prometheus(telltale_handle_t handle, char *out, size_t size,
                                     size_t *out_needed);

/** Renders the current registry as an OTLP/HTTP JSON export request. */
esp_err_t telltale_render_otlp(telltale_handle_t handle, char *out, size_t size,
                               size_t *out_needed);

/**
 * Pushes the current registry to the configured OTLP endpoint, once.
 *
 * Returns ESP_ERR_INVALID_STATE when no endpoint is configured, and the
 * transport error otherwise. A non-2xx response is reported as ESP_FAIL with
 * the status code logged, never swallowed.
 */
esp_err_t telltale_push_otlp(telltale_handle_t handle);

/**
 * Watches a task's stack headroom.
 *
 * Reports `device.task.stack.free` labelled by task name, from the FreeRTOS
 * high-water mark — the smallest amount of free stack that task has ever had.
 * That number, not the current one, is what predicts a stack overflow.
 *
 * Up to CONFIG_TELLTALE_MAX_WATCHED_TASKS tasks; returns ESP_ERR_NO_MEM past
 * that. Pass NULL for the calling task.
 */
esp_err_t telltale_watch_task(telltale_handle_t handle, TaskHandle_t task, const char *name);

/**
 * Records a link disconnection.
 *
 * telltale does not subscribe to Wi-Fi events: an application that already
 * has an event handler should not have a second one behind its back, and a
 * component that installs handlers is harder to reason about. Call this from
 * your own handler.
 */
esp_err_t telltale_note_disconnect(telltale_handle_t handle);

/**
 * Publishes an application metric through the same pipeline.
 *
 * Use a dotted semantic-convention name ("pump.pressure"); the Prometheus
 * name and the OTLP unit are derived from it and the unit you pass.
 */
esp_err_t telltale_set_metric(telltale_handle_t handle, const char *name,
                              telltale_metric_type_t type, telltale_unit_t unit,
                              const char *help, double value);

/**
 * Direct access to the registry, for callers that need the portable API.
 *
 * The returned pointer is owned by the handle and is not thread-safe against
 * a concurrent `telltale_collect()`; hold your own lock if you write to it
 * from several tasks.
 */
telltale_registry_t *telltale_registry_of(telltale_handle_t handle);

/** The reset reason recorded at init, already mapped to the portable enum. */
telltale_reset_reason_t telltale_last_reset_reason(telltale_handle_t handle);

/** Boots since the counter was first written to NVS, including this one. */
uint32_t telltale_boot_count(telltale_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TELLTALE_H */
