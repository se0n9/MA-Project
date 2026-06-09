# COMPASS / Project HYBE - nRF52840 Dual-Mode Firmware (Phase 1)

Zephyr / nRF Connect SDK application for the ceiling-mounted nRF52840. It runs
three BLE roles at once:

- **Broadcaster** - connectable advertising carrying the iBeacon manufacturer
  data. This is the "location id" the phone app detects for the **active** path.
  UUID/major/minor are set in `src/app_config.h` and must match the Flutter
  region UUID and the Phase 0 test UUID (`E2C56DB5-DFFB-48D2-B060-D0F5A71096E0`,
  major 1, minor 101). The scan response adds the NUS service UUID + device name
  so the app can discover the unit (see **Attendance channel** below).
- **Observer** - passive scanning that aggregates ambient BLE traffic over
  10-second windows for the **passive** crowd estimate, splitting Apple
  (Company ID `0x004C`) from other manufacturers.
- **Peripheral (NUS)** - a connectable Nordic UART Service the phone app writes
  a check-in signal to. The beacon owns an attendance counter, gates each tap on
  proximity (connection RSSI), and increments on accept. See **Attendance
  channel**.

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

## Attendance channel (NUS)

A connectable **Nordic UART Service** (`6E400001-…`) lets the phone app check a
student in. The app connects, writes the bare signal `CHECKIN\n` to the RX
characteristic (`6E400002-…`), and disconnects; the firmware **owns the
counter** (`src/attendance.c`).

On each `CHECKIN` the firmware reads the **connection RSSI** of the tapping phone
(HCI Read_RSSI) and gates on proximity, so only phones near the beacon count:

- **Accept** (`rssi >= ATT_RSSI_THRESHOLD`): increment `attendance_count`, print
  it, and notify `OK:<count>\n` on the TX characteristic (`6E400003-…`).
- **Reject** (too far, or RSSI unreadable -> fail-safe): no increment, notify
  `FAR\n`.

```text
NUS connected
ATT-RX count=1
NUS disconnected (reason 19)
```

The counter starts at 0 and resets on reboot (persistence is future work). The
beacon advertises **connectable** (Approach A): advertising pauses for the ~1-2 s
of a check-in connection and auto-restarts on disconnect, so other phones briefly
stop ranging this unit during a tap - acceptable for occasional, user-triggered
check-ins.

**Tuning the proximity gate:** `ATT_RSSI_THRESHOLD` in `src/app_config.h`
(default `-70` dBm; close range is roughly `-40…-65`). Watch the `rssi=` value
printed on rejects and set the threshold just below the weakest distance you want
to accept.

**Forward-compat:** the RX signal is matched by prefix, so a future
`CHECKIN:<token>\n` (per-user de-duplication) is non-breaking.
`attendance_display(uint32_t)` is the single drop-in point for the future
LED-matrix renderer.

> Required Kconfig for the gate: `CONFIG_BT_CTLR_CONN_RSSI=y`. Without it the
> SoftDevice Controller rejects HCI Read_RSSI and every tap fails the fail-safe
> as "far" (`rssi read failed, err -5`).

## Implementation notes

- **RSSI std**: Welford online algorithm (`src/ble_window.c`), population stddev.
- **Unique MACs**: FNV-1a 32-bit hash of the 6-byte address into a 1024-bit
  bitmap; `unique_mac_count` is the popcount. Approximate (hash collisions
  saturate near full occupancy) and intended as a relative density signal.
- **RSSI thresholds (two, distinct)**: `RSSI_THRESHOLD` = `-95 dBm` (relaxed)
  gates *passive-scan* packets so people farther from the ceiling-centre unit are
  still counted; `ATT_RSSI_THRESHOLD` = `-70 dBm` gates the *attendance check-in*
  by proximity (close students only). Don't confuse the two.
- **Concurrent roles**: advertising and passive scanning are interleaved by the
  nRF52840 controller across the duty cycle; while a check-in connection is up
  the radio also time-slices the connection (and advertising pauses under
  Approach A). The 10 s passive window keeps running throughout.
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
- **Every check-in rejected with `rssi read failed, err -5`**: the controller's
  connection-RSSI measurement is off - add `CONFIG_BT_CTLR_CONN_RSSI=y` to
  `prj.conf` and rebuild pristine (`west build … -p auto`).
- **Check-in works but is rejected as `far` at close range**: lower
  `ATT_RSSI_THRESHOLD` in `src/app_config.h` toward the `rssi=` values you see
  printed on rejects.

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
reads today), forwarding the manufacturer-split fields too. The bridge owns the
serial port, so it also **echoes** the attendance lines (`ATT-RX …`, `NUS …`) to
the console instead of dropping them (it does not write attendance to Firestore -
the count is consumed by the app's `OK:<count>` notify):

```sh
pip install pyserial firebase-admin
python tools/firestore_bridge.py --port /dev/tty.usbmodemXXXX --cred sa.json
```

## Files

| File | Purpose |
| --- | --- |
| `src/main.c` | BLE bring-up, connectable iBeacon adv, passive scan, 10 s window emit, NUS connection lifecycle |
| `src/ble_window.c/.h` | Welford stats, FNV-1a bitmap, manufacturer split, JSON |
| `src/attendance.c/.h` | NUS RX `CHECKIN` handler, RSSI proximity gate, attendance counter, `attendance_display()` |
| `src/app_config.h` | Per-unit identity + thresholds (incl. `ATT_RSSI_THRESHOLD`) |
| `prj.conf` | Zephyr Kconfig (BT broadcaster+observer+peripheral/NUS, direct UART printk) |
| `tools/firestore_bridge.py` | Host-side serial -> Firestore forwarder (also echoes `ATT-RX`/`NUS` lines) |
