# Metric reference

Every metric telltale emits, what it is for, and the alert it is meant to make possible.

Names are given in the semantic-convention (dotted) form. The Prometheus name is derived mechanically: dots become underscores, the unit is appended, counters get `_total`, and info metrics get `_info`. Every series carries `device_id`.

---

## Memory

| Dotted name | Prometheus | Unit | Type |
|---|---|---|---|
| `device.heap.free` | `device_heap_free_bytes` | bytes | gauge |
| `device.heap.total` | `device_heap_total_bytes` | bytes | gauge |
| `device.heap.min_free` | `device_heap_min_free_bytes` | bytes | gauge |
| `device.heap.largest_free_block` | `device_heap_largest_free_block_bytes` | bytes | gauge |
| `device.heap.fragmentation` | `device_heap_fragmentation_ratio` | 0..1 | gauge |

**Why five metrics and not one.** Free heap alone does not predict failure. The pair that does is `free` against `largest_free_block`: a device with 100 kB free and a 4 kB largest block will fail the next 8 kB allocation while a dashboard built on free heap shows a comfortable margin. `fragmentation` is that comparison precomputed, so an alert needs no arithmetic:

```promql
device_heap_fragmentation_ratio > 0.8
```

**`min_free` earns its place because sampling misses troughs.** A 30-second interval will not catch the burst that nearly exhausted the heap; the RTOS tracks the minimum continuously, so exporting it turns a sampled metric into a guaranteed one. A `min_free` that keeps falling across reboots is a leak; one that is stable but low is a device sized too tightly.

```promql
device_heap_min_free_bytes < 8192
```

All figures come from `MALLOC_CAP_8BIT`, the allocation class general code actually uses. On a board with PSRAM, internal-only figures differ; if that distinction matters to you, open an issue — splitting by capability is a small change we would rather make on a real need than on a guess.

---

## Lifecycle

| Dotted name | Prometheus | Unit | Type |
|---|---|---|---|
| `device.uptime` | `device_uptime_seconds` | seconds | gauge |
| `device.boot.count` | `device_boot_count_total` | boots | counter |
| `device.reset` | `device_reset_info` | — | info (labelled `reason`) |
| `device.reset.fault` | `device_reset_fault` | 0/1 | gauge |

**`boot.count` is persisted in NVS** and survives the restart it is describing, which is what makes a reboot loop visible in a backend rather than only on a serial console:

```promql
increase(device_boot_count_total[1h]) > 3
```

**`reason` is a label because a string cannot be a sample.** Values: `power_on`, `external`, `software`, `panic`, `interrupt_watchdog`, `task_watchdog`, `other_watchdog`, `deep_sleep`, `brownout`, `unknown`.

The categories are the whole diagnostic. A fleet rebooting with `power_on` has a supply problem, one rebooting with `panic` has a firmware problem, and one rebooting with `brownout` has a battery or regulator problem. Uptime alone cannot tell them apart, and these are three different teams.

**`reset.fault`** is 1 when the reason was a panic, a watchdog or a brownout — an unintended restart — and 0 for a planned reboot or a deep-sleep wake. It exists so an alert does not have to enumerate label values:

```promql
count(device_reset_fault == 1) > 0
```

A device that has never had NVS provisioned reports `boot.count` as 1 and logs a warning. telltale will not refuse to boot over a diagnostic counter.

---

## Tasks

| Dotted name | Prometheus | Unit | Type |
|---|---|---|---|
| `device.task.stack.free` | `device_task_stack_free_bytes` | bytes | gauge, labelled `task` |

Reported only for tasks registered with `telltale_watch_task()`, up to `CONFIG_TELLTALE_MAX_WATCHED_TASKS`.

The value is the FreeRTOS **high-water mark**: the smallest free stack that task has ever had. The current free stack of a sleeping task is meaningless — it tells you about the moment of sampling, not about the deepest call path the task has taken. The minimum is the only number that predicts an overflow:

```promql
min by (device_id, task) (device_task_stack_free_bytes) < 512
```

A stack overflow on an ESP32 is usually a corrupted neighbour rather than a clean crash, which makes it one of the most expensive bugs to diagnose after the fact and one of the cheapest to see coming.

