/*
 * Host tests for the telltale portable core.
 *
 * These cover the parts that are easy to get wrong and impossible to notice
 * on a device: Prometheus naming rules, label escaping, float formatting,
 * buffer truncation, and the registry's idempotence.
 *
 * Copyright (c) 2026 Opsvex SpA. SPDX-License-Identifier: Apache-2.0
 */

#include "telltale_core.h"
#include "tinytest.h"

static char buffer[8192];

static telltale_registry_t make_registry(void) {
    telltale_registry_t registry;
    telltale_registry_init(&registry, "esp32-ab12cd", "gateway");
    return registry;
}

/* --- Registry ----------------------------------------------------------- */

TT_TEST(registry_starts_empty) {
    telltale_registry_t registry = make_registry();
    TT_ASSERT_EQ_INT(0, telltale_registry_count(&registry));
    TT_ASSERT(telltale_find(&registry, "device.heap.free") == NULL);
}

TT_TEST(registry_stores_resource_attributes) {
    telltale_registry_t registry = make_registry();
    telltale_registry_set_build(&registry, "1.4.2", "esp32-devkit-v1");
    TT_ASSERT_STR_EQ("esp32-ab12cd", registry.device_id);
    TT_ASSERT_STR_EQ("gateway", registry.service_name);
    TT_ASSERT_STR_EQ("1.4.2", registry.firmware_version);
    TT_ASSERT_STR_EQ("esp32-devkit-v1", registry.hardware_model);
}

TT_TEST(registry_defaults_service_name) {
    telltale_registry_t registry;
    telltale_registry_init(&registry, "dev-1", NULL);
    TT_ASSERT_STR_EQ("telltale", registry.service_name);
}

TT_TEST(registry_truncates_an_overlong_resource_value) {
    telltale_registry_t registry;
    telltale_registry_init(&registry,
                           "a-device-identifier-far-longer-than-the-fixed-buffer-allows", "svc");
    /* Truncated, never overflowed, and always terminated. */
    TT_ASSERT_EQ_INT(TELLTALE_MAX_RESOURCE_LEN - 1, (int)strlen(registry.device_id));
}

TT_TEST(set_registers_a_metric) {
    telltale_registry_t registry = make_registry();
    TT_ASSERT_EQ_INT(TELLTALE_OK, telltale_set(&registry, "device.heap.free", TELLTALE_GAUGE,
                                               TELLTALE_UNIT_BYTES, "Free heap", 4096));
    TT_ASSERT_EQ_INT(1, telltale_registry_count(&registry));

    const telltale_metric_t *metric = telltale_find(&registry, "device.heap.free");
    TT_ASSERT(metric != NULL);
    TT_ASSERT_NEAR(4096, metric->value, 0.0001);
    TT_ASSERT_EQ_INT(TELLTALE_UNIT_BYTES, metric->unit);
}

TT_TEST(set_is_idempotent_so_a_collector_cannot_leak_slots) {
    telltale_registry_t registry = make_registry();
    for (int i = 0; i < 100; ++i) {
        telltale_set(&registry, "device.heap.free", TELLTALE_GAUGE, TELLTALE_UNIT_BYTES, NULL, i);
    }
    TT_ASSERT_EQ_INT(1, telltale_registry_count(&registry));
    TT_ASSERT_NEAR(99, telltale_find(&registry, "device.heap.free")->value, 0.0001);
}

TT_TEST(labels_are_part_of_identity) {
    telltale_registry_t registry = make_registry();
    telltale_label_t panic[] = {{"reason", "panic"}};
    telltale_label_t brownout[] = {{"reason", "brownout"}};

    telltale_set_labeled(&registry, "device.reset", TELLTALE_INFO, TELLTALE_UNIT_NONE, NULL, 1,
                         panic, 1);
    telltale_set_labeled(&registry, "device.reset", TELLTALE_INFO, TELLTALE_UNIT_NONE, NULL, 1,
                         brownout, 1);

    /* Same name, different labels: two distinct series. */
    TT_ASSERT_EQ_INT(2, telltale_registry_count(&registry));
    TT_ASSERT(telltale_find_labeled(&registry, "device.reset", panic, 1) != NULL);
    TT_ASSERT(telltale_find_labeled(&registry, "device.reset", brownout, 1) != NULL);
}

