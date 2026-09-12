/*
 * telltale — portable core
 *
 * The part of telltale that has no idea it is running on a microcontroller:
 * a fixed-capacity metric registry and two serialisers (Prometheus text
 * exposition and OTLP/HTTP JSON). Plain C11, no ESP-IDF headers, no malloc,
 * no static mutable state of its own.
 *
 * That boundary exists for one reason: this is the code that is easy to get
 * subtly wrong — metric naming rules, label escaping, float formatting,
 * buffer truncation — and keeping it free of hardware dependencies means it
 * can be compiled and tested on a laptop in under a second.
 *
 * Every serialiser follows the snprintf contract: it writes at most
 * `out_size` bytes including the NUL terminator, and returns the number of
 * bytes it *would* have written. A caller can therefore detect truncation
 * with `returned >= out_size` and never read a half-written buffer.
 *
 * Copyright (c) 2026 Opsvex SpA. SPDX-License-Identifier: Apache-2.0
 */

#ifndef TELLTALE_CORE_H
#define TELLTALE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Version of the portable core, independent of the ESP-IDF component. */
#define TELLTALE_CORE_VERSION "0.1.0"

/* --- Fixed capacities -----------------------------------------------------
 * Everything is sized at compile time. A device that has run out of metric
 * slots fails loudly at registration rather than allocating in a hot path.
 */
#ifndef TELLTALE_MAX_METRICS
#define TELLTALE_MAX_METRICS 32
#endif

#define TELLTALE_MAX_NAME_LEN 48
#define TELLTALE_MAX_LABELS 2
#define TELLTALE_MAX_LABEL_KEY_LEN 20
#define TELLTALE_MAX_LABEL_VALUE_LEN 40
#define TELLTALE_MAX_RESOURCE_LEN 40

/** Metric kinds telltale emits. Deliberately three, not the full OTel set. */
typedef enum {
    /** A value that goes up and down: free heap, RSSI, uptime. */
    TELLTALE_GAUGE = 0,
    /** A monotonically increasing count: boots, disconnects. */
    TELLTALE_COUNTER = 1,
    /**
     * A constant 1 whose labels carry the information: reset reason, firmware
     * version. Strings cannot be metric values, and this is the established
     * idiom for reporting them in both Prometheus and OTel.
     */
    TELLTALE_INFO = 2,
} telltale_metric_type_t;

/** Units, restricted to the ones with an agreed convention. */
typedef enum {
    TELLTALE_UNIT_NONE = 0,
    TELLTALE_UNIT_BYTES,
    TELLTALE_UNIT_SECONDS,
    TELLTALE_UNIT_RATIO,
    TELLTALE_UNIT_DBM,
    TELLTALE_UNIT_CELSIUS,
    TELLTALE_UNIT_VOLTS,
} telltale_unit_t;

/** Canonical reset reasons, mapped from the platform's own enum. */
typedef enum {
    TELLTALE_RESET_UNKNOWN = 0,
    TELLTALE_RESET_POWER_ON,
    TELLTALE_RESET_EXTERNAL,
    TELLTALE_RESET_SOFTWARE,
    TELLTALE_RESET_PANIC,
    TELLTALE_RESET_INT_WATCHDOG,
    TELLTALE_RESET_TASK_WATCHDOG,
    TELLTALE_RESET_OTHER_WATCHDOG,
    TELLTALE_RESET_DEEP_SLEEP,
    TELLTALE_RESET_BROWNOUT,
    TELLTALE_RESET_SDIO,
    TELLTALE_RESET_USB,
    TELLTALE_RESET_JTAG,
} telltale_reset_reason_t;

typedef struct {
    char key[TELLTALE_MAX_LABEL_KEY_LEN];
    char value[TELLTALE_MAX_LABEL_VALUE_LEN];
} telltale_label_t;

typedef struct {
    /** Semantic-convention name in dotted form, e.g. "device.heap.free". */
    char name[TELLTALE_MAX_NAME_LEN];
    telltale_metric_type_t type;
    telltale_unit_t unit;
    /** Static string, not copied. May be NULL. */
    const char *help;
    double value;
    telltale_label_t labels[TELLTALE_MAX_LABELS];
    uint8_t label_count;
    bool used;
} telltale_metric_t;

typedef struct {
    telltale_metric_t metrics[TELLTALE_MAX_METRICS];
    size_t count;
    /** Resource attributes, emitted by both serialisers. */
    char device_id[TELLTALE_MAX_RESOURCE_LEN];
    char service_name[TELLTALE_MAX_RESOURCE_LEN];
    char firmware_version[TELLTALE_MAX_RESOURCE_LEN];
    char hardware_model[TELLTALE_MAX_RESOURCE_LEN];
} telltale_registry_t;

/** Result codes. Negative values are failures. */
typedef enum {
    TELLTALE_OK = 0,
    TELLTALE_ERR_INVALID_ARG = -1,
    TELLTALE_ERR_NO_SPACE = -2,
    TELLTALE_ERR_NAME_TOO_LONG = -3,
    TELLTALE_ERR_TOO_MANY_LABELS = -4,
} telltale_result_t;

/* --- Registry ----------------------------------------------------------- */

/**
 * Zeroes a registry and sets its resource attributes.
 *
 * `device_id` is the only one that matters for correlation; the rest may be
 * NULL. Values longer than the fixed buffers are truncated, not rejected: a
 * long board name is not worth failing a boot over.
 */
