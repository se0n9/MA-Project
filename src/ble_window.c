/*
 * COMPASS / Project HYBE - Passive BLE aggregation window implementation.
 */

#include "ble_window.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* FNV-1a 32-bit constants. */
#define FNV1A_OFFSET_BASIS 2166136261u
#define FNV1A_PRIME 16777619u

void ble_window_reset(struct ble_window *w)
{
	memset(w, 0, sizeof(*w));
	/* min/max sentinels so the first sample sets both. */
	w->rssi_min = 127;
	w->rssi_max = -128;
}

static uint32_t fnv1a_hash(const uint8_t *data, size_t len)
{
	uint32_t hash = FNV1A_OFFSET_BASIS;

	for (size_t i = 0; i < len; i++) {
		hash ^= data[i];
		hash *= FNV1A_PRIME;
	}
	return hash;
}

void ble_window_add(struct ble_window *w, const uint8_t addr6[6], int8_t rssi,
		    bool is_apple)
{
	w->packet_count++;
	if (is_apple) {
		w->apple_packet_count++;
	}

	/* Welford online mean/variance. */
	w->rssi_n++;
	const double delta = (double)rssi - w->rssi_mean;
	w->rssi_mean += delta / (double)w->rssi_n;
	const double delta2 = (double)rssi - w->rssi_mean;
	w->rssi_m2 += delta * delta2;

	if (rssi < w->rssi_min) {
		w->rssi_min = rssi;
	}
	if (rssi > w->rssi_max) {
		w->rssi_max = rssi;
	}

	/* Mark presence in the FNV-1a hashed bitmap. */
	const uint32_t idx = fnv1a_hash(addr6, 6) % BLE_WINDOW_BITMAP_BITS;
	w->mac_bitmap[idx / 8] |= (uint8_t)(1u << (idx % 8));
}

uint32_t ble_window_unique_macs(const struct ble_window *w)
{
	uint32_t bits = 0;

	for (size_t i = 0; i < BLE_WINDOW_BITMAP_BYTES; i++) {
		uint8_t b = w->mac_bitmap[i];

		while (b) {
			bits += (b & 1u);
			b >>= 1;
		}
	}
	return bits;
}

double ble_window_rssi_std(const struct ble_window *w)
{
	if (w->rssi_n < 2) {
		return 0.0;
	}
	return sqrt(w->rssi_m2 / (double)w->rssi_n);
}

int ble_window_rssi_avg(const struct ble_window *w)
{
	if (w->rssi_n == 0) {
		return 0;
	}
	return (int)lround(w->rssi_mean);
}

int ble_window_format_json(const struct ble_window *w, const char *beacon_id,
			   uint32_t uptime_sec, char *buf, size_t buf_len)
{
	const uint32_t apple = w->apple_packet_count;
	const uint32_t other = w->packet_count - apple;

	/* Format std to one decimal as integer tenths to avoid pulling in
	 * floating-point printf support (std is always >= 0). */
	const int std_x10 = (int)(ble_window_rssi_std(w) * 10.0 + 0.5);

	return snprintf(buf, buf_len,
			"{\"id\":\"%s\","
			"\"packet_count\":%u,"
			"\"apple_packet_count\":%u,"
			"\"other_packet_count\":%u,"
			"\"rssi_avg\":%d,"
			"\"rssi_std\":%d.%d,"
			"\"rssi_min\":%d,"
			"\"rssi_max\":%d,"
			"\"unique_mac_count\":%u,"
			"\"uptime_sec\":%u}",
			beacon_id, w->packet_count, apple, other,
			ble_window_rssi_avg(w), std_x10 / 10, std_x10 % 10,
			(int)(w->rssi_n ? w->rssi_min : 0),
			(int)(w->rssi_n ? w->rssi_max : 0),
			ble_window_unique_macs(w), uptime_sec);
}
