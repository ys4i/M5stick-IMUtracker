#pragma once
#include <Wire.h>
#include <Arduino.h>
#include "config.h"

constexpr uint8_t SH200Q_ADDR = 0x6C;
constexpr uint8_t REG_ACC_CONFIG = 0x0E;
constexpr uint8_t REG_GYRO_CONFIG = 0x0F;
constexpr uint8_t REG_FIFO_CONFIG = 0x12;
constexpr uint8_t REG_ACC_RANGE = 0x16;
constexpr uint8_t REG_OUTPUT_ACC = 0x00;
constexpr uint8_t REG_RESET = 0x75;
constexpr uint8_t REG_CALIBRATE = 0x78;

inline bool imu_write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(SH200Q_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

inline bool imu_read_regs(uint8_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(SH200Q_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((int)SH200Q_ADDR, (int)len);
    for (size_t i = 0; i < len && Wire.available(); ++i) {
        buf[i] = Wire.read();
    }
    return true;
}

inline bool imu_init() {
    Wire.begin();
    Wire.setClock(400000);
    uint8_t tmp;
    if (!imu_read_regs(REG_RESET, &tmp, 1)) return false;
    if (!imu_write_reg(REG_RESET, tmp | 0x80)) return false;
    delay(1);
    if (!imu_write_reg(REG_RESET, tmp & 0x7F)) return false;
    if (!imu_write_reg(REG_CALIBRATE, 0x61)) return false;
    delay(1);
    if (!imu_write_reg(REG_CALIBRATE, 0x00)) return false;
    uint8_t odr;
    switch (ODR_HZ) {
        case 100: odr = 0x99; break;
        case 200: odr = 0x91; break;
        case 400: odr = 0x89; break;
        case 1000: default: odr = 0x81; break;
    }
    if (!imu_write_reg(REG_ACC_CONFIG, odr)) return false;
    if (!imu_write_reg(REG_GYRO_CONFIG, 0x13)) return false; // 500 Hz gyro
    if (!imu_write_reg(REG_FIFO_CONFIG, 0x00)) return false;
    uint8_t range = 0;
    switch (RANGE_G) {
        case 2: range = 0x00; break;
        case 4: range = 0x01; break;
        case 8: range = 0x02; break;
        case 16: range = 0x03; break;
    }
    if (!imu_write_reg(REG_ACC_RANGE, range)) return false;
    return true;
}

inline bool imu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az) {
    uint8_t buf[6];
    if (!imu_read_regs(REG_OUTPUT_ACC, buf, 6)) return false;
    ax = (int16_t)((buf[1] << 8) | buf[0]);
    ay = (int16_t)((buf[3] << 8) | buf[2]);
    az = (int16_t)((buf[5] << 8) | buf[4]);
    return true;
}
