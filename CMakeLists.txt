cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(compass_beacon)

# Production firmware: BLE bring-up + passive window emit (main.c), the NUS
# attendance counter (attendance.c), the aggregation window (ble_window.c), and
# the LED-matrix / battery-bar renderer the accepted count is drawn on
# (led_display.c). attendance_display() -> led_display_update() is the wiring
# that puts the accumulated attendance count on the matrix.
target_sources(app PRIVATE
  src/main.c
  src/ble_window.c
  src/attendance.c
  src/led_display.c
)

# Standalone display bring-up test (its own main(), feeds simulated counts to
# the matrix). Mutually exclusive with src/main.c -- enable this and disable the
# four sources above to bench the display without BLE:
#   src/led_display_test.c
