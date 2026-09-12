# telltale — basic example

Brings up telltale with the defaults and prints the Prometheus exposition to the
console every 30 seconds. No Wi-Fi, no collector, no dashboard: the smallest
thing that shows what the device is actually reporting.

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Expected output after a power-on reset:

```
# HELP device_heap_free_bytes Free heap right now
# TYPE device_heap_free_bytes gauge
device_heap_free_bytes{device_id="esp32-a1b2c3"} 254812
...
device_reset_info{device_id="esp32-a1b2c3",reason="power_on"} 1
device_boot_count_total{device_id="esp32-a1b2c3"} 1
```

Reset the board with the EN button and `reason` becomes `external`; trigger a
panic (`abort()`) and it becomes `panic` on the next boot, with
`device_reset_fault` at 1. That transition is the whole point of the library:
the device tells you *why* it restarted, without anyone watching the serial
port at the time.
