/*
 * COMPASS / Project HYBE - nRF52840 dual-mode firmware.
 *
 * Runs two BLE roles concurrently:
 *   - Broadcaster: non-connectable iBeacon advertising (the "location id" the
 *     phone app detects for the ACTIVE path).
 *   - Observer:    passive scanning that aggregates ambient BLE traffic into
 *     fixed windows for the PASSIVE crowd estimate, splitting Apple vs other
 *     manufacturers.
 *
 * Every WINDOW_SECONDS the aggregated window is emitted as a single JSON line
 * on the console (UART/RTT). A host bridge forwards each line to Firestore,
 * adding server_ts. See README.md.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

#include "app_config.h"
#include "ble_window.h"

/* ---- iBeacon advertising data (broadcaster role) ----------------------- */

static const uint8_t ibeacon_mfg_data[] = {
	0x4C, 0x00,             /* Apple Company ID (little-endian) */
	0x02, 0x15,             /* iBeacon type + remaining length (21 bytes) */
	APP_IBEACON_UUID,       /* 16-byte proximity UUID */
	(APP_IBEACON_MAJOR >> 8) & 0xFF, APP_IBEACON_MAJOR & 0xFF, /* major BE */
	(APP_IBEACON_MINOR >> 8) & 0xFF, APP_IBEACON_MINOR & 0xFF, /* minor BE */
	APP_IBEACON_MEASURED_POWER,
};

static const struct bt_data ibeacon_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, ibeacon_mfg_data,
		sizeof(ibeacon_mfg_data)),
};

/* ---- Passive aggregation window (observer role) ------------------------ */

static struct ble_window window;
static struct k_mutex window_mutex;
static struct k_work_delayable window_work;

/* Manufacturer-classification context for one parsed advertisement. */
struct adv_scan_ctx {
	bool is_apple;
};

static bool adv_data_cb(struct bt_data *data, void *user_data)
{
	struct adv_scan_ctx *ctx = user_data;

	if (data->type == BT_DATA_MANUFACTURER_DATA && data->data_len >= 2) {
		uint16_t company = sys_get_le16(data->data);

		if (company == COMPANY_ID_APPLE) {
			ctx->is_apple = true;
		}
	}
	return true; /* keep parsing remaining AD structures */
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *buf)
{
	ARG_UNUSED(adv_type);

	/* Drop weak packets so distant rooms/neighbours do not inflate counts. */
	if (rssi < RSSI_THRESHOLD) {
		return;
	}

	struct adv_scan_ctx ctx = {.is_apple = false};

	bt_data_parse(buf, adv_data_cb, &ctx);

	k_mutex_lock(&window_mutex, K_FOREVER);
	ble_window_add(&window, addr->a.val, rssi, ctx.is_apple);
	k_mutex_unlock(&window_mutex);
}

static void window_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct ble_window snapshot;

	k_mutex_lock(&window_mutex, K_FOREVER);
	snapshot = window;
	ble_window_reset(&window);
	k_mutex_unlock(&window_mutex);

	char json[256];
	uint32_t uptime_sec = (uint32_t)(k_uptime_get() / 1000);

	int n = ble_window_format_json(&snapshot, BEACON_ID, uptime_sec, json,
				       sizeof(json));
	/* "TX -> " prefix lets the host bridge pick data lines out of the
	 * interleaved boot banner / CONFIG_LOG output on the same UART. */
	if (n > 0 && n < (int)sizeof(json)) {
		printk("TX -> %s\n", json);
	} else {
		printk("TX -> {\"id\":\"%s\",\"error\":\"json_truncated\"}\n",
		       BEACON_ID);
	}

	k_work_schedule(&window_work, K_SECONDS(WINDOW_SECONDS));
}

/* ---- Bring-up ---------------------------------------------------------- */

static int start_ibeacon_adv(void)
{
	/* Non-connectable, non-scannable legacy advertising for iBeacon. */
	return bt_le_adv_start(BT_LE_ADV_NCONN, ibeacon_ad,
			       ARRAY_SIZE(ibeacon_ad), NULL, 0);
}

static int start_passive_scan(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	return bt_le_scan_start(&scan_param, scan_cb);
}

int main(void)
{
	int err;

	k_mutex_init(&window_mutex);
	ble_window_reset(&window);

	err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed (err %d)\n", err);
		return 0;
	}
	printk("COMPASS dual-mode firmware started (beacon=%s)\n", BEACON_ID);

	err = start_ibeacon_adv();
	if (err) {
		printk("iBeacon advertising failed (err %d)\n", err);
		return 0;
	}
	printk("iBeacon advertising: major=%d minor=%d\n", APP_IBEACON_MAJOR,
	       APP_IBEACON_MINOR);

	err = start_passive_scan();
	if (err) {
		printk("Passive scan failed (err %d)\n", err);
		return 0;
	}
	printk("Passive scan running, window=%ds threshold=%ddBm\n",
	       WINDOW_SECONDS, RSSI_THRESHOLD);

	k_work_init_delayable(&window_work, window_work_handler);
	k_work_schedule(&window_work, K_SECONDS(WINDOW_SECONDS));

	return 0;
}
