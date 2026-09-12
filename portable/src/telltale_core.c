/*
 * telltale — portable core implementation.
 *
 * Copyright (c) 2026 Opsvex SpA. SPDX-License-Identifier: Apache-2.0
 */

#include "telltale_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Small helpers ------------------------------------------------------ */

/** Copies at most `size - 1` bytes and always terminates. */
static void copy_bounded(char *dst, size_t size, const char *src) {
    if (size == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < size && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/**
 * Appends to an output buffer while tracking how much *would* have been
 * written. This is the whole truncation-safety story: every serialiser
 * accumulates through here, so a short buffer produces a terminated string
 * and a return value the caller can compare against the buffer size.
 */
typedef struct {
    char *out;
    size_t size;
    size_t written; /* bytes that would have been written, excluding NUL */
} appender_t;

static void append_str(appender_t *a, const char *text) {
    if (text == NULL) {
        return;
    }
    size_t len = strlen(text);
    if (a->out != NULL && a->written < a->size) {
        size_t room = a->size - a->written - 1; /* keep space for NUL */
        size_t n = len < room ? len : room;
        memcpy(a->out + a->written, text, n);
        a->out[a->written + n] = '\0';
    }
    a->written += len;
}

static void append_char(appender_t *a, char c) {
    const char buf[2] = {c, '\0'};
    append_str(a, buf);
}

static void append_double(appender_t *a, double value) {
    char buf[32];
    telltale_format_value(value, buf, sizeof(buf));
    append_str(a, buf);
}

static void append_u64(appender_t *a, uint64_t value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    append_str(a, buf);
}

static bool labels_equal(const telltale_metric_t *metric, const telltale_label_t *labels,
                         size_t label_count) {
    if (metric->label_count != label_count) {
        return false;
    }
    for (size_t i = 0; i < label_count; ++i) {
        if (strcmp(metric->labels[i].key, labels[i].key) != 0 ||
            strcmp(metric->labels[i].value, labels[i].value) != 0) {
            return false;
        }
    }
    return true;
}

/* --- Registry ----------------------------------------------------------- */

void telltale_registry_init(telltale_registry_t *registry, const char *device_id,
                            const char *service_name) {
    if (registry == NULL) {
        return;
    }
    memset(registry, 0, sizeof(*registry));
    copy_bounded(registry->device_id, sizeof(registry->device_id), device_id);
    copy_bounded(registry->service_name, sizeof(registry->service_name),
                 service_name != NULL ? service_name : "telltale");
}

void telltale_registry_set_build(telltale_registry_t *registry, const char *firmware_version,
                                 const char *hardware_model) {
    if (registry == NULL) {
        return;
    }
    copy_bounded(registry->firmware_version, sizeof(registry->firmware_version), firmware_version);
    copy_bounded(registry->hardware_model, sizeof(registry->hardware_model), hardware_model);
}

size_t telltale_registry_count(const telltale_registry_t *registry) {
    return registry == NULL ? 0 : registry->count;
}

telltale_result_t telltale_set_labeled(telltale_registry_t *registry, const char *name,
                                       telltale_metric_type_t type, telltale_unit_t unit,
                                       const char *help, double value,
                                       const telltale_label_t *labels, size_t label_count) {
    if (registry == NULL || name == NULL || name[0] == '\0') {
        return TELLTALE_ERR_INVALID_ARG;
    }
    if (strlen(name) >= TELLTALE_MAX_NAME_LEN) {
        return TELLTALE_ERR_NAME_TOO_LONG;
    }
    if (label_count > TELLTALE_MAX_LABELS) {
        return TELLTALE_ERR_TOO_MANY_LABELS;
    }
    if (label_count > 0 && labels == NULL) {
        return TELLTALE_ERR_INVALID_ARG;
    }

    /* Update in place when the same name+labels is already registered, so a
     * collector running every cycle does not consume a slot per cycle. */
    for (size_t i = 0; i < registry->count; ++i) {
        telltale_metric_t *metric = &registry->metrics[i];
        if (metric->used && strcmp(metric->name, name) == 0 &&
            labels_equal(metric, labels, label_count)) {
            metric->value = value;
            metric->type = type;
            metric->unit = unit;
            if (help != NULL) {
                metric->help = help;
            }
            return TELLTALE_OK;
        }
    }

    if (registry->count >= TELLTALE_MAX_METRICS) {
        return TELLTALE_ERR_NO_SPACE;
    }

    telltale_metric_t *metric = &registry->metrics[registry->count];
    memset(metric, 0, sizeof(*metric));
    copy_bounded(metric->name, sizeof(metric->name), name);
    metric->type = type;
    metric->unit = unit;
    metric->help = help;
    metric->value = value;
    metric->label_count = (uint8_t)label_count;
    for (size_t i = 0; i < label_count; ++i) {
        copy_bounded(metric->labels[i].key, sizeof(metric->labels[i].key), labels[i].key);
        copy_bounded(metric->labels[i].value, sizeof(metric->labels[i].value), labels[i].value);
    }
    metric->used = true;
    registry->count += 1;
    return TELLTALE_OK;
}

telltale_result_t telltale_set(telltale_registry_t *registry, const char *name,
                               telltale_metric_type_t type, telltale_unit_t unit,
                               const char *help, double value) {
    return telltale_set_labeled(registry, name, type, unit, help, value, NULL, 0);
}

telltale_result_t telltale_increment(telltale_registry_t *registry, const char *name,
                                     const char *help, double delta) {
    if (registry == NULL) {
        return TELLTALE_ERR_INVALID_ARG;
    }
    const telltale_metric_t *existing = telltale_find(registry, name);
    double base = existing != NULL ? existing->value : 0.0;
    return telltale_set(registry, name, TELLTALE_COUNTER, TELLTALE_UNIT_NONE, help, base + delta);
}

const telltale_metric_t *telltale_find(const telltale_registry_t *registry, const char *name) {
    if (registry == NULL || name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < registry->count; ++i) {
        const telltale_metric_t *metric = &registry->metrics[i];
        if (metric->used && strcmp(metric->name, name) == 0) {
            return metric;
        }
    }
    return NULL;
}

const telltale_metric_t *telltale_find_labeled(const telltale_registry_t *registry,
                                               const char *name, const telltale_label_t *labels,
                                               size_t label_count) {
    if (registry == NULL || name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < registry->count; ++i) {
        const telltale_metric_t *metric = &registry->metrics[i];
        if (metric->used && strcmp(metric->name, name) == 0 &&
            labels_equal(metric, labels, label_count)) {
            return metric;
        }
    }
    return NULL;
}

/* --- Naming and formatting ---------------------------------------------- */

static const char *unit_prometheus_suffix(telltale_unit_t unit) {
    switch (unit) {
        case TELLTALE_UNIT_BYTES:
            return "_bytes";
        case TELLTALE_UNIT_SECONDS:
            return "_seconds";
        case TELLTALE_UNIT_RATIO:
            return "_ratio";
        case TELLTALE_UNIT_DBM:
            return "_dbm";
        case TELLTALE_UNIT_CELSIUS:
            return "_celsius";
        case TELLTALE_UNIT_VOLTS:
            return "_volts";
        case TELLTALE_UNIT_NONE:
        default:
            return "";
    }
}

const char *telltale_unit_otel(telltale_unit_t unit) {
    switch (unit) {
        case TELLTALE_UNIT_BYTES:
            return "By";
        case TELLTALE_UNIT_SECONDS:
            return "s";
        case TELLTALE_UNIT_RATIO:
            return "1";
        case TELLTALE_UNIT_DBM:
            return "dBm";
        case TELLTALE_UNIT_CELSIUS:
            return "Cel";
        case TELLTALE_UNIT_VOLTS:
            return "V";
        case TELLTALE_UNIT_NONE:
        default:
            return "";
    }
}

static bool is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == ':';
}

size_t telltale_prometheus_name(const char *name, telltale_unit_t unit,
                                telltale_metric_type_t type, char *out, size_t out_size) {
    appender_t a = {out, out_size, 0};
    if (name == NULL) {
        append_str(&a, "");
        return a.written;
    }

    /* A Prometheus metric name may not start with a digit. */
    if (name[0] >= '0' && name[0] <= '9') {
        append_char(&a, '_');
    }

    for (size_t i = 0; name[i] != '\0'; ++i) {
        char c = name[i];
        append_char(&a, is_name_char(c) ? c : '_');
    }

    const char *suffix = unit_prometheus_suffix(unit);
    /* Do not double a suffix the caller already wrote into the name. */
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    bool already_suffixed = suffix_len > 0 && name_len >= suffix_len &&
                            strcmp(name + name_len - suffix_len, suffix) == 0;
    if (!already_suffixed) {
        append_str(&a, suffix);
    }

    if (type == TELLTALE_COUNTER) {
        append_str(&a, "_total");
    } else if (type == TELLTALE_INFO) {
        append_str(&a, "_info");
    }

    return a.written;
}

size_t telltale_format_value(double value, char *out, size_t out_size) {
    appender_t a = {out, out_size, 0};

    if (isnan(value)) {
        append_str(&a, "NaN");
        return a.written;
    }
    if (isinf(value)) {
        append_str(&a, value > 0 ? "+Inf" : "-Inf");
        return a.written;
    }

    char buf[32];
    /* 2^53 is where a double stops representing every integer exactly; below
     * it, an integral value is printed as an integer because that is what
     * every exposition example shows and what a human reviewer expects. */
    if (value == floor(value) && fabs(value) < 9007199254740992.0) {
        snprintf(buf, sizeof(buf), "%.0f", value);
    } else {
        snprintf(buf, sizeof(buf), "%.9g", value);
    }
    append_str(&a, buf);
    return a.written;
}

const char *telltale_reset_reason_name(telltale_reset_reason_t reason) {
    switch (reason) {
        case TELLTALE_RESET_POWER_ON:
            return "power_on";
        case TELLTALE_RESET_EXTERNAL:
            return "external";
        case TELLTALE_RESET_SOFTWARE:
            return "software";
        case TELLTALE_RESET_PANIC:
            return "panic";
        case TELLTALE_RESET_INT_WATCHDOG:
            return "interrupt_watchdog";
        case TELLTALE_RESET_TASK_WATCHDOG:
            return "task_watchdog";
        case TELLTALE_RESET_OTHER_WATCHDOG:
            return "other_watchdog";
        case TELLTALE_RESET_DEEP_SLEEP:
            return "deep_sleep";
        case TELLTALE_RESET_BROWNOUT:
            return "brownout";
        case TELLTALE_RESET_SDIO:
            return "sdio";
        case TELLTALE_RESET_USB:
            return "usb";
        case TELLTALE_RESET_JTAG:
            return "jtag";
        case TELLTALE_RESET_UNKNOWN:
        default:
            return "unknown";
    }
}

bool telltale_reset_reason_is_fault(telltale_reset_reason_t reason) {
    switch (reason) {
        case TELLTALE_RESET_PANIC:
        case TELLTALE_RESET_INT_WATCHDOG:
        case TELLTALE_RESET_TASK_WATCHDOG:
        case TELLTALE_RESET_OTHER_WATCHDOG:
        case TELLTALE_RESET_BROWNOUT:
            return true;
        default:
            return false;
    }
}

double telltale_heap_fragmentation(size_t free_bytes, size_t largest_free_block) {
    if (free_bytes == 0) {
        return 0.0;
    }
    if (largest_free_block >= free_bytes) {
        return 0.0;
    }
    double ratio = 1.0 - ((double)largest_free_block / (double)free_bytes);
    if (ratio < 0.0) {
        return 0.0;
    }
    if (ratio > 1.0) {
        return 1.0;
    }
    return ratio;
}

/* --- Escaping ----------------------------------------------------------- */

size_t telltale_escape_json(const char *input, char *out, size_t out_size) {
    appender_t a = {out, out_size, 0};
    if (input == NULL) {
        append_str(&a, "");
        return a.written;
    }
    for (size_t i = 0; input[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)input[i];
        switch (c) {
            case '"':
                append_str(&a, "\\\"");
                break;
            case '\\':
                append_str(&a, "\\\\");
                break;
            case '\n':
                append_str(&a, "\\n");
                break;
            case '\r':
                append_str(&a, "\\r");
                break;
            case '\t':
                append_str(&a, "\\t");
                break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    /* Control characters must be escaped to keep the JSON
                     * valid; a device that puts a NUL in its board name should
                     * not break the export for the whole fleet. */
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    append_str(&a, buf);
                } else {
                    append_char(&a, (char)c);
                }
                break;
        }
    }
    return a.written;
}