---

## Link

| Dotted name | Prometheus | Unit | Type |
|---|---|---|---|
| `link.connected` | `link_connected` | 0/1 | gauge |
| `link.rssi` | `link_rssi_dbm` | dBm | gauge |
| `link.channel` | `link_channel` | — | gauge |
| `link.disconnect.count` | `link_disconnect_count_total` | disconnects | counter |

**RSSI is absent while disassociated, not zero.** 0 dBm is an exceptionally strong signal; reporting it for a disconnected device would put a flat line at the top of every graph. The gap is the truth, and `link.connected` states it explicitly.

Rules of thumb for the value: above -67 dBm is comfortable, -70 to -80 is marginal, below -80 expect retransmissions and power cost. A device that *works* at -85 dBm still burns battery retrying.

**`disconnect.count` is fed by the application**, through `telltale_note_disconnect()`. telltale does not install a Wi-Fi event handler — see [ADR 0003](adr/0003-no-hidden-event-handlers.md). The number that matters is churn, not presence:

```promql
increase(link_disconnect_count_total[1h]) > 10
```

---

## Power

| Dotted name | Prometheus | Unit | Type |
|---|---|---|---|
| `device.battery.voltage` | `device_battery_voltage_volts` | volts | gauge |

Present only when the application supplies a `battery_provider` callback. Absent otherwise — telltale will not read an ADC on your behalf, because the divider ratio, the channel and the calibration are board decisions and a wrong guess reports a healthy battery on a device that is about to die.

**Volts, not percent.** The conversion from voltage to remaining charge depends on the cell chemistry, the temperature and the load. A device that reports a percentage has already thrown that context away, and two vendors' percentages are not comparable. Convert in the dashboard, where the curve for *your* cell can live.

---

## Application metrics

`telltale_set_metric()` publishes your own values through the same pipeline, with the same naming and escaping rules:

```c
telltale_set_metric(handle, "pump.pressure", TELLTALE_GAUGE, TELLTALE_UNIT_NONE,
                    "Pump pressure in bar", 2.4);
```

They share the registry's fixed capacity (`TELLTALE_MAX_METRICS`, default 32). Registration is idempotent, so calling it every cycle updates in place rather than consuming a slot each time; past capacity you get `ESP_ERR_NO_MEM` rather than a silent drop.

---

## Resource attributes

Attached to every series and to the OTLP resource:

| Attribute | Source |
|---|---|
| `device_id` / `device.id` | Your `device_id`, or derived from the Wi-Fi station MAC as `esp32-a1b2c3` |
| `service.name` | Your `service_name`, default `telltale` |
| `service.version` | The application version from the ESP-IDF app descriptor |
| `device.model.identifier` | Your `hardware_model` |

---

## Alert starter pack

Rules worth having on day one, in the order they earn their keep:

```yaml
groups:
  - name: telltale
    rules:
      - alert: DeviceRebootLoop
        expr: increase(device_boot_count_total[1h]) > 3
        for: 5m
        annotations:
          summary: "{{ $labels.device_id }} restarted more than 3 times in an hour"

      - alert: DeviceRestartedWithFault
        expr: device_reset_fault == 1 and increase(device_boot_count_total[15m]) > 0
        annotations:
          summary: "{{ $labels.device_id }} restarted on a fault"

      - alert: HeapFragmented
        expr: device_heap_fragmentation_ratio > 0.8
        for: 15m
        annotations:
          summary: "{{ $labels.device_id }} can no longer allocate large blocks"

      - alert: StackNearlyExhausted
        expr: device_task_stack_free_bytes < 512
        for: 5m
        annotations:
          summary: "{{ $labels.device_id }} task {{ $labels.task }} is close to overflowing"

      - alert: DeviceStoppedReporting
        expr: time() - timestamp(device_uptime_seconds) > 600
        annotations:
          summary: "{{ $labels.device_id }} has not been seen for 10 minutes"
```

The last one is the one people forget: a device that stops reporting produces no metric at all, so no threshold on any of the others will ever fire for it.
