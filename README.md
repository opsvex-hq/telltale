# telltale

> **The node_exporter that does not exist for microcontrollers.** Free heap and fragmentation, reboot count *with its cause*, link quality, uptime and task stack headroom — exported from an ESP32 in the format Prometheus and OpenTelemetry already understand.

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-%E2%89%A55.0-E7352C?logo=espressif&logoColor=white)](https://docs.espressif.com/projects/esp-idf/)
[![Host tests](https://img.shields.io/badge/host%20tests-47-success)](docs/TESTING.md)
[![Targets](https://img.shields.io/badge/targets-esp32%20%C2%B7%20s2%20%C2%B7%20s3%20%C2%B7%20c3%20%C2%B7%20c6-informational)](idf_component.yml)

Built and maintained by [Opsvex](https://opsvex.com). We run operations infrastructure for companies that cannot afford downtime — **[talk to us](https://opsvex.com)** if you would rather someone else owned the fleet layer.

---

## The gap this fills

A server has `node_exporter`. A container has cAdvisor. A microcontroller in the field has a serial port nobody is looking at.

When a gateway in a substation reboots at 03:40, three questions decide what you do next, and none of them can be answered from uptime alone:

- **Why did it restart?** A panic is a firmware bug. A brownout is a battery or regulator. A power-on is somebody's breaker. The device knows, and forgets the moment it reboots unless something records it.
- **Is it running out of memory?** Not "how much is free" — how much is free *contiguously*. A device with 100 kB free and a 4 kB largest block is about to fail an allocation while every dashboard says it is fine.
- **Can it hear the network?** An RSSI of -89 dBm and forty reconnections an hour is a device that is technically online and practically useless.

telltale answers all three in metrics a Prometheus or OpenTelemetry stack already knows how to store, alert on and graph.

### What exists today

Searching `esp32 prometheus` and `opentelemetry embedded` returns single-author projects — the largest at 5 stars, last touched in 2022. Espressif's own `esp-insights` sits at 147 stars against 18,951 for `esp-idf`, and it is a vendor agent rather than an open format. **OpenTelemetry has no semantic conventions for constrained devices at all**: no heap, no reset cause, no task stacks, no link quality. This repository ships [a proposal for them](semconv/iot-device.yaml), with [the evidence behind the claim](semconv/RATIONALE.md).

## Install

Via the ESP-IDF Component Manager, in your project's `idf_component.yml`:

```yaml
dependencies:
  opsvex/telltale:
    version: "^0.1.0"
```

Or as a git submodule under `components/`.

## Use it

```c
#include "telltale.h"

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());      /* telltale persists the boot counter */

    telltale_config_t config = TELLTALE_DEFAULT_CONFIG();
    config.service_name = "fleet-gateway";
    config.enable_http_endpoint = true;      /* GET /metrics */
    config.http_port = 8080;
    config.otlp_endpoint = "http://collector.lan:4318/v1/metrics";

    telltale_handle_t telltale;
    ESP_ERROR_CHECK(telltale_init(&config, &telltale));
    ESP_ERROR_CHECK(telltale_watch_task(telltale, NULL, "app_main"));
}
```

That is the whole integration. A background task samples on your interval, serves the scrape and pushes to OTLP.

```
# HELP device_heap_free_bytes Free heap right now
# TYPE device_heap_free_bytes gauge
device_heap_free_bytes{device_id="esp32-a1b2c3"} 254812
# TYPE device_heap_fragmentation_ratio gauge
device_heap_fragmentation_ratio{device_id="esp32-a1b2c3"} 0.23
# TYPE device_reset_info gauge
device_reset_info{device_id="esp32-a1b2c3",reason="task_watchdog"} 1
# TYPE device_boot_count_total counter
device_boot_count_total{device_id="esp32-a1b2c3"} 47
# TYPE link_rssi_dbm gauge
link_rssi_dbm{device_id="esp32-a1b2c3"} -67
# TYPE device_task_stack_free_bytes gauge
device_task_stack_free_bytes{device_id="esp32-a1b2c3",task="app_main"} 1840
```

Full metric reference: [`docs/METRICS.md`](docs/METRICS.md). Reference Grafana dashboard: [`dashboards/grafana/telltale-device.json`](dashboards/grafana/telltale-device.json).

## Decisions you may disagree with

Stated plainly, because they are the ones that shape how it feels to use:

**The reset reason is a label on a constant 1, not a number.** No metrics backend accepts a string as a sample. An integer enum would push the decoding into every dashboard.

**RSSI is absent when disconnected, not zero.** 0 dBm reads as an exceptionally strong signal. `link_connected` carries the fact explicitly instead.

**Battery is a callback you provide, not an ADC read.** The divider ratio, the channel and the calibration are board decisions. A wrong guess reports a healthy battery on a device about to die.

**telltale installs no Wi-Fi event handler.** An application that already has one should not get a second, invisible one. Call `telltale_note_disconnect()` from yours.

**Task stacks are reported as the smallest free stack ever, not the current one.** The current value of a sleeping task tells you nothing; the high-water mark is what predicts an overflow.

**Nothing is allocated after `telltale_init` returns.** One render buffer, one fixed-capacity registry, no malloc in the collector, the HTTP handler or the pusher.

## Footprint

| | |
|---|---|
| Flash | ~6 kB for the library, plus whatever the HTTP server and client cost if you enable them |
| RAM | The registry (about 3 kB with the default 32 slots) plus one render buffer you size yourself (4 kB default) |
| Collector task | 3 kB stack, runs on your interval and sleeps |
| Allocations after init | **None** |

These are the design targets, not a measurement — see Validation status.

## Configuration

`idf.py menuconfig` -> *telltale — device health metrics*:

| Option | Default | What it does |
|---|---|---|
| `TELLTALE_MAX_WATCHED_TASKS` | 4 | Tasks whose stack headroom is reported |
| `TELLTALE_TASK_STACK_SIZE` | 3072 | Collector task stack, in bytes |
| `TELLTALE_ENABLE_WIFI_COLLECTOR` | y | RSSI, channel, association |
| `TELLTALE_ENABLE_HTTP_ENDPOINT` | y | GET /metrics |
| `TELLTALE_ENABLE_OTLP` | y | Push to an OTLP/HTTP collector |

Disabling a collector drops both its metrics and its dependency.

## Validation status — read this before flashing it to a fleet

| | Status |
|---|---|
| Portable core (registry, naming, escaping, both serialisers) | **47 host tests**, run on gcc and clang, under address and UB sanitizers, and in a 32-bit build |
| ESP-IDF layer | Compiled in CI for `esp32` and `esp32c3` on ESP-IDF 5.1 and 5.3, both examples |
| **Real hardware** | **Not yet.** No board has run this. See [`docs/HARDWARE-VALIDATION.md`](docs/HARDWARE-VALIDATION.md) for exactly what is unverified and the checklist being worked through |
| Memory figures | Design targets, not measurements |
| OTLP payload | Shape verified against the specification and asserted in tests; **not yet accepted by a running collector** |

Version `0.1.0`. Metric names follow [the proposal](semconv/iot-device.yaml) and **will change if it changes upstream** — that is what pre-1.0 is for.

If you flash this to real hardware, a report either way is the most useful thing you can contribute.

## Open core

telltale is Apache-2.0 and stays that way: the library on the device is the piece that should be free, because a component nobody can inspect is a component nobody should run.

What Opsvex sells on top is the fleet layer — mass provisioning, OTA orchestration by cohort with rollback, certificate rotation, aggregated retention and alerting. That boundary is written down in [`docs/OPEN-CORE.md`](docs/OPEN-CORE.md) so nobody has to guess which side a contribution lands on.

## Maintenance commitment

**Budget: 2–4 hours per month.** Issues get a reply within **5 business days**. This is reference software from a two-person firm, not a supported product.

If six months from now it has produced no real conversation, it gets frozen and marked as a portfolio piece rather than rotting quietly. Written down on purpose.

## Development

```bash
make -C host          # build and run the host suite
make -C host asan     # again, under sanitizers
idf.py -C examples/basic build
```

The portable core has no ESP-IDF dependency, which is what makes the suite run in under a second on a laptop. [`docs/TESTING.md`](docs/TESTING.md) explains the split; [`docs/PORTING.md`](docs/PORTING.md) covers taking the core to Zephyr, nRF Connect or bare metal.

## Documentation

| Document | What is in it |
|---|---|
| [`docs/METRICS.md`](docs/METRICS.md) | Every metric, its unit, and what it is for |
| [`docs/TESTING.md`](docs/TESTING.md) | The test split and how to extend it |
| [`docs/PORTING.md`](docs/PORTING.md) | Taking the portable core to another platform |
| [`docs/HARDWARE-VALIDATION.md`](docs/HARDWARE-VALIDATION.md) | What has not been verified on a board yet |
| [`docs/OPEN-CORE.md`](docs/OPEN-CORE.md) | What is free, what is not, and why |
| [`semconv/RATIONALE.md`](semconv/RATIONALE.md) | The semantic-convention proposal and its evidence |
| [`docs/es/GUIA.md`](docs/es/GUIA.md) | **Guía completa en español** |
| [`docs/adr/`](docs/adr/) | Why C, why a portable core, why no event handler |

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

---

<div align="center">
<sub><a href="https://opsvex.com">opsvex.com</a> · <a href="mailto:hola@opsvex.com">hola@opsvex.com</a> · <a href="https://calendly.com/opsvex-hq/30min">Book 30 minutes</a></sub>
</div>
