#pragma once
#include <Wire.h>
#include "config.h"

constexpr uint8_t MPU6886_ADDR = 0x68;
constexpr uint8_t MPU6886_PWR_MGMT_1 = 0x6B;
constexpr uint8_t MPU6886_SMPLRT_DIV = 0x19;
constexpr uint8_t MPU6886_ACCEL_CONFIG = 0x1C;
constexpr uint8_t MPU6886_ACCEL_XOUT_H = 0x3B;

inline bool imu_write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MPU6886_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

inline bool imu_read_regs(uint8_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(MPU6886_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((int)MPU6886_ADDR, (int)len);
    for (size_t i = 0; i < len && Wire.available(); ++i) {
        buf[i] = Wire.read();
    }
    return true;
}

inline bool imu_init() {
    Wire.begin();
    Wire.setClock(400000);
    if (!imu_write_reg(MPU6886_PWR_MGMT_1, 0x00)) return false;
    uint8_t div;
    switch (ODR_HZ) {
        case 100: div = 9; break;  // 1000/(1+div)
        case 200: div = 4; break;
        case 400: div = 1; break;
        case 1000: default: div = 0; break;
    }
    if (!imu_write_reg(MPU6886_SMPLRT_DIV, div)) return false;
    uint8_t range = 0;
    switch (RANGE_G) {
        case 2: range = 0x00; break;
        case 4: range = 0x08; break;
        case 8: range = 0x10; break;
        case 16: range = 0x18; break;
    }
    if (!imu_write_reg(MPU6886_ACCEL_CONFIG, range)) return false;
    return true;
}

inline bool imu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az) {
    uint8_t buf[6];
    if (!imu_read_regs(MPU6886_ACCEL_XOUT_H, buf, 6)) return false;
    ax = (int16_t)((buf[0] << 8) | buf[1]);
    ay = (int16_t)((buf[2] << 8) | buf[3]);
    az = (int16_t)((buf[4] << 8) | buf[5]);
    return true;
}
