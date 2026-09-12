# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Semantic versioning applies from 1.0; before that, **metric names may change** if the semantic-convention proposal changes upstream.

## [Unreleased]

Nothing yet.

## [0.1.0] - 2026-09-12

First release. **Not yet run on hardware** — `docs/HARDWARE-VALIDATION.md` lists exactly what that leaves unverified.

### Added

- **Collectors:** heap (free, total, minimum-ever-free, largest free block, and the derived fragmentation ratio), uptime, a boot counter persisted in NVS, reset cause with a fault/not-fault split, per-task stack high-water marks, Wi-Fi association, RSSI and channel, and battery voltage through an application-supplied callback.
- **Two exporters:** Prometheus text exposition over `GET /metrics`, and OTLP/HTTP JSON push to a collector.
- **A portable core** in plain C11 with no ESP-IDF dependency, holding the registry, the naming rules, the escaping and both serialisers — the code where the bugs actually live, testable on a laptop.
- **47 host tests**, run on gcc and clang, under address and UB sanitizers, and in a 32-bit build.
- **A proposed set of OpenTelemetry semantic conventions** for constrained devices (`semconv/iot-device.yaml`), with the evidence for the gap and the process for proposing it in `semconv/RATIONALE.md`.
- **A reference Grafana dashboard**, organised around the three questions a fleet actually raises: is it restarting, is it running out of memory, can it hear the network.
- **Two examples**, built in CI for `esp32` and `esp32c3` on ESP-IDF 5.1 and 5.3.
- Kconfig options to drop any collector along with its dependency.
- Documentation: metric reference, testing guide, porting guide, hardware-validation checklist, the open-core boundary, three ADRs, and a full Spanish guide.

### Known limitations

- No board has run this code.
- The flash and RAM figures in the README are design targets, not measurements.
- No OpenTelemetry Collector has accepted the OTLP payload yet; its shape is asserted against the specification in tests.
- The semantic-convention proposal has not been submitted upstream.

[Unreleased]: https://github.com/opsvex-hq/telltale/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/opsvex-hq/telltale/releases/tag/v0.1.0
