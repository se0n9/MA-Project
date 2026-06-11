/*
 * COMPASS / Project HYBE - NUS attendance RX channel.
 *
 * Stands up a Nordic UART Service (NUS) GATT server so the phone app (BLE
 * central) can check a student in. The app connects, writes the bare signal
 * "CHECKIN\n" to the NUS RX characteristic, and disconnects.
 *
 * The beacon owns the counter: each CHECKIN is gated on the connection RSSI
 * (proximity), and only accepted taps increment attendance_count, which is
 * echoed to the UART console and rendered on the LED matrix + battery bar via
 * attendance_display(), and ACKed to the app ("OK:<count>" on accept, "FAR" on
 * reject). See firmware-nus-attendance-spec.md.
 */

#ifndef ATTENDANCE_H_
#define ATTENDANCE_H_

#include <stdint.h>

/*
 * Registers the NUS GATT service and its RX write handler. Call once after
 * bt_enable(). Returns 0 on success, or a negative errno on failure.
 */
int attendance_init(void);

/*
 * Surfaces the current attendance count to the operator: prints
 * "ATT-RX count=%u" to the UART console (for the host bridge) and renders the
 * count on the LED matrix + TM1651 battery bar (led_display_update()). Single
 * drop-in point for any display change - the signature and call sites stay put.
 */
void attendance_display(uint32_t count);

#endif /* ATTENDANCE_H_ */
