#pragma once
#include <M5StickC.h>
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// Use the built-in M5.IMU helper instead of manual I2C transactions.
// The library internally configures the SH200Q sensor, so initialization
// is simply a call to M5.IMU.Init().

// SH200Q register map (7-bit address 0x6C)
#ifndef SH200I_ADDRESS
#define SH200I_ADDRESS     0x6C
#endif
#define SH200I_ACC_CONFIG  0x0E
#define SH200I_GYRO_CONFIG 0x0F
#define SH200I_GYRO_DLPF   0x11
#define SH200I_FIFO_CONFIG 0x12
#define SH200I_ACC_RANGE   0x16
#define SH200I_GYRO_RANGE  0x2B

inline void sh200q_write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(SH200I_ADDRESS);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

inline uint8_t sh200q_read_u8(uint8_t reg) {
    Wire1.beginTransmission(SH200I_ADDRESS);
    Wire1.write(reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom(SH200I_ADDRESS, (uint8_t)1);
    if (Wire1.available()) return Wire1.read();
    return 0;
}

inline uint8_t map_acc_range_value(uint16_t range_g) {
    // 0x00: ±4g, 0x01: ±8g, 0x02: ±16g
    if (range_g >= 16) return 0x02;
    if (range_g >= 8)  return 0x01;
    return 0x00; // default to ±4g
}

inline uint8_t map_gyro_range_value(uint16_t dps) {
    // Datasheet mapping not fully documented here; 0x00 is ±2000dps in M5 lib
    // Favor widest range to avoid clipping when unsure
    if (dps >= 2000) return 0x00; // ±2000 dps
    if (dps >= 1000) return 0x01; // guess mapping
    if (dps >= 500)  return 0x02;
    if (dps >= 250)  return 0x03;
    return 0x04; // 125 dps (guess)
}

inline uint8_t map_acc_odr_reg(uint16_t odr_hz) {
    // Supported: 1024, 512, 256, 128, 64, 32, 16, 8 Hz
    // Register values: 0x81, 0x89, 0x91, 0x99, 0xA1, 0xA9, 0xB1, 0xB9
    const struct { uint16_t hz; uint8_t reg; } lut[] = {
        {1024, 0x81}, {512, 0x89}, {256, 0x91}, {128, 0x99},
        {  64, 0xA1}, { 32, 0xA9}, { 16, 0xB1}, {  8, 0xB9},
    };
    // pick nearest
    uint8_t best = lut[2].reg; // default 256Hz
    uint32_t best_err = 0xFFFFFFFFu;
    for (auto &e : lut) {
        uint32_t err = (odr_hz > e.hz) ? (odr_hz - e.hz) : (e.hz - odr_hz);
        if (err < best_err) { best_err = err; best = e.reg; }
    }
    return best;
}

inline uint8_t map_gyro_odr_reg(uint16_t odr_hz) {
    // Supported: 1000, 500, 256, 128, 64 Hz (128/64 are inferred)
    // Register values pattern: 0x11, 0x13, 0x15, 0x17, 0x19 ...
    const struct { uint16_t hz; uint8_t reg; } lut[] = {
        {1000, 0x11}, {500, 0x13}, {256, 0x15}, {128, 0x17}, { 64, 0x19},
    };
    uint8_t best = lut[1].reg; // default 500Hz
    uint32_t best_err = 0xFFFFFFFFu;
    for (auto &e : lut) {
        uint32_t err = (odr_hz > e.hz) ? (odr_hz - e.hz) : (e.hz - odr_hz);
        if (err < best_err) { best_err = err; best = e.reg; }
    }
    return best;
}

inline bool imu_init() {
    // Initialize IMU and I2C
    M5.IMU.Init();

    // Ensure Wire1 is initialized (M5.IMU.Init usually does this)
    // Configure ODR and ranges directly on SH200Q to match config.h
    const uint8_t acc_cfg = map_acc_odr_reg(ODR_HZ);
    const uint8_t gyr_cfg = map_gyro_odr_reg(ODR_HZ);
    const uint8_t acc_rng = map_acc_range_value(RANGE_G);
    const uint8_t gyr_rng = map_gyro_range_value(GYRO_RANGE_DPS);

    sh200q_write(SH200I_ACC_CONFIG, acc_cfg);
    sh200q_write(SH200I_GYRO_CONFIG, gyr_cfg);
    // Optional: modest gyro DLPF to reduce noise (50 Hz)
    sh200q_write(SH200I_GYRO_DLPF, 0x03);
    sh200q_write(SH200I_FIFO_CONFIG, 0x00); // no FIFO buffer
    sh200q_write(SH200I_ACC_RANGE, acc_rng);
    sh200q_write(SH200I_GYRO_RANGE, gyr_rng);

    return true;
}

// Read raw accelerometer values via the M5.IMU helper. The function outputs
// 16-bit ADC values matching the original implementation.
inline bool imu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az) {
    M5.IMU.getAccelAdc(&ax, &ay, &az);
    return true;
}

// Read raw gyroscope values (ADC units) via M5.IMU helper
inline bool imu_read_gyro_raw(int16_t& gx, int16_t& gy, int16_t& gz) {
    // Use float API (dps) and cast to int16 for logging.
    // Decoder scales back to dps using header range.
    float fx, fy, fz;
    M5.IMU.getGyroData(&fx, &fy, &fz); // dps
    gx = (int16_t)fx; gy = (int16_t)fy; gz = (int16_t)fz;
    return true;
}
