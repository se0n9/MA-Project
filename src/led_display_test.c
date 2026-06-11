/*
 * led_display_test.c
 *
 * Simulates attendance count arriving from BLE and verifies
 * that the LED matrix and battery bar update correctly on each change.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "led_display.h"

#define TEST_CAPACITY 50U

static const uint32_t sim_counts[] = {
    0, 1, 2, 5, 10, 15, 20, 25, 30, 40, 50, 55
};

static void on_attendance_update(uint32_t new_count)
{
    static uint32_t last_count = UINT32_MAX;

    if (new_count == last_count) {
        return;
    }

    last_count = new_count;

    uint32_t percent = (new_count * 100U) / TEST_CAPACITY;
    if (percent > 100) percent = 100;

    printk("Attendance: %u / %u  (%u%%)\n", new_count, TEST_CAPACITY, percent);

    led_display_update(new_count, TEST_CAPACITY);
}

int main(void)
{
    printk("=== led_display_test start ===\n");

    if (led_display_init() != 0) {
        printk("Display init failed\n");
        return 0;
    }

    for (int i = 0; i < (int)(sizeof(sim_counts) / sizeof(sim_counts[0])); i++) {
        on_attendance_update(sim_counts[i]);
        k_sleep(K_MSEC(1000));
    }

    printk("=== led_display_test done ===\n");

    while (1) {
        k_sleep(K_MSEC(1000));
    }
    return 0;
}
