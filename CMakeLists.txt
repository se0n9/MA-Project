cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(compass_beacon)

target_sources(app PRIVATE
  # src/main.c
  # src/ble_window.c
  # src/attendance.c
  src/led_display_test.c
  src/led_display.c
)
