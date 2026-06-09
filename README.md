# COMPASS / Project HYBE - nRF52840 Dual-Mode Firmware (Phase 1)

Zephyr / nRF Connect SDK application for the ceiling-mounted nRF52840. It runs
two BLE roles at once:

- **Broadcaster** - non-connectable iBeacon advertising. This is the "location
  id" the phone app detects for the **active** path. UUID/major/minor are set in
  `src/app_config.h` and must match the Flutter region UUID and the Phase 0 test
  UUID (`E2C56DB5-DFFB-48D2-B060-D0F5A71096E0`, major 1, minor 101).
- **Observer** - passive scanning that aggregates ambient BLE traffic over
  10-second windows for the **passive** crowd estimate, splitting Apple
  (Company ID `0x004C`) from other manufacturers.

## Output

Every `WINDOW_SECONDS` (10 s) one line is printed to the console (UART/RTT),
prefixed with `TX -> ` so the host bridge can separate data lines from the boot
banner / `CONFIG_LOG` output:

```text
TX -> {"id":"beacon_A1","packet_count":1756,"apple_packet_count":1200,"other_packet_count":556,"rssi_avg":-58,"rssi_std":8.3,"rssi_min":-85,"rssi_max":-42,"unique_mac_count":27,"uptime_sec":710}
```

`apple_packet_count + other_packet_count == packet_count` by construction
(non-Apple and manufacturer-less packets fall into `other`). `id` is the
Firestore document id the app reads (`beacons/{id}`); `updated_at` is added by
the host bridge from the host clock.

## Implementation notes

- **RSSI std**: Welford online algorithm (`src/ble_window.c`), population stddev.
- **Unique MACs**: FNV-1a 32-bit hash of the 6-byte address into a 1024-bit
  bitmap; `unique_mac_count` is the popcount. Approximate (hash collisions
  saturate near full occupancy) and intended as a relative density signal.
- **RSSI threshold**: `-95 dBm` (relaxed) so people farther from the
  ceiling-centre unit are still counted.
- **Dual mode**: advertising is scheduled by the radio while passive scanning
  fills the rest of the duty cycle; the nRF52840 controller interleaves both.
- **Console**: the logging subsystem is deliberately disabled. With
  `CONFIG_LOG=y` (deferred mode) the high-priority BLE RX thread starves the log
  thread during scanning and no `printk` output ever flushes - the board looks
  silent on UART even though advertising keeps working. Plain `printk` writes
  synchronously and cannot be starved.

## Troubleshooting

- **Advertising works but UART is silent (no boot banner either)**: classic
  deferred-logging starvation - ensure `CONFIG_LOG` is NOT set (see above).
- **`rssi_std` prints blank/garbage**: a `%f` crept into a format string; this
  build formats std as integer tenths to avoid needing newlib float printf.

## Build & flash (nRF Connect SDK / Zephyr)

```sh
# from this firmware/ directory, with the nRF Connect SDK environment active
west build -b nrf52840dk/nrf52840 .
west flash
```

Per-unit config: give each room its own `BEACON_ID` and `APP_IBEACON_MINOR` in
`src/app_config.h`; keep `APP_IBEACON_UUID` identical across all units.

## Host bridge (Firestore)

The nRF has no IP stack, so `tools/firestore_bridge.py` reads the `TX -> ` JSON
lines over serial and writes them to `beacons/{id}` (the collection the app
reads today), forwarding the manufacturer-split fields too:

```sh
pip install pyserial firebase-admin
python tools/firestore_bridge.py --port /dev/tty.usbmodemXXXX --cred sa.json
```

## Files

| File | Purpose |
| --- | --- |
| `src/main.c` | BLE bring-up, iBeacon adv, passive scan, 10 s window emit |
| `src/ble_window.c/.h` | Welford stats, FNV-1a bitmap, manufacturer split, JSON |
| `src/app_config.h` | Per-unit identity + thresholds |
| `prj.conf` | Zephyr Kconfig (BT broadcaster+observer, direct UART printk) |
| `tools/firestore_bridge.py` | Host-side serial -> Firestore forwarder |
