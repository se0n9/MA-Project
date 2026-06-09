/*
 * COMPASS / Project HYBE - Per-unit firmware configuration.
 *
 * Each nRF52840 installed at a different reading room gets a distinct
 * BEACON_ID + APP_IBEACON_MINOR. Keep APP_IBEACON_UUID identical across all
 * units and matching the Flutter active-path region UUID.
 */

#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

/* Logical location id reported in the payload. MUST match a Firestore doc id
 * registered in the app (_beaconMeta: beacon_A1 / beacon_A2 / beacon_A3). */
#define BEACON_ID "beacon_A1"

/* iBeacon identity (advertised for the active path). */
#define APP_IBEACON_UUID                                                       \
	0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2, 0xB0, 0x60, 0xD0,      \
		0xF5, 0xA7, 0x10, 0x96, 0xE0
#define APP_IBEACON_MAJOR 1
#define APP_IBEACON_MINOR 101
/* Calibrated RSSI at 1 m, two's-complement (0xC5 = -59 dBm). */
#define APP_IBEACON_MEASURED_POWER 0xC5

/* Passive scanning / aggregation. */
#define WINDOW_SECONDS 10
/* Relaxed to capture people farther from the ceiling-mounted unit. */
#define RSSI_THRESHOLD (-95)

/* Apple Bluetooth Company Identifier. */
#define COMPANY_ID_APPLE 0x004C

/* NUS attendance proximity gate (FR5): a CHECKIN is counted only if the
 * connection RSSI of the tapping phone is at least this value (closer => higher
 * / less negative). Close range is roughly -40..-65 dBm; tune on-site. */
#define ATT_RSSI_THRESHOLD (-70)

#endif /* APP_CONFIG_H_ */
