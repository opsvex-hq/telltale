# Security policy

## Reporting

Email **hola@opsvex.com** with `telltale` in the subject. Please do not open a public issue.

Expect an acknowledgement within **5 business days**. This is a two-person firm with a declared budget of 2–4 hours a month on this repository; we will be honest about timing rather than promise an SLA we cannot hold.

## In scope

telltale runs on devices in the field, so the interesting failures are memory safety and exposure:

- **A buffer overrun or out-of-bounds read** anywhere in the portable core. The serialisers and escapers take attacker-influenced strings — a device name, a task name, an application metric label — and write them into fixed buffers.
- **A truncation that produces valid-looking output.** A partial scrape is silently wrong rather than obviously broken, which is worse.
- **Anything that lets a value or label escape its quoting** in either output format.
- **Secrets in output:** an OTLP bearer token, a Wi-Fi credential or an NVS value appearing in exposition text or a log line.
- **A crash reachable from the network** through the `/metrics` endpoint.

## Out of scope, and stated up front

- **The `/metrics` endpoint has no authentication.** It exposes device health to whatever network it is bound to. That is a documented property, not a vulnerability: put it on a trusted segment or behind a reverse proxy.
- **OTLP push is plain HTTP unless you give it an HTTPS URL.** Certificate handling is ESP-IDF's; configure it as your deployment requires.
- **Metrics are information.** Free heap and reset causes tell an observer something about what a device is doing. If that matters in your threat model, do not expose the endpoint.
- Vulnerabilities in ESP-IDF, FreeRTOS or the Espressif HTTP stack. Report those upstream.

## Supported versions

Pre-1.0: the latest release only. No backports.

## Hardening notes for operators

1. Bind the endpoint to a management VLAN, not to the network the devices share with everything else.
2. Prefer OTLP push over scraping when devices sit on an untrusted network: push needs no inbound reachability.
3. Use an HTTPS OTLP endpoint with a bearer token. The token is never logged, but it does live in RAM for the life of the process.
4. Size the render buffer for your metric set. An undersized buffer returns a 500, which is loud — but a monitoring gap is still a gap.
5. Put nothing secret in a device id, a service name or a metric label. They appear in every single sample.
