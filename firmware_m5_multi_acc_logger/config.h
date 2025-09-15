#pragma once

// Configuration constants for logging

// Output data rate in Hz (supports 100,200,400,1000)
constexpr uint16_t ODR_HZ = 100;
// Accelerometer range in g (2,4,8,16)
constexpr uint16_t RANGE_G = 4;
// Gyroscope range in dps (250, 500, 1000, 2000)
constexpr uint16_t GYRO_RANGE_DPS = 2000;
// Log file name stored in LittleFS
constexpr const char* LOG_FILE_NAME = "/ACCLOG.BIN";
// Serial baud rate for communication
constexpr unsigned long SERIAL_BAUD = 115200;

// LSB per g for ±4g on SH200Q (datasheet value)
constexpr float LSB_PER_G = 8192.0f;
// For CSV scaling we assume symmetric mapping: LSB_PER_DPS = 32768 / range_dps

// LCD backlight brightness (M5StickC AXP192 ScreenBreath 0..12)
constexpr uint8_t LCD_BRIGHT_ACTIVE = 100;  // brightness when screen is on
constexpr uint8_t LCD_BRIGHT_OFF    = 0;   // brightness when screen is off
