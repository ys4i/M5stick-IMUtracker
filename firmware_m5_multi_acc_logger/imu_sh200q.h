#pragma once
#include <M5StickC.h>
#include <Arduino.h>
#include "config.h"

// Use the built-in M5.IMU helper instead of manual I2C transactions.
// The library internally configures the SH200Q sensor, so initialization
// is simply a call to M5.IMU.Init().

inline bool imu_init() {
    M5.IMU.Init();
    return true;
}

// Read raw accelerometer values via the M5.IMU helper. The function outputs
// 16-bit ADC values matching the original implementation.
inline bool imu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az) {
    M5.IMU.getAccelAdc(&ax, &ay, &az);
    return true;
}
