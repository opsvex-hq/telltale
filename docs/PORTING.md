# Porting telltale

The portable core has no platform dependencies: C11, `<string.h>`, `<stdio.h>` for `snprintf`, and `<math.h>` for `floor`/`isnan`. It never allocates and keeps no global state. Porting telltale to another platform means writing collectors — the metric model, the naming rules and both serialisers come along unchanged.

## What you get for free

`portable/` is roughly 550 lines and gives you:

- a fixed-capacity registry with gauges, counters and info metrics;
- Prometheus metric naming, label escaping and text exposition;
- OTLP/HTTP JSON encoding;
- reset-reason names and the fault classification;
- the fragmentation calculation;
- truncation-safe output everywhere.

Copy the two files, or add the directory as a subdirectory in your build. There is nothing to configure beyond `TELLTALE_MAX_METRICS`, which you can override at compile time.

## What you have to write

Four functions, in the shape your platform provides them:

```c
/* 1. Identity. Anything stable and unique: a MAC, a serial number, a UUID. */
telltale_registry_init(&registry, device_id, "my-service");

/* 2. Memory. Whatever your allocator exposes. */
telltale_set(&registry, "device.heap.free", TELLTALE_GAUGE, TELLTALE_UNIT_BYTES,
             "Free heap right now", (double)my_heap_free());
telltale_set(&registry, "device.heap.fragmentation", TELLTALE_GAUGE, TELLTALE_UNIT_RATIO,
             "Heap fragmentation", telltale_heap_fragmentation(free, largest));

/* 3. Lifecycle. Map your platform's reset enum onto the portable one. */
telltale_label_t reason[] = {{"reason", ""}};
snprintf(reason[0].value, sizeof(reason[0].value), "%s",
         telltale_reset_reason_name(map_my_reset_reason()));
telltale_set_labeled(&registry, "device.reset", TELLTALE_INFO, TELLTALE_UNIT_NONE,
                     "Cause of the last reset", 1, reason, 1);

/* 4. Output. Hand the rendered text to whatever transport you have. */
char buffer[4096];
size_t needed = telltale_core_render_prometheus(&registry, buffer, sizeof(buffer));
if (needed >= sizeof(buffer)) { /* grow the buffer; do not ship a truncated scrape */ }
```

## Platform notes

**Zephyr.** `sys_heap_runtime_stats_get()` gives free and allocated bytes; `k_thread_stack_space_get()` gives per-thread stack use; the reset cause comes from `hwinfo_get_reset_cause()`, whose flags map onto the portable enum almost one to one. Expose the exposition through the HTTP server sample or over MQTT.

**nRF Connect SDK.** As Zephyr, plus `modem_info` for link quality on cellular parts. RSRP rather than RSSI — report it under `link.rssi` with the `dBm` unit and note the difference in your dashboard, or add `link.rsrp` and propose it upstream.

**Raspberry Pi Pico / pico-sdk.** `mallinfo()` covers the heap; `watchdog_caused_reboot()` distinguishes a watchdog reset from a power-on. Serve over `cyw43` and lwIP.

**Bare metal, no network.** The serialisers write to a buffer; the transport is your problem and can be a UART, a CAN frame or a file on an SD card. A device that logs exposition text to flash and uploads it on the next visit is a perfectly good deployment.

**Arduino.** The public C API is `extern "C"` and the core is plain C, so it compiles inside a C++ translation unit unchanged.

## The one thing to get right

**Units.** Seconds, not milliseconds. Bytes, not kilobytes. Volts, not millivolts. Ratios from 0 to 1, not percentages.

Every one of these is somewhere in someone's firmware in the other unit, and a fleet with two conventions produces dashboards that are quietly wrong rather than obviously broken. If your platform gives you milliseconds, divide at the collector. The tests in `host/` cover the formatting; they cannot catch a unit you converted incorrectly on the way in, and neither will a reviewer.

## If you port it

Open an issue. A second platform is the strongest evidence that the core boundary is in the right place, and if a collector needs something the core does not offer, that is worth changing while the API is still pre-1.0.
