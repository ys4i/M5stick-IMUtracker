#pragma once

// Configuration constants for logging

// Output data rate in Hz (supports 100,200,400,1000)
constexpr uint16_t ODR_HZ = 200;
// Accelerometer range in g (2,4,8,16)
constexpr uint16_t RANGE_G = 4;
// Log file name stored in LittleFS
constexpr const char* LOG_FILE_NAME = "/ACCLOG.BIN";
// Serial baud rate for communication
constexpr unsigned long SERIAL_BAUD = 921600;

// LSB per g for ±4g on MPU6886 (datasheet value)
constexpr float LSB_PER_G = 8192.0f;