TT_TEST(increment_creates_then_accumulates) {
    telltale_registry_t registry = make_registry();
    telltale_increment(&registry, "link.disconnect.count", "Disconnects", 1);
    telltale_increment(&registry, "link.disconnect.count", NULL, 2);
    TT_ASSERT_EQ_INT(1, telltale_registry_count(&registry));
    TT_ASSERT_NEAR(3, telltale_find(&registry, "link.disconnect.count")->value, 0.0001);
}

TT_TEST(registry_refuses_to_overflow) {
    telltale_registry_t registry = make_registry();
    char name[TELLTALE_MAX_NAME_LEN];
    for (int i = 0; i < TELLTALE_MAX_METRICS; ++i) {
        snprintf(name, sizeof(name), "device.metric.%d", i);
        TT_ASSERT_EQ_INT(TELLTALE_OK,
                         telltale_set(&registry, name, TELLTALE_GAUGE, TELLTALE_UNIT_NONE, NULL, i));
    }
    TT_ASSERT_EQ_INT(TELLTALE_ERR_NO_SPACE, telltale_set(&registry, "device.one.too.many",
                                                         TELLTALE_GAUGE, TELLTALE_UNIT_NONE, NULL, 1));
    TT_ASSERT_EQ_INT(TELLTALE_MAX_METRICS, (int)telltale_registry_count(&registry));
}

TT_TEST(registry_rejects_bad_arguments) {
    telltale_registry_t registry = make_registry();
    char long_name[TELLTALE_MAX_NAME_LEN + 8];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';

    TT_ASSERT_EQ_INT(TELLTALE_ERR_INVALID_ARG,
                     telltale_set(&registry, NULL, TELLTALE_GAUGE, TELLTALE_UNIT_NONE, NULL, 1));
    TT_ASSERT_EQ_INT(TELLTALE_ERR_INVALID_ARG,
                     telltale_set(&registry, "", TELLTALE_GAUGE, TELLTALE_UNIT_NONE, NULL, 1));
    TT_ASSERT_EQ_INT(TELLTALE_ERR_NAME_TOO_LONG,
                     telltale_set(&registry, long_name, TELLTALE_GAUGE, TELLTALE_UNIT_NONE, NULL, 1));
    TT_ASSERT_EQ_INT(TELLTALE_ERR_INVALID_ARG,
                     telltale_set(NULL, "device.x", TELLTALE_GAUGE, TELLTALE_UNIT_NONE, NULL, 1));
}

TT_TEST(registry_rejects_too_many_labels) {
    telltale_registry_t registry = make_registry();
    telltale_label_t labels[TELLTALE_MAX_LABELS + 1];
    memset(labels, 0, sizeof(labels));
    TT_ASSERT_EQ_INT(TELLTALE_ERR_TOO_MANY_LABELS,
                     telltale_set_labeled(&registry, "device.x", TELLTALE_GAUGE,
                                          TELLTALE_UNIT_NONE, NULL, 1, labels,
                                          TELLTALE_MAX_LABELS + 1));
}

/* --- Naming ------------------------------------------------------------- */

TT_TEST(prometheus_name_converts_dots_and_appends_unit) {
    telltale_prometheus_name("device.heap.free", TELLTALE_UNIT_BYTES, TELLTALE_GAUGE, buffer,
                             sizeof(buffer));
    TT_ASSERT_STR_EQ("device_heap_free_bytes", buffer);
}

TT_TEST(prometheus_name_suffixes_counters_with_total) {
    telltale_prometheus_name("device.boot.count", TELLTALE_UNIT_NONE, TELLTALE_COUNTER, buffer,
                             sizeof(buffer));
    TT_ASSERT_STR_EQ("device_boot_count_total", buffer);
}