size_t telltale_escape_prometheus_label(const char *input, char *out, size_t out_size) {
    appender_t a = {out, out_size, 0};
    if (input == NULL) {
        append_str(&a, "");
        return a.written;
    }
    for (size_t i = 0; input[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)input[i];
        switch (c) {
            case '"':
                append_str(&a, "\\\"");
                break;
            case '\\':
                append_str(&a, "\\\\");
                break;
            case '\n':
                append_str(&a, "\\n");
                break;
            default:
                /* The exposition format has no escape for other control
                 * characters, so they are dropped rather than emitted raw. */
                if (c >= 0x20 && c != 0x7f) {
                    append_char(&a, (char)c);
                }
                break;
        }
    }
    return a.written;
}

/* --- Prometheus serialiser ---------------------------------------------- */

static void append_prometheus_labels(appender_t *a, const telltale_registry_t *registry,
                                     const telltale_metric_t *metric) {
    char escaped[TELLTALE_MAX_LABEL_VALUE_LEN * 2 + 4];
    bool first = true;

    append_char(a, '{');

    if (registry->device_id[0] != '\0') {
        telltale_escape_prometheus_label(registry->device_id, escaped, sizeof(escaped));
        append_str(a, "device_id=\"");
        append_str(a, escaped);
        append_char(a, '"');
        first = false;
    }

    for (size_t i = 0; i < (size_t)metric->label_count; ++i) {
        if (!first) {
            append_char(a, ',');
        }
        append_str(a, metric->labels[i].key);
        append_str(a, "=\"");
        telltale_escape_prometheus_label(metric->labels[i].value, escaped, sizeof(escaped));
        append_str(a, escaped);
        append_char(a, '"');
        first = false;
    }

    append_char(a, '}');
}

