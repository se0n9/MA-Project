#ifndef LED_DISPLAY_H
#define LED_DISPLAY_H

#include <stdint.h>

/*
 * Initialize LED matrix (HT16K33) and battery bar (TM1651).
 * Call once after boot, before any update calls.
 * Returns 0 on success, negative errno on failure.
 */
int led_display_init(void);

/*
 * Light up `count` LEDs on the matrix (index 0 … min(count,128)-1).
 * All higher-index LEDs are turned off.
 */
void led_matrix_set_count(uint32_t count);

/*
 * Drive the TM1651 battery bar to (count * 100) / capacity percent.
 * capacity must be > 0.  Result is clamped to 0–100.
 */
void battery_display_set_count(uint32_t count, uint32_t capacity);

/*
 * Convenience wrapper: calls both led_matrix_set_count and
 * battery_display_set_count in one call.
 */
void led_display_update(uint32_t count, uint32_t capacity);

/*
 * Diagnostic: send raw bytes directly to TM1651 registers.
 * high → 0xC0, low → 0xC1.  Use this to map bits to physical segments.
 */
void battery_raw(uint8_t high, uint8_t low);

/*
 * Display count as a 2-digit number (00–99) on the LED matrix using a 5x3
 * pixel font.  Values above 99 are clamped to 99.
 * Assumes a 16-column x 8-row matrix (128 LEDs, index = row*16 + col).
 * Tens digit is drawn at columns 2–4, units digit at columns 10–12, rows 1–5.
 */
void led_matrix_set_number(uint32_t count);

#endif /* LED_DISPLAY_H */
