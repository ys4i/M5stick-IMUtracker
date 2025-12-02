#pragma once
// MPU6886 access for M5Unified-based boards (e.g., Core2).
// After M5.begin() (Unified) we override key registers to match config.h,
// then read raw int16 samples via I2C. We avoid float conversions for consistency.

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "board_hal.h"

#define MPU6886_ADDR 0x68

// Registers
#define MPU6886_REG_PWR_MGMT_1   0x6B
#define MPU6886_REG_SMPLRT_DIV   0x19
#define MPU6886_REG_CONFIG       0x1A
#define MPU6886_REG_GYRO_CONFIG  0x1B
#define MPU6886_REG_ACCEL_CONFIG 0x1C
#define MPU6886_REG_ACCEL_CONFIG2 0x1D
#define MPU6886_REG_ACCEL_XOUT_H 0x3B
#define MPU6886_REG_GYRO_XOUT_H  0x43

inline void mpu_write_u8(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MPU6886_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

inline void mpu_read_xyz16(uint8_t start_reg, int16_t& x, int16_t& y, int16_t& z) {
    uint8_t buf[6] = {0};
    Wire.beginTransmission(MPU6886_ADDR);
    Wire.write(start_reg);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU6886_ADDR, (uint8_t)6);
    for (int i = 0; i < 6 && Wire.available(); ++i) buf[i] = Wire.read();
    x = (int16_t)((buf[0] << 8) | buf[1]);
    y = (int16_t)((buf[2] << 8) | buf[3]);
    z = (int16_t)((buf[4] << 8) | buf[5]);
}

inline uint8_t map_acc_range_mpu(uint16_t g) {
    if (g >= 16) return 0x18; // +/-16g
    if (g >= 8)  return 0x10; // +/-8g
    if (g >= 4)  return 0x08; // +/-4g
    return 0x00;             // +/-2g
}

inline uint8_t map_gyro_range_mpu(uint16_t dps) {
    if (dps >= 2000) return 0x18; // +/-2000 dps
    if (dps >= 1000) return 0x10; // +/-1000 dps
    if (dps >= 500)  return 0x08; // +/-500 dps
    return 0x00;                 // +/-250 dps
}

inline uint8_t map_dlpf_cfg(uint16_t hz) {
    // CONFIG/ACCEL_CONFIG2 DLPF mapping (gyro/accel共通)
    // 0:260Hz/256Hz, 1:184Hz, 2:94Hz, 3:44Hz, 4:21Hz, 5:10Hz, 6:5Hz
    if (hz >= 180) return 1;
    if (hz >= 90) return 2;
    if (hz >= 50) return 3;
    if (hz >= 20) return 4;
    if (hz >= 8)  return 5;
    if (hz >= 5)  return 6;
    return 0; // off/260Hz
}

inline void mpu_set_odr(uint16_t odr_hz, bool dlpf_off) {
    // Gyro output rate: 8kHz when DLPF off, 1kHz when on.
    uint16_t base = dlpf_off ? 8000 : 1000;
    if (odr_hz == 0) odr_hz = 200;
    uint16_t div = (odr_hz >= base) ? 0 : (uint16_t)((base / odr_hz) - 1);
    if (div > 255) div = 255;
    mpu_write_u8(MPU6886_REG_SMPLRT_DIV, (uint8_t)div);
}

inline bool imu_init() {
    // M5Unified already initialized I2C/IMU; override key registers.
    mpu_write_u8(MPU6886_REG_PWR_MGMT_1, 0x00); // wake
    delay(10);
    uint8_t dlpf = map_dlpf_cfg(0); // default off
    bool dlpf_off = (dlpf == 0);
    mpu_write_u8(MPU6886_REG_CONFIG, dlpf);
    mpu_set_odr(ODR_HZ, dlpf_off);
    mpu_write_u8(MPU6886_REG_GYRO_CONFIG, map_gyro_range_mpu(GYRO_RANGE_DPS));
    mpu_write_u8(MPU6886_REG_ACCEL_CONFIG, map_acc_range_mpu(RANGE_G));
    mpu_write_u8(MPU6886_REG_ACCEL_CONFIG2, dlpf);
    return true;
}

// One-time gyro bias
static int32_t s_gbias_x = 0, s_gbias_y = 0, s_gbias_z = 0;
static bool s_imu_calibrated = false;
inline bool imu_is_calibrated() { return s_imu_calibrated; }
inline void imu_calibrate_once(uint16_t samples = 512) {
    if (s_imu_calibrated) return;
    int64_t sx = 0, sy = 0, sz = 0;
    for (uint16_t i = 0; i < samples; ++i) {
        int16_t gx, gy, gz;
        mpu_read_xyz16(MPU6886_REG_GYRO_XOUT_H, gx, gy, gz);
        sx += gx; sy += gy; sz += gz;
        delay(1000UL / (ODR_HZ ? ODR_HZ : 200));
    }
    s_gbias_x = (int32_t)(sx / (int32_t)samples);
    s_gbias_y = (int32_t)(sy / (int32_t)samples);
    s_gbias_z = (int32_t)(sz / (int32_t)samples);
    s_imu_calibrated = true;
}

inline bool imu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az) {
    mpu_read_xyz16(MPU6886_REG_ACCEL_XOUT_H, ax, ay, az);
    return true;
}

inline bool imu_read_gyro_raw(int16_t& gx, int16_t& gy, int16_t& gz) {
    int16_t rx, ry, rz;
    mpu_read_xyz16(MPU6886_REG_GYRO_XOUT_H, rx, ry, rz);
    gx = (int16_t)(rx - s_gbias_x);
    gy = (int16_t)(ry - s_gbias_y);
    gz = (int16_t)(rz - s_gbias_z);
    return true;
}
