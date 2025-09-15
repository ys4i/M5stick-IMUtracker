#pragma once

// Configuration constants for logging

// Output data rate in Hz.
// SH200Q側の実機設定は離散値のみ対応のため、最も近い値に丸めて設定します。
// 目安: Accel ODR = {8,16,32,64,128,256,512,1024} Hz, Gyro ODR = {64,128,256,500,1000} Hz
// 推奨: 128Hz（Accel=128Hz, Gyro≈128Hzに設定されます）
constexpr uint16_t ODR_HZ = 128;
// Accelerometer range in g (2,4,8,16)
constexpr uint16_t RANGE_G = 8;
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
