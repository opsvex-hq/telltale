# Hardware validation status

**No board has run this code.** Version 0.1.0 has been compiled, tested on a host, and built in CI for two architectures and two ESP-IDF releases. It has not been flashed.

This document exists so nobody has to infer that from silence. It is updated as each line gets verified, and it stays in the repository whatever the outcome.

## What is verified, and by what

| Area | Status | Evidence |
|---|---|---|
| Registry, naming, escaping, both serialisers | **Verified** | 47 host tests on gcc and clang, sanitizers, 32-bit build |
| Truncation safety of both serialisers | **Verified** | Host tests with undersized buffers |
| ESP-IDF layer compiles | **Verified** | CI: ESP-IDF 5.1 and 5.3, `esp32` and `esp32c3`, both examples |
| Heap figures match reality | **Unverified** | Needs a board |
| Reset reasons map correctly | **Unverified** | Needs a board, and each cause deliberately triggered |
| Boot counter persists across power loss | **Unverified** | Needs a board and a real power cut |
| RSSI matches what the AP reports | **Unverified** | Needs a board and an AP |
| Task high-water marks are plausible | **Unverified** | Needs a board |
| `/metrics` is scrapeable by Prometheus | **Unverified** | Needs a board and a Prometheus |
| OTLP payload is accepted by a collector | **Unverified** | Shape asserted against the spec in tests; no collector has replied 200 |
| Flash and RAM figures | **Estimates** | CI prints `size-components`; the README will carry measured numbers once a board confirms them |
| Behaviour over days | **Unverified** | Counter wrap, clock drift, NVS wear |

## The checklist being worked through

Each item has an expected result, so "it ran" is not mistaken for "it worked".

### 1. Boot and identity
- [ ] Flash `examples/basic` to an ESP32 DevKit v1 and confirm `device_id` matches the last three bytes of the station MAC.
- [ ] Confirm `service.version` matches the app descriptor.

### 2. Reset reasons — one power cycle per cause
- [ ] Power-on: `reason="power_on"`, `device_reset_fault` 0.
- [ ] EN button: `reason="external"`.
- [ ] `esp_restart()`: `reason="software"`, fault 0.
- [ ] `abort()`: `reason="panic"`, fault 1.
- [ ] A task looping without yielding, with the task watchdog on: `reason="task_watchdog"`, fault 1.
- [ ] Supply dropped below the brownout threshold: `reason="brownout"`, fault 1.
- [ ] Deep sleep and wake: `reason="deep_sleep"`, fault 0.

That list is the most valuable part of this checklist. The reset cause is the library's headline claim, and the mapping from `esp_reset_reason_t` is exactly the kind of code that looks right and is off by one category.

### 3. Boot counter
- [ ] Ten power cycles increment the counter by exactly ten.
- [ ] Pulling power mid-write does not corrupt NVS or reset the count to zero.
- [ ] A device with no NVS partition provisioned logs a warning, reports 1, and still boots.

### 4. Heap
- [ ] `device_heap_total_bytes` matches `heap_caps_get_total_size` from a direct call.
- [ ] Allocating and freeing a large block moves `free` and `largest_free_block` as expected.
- [ ] Deliberately fragmenting the heap raises `fragmentation` toward 1.
- [ ] `min_free` never rises during a run.

### 5. Tasks
- [ ] A watched task's reported headroom falls as it recurses and never rises.
- [ ] Watching more tasks than `CONFIG_TELLTALE_MAX_WATCHED_TASKS` returns `ESP_ERR_NO_MEM` and does not corrupt the registry.

### 6. Link
- [ ] RSSI is within a few dBm of what the AP reports for the same client.
- [ ] Walking out of range: RSSI disappears, `link_connected` goes to 0, and the disconnect counter increments once per event.

### 7. Scrape and push
- [ ] `curl http://<device>:8080/metrics` returns a body Prometheus parses without warnings (`promtool check metrics`).
- [ ] A Prometheus instance scrapes it for an hour with no parse errors.
- [ ] An OpenTelemetry Collector accepts the OTLP payload with a 200 and the metrics appear downstream.
- [ ] A render buffer deliberately sized too small produces a 500 and a log line, never a truncated body.

### 8. Endurance
- [ ] 72 hours of uptime with no heap trend and no task stack decline.
- [ ] Sampling every 5 seconds for 24 hours does not measurably shorten battery life versus a build with telltale disabled.

## How the results will be recorded

Each verified item gets ticked here with the board, the ESP-IDF version and the date. **Anything that fails gets written down too, with what was wrong**, because a validation document that only records successes is marketing.

## If you flash it first

You will have done something we have not, and a report either way is the most useful contribution this repository can receive. Please include the board, the ESP-IDF version, the target, and what you expected versus what you saw. The [bug report template](../.github/ISSUE_TEMPLATE/bug_report.yml) asks for exactly that.