TT_TEST(prometheus_name_suffixes_info_metrics) {
    telltale_prometheus_name("device.reset", TELLTALE_UNIT_NONE, TELLTALE_INFO, buffer,
                             sizeof(buffer));
    TT_ASSERT_STR_EQ("device_reset_info", buffer);
}

TT_TEST(prometheus_name_does_not_double_a_unit_suffix) {
    telltale_prometheus_name("device.heap.free_bytes", TELLTALE_UNIT_BYTES, TELLTALE_GAUGE, buffer,
                             sizeof(buffer));
    TT_ASSERT_STR_EQ("device_heap_free_bytes", buffer);
}

TT_TEST(prometheus_name_replaces_illegal_characters) {
    telltale_prometheus_name("device-link/quality!", TELLTALE_UNIT_NONE, TELLTALE_GAUGE, buffer,
                             sizeof(buffer));
    TT_ASSERT_STR_EQ("device_link_quality_", buffer);
}

TT_TEST(prometheus_name_cannot_start_with_a_digit) {
    telltale_prometheus_name("2fa.attempts", TELLTALE_UNIT_NONE, TELLTALE_GAUGE, buffer,
                             sizeof(buffer));
    TT_ASSERT_STR_EQ("_2fa_attempts", buffer);
}

TT_TEST(prometheus_name_reports_the_length_it_needed) {
    char small[8];
    size_t needed = telltale_prometheus_name("device.heap.free", TELLTALE_UNIT_BYTES,
                                             TELLTALE_GAUGE, small, sizeof(small));
    TT_ASSERT_EQ_INT((int)strlen("device_heap_free_bytes"), (int)needed);
    TT_ASSERT(needed >= sizeof(small)); /* the caller can detect truncation */
    TT_ASSERT_EQ_INT(7, (int)strlen(small)); /* and what is there is terminated */
}

TT_TEST(otel_units_use_the_ucum_spelling) {
    TT_ASSERT_STR_EQ("By", telltale_unit_otel(TELLTALE_UNIT_BYTES));
    TT_ASSERT_STR_EQ("s", telltale_unit_otel(TELLTALE_UNIT_SECONDS));
    TT_ASSERT_STR_EQ("1", telltale_unit_otel(TELLTALE_UNIT_RATIO));
    TT_ASSERT_STR_EQ("Cel", telltale_unit_otel(TELLTALE_UNIT_CELSIUS));
    TT_ASSERT_STR_EQ("", telltale_unit_otel(TELLTALE_UNIT_NONE));
}

/* --- Value formatting ---------------------------------------------------- */