size_t telltale_core_render_prometheus(const telltale_registry_t *registry, char *out,
                                       size_t out_size) {
    appender_t a = {out, out_size, 0};
    if (registry == NULL) {
        append_str(&a, "");
        return a.written;
    }

    char name[TELLTALE_MAX_NAME_LEN * 2];

    for (size_t i = 0; i < registry->count; ++i) {
        const telltale_metric_t *metric = &registry->metrics[i];
        if (!metric->used) {
            continue;
        }

        telltale_prometheus_name(metric->name, metric->unit, metric->type, name, sizeof(name));

        if (metric->help != NULL && metric->help[0] != '\0') {
            append_str(&a, "# HELP ");
            append_str(&a, name);
            append_char(&a, ' ');
            append_str(&a, metric->help);
            append_char(&a, '\n');
        }

        append_str(&a, "# TYPE ");
        append_str(&a, name);
        append_char(&a, ' ');
        append_str(&a, metric->type == TELLTALE_COUNTER ? "counter" : "gauge");
        append_char(&a, '\n');

        append_str(&a, name);
        append_prometheus_labels(&a, registry, metric);
        append_char(&a, ' ');
        append_double(&a, metric->type == TELLTALE_INFO ? 1.0 : metric->value);
        append_char(&a, '\n');
    }

    return a.written;
}

