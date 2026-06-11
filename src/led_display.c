#include "led_display.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/printk.h>

/* ── LED Matrix (HT16K33) ─────────────────────────────────────────────────── */

#define LED_MATRIX_COUNT  128
#define MATRIX_COLS       16   /* physical columns in the 16x8 matrix */

/* 5-row x 3-col pixel font for digits 0–9.
 * Each row is a 3-bit mask: bit2=left, bit1=mid, bit0=right. */
static const uint8_t digit_font[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, /* 0 */
    {0b010, 0b110, 0b010, 0b010, 0b111}, /* 1 */
    {0b111, 0b001, 0b111, 0b100, 0b111}, /* 2 */
    {0b111, 0b001, 0b111, 0b001, 0b111}, /* 3 */
    {0b101, 0b101, 0b111, 0b001, 0b001}, /* 4 */
    {0b111, 0b100, 0b111, 0b001, 0b111}, /* 5 */
    {0b111, 0b100, 0b111, 0b101, 0b111}, /* 6 */
    {0b111, 0b001, 0b001, 0b001, 0b001}, /* 7 */
    {0b111, 0b101, 0b111, 0b101, 0b111}, /* 8 */
    {0b111, 0b101, 0b111, 0b001, 0b111}, /* 9 */
};

#define LED_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(holtek_ht16k33)
static const struct device *const led_dev = DEVICE_DT_GET(LED_NODE);

static void draw_digit(uint8_t digit, int col_offset, int row_offset)
{
    for (int row = 0; row < 5; row++) {
        uint8_t bits = digit_font[digit][row];
        for (int col = 0; col < 3; col++) {
            int led = (row + row_offset) * MATRIX_COLS + (col + col_offset);
            if ((bits >> (2 - col)) & 1) {
                led_on(led_dev, led);
            } else {
                led_off(led_dev, led);
            }
        }
    }
}

/* ── Battery Bar (TM1651, bit-banged) ─────────────────────────────────────── */

/* CLK = D10 = P1.12,  DIO = D11 = P1.13 */
#define TM1651_CLK 12
#define TM1651_DIO 13

static const struct device *gpio1_dev;

static void tm_delay(void) { k_busy_wait(50); }

static void tm_start(void)
{
    gpio_pin_set(gpio1_dev, TM1651_CLK, 1);
    gpio_pin_set(gpio1_dev, TM1651_DIO, 1);
    tm_delay();
    gpio_pin_set(gpio1_dev, TM1651_DIO, 0);
    tm_delay();
    gpio_pin_set(gpio1_dev, TM1651_CLK, 0);
    tm_delay();
}

static void tm_stop(void)
{
    gpio_pin_set(gpio1_dev, TM1651_CLK, 0);
    tm_delay();
    gpio_pin_set(gpio1_dev, TM1651_DIO, 0);
    tm_delay();
    gpio_pin_set(gpio1_dev, TM1651_CLK, 1);
    tm_delay();
    gpio_pin_set(gpio1_dev, TM1651_DIO, 1);
    tm_delay();
}

static void tm_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        gpio_pin_set(gpio1_dev, TM1651_CLK, 0);
        tm_delay();
        gpio_pin_set(gpio1_dev, TM1651_DIO, (byte >> i) & 1);
        tm_delay();
        gpio_pin_set(gpio1_dev, TM1651_CLK, 1);
        tm_delay();
    }
    /* ACK clock */
    gpio_pin_set(gpio1_dev, TM1651_CLK, 0);
    gpio_pin_configure(gpio1_dev, TM1651_DIO, GPIO_INPUT);
    tm_delay();
    gpio_pin_set(gpio1_dev, TM1651_CLK, 1);
    tm_delay();
    gpio_pin_set(gpio1_dev, TM1651_CLK, 0);
    gpio_pin_configure(gpio1_dev, TM1651_DIO, GPIO_OUTPUT_LOW);
    tm_delay();
}