TT_TEST(integral_values_print_without_a_decimal_point) {
    telltale_format_value(4096, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("4096", buffer);
    telltale_format_value(0, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("0", buffer);
    telltale_format_value(-71, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("-71", buffer);
}

TT_TEST(fractional_values_keep_enough_precision) {
    telltale_format_value(0.125, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("0.125", buffer);
    telltale_format_value(0.333333333, buffer, sizeof(buffer));
    TT_ASSERT_CONTAINS(buffer, "0.3333");
}

TT_TEST(non_finite_values_use_the_prometheus_spelling) {
    telltale_format_value(NAN, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("NaN", buffer);
    telltale_format_value(INFINITY, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("+Inf", buffer);
    telltale_format_value(-INFINITY, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("-Inf", buffer);
}

/* --- Reset reasons ------------------------------------------------------- */

TT_TEST(reset_reasons_have_stable_lowercase_names) {
    TT_ASSERT_STR_EQ("power_on", telltale_reset_reason_name(TELLTALE_RESET_POWER_ON));
    TT_ASSERT_STR_EQ("panic", telltale_reset_reason_name(TELLTALE_RESET_PANIC));
    TT_ASSERT_STR_EQ("task_watchdog", telltale_reset_reason_name(TELLTALE_RESET_TASK_WATCHDOG));
    TT_ASSERT_STR_EQ("brownout", telltale_reset_reason_name(TELLTALE_RESET_BROWNOUT));
    TT_ASSERT_STR_EQ("unknown", telltale_reset_reason_name(TELLTALE_RESET_UNKNOWN));
    TT_ASSERT_STR_EQ("unknown", telltale_reset_reason_name((telltale_reset_reason_t)999));
}

TT_TEST(faults_are_distinguished_from_intended_restarts) {
    TT_ASSERT(telltale_reset_reason_is_fault(TELLTALE_RESET_PANIC));
    TT_ASSERT(telltale_reset_reason_is_fault(TELLTALE_RESET_TASK_WATCHDOG));
    TT_ASSERT(telltale_reset_reason_is_fault(TELLTALE_RESET_BROWNOUT));
    /* A planned OTA reboot and a wake from deep sleep are not faults. */
    TT_ASSERT(!telltale_reset_reason_is_fault(TELLTALE_RESET_SOFTWARE));
    TT_ASSERT(!telltale_reset_reason_is_fault(TELLTALE_RESET_DEEP_SLEEP));
    TT_ASSERT(!telltale_reset_reason_is_fault(TELLTALE_RESET_POWER_ON));
}

/* --- Fragmentation ------------------------------------------------------- */

TT_TEST(fragmentation_is_zero_for_one_contiguous_block) {
    TT_ASSERT_NEAR(0.0, telltale_heap_fragmentation(10000, 10000), 0.0001);
}

TT_TEST(fragmentation_reports_the_dangerous_case) {
    /* 100 kB free but the biggest allocation that can succeed is 10 kB: the
     * failure mode that looks like plenty of RAM until malloc returns NULL. */
    TT_ASSERT_NEAR(0.9, telltale_heap_fragmentation(100000, 10000), 0.0001);
}

TT_TEST(fragmentation_handles_degenerate_inputs) {
    TT_ASSERT_NEAR(0.0, telltale_heap_fragmentation(0, 0), 0.0001);
    TT_ASSERT_NEAR(0.0, telltale_heap_fragmentation(0, 100), 0.0001);
    /* Largest block bigger than free space is inconsistent, not 'negative'. */
    TT_ASSERT_NEAR(0.0, telltale_heap_fragmentation(100, 200), 0.0001);
}

/* --- Escaping ------------------------------------------------------------ */

TT_TEST(json_escaping_covers_quotes_backslashes_and_controls) {
    telltale_escape_json("say \"hi\"\\ now\nthen", buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("say \\\"hi\\\"\\\\ now\\nthen", buffer);
}

TT_TEST(json_escaping_encodes_other_control_characters) {
    const char input[] = {'a', 0x01, 'b', '\0'};
    telltale_escape_json(input, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("a\\u0001b", buffer);
}

TT_TEST(prometheus_label_escaping_follows_the_exposition_rules) {
    telltale_escape_prometheus_label("a\"b\\c\nd", buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("a\\\"b\\\\c\\nd", buffer);
}

TT_TEST(prometheus_label_escaping_drops_unescapable_controls) {
    const char input[] = {'a', 0x07, 'b', '\0'};
    telltale_escape_prometheus_label(input, buffer, sizeof(buffer));
    TT_ASSERT_STR_EQ("ab", buffer);
}

/* --- Prometheus rendering ------------------------------------------------ */

static telltale_registry_t populated_registry(void) {
    telltale_registry_t registry = make_registry();
    telltale_registry_set_build(&registry, "1.4.2", "esp32-devkit-v1");
    telltale_set(&registry, "device.heap.free", TELLTALE_GAUGE, TELLTALE_UNIT_BYTES,
                 "Free heap in bytes", 41264);
    telltale_set(&registry, "device.heap.fragmentation", TELLTALE_GAUGE, TELLTALE_UNIT_RATIO,
                 "Heap fragmentation, 0 to 1", 0.25);
    telltale_set(&registry, "device.uptime", TELLTALE_GAUGE, TELLTALE_UNIT_SECONDS,
                 "Seconds since boot", 3600);
    telltale_increment(&registry, "device.boot.count", "Boots since first provisioning", 7);
    telltale_set(&registry, "link.rssi", TELLTALE_GAUGE, TELLTALE_UNIT_DBM, "Wi-Fi RSSI", -67);

    telltale_label_t reason[] = {{"reason", "task_watchdog"}};
    telltale_set_labeled(&registry, "device.reset", TELLTALE_INFO, TELLTALE_UNIT_NONE,
                         "Cause of the last reset", 1, reason, 1);
    return registry;
}

TT_TEST(prometheus_output_has_help_type_and_sample_lines) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_prometheus(&registry, buffer, sizeof(buffer));

    TT_ASSERT_CONTAINS(buffer, "# HELP device_heap_free_bytes Free heap in bytes\n");
    TT_ASSERT_CONTAINS(buffer, "# TYPE device_heap_free_bytes gauge\n");
    TT_ASSERT_CONTAINS(buffer, "device_heap_free_bytes{device_id=\"esp32-ab12cd\"} 41264\n");
}

TT_TEST(prometheus_output_marks_counters_as_counters) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_prometheus(&registry, buffer, sizeof(buffer));
    TT_ASSERT_CONTAINS(buffer, "# TYPE device_boot_count_total counter\n");
    TT_ASSERT_CONTAINS(buffer, "device_boot_count_total{device_id=\"esp32-ab12cd\"} 7\n");
}

TT_TEST(prometheus_output_puts_the_reset_reason_in_a_label) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_prometheus(&registry, buffer, sizeof(buffer));
    /* A string cannot be a metric value: the reason rides as a label on a 1. */
    TT_ASSERT_CONTAINS(buffer,
                       "device_reset_info{device_id=\"esp32-ab12cd\",reason=\"task_watchdog\"} 1\n");
}

TT_TEST(prometheus_output_keeps_negative_and_fractional_values_readable) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_prometheus(&registry, buffer, sizeof(buffer));
    TT_ASSERT_CONTAINS(buffer, "link_rssi_dbm{device_id=\"esp32-ab12cd\"} -67\n");
    TT_ASSERT_CONTAINS(buffer, "device_heap_fragmentation_ratio{device_id=\"esp32-ab12cd\"} 0.25\n");
}

TT_TEST(prometheus_output_is_truncation_safe) {
    telltale_registry_t registry = populated_registry();
    char small[64];
    size_t needed = telltale_core_render_prometheus(&registry, small, sizeof(small));

    TT_ASSERT(needed > sizeof(small));          /* caller can detect truncation */
    TT_ASSERT(strlen(small) < sizeof(small));   /* and the buffer is terminated */
}

TT_TEST(prometheus_rendering_survives_a_null_registry) {
    size_t needed = telltale_core_render_prometheus(NULL, buffer, sizeof(buffer));
    TT_ASSERT_EQ_INT(0, (int)needed);
}

TT_TEST(prometheus_rendering_accepts_a_null_buffer_for_sizing) {
    telltale_registry_t registry = populated_registry();
    size_t needed = telltale_core_render_prometheus(&registry, NULL, 0);
    TT_ASSERT(needed > 100);
}

/* --- OTLP rendering ------------------------------------------------------ */

TT_TEST(otlp_output_carries_resource_attributes) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_otlp_json(&registry, 1789000000000000000ULL, buffer, sizeof(buffer));

    TT_ASSERT_CONTAINS(buffer, "\"resourceMetrics\":[{\"resource\":{\"attributes\":[");
    TT_ASSERT_CONTAINS(buffer, "{\"key\":\"service.name\",\"value\":{\"stringValue\":\"gateway\"}}");
    TT_ASSERT_CONTAINS(buffer, "{\"key\":\"device.id\",\"value\":{\"stringValue\":\"esp32-ab12cd\"}}");
    TT_ASSERT_CONTAINS(buffer, "{\"key\":\"service.version\",\"value\":{\"stringValue\":\"1.4.2\"}}");
}

TT_TEST(otlp_output_names_the_instrumentation_scope) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_otlp_json(&registry, 0, buffer, sizeof(buffer));
    TT_ASSERT_CONTAINS(buffer, "\"scope\":{\"name\":\"telltale\",\"version\":\"" TELLTALE_CORE_VERSION "\"}");
}

TT_TEST(otlp_gauges_use_the_gauge_shape_with_ucum_units) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_otlp_json(&registry, 1789000000000000000ULL, buffer, sizeof(buffer));

    TT_ASSERT_CONTAINS(buffer, "{\"name\":\"device.heap.free\",\"unit\":\"By\"");
    TT_ASSERT_CONTAINS(buffer, "\"gauge\":{\"dataPoints\":[{\"asDouble\":41264,"
                               "\"timeUnixNano\":\"1789000000000000000\"}]}");
}

TT_TEST(otlp_counters_are_monotonic_cumulative_sums) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_otlp_json(&registry, 0, buffer, sizeof(buffer));
    TT_ASSERT_CONTAINS(buffer, "\"sum\":{\"dataPoints\":");
    TT_ASSERT_CONTAINS(buffer, "\"aggregationTemporality\":2,\"isMonotonic\":true");
}

TT_TEST(otlp_labels_become_datapoint_attributes) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_otlp_json(&registry, 0, buffer, sizeof(buffer));
    TT_ASSERT_CONTAINS(buffer, "\"attributes\":[{\"key\":\"reason\","
                               "\"value\":{\"stringValue\":\"task_watchdog\"}}]");
}

TT_TEST(otlp_output_is_balanced_json) {
    telltale_registry_t registry = populated_registry();
    telltale_core_render_otlp_json(&registry, 0, buffer, sizeof(buffer));

    int braces = 0;
    int brackets = 0;
    int quotes = 0;
    for (size_t i = 0; buffer[i] != '\0'; ++i) {
        if (buffer[i] == '"' && (i == 0 || buffer[i - 1] != '\\')) {
            quotes++;
        }
        if (quotes % 2 != 0) {
            continue; /* inside a string literal */
        }
        if (buffer[i] == '{') braces++;
        if (buffer[i] == '}') braces--;
        if (buffer[i] == '[') brackets++;
        if (buffer[i] == ']') brackets--;
        TT_ASSERT(braces >= 0);
        TT_ASSERT(brackets >= 0);
    }
    TT_ASSERT_EQ_INT(0, braces);
    TT_ASSERT_EQ_INT(0, brackets);
    TT_ASSERT_EQ_INT(0, quotes % 2);
}

TT_TEST(otlp_output_omits_empty_resource_attributes) {
    telltale_registry_t registry = make_registry(); /* no build info set */
    telltale_set(&registry, "device.uptime", TELLTALE_GAUGE, TELLTALE_UNIT_SECONDS, NULL, 10);
    telltale_core_render_otlp_json(&registry, 0, buffer, sizeof(buffer));

    TT_ASSERT_NOT_CONTAINS(buffer, "service.version");
    TT_ASSERT_NOT_CONTAINS(buffer, ",,");
    TT_ASSERT_CONTAINS(buffer, "\"device.id\"");
}

TT_TEST(otlp_output_is_truncation_safe) {
    telltale_registry_t registry = populated_registry();
    char small[80];
    size_t needed = telltale_core_render_otlp_json(&registry, 0, small, sizeof(small));
    TT_ASSERT(needed > sizeof(small));
    TT_ASSERT(strlen(small) < sizeof(small));
}

TT_TEST(hostile_strings_cannot_break_either_format) {
    telltale_registry_t registry;
    telltale_registry_init(&registry, "dev\"1\\x", "svc");
    telltale_label_t labels[] = {{"reason", "a\"b\\c"}};
    telltale_set_labeled(&registry, "device.reset", TELLTALE_INFO, TELLTALE_UNIT_NONE, NULL, 1,
                         labels, 1);

    telltale_core_render_prometheus(&registry, buffer, sizeof(buffer));
    TT_ASSERT_CONTAINS(buffer, "device_id=\"dev\\\"1\\\\x\"");
    TT_ASSERT_CONTAINS(buffer, "reason=\"a\\\"b\\\\c\"");

    telltale_core_render_otlp_json(&registry, 0, buffer, sizeof(buffer));
    TT_ASSERT_CONTAINS(buffer, "\"stringValue\":\"dev\\\"1\\\\x\"");
}

int main(void) {
    printf("telltale portable core\n");

    TT_RUN(registry_starts_empty);
    TT_RUN(registry_stores_resource_attributes);
    TT_RUN(registry_defaults_service_name);
    TT_RUN(registry_truncates_an_overlong_resource_value);
    TT_RUN(set_registers_a_metric);
    TT_RUN(set_is_idempotent_so_a_collector_cannot_leak_slots);
    TT_RUN(labels_are_part_of_identity);
    TT_RUN(increment_creates_then_accumulates);
    TT_RUN(registry_refuses_to_overflow);
    TT_RUN(registry_rejects_bad_arguments);
    TT_RUN(registry_rejects_too_many_labels);

    TT_RUN(prometheus_name_converts_dots_and_appends_unit);
    TT_RUN(prometheus_name_suffixes_counters_with_total);
    TT_RUN(prometheus_name_suffixes_info_metrics);
    TT_RUN(prometheus_name_does_not_double_a_unit_suffix);
    TT_RUN(prometheus_name_replaces_illegal_characters);
    TT_RUN(prometheus_name_cannot_start_with_a_digit);
    TT_RUN(prometheus_name_reports_the_length_it_needed);
    TT_RUN(otel_units_use_the_ucum_spelling);

    TT_RUN(integral_values_print_without_a_decimal_point);
    TT_RUN(fractional_values_keep_enough_precision);
    TT_RUN(non_finite_values_use_the_prometheus_spelling);

    TT_RUN(reset_reasons_have_stable_lowercase_names);
    TT_RUN(faults_are_distinguished_from_intended_restarts);

    TT_RUN(fragmentation_is_zero_for_one_contiguous_block);
    TT_RUN(fragmentation_reports_the_dangerous_case);
    TT_RUN(fragmentation_handles_degenerate_inputs);

    TT_RUN(json_escaping_covers_quotes_backslashes_and_controls);
    TT_RUN(json_escaping_encodes_other_control_characters);
    TT_RUN(prometheus_label_escaping_follows_the_exposition_rules);
    TT_RUN(prometheus_label_escaping_drops_unescapable_controls);

    TT_RUN(prometheus_output_has_help_type_and_sample_lines);
    TT_RUN(prometheus_output_marks_counters_as_counters);
    TT_RUN(prometheus_output_puts_the_reset_reason_in_a_label);
    TT_RUN(prometheus_output_keeps_negative_and_fractional_values_readable);
    TT_RUN(prometheus_output_is_truncation_safe);
    TT_RUN(prometheus_rendering_survives_a_null_registry);
    TT_RUN(prometheus_rendering_accepts_a_null_buffer_for_sizing);

    TT_RUN(otlp_output_carries_resource_attributes);
    TT_RUN(otlp_output_names_the_instrumentation_scope);
    TT_RUN(otlp_gauges_use_the_gauge_shape_with_ucum_units);
    TT_RUN(otlp_counters_are_monotonic_cumulative_sums);
    TT_RUN(otlp_labels_become_datapoint_attributes);
    TT_RUN(otlp_output_is_balanced_json);
    TT_RUN(otlp_output_omits_empty_resource_attributes);
    TT_RUN(otlp_output_is_truncation_safe);
    TT_RUN(hostile_strings_cannot_break_either_format);

    return TT_SUMMARY();
}
