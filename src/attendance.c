/*
 * COMPASS / Project HYBE - NUS attendance RX channel (see attendance.h).
 *
 * The beacon owns the attendance counter. On each "CHECKIN" RX write it gates
 * the tap on the connection RSSI (proximity) and, if close enough, increments
 * the counter, prints it, and ACKs the central.
 *
 * The RSSI read is an HCI Read_RSSI command issued synchronously
 * (bt_hci_cmd_send_sync), which blocks - and the NUS RX callback runs in the BT
 * RX context where blocking is forbidden (spec 7). So the RX callback stays
 * short (validate "CHECKIN", ref the connection, submit work) and the actual
 * gate + increment + notify happen on the system workqueue.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/net/buf.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>

#include <bluetooth/services/nus.h>

#include "app_config.h"
#include "attendance.h"

/* The signal the app writes. Matched by prefix so a future "CHECKIN:<token>\n"
 * (per-user de-dup, spec 6) stays non-breaking. */
#define CHECKIN_PREFIX     "CHECKIN"
#define CHECKIN_PREFIX_LEN (sizeof(CHECKIN_PREFIX) - 1)

/* Beacon-owned counter (resets to 0 on reboot; persistence is future work). */
static uint32_t attendance_count;

/* One in-flight tap at a time (CONFIG_BT_MAX_CONN=1, single deliberate tap).
 * pending_conn hands the ref'd connection from the RX callback to the work. */
static struct bt_conn *pending_conn;
static struct k_work checkin_work;
static struct k_spinlock lock;

/* Reads the live connection RSSI via HCI Read_RSSI (opcode 0x1405).
 * Returns 0 and writes *rssi on success, or a negative errno. */
static int conn_rssi(struct bt_conn *conn, int8_t *rssi)
{
	uint16_t handle;
	int err = bt_hci_get_conn_handle(conn, &handle);

	if (err) {
		return err;
	}

	struct net_buf *buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI,
						sizeof(struct bt_hci_cp_read_rssi));
	if (buf == NULL) {
		return -ENOBUFS;
	}

	struct bt_hci_cp_read_rssi *cp = net_buf_add(buf, sizeof(*cp));

	cp->handle = sys_cpu_to_le16(handle);

	struct net_buf *rsp = NULL;

	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
	if (err) {
		return err;
	}

	struct bt_hci_rp_read_rssi *rp = (void *)rsp->data;

	if (rp->status) {
		err = -EIO;
	} else {
		*rssi = rp->rssi;
	}
	net_buf_unref(rsp);
	return err;
}

/* Best-effort TX notify; ignored if the central never subscribed. */
static void notify(struct bt_conn *conn, const char *msg)
{
	(void)bt_nus_send(conn, (const uint8_t *)msg, (uint16_t)strlen(msg));
}

/* System-workqueue handler: RSSI gate -> increment -> display -> ACK. */
static void checkin_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_spinlock_key_t key = k_spin_lock(&lock);
	struct bt_conn *conn = pending_conn;

	pending_conn = NULL;
	k_spin_unlock(&lock, key);

	if (conn == NULL) {
		return;
	}

	int8_t rssi = 0;
	int err = conn_rssi(conn, &rssi);

	if (err) {
		/* Fail-safe: an unreadable RSSI is treated as a reject. */
		printk("ATT-RX rejected (rssi read failed, err %d)\n", err);
		notify(conn, "FAR\n");
	} else if (rssi < ATT_RSSI_THRESHOLD) {
		printk("ATT-RX rejected (far, rssi=%d dBm)\n", rssi);
		notify(conn, "FAR\n");
	} else {
		attendance_count++;
		attendance_display(attendance_count);

		char ack[16];
		int n = snprintf(ack, sizeof(ack), "OK:%u\n", attendance_count);

		if (n > 0 && n < (int)sizeof(ack)) {
			(void)bt_nus_send(conn, (const uint8_t *)ack,
					  (uint16_t)n);
		}
	}

	bt_conn_unref(conn);
}

/* NUS RX write handler: app -> device. Short and non-blocking. */
static void nus_received(struct bt_conn *conn, const uint8_t *const data,
			 uint16_t len)
{
	if (data == NULL || len < CHECKIN_PREFIX_LEN ||
	    memcmp(data, CHECKIN_PREFIX, CHECKIN_PREFIX_LEN) != 0) {
		printk("ATT-RX malformed payload (%u bytes), dropped\n", len);
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);
	bool busy = (pending_conn != NULL);

	if (!busy) {
		pending_conn = bt_conn_ref(conn);
	}
	k_spin_unlock(&lock, key);

	if (busy) {
		printk("ATT-RX busy, tap dropped\n");
		return;
	}

	k_work_submit(&checkin_work);
}

static struct bt_nus_cb nus_cb = {
	.received = nus_received,
};

void attendance_display(uint32_t count)
{
	/* Phase 1 placeholder. Replace with the LED-matrix render later. */
	printk("ATT-RX count=%u\n", count);
}

int attendance_init(void)
{
	k_work_init(&checkin_work, checkin_work_handler);
	return bt_nus_init(&nus_cb);
}
