## What this changes

<!-- One or two sentences. -->

## Why

<!-- The operational question it answers, or the bug it fixes. -->

## How it was tested

<!-- Host suite only, or on hardware? If on hardware, say which board, which
     ESP-IDF version and which target. That is the most useful line here. -->

## Checklist

- [ ] `make -C host` and `make -C host asan` pass
- [ ] Anything computable without hardware lives in `portable/`, with a test
- [ ] No allocation after init, and no new dependency
- [ ] Units are seconds, bytes, volts or 0..1 ratios
- [ ] A new metric is in `semconv/iot-device.yaml` **and** `docs/METRICS.md`
- [ ] Unknown values are absent, never reported as zero