/*
 * percent: 0–100, fills from bottom to top.
 *
 * Hardware mapping (0xC0 only, 0xC1 unused):
 *   bit6 (0x40) → bottom 2 segments
 *   bit5 (0x20) → 8th from top
 *   bit4 (0x10) → 7th from top
 *   bit3 (0x08) → 6th from top
 *   bit2 (0x04) → 4th,5th from top
 *   bit1 (0x02) → 2nd,3rd from top
 *   bit0 (0x01) → top segment
 *
 * Some segments are paired (same bit), so 7 distinct levels exist for 10 physical bars.
 */
static const uint8_t bat_levels[11] = {
    0x00,  /* 0%   → empty */
    0x40,  /* 10%  → bottom 2 bars */
    0x40,  /* 20%  → same (hardware limitation) */
    0x60,  /* 30%  → + 8th bar */
    0x70,  /* 40%  → + 7th bar */
    0x78,  /* 50%  → + 6th bar */
    0x7C,  /* 60%  → + 4th,5th bars */
    0x7C,  /* 70%  → same (hardware limitation) */
    0x7E,  /* 80%  → + 2nd,3rd bars */
    0x7E,  /* 90%  → same (hardware limitation) */
    0x7F,  /* 100% → + top bar, full */
};

static void battery_set_level(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    int bars = percent / 10;
    if (bars > 10) bars = 10;

    uint8_t high = bat_levels[bars];

    tm_start();
    tm_write_byte(0x44);
    tm_stop();

    tm_start();
    tm_write_byte(0xC0);
    tm_write_byte(high);
    tm_stop();

    tm_start();
    tm_write_byte(0x8A);
    tm_stop();
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void battery_raw(uint8_t high, uint8_t low)
{
    tm_start(); tm_write_byte(0x44); tm_stop();
    tm_start(); tm_write_byte(0xC0); tm_write_byte(high); tm_stop();
    tm_start(); tm_write_byte(0x44); tm_stop();
    tm_start(); tm_write_byte(0xC1); tm_write_byte(low);  tm_stop();
    tm_start(); tm_write_byte(0x8A); tm_stop();
}

int led_display_init(void)
{
    gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    gpio_pin_configure(gpio1_dev, TM1651_CLK, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(gpio1_dev, TM1651_DIO, GPIO_OUTPUT_HIGH);

    if (!device_is_ready(led_dev)) {
        printk("led_display: LED matrix not ready\n");
        return -ENODEV;
    }

    /* Clear both displays */
    for (int i = 0; i < LED_MATRIX_COUNT; i++) {
        led_off(led_dev, i);
    }
    battery_set_level(0);

    return 0;
}

void led_matrix_set_count(uint32_t count)
{
    uint32_t on = (count > LED_MATRIX_COUNT) ? LED_MATRIX_COUNT : count;

    for (uint32_t i = 0; i < on; i++) {
        led_on(led_dev, i);
    }
    for (uint32_t i = on; i < LED_MATRIX_COUNT; i++) {
        led_off(led_dev, i);
    }
}

void battery_display_set_count(uint32_t count, uint32_t capacity)
{
    if (capacity == 0) {
        return;
    }
    int percent = (int)((count * 100U) / capacity);
    if (percent > 100) percent = 100;
    battery_set_level(percent);
}

void led_display_update(uint32_t count, uint32_t capacity)
{
    /* Matrix shows the count as a 2-digit number; battery bar shows the fill
     * ratio against capacity. (led_matrix_set_count() lights one LED per head
     * instead of drawing the number - kept in the API for bring-up.) */
    led_matrix_set_number(count);
    battery_display_set_count(count, capacity);
}

void led_matrix_set_number(uint32_t count)
{
    if (count > 99) count = 99;

    uint8_t tens  = count / 10;
    uint8_t units = count % 10;

    for (int i = 0; i < LED_MATRIX_COUNT; i++) {
        led_off(led_dev, i);
    }

    /* tens digit: cols 2–4, rows 1–5 */
    draw_digit(tens,  2, 1);
    /* units digit: cols 10–12, rows 1–5 */
    draw_digit(units, 10, 1);
}
