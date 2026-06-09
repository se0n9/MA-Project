/*
 * COMPASS / Project HYBE - Passive BLE aggregation window.
 *
 * Accumulates ambient BLE advertising packets over a fixed time window and
 * produces the Firestore-bound metrics: total / Apple / other packet counts,
 * RSSI mean+std (Welford), RSSI min/max, and an approximate unique-MAC count
 * via an FNV-1a hashed bitmap.
 */

#ifndef BLE_WINDOW_H_
#define BLE_WINDOW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* FNV-1a hash bitmap size for unique-MAC estimation (bits). */
#define BLE_WINDOW_BITMAP_BITS 1024
#define BLE_WINDOW_BITMAP_BYTES (BLE_WINDOW_BITMAP_BITS / 8)

struct ble_window {
	uint32_t packet_count;
	uint32_t apple_packet_count;

	/* Welford running stats over RSSI (signed dBm). */
	uint32_t rssi_n;
	double rssi_mean;
	double rssi_m2;
	int8_t rssi_min;
	int8_t rssi_max;

	/* FNV-1a hashed presence bitmap for unique BLE addresses. */
	uint8_t mac_bitmap[BLE_WINDOW_BITMAP_BYTES];
};

/* Clears all counters/stats for a fresh window. */
void ble_window_reset(struct ble_window *w);

/*
 * Records one accepted advertising packet.
 *   addr6    : 6-byte BLE device address (little-endian, as from bt_addr_le_t).
 *   rssi     : received signal strength in dBm.
 *   is_apple : true if the manufacturer Company ID was Apple (0x004C).
 */
void ble_window_add(struct ble_window *w, const uint8_t addr6[6], int8_t rssi,
		    bool is_apple);

/* Approximate count of distinct addresses (set bits in the bitmap). */
uint32_t ble_window_unique_macs(const struct ble_window *w);

/* Population RSSI standard deviation; 0 if fewer than 2 samples. */
double ble_window_rssi_std(const struct ble_window *w);

/* Rounded RSSI mean; 0 if no samples. */
int ble_window_rssi_avg(const struct ble_window *w);

/*
 * Serialises the window as a single-line JSON object matching the Firestore
 * schema (server_ts is added downstream by the host bridge). Returns the
 * number of bytes written (excluding NUL), or a negative value on truncation.
 */
int ble_window_format_json(const struct ble_window *w, const char *beacon_id,
			   uint32_t uptime_sec, char *buf, size_t buf_len);

#endif /* BLE_WINDOW_H_ */
