#pragma once

// Configuration constants for logging

// Output data rate in Hz (rounded to nearest supported value per IMU).
// SH200Q accel ODR set from {8,16,32,64,128,256,512,1024} Hz (nearest).
// MPU6886 ODR derived from SMPLRT_DIV (base 1kHz with DLPF on / 8kHz off).
constexpr uint16_t ODR_HZ = 200;
// Accelerometer range in g (2,4,8,16). Rounded to nearest supported.
constexpr uint16_t RANGE_G = 8;
// Gyroscope range in dps (250, 500, 1000, 2000). Rounded to nearest supported.
constexpr uint16_t GYRO_RANGE_DPS = 2000;
// DLPF setting in Hz. Use 0 to disable (default). Otherwise best-effort mapping
// to nearest supported value (e.g., 50 -> ~44Hz, 92 -> ~94Hz on MPU6886).
constexpr uint16_t DLPF_HZ = 0;
// Log file name stored in LittleFS
constexpr const char* LOG_FILE_NAME = "/ACCLOG.BIN";
// Enable on-device debug overlay (IMU/I2C info) on LCD
constexpr bool DEBUG_MODE = false;
// Interval for Serial debug printing of raw IMU data when DEBUG_MODE is true (milliseconds)
constexpr uint32_t DEBUG_RAW_PRINT_INTERVAL_MS = 200;
// Serial baud rate for communication
// Default 115200 (PCツールは高速→低速の順で自動プローブ)
constexpr unsigned long SERIAL_BAUD = 115200;

// Calibration behavior
// Delay after long-press before starting calibration (seconds)
constexpr uint8_t CALIB_DELAY_SEC = 2;

// LSB per g for ±4g on SH200Q (datasheet value)
constexpr float LSB_PER_G = 8192.0f;
// For CSV scaling we assume symmetric mapping: LSB_PER_DPS = 32768 / range_dps

// LCD backlight brightness (0..100)
constexpr uint8_t LCD_BRIGHT_ACTIVE = 100;  // brightness when screen is on
constexpr uint8_t LCD_BRIGHT_OFF    = 0;    // brightness when screen is off
// LCD rotation (0=default). "画面回転なし"の方針で 0 を既定にする。
constexpr uint8_t LCD_ROTATION = 0;