/* --- OTLP/HTTP JSON serialiser ------------------------------------------ */

static void append_json_attribute(appender_t *a, const char *key, const char *value,
                                  bool *first) {
    if (value == NULL || value[0] == '\0') {
        return;
    }
    char escaped[TELLTALE_MAX_LABEL_VALUE_LEN * 2 + 4];
    if (!*first) {
        append_char(a, ',');
    }
    telltale_escape_json(value, escaped, sizeof(escaped));
    append_str(a, "{\"key\":\"");
    append_str(a, key);
    append_str(a, "\",\"value\":{\"stringValue\":\"");
    append_str(a, escaped);
    append_str(a, "\"}}");
    *first = false;
}

size_t telltale_core_render_otlp_json(const telltale_registry_t *registry,
                                      uint64_t unix_time_nanos, char *out, size_t out_size) {
    appender_t a = {out, out_size, 0};
    if (registry == NULL) {
        append_str(&a, "");
        return a.written;
    }

    append_str(&a, "{\"resourceMetrics\":[{\"resource\":{\"attributes\":[");
    bool first_attribute = true;
    append_json_attribute(&a, "service.name", registry->service_name, &first_attribute);
    append_json_attribute(&a, "device.id", registry->device_id, &first_attribute);
    append_json_attribute(&a, "service.version", registry->firmware_version, &first_attribute);
    append_json_attribute(&a, "device.model.identifier", registry->hardware_model,
                          &first_attribute);
    append_str(&a, "]},\"scopeMetrics\":[{\"scope\":{\"name\":\"telltale\",\"version\":\"");
    append_str(&a, TELLTALE_CORE_VERSION);
    append_str(&a, "\"},\"metrics\":[");

    bool first_metric = true;
    for (size_t i = 0; i < registry->count; ++i) {
        const telltale_metric_t *metric = &registry->metrics[i];
        if (!metric->used) {
            continue;
        }
        if (!first_metric) {
            append_char(&a, ',');
        }
        first_metric = false;

        char escaped_name[TELLTALE_MAX_NAME_LEN * 2];
        telltale_escape_json(metric->name, escaped_name, sizeof(escaped_name));

        append_str(&a, "{\"name\":\"");
        append_str(&a, escaped_name);
        append_str(&a, "\",\"unit\":\"");
        append_str(&a, telltale_unit_otel(metric->unit));
        append_str(&a, "\"");

        if (metric->help != NULL && metric->help[0] != '\0') {
            char escaped_help[192];
            telltale_escape_json(metric->help, escaped_help, sizeof(escaped_help));
            append_str(&a, ",\"description\":\"");
            append_str(&a, escaped_help);
            append_str(&a, "\"");
        }

        /* A counter is a cumulative, monotonic sum; aggregationTemporality 2
         * is AGGREGATION_TEMPORALITY_CUMULATIVE in the OTLP enum. */
        const bool is_sum = metric->type == TELLTALE_COUNTER;
        append_str(&a, is_sum ? ",\"sum\":{\"dataPoints\":[" : ",\"gauge\":{\"dataPoints\":[");

        append_str(&a, "{\"asDouble\":");
        append_double(&a, metric->type == TELLTALE_INFO ? 1.0 : metric->value);
        append_str(&a, ",\"timeUnixNano\":\"");
        append_u64(&a, unix_time_nanos);
        append_str(&a, "\"");

        if (metric->label_count > 0) {
            append_str(&a, ",\"attributes\":[");
            bool first_label = true;
            for (size_t j = 0; j < (size_t)metric->label_count; ++j) {
                append_json_attribute(&a, metric->labels[j].key, metric->labels[j].value,
                                      &first_label);
            }
            append_char(&a, ']');
        }

        append_str(&a, "}]");
        if (is_sum) {
            append_str(&a, ",\"aggregationTemporality\":2,\"isMonotonic\":true");
        }
        append_str(&a, "}}");
    }

    append_str(&a, "]}]}]}");
    return a.written;
}