void telltale_registry_init(telltale_registry_t *registry, const char *device_id,
                            const char *service_name);

/** Sets the optional firmware/hardware resource attributes. */
void telltale_registry_set_build(telltale_registry_t *registry, const char *firmware_version,
                                 const char *hardware_model);

/** Number of metrics currently registered. */
size_t telltale_registry_count(const telltale_registry_t *registry);

/**
 * Registers or updates a metric by name.
 *
 * Registration is idempotent: setting the same name twice updates the value
 * in place rather than consuming another slot, so a collector can be called
 * every cycle without leaking slots.
 */
telltale_result_t telltale_set(telltale_registry_t *registry, const char *name,
                               telltale_metric_type_t type, telltale_unit_t unit,
                               const char *help, double value);

/**
 * Registers or updates a metric carrying labels.
 *
 * Labels are part of a metric's identity: the same name with different labels
 * occupies different slots, which is what makes `device.reset.reason` with
 * `reason="panic"` distinguishable from `reason="brownout"`.
 */
telltale_result_t telltale_set_labeled(telltale_registry_t *registry, const char *name,
                                       telltale_metric_type_t type, telltale_unit_t unit,
                                       const char *help, double value,
                                       const telltale_label_t *labels, size_t label_count);

/** Adds `delta` to a counter, creating it at `delta` if absent. */
telltale_result_t telltale_increment(telltale_registry_t *registry, const char *name,
                                     const char *help, double delta);

/** Finds a metric by name (first matching slot), or NULL. */
const telltale_metric_t *telltale_find(const telltale_registry_t *registry, const char *name);

/** Finds a metric by name and exact label set, or NULL. */
const telltale_metric_t *telltale_find_labeled(const telltale_registry_t *registry,
                                               const char *name, const telltale_label_t *labels,
                                               size_t label_count);

/* --- Naming and formatting ---------------------------------------------- */

/**
 * Converts a semantic-convention name into a Prometheus metric name.
 *
 * Applies the rules a Prometheus scrape actually enforces: dots become
 * underscores, anything outside [a-zA-Z0-9_:] becomes an underscore, a
 * leading digit is prefixed, the unit is appended as a suffix, counters get
 * `_total` and info metrics get `_info`.
 *
 *   "device.heap.free"  + BYTES  + GAUGE   -> "device_heap_free_bytes"
 *   "device.boot.count" + NONE   + COUNTER -> "device_boot_count_total"
 *   "device.reset"      + NONE   + INFO    -> "device_reset_info"
 *
 * Follows the snprintf contract.
 */
size_t telltale_prometheus_name(const char *name, telltale_unit_t unit,
                                telltale_metric_type_t type, char *out, size_t out_size);

/** The OTel unit string for a unit ("By", "s", "1", ...), never NULL. */
const char *telltale_unit_otel(telltale_unit_t unit);

/**
 * Formats a metric value the way a time-series database expects.
 *
 * An integral value prints without a decimal point (`4096`, not `4096.000000`)
 * because that is what every exposition example shows and what reviewers
 * expect; a fractional one prints with enough precision to survive a round
 * trip. Non-finite values print as Prometheus spells them: `NaN`, `+Inf`.
 */
size_t telltale_format_value(double value, char *out, size_t out_size);

/** Canonical lowercase name of a reset reason, e.g. "panic". Never NULL. */
const char *telltale_reset_reason_name(telltale_reset_reason_t reason);

/** True when the reason indicates a fault rather than an intended restart. */
bool telltale_reset_reason_is_fault(telltale_reset_reason_t reason);

/**
 * Heap fragmentation as a 0..1 ratio: `1 - largest_free_block / free_bytes`.
 *
 * 0 means the free heap is one contiguous block; 0.9 means the largest single
 * allocation that can succeed is a tenth of the free space, which is the
 * failure mode that looks like "plenty of RAM" right up to the malloc that
 * returns NULL. Returns 0 when `free_bytes` is 0 (nothing free is not
 * fragmented), and clamps to 0..1 against inconsistent inputs.
 */
double telltale_heap_fragmentation(size_t free_bytes, size_t largest_free_block);

/* --- Serialisers -------------------------------------------------------- */

/**
 * Renders the registry as Prometheus text exposition (version 0.0.4).
 *
 * Emits `# HELP` and `# TYPE` lines per metric, escapes label values, and
 * ends with a trailing newline. Follows the snprintf contract, so a buffer
 * too small yields a truncated-but-terminated string and a return value
 * larger than `out_size`.
 */
size_t telltale_core_render_prometheus(const telltale_registry_t *registry, char *out,
                                       size_t out_size);

/**
 * Renders the registry as an OTLP/HTTP JSON export request.
 *
 * Gauges become `gauge`, counters become a monotonic cumulative `sum`, and
 * info metrics become gauges with a value of 1 — the wire shape an OTel
 * collector accepts on `/v1/metrics`. `unix_time_nanos` is the observation
 * timestamp; pass 0 when the device has no wall clock and the collector will
 * stamp arrival time instead.
 */
size_t telltale_core_render_otlp_json(const telltale_registry_t *registry,
                                      uint64_t unix_time_nanos, char *out, size_t out_size);

/** Escapes a string for a JSON string literal. Follows the snprintf contract. */
size_t telltale_escape_json(const char *input, char *out, size_t out_size);

/** Escapes a string for a Prometheus label value. Follows the snprintf contract. */
size_t telltale_escape_prometheus_label(const char *input, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* TELLTALE_CORE_H */
