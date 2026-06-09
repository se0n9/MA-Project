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
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>

#include <bluetooth/services/nus.h>

#include "app_config.h"
#include "ble_window.h"
#include "attendance.h"

/* ---- iBeacon advertising data (broadcaster role) ----------------------- */

static const uint8_t ibeacon_mfg_data[] = {
	0x4C, 0x00,             /* Apple Company ID (little-endian) */
	0x02, 0x15,             /* iBeacon type + remaining length (21 bytes) */
	APP_IBEACON_UUID,       /* 16-byte proximity UUID */
	(APP_IBEACON_MAJOR >> 8) & 0xFF, APP_IBEACON_MAJOR & 0xFF, /* major BE */
	(APP_IBEACON_MINOR >> 8) & 0xFF, APP_IBEACON_MINOR & 0xFF, /* minor BE */
	APP_IBEACON_MEASURED_POWER,
};

/*
 * Advertising payload (Approach A): a single CONNECTABLE legacy set carrying the
 * iBeacon manufacturer data in the AD, plus the NUS service UUID and device name
 * in the scan response so the app can scan-filter by UUID (and fall back to the
 * name). Flags advertise LE General Discoverable + BR/EDR-not-supported; iOS /
 * Android ranging keys on the manufacturer payload, not the flags, so the
 * iBeacon stays detectable.
 */
static const struct bt_data connectable_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, ibeacon_mfg_data,
		sizeof(ibeacon_mfg_data)),
};

/* Scan response: 18 B (128-bit NUS UUID) + 13 B (name) = 31 B, the legacy max. */
static const struct bt_data connectable_sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
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

static int start_connectable_adv(void)
{
	/*
	 * Connectable legacy advertising carrying the iBeacon AD + NUS scan
	 * response. Legacy connectable advertising auto-stops once a central
	 * connects, so the disconnected callback restarts it (see below).
	 */
	return bt_le_adv_start(BT_LE_ADV_CONN, connectable_ad,
			       ARRAY_SIZE(connectable_ad), connectable_sd,
			       ARRAY_SIZE(connectable_sd));
}

/* ---- NUS connection lifecycle (peripheral role) ------------------------ */

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);

	if (err) {
		printk("NUS connection failed (err %u)\n", err);
		return;
	}
	printk("NUS connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	printk("NUS disconnected (reason %u)\n", reason);

	/* Approach A: re-advertise so the next attendance write can connect. */
	int err = start_connectable_adv();

	if (err) {
		printk("connectable adv restart failed (err %d)\n", err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

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

	err = attendance_init();
	if (err) {
		printk("NUS (attendance) init failed (err %d)\n", err);
		return 0;
	}

	err = start_connectable_adv();
	if (err) {
		printk("connectable advertising failed (err %d)\n", err);
		return 0;
	}
	printk("connectable iBeacon+NUS adv: major=%d minor=%d\n",
	       APP_IBEACON_MAJOR, APP_IBEACON_MINOR);

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
