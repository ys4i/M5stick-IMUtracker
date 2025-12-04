#pragma once
// MPU6886 access for M5Unified-based boards (e.g., Core2).
// After M5.begin() (Unified) we override key registers to match config.h,
// then read raw int16 samples via I2C. We avoid float conversions for consistency.

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "board_hal.h"

static const uint8_t MPU6886_ADDR = hal_imu_addr();

// Runtime configuration (actual values after rounding to supported settings)
static uint16_t s_odr_hz = ODR_HZ;
static uint16_t s_acc_range_g = RANGE_G;
static uint16_t s_gyro_range_dps = GYRO_RANGE_DPS;
static uint16_t s_dlpf_hz = DLPF_HZ;
static float s_lsb_per_g = 32768.0f / (float)RANGE_G;
static float s_lsb_per_dps = 32768.0f / (float)GYRO_RANGE_DPS;

// Registers
#define MPU6886_REG_PWR_MGMT_1   0x6B
#define MPU6886_REG_SMPLRT_DIV   0x19
#define MPU6886_REG_CONFIG       0x1A
#define MPU6886_REG_GYRO_CONFIG  0x1B
#define MPU6886_REG_ACCEL_CONFIG 0x1C
#define MPU6886_REG_ACCEL_CONFIG2 0x1D
#define MPU6886_REG_PWR_MGMT_2   0x6C
#define MPU6886_REG_WHOAMI       0x75
#define MPU6886_REG_ACCEL_XOUT_H 0x3B
#define MPU6886_REG_GYRO_XOUT_H  0x43

inline void mpu_write_u8(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(MPU6886_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

inline bool mpu_read_xyz16(uint8_t start_reg, int16_t& x, int16_t& y, int16_t& z) {
    uint8_t buf[6] = {0};
    Wire.beginTransmission(MPU6886_ADDR);
    Wire.write(start_reg);
    Wire.endTransmission(false);
    int n = Wire.requestFrom(MPU6886_ADDR, (uint8_t)6);
    for (int i = 0; i < 6 && Wire.available(); ++i) buf[i] = Wire.read();
    if (n < 6) {
        static uint32_t last_err_ms = 0;
        uint32_t now_ms = millis();
        if (now_ms - last_err_ms > 1000) {
            Serial.printf("IMU I2C short read reg 0x%02X n=%d\n", start_reg, n);
            last_err_ms = now_ms;
        }
        x = y = z = 0;
        return false;
    }
    x = (int16_t)((buf[0] << 8) | buf[1]);
    y = (int16_t)((buf[2] << 8) | buf[3]);
    z = (int16_t)((buf[4] << 8) | buf[5]);
    return true;
}

inline uint8_t map_acc_range_mpu(uint16_t g) {
    if (g >= 16) return 0x18; // +/-16g
    if (g >= 8)  return 0x10; // +/-8g
    if (g >= 4)  return 0x08; // +/-4g
    return 0x00;             // +/-2g
}

inline uint16_t decode_acc_range_mpu(uint8_t cfg) {
    switch (cfg & 0x18) {
        case 0x18: return 16;
        case 0x10: return 8;
        case 0x08: return 4;
        default: return 2;
    }
}

inline uint8_t map_gyro_range_mpu(uint16_t dps) {
    if (dps >= 2000) return 0x18; // +/-2000 dps
    if (dps >= 1000) return 0x10; // +/-1000 dps
    if (dps >= 500)  return 0x08; // +/-500 dps
    return 0x00;                 // +/-250 dps
}

inline uint16_t decode_gyro_range_mpu(uint8_t cfg) {
    switch (cfg & 0x18) {
        case 0x18: return 2000;
        case 0x10: return 1000;
        case 0x08: return 500;
        default: return 250;
    }
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

inline uint16_t dlpf_actual_hz(uint8_t cfg) {
    switch (cfg & 0x07) {
        case 1: return 184;
        case 2: return 94;
        case 3: return 44;
        case 4: return 21;
        case 5: return 10;
        case 6: return 5;
        default: return 0; // off / 260Hz nominal (treated as off)
    }
}

inline void mpu_set_odr(uint16_t odr_hz, bool dlpf_off) {
    // Gyro output rate: 8kHz when DLPF off, 1kHz when on.
    uint16_t base = dlpf_off ? 8000 : 1000;
    if (odr_hz == 0) odr_hz = 200;
    // Round to nearest by adding half before integer divide
    uint16_t div = (uint16_t)(((base + (odr_hz / 2)) / odr_hz) - 1);
    if (div > 255) div = 255;
    mpu_write_u8(MPU6886_REG_SMPLRT_DIV, (uint8_t)div);
    // Store actual ODR
    s_odr_hz = (uint16_t)(base / (div + 1));
}

inline bool imu_init() {
    // Ensure IMU is powered and initialized, then override registers.
#if HAS_M5UNIFIED
    bool ok = M5.Imu.begin();
    if (!ok) {
        Serial.println("IMU begin failed (M5.Imu.begin)");
    }
#endif
    // Ensure Wire is started on expected pins (Core2: SDA=21/SCL=22)
    Wire.begin(hal_i2c_sda_pin(), hal_i2c_scl_pin());
    // Debug時は安定重視で100kHz、通常は400kHz
    Wire.setClock(DEBUG_MODE ? 100000 : 400000);
    // Soft reset then wake
    mpu_write_u8(MPU6886_REG_PWR_MGMT_1, 0x80);
    delay(10);
    mpu_write_u8(MPU6886_REG_PWR_MGMT_1, 0x00); // wake
    delay(10);
    mpu_write_u8(MPU6886_REG_PWR_MGMT_2, 0x00); // enable all axes
    delay(1);
    uint8_t dlpf = map_dlpf_cfg(DLPF_HZ);
    s_dlpf_hz = dlpf_actual_hz(dlpf);
    bool dlpf_off = (dlpf == 0);
    mpu_write_u8(MPU6886_REG_CONFIG, dlpf);
    mpu_set_odr(ODR_HZ, dlpf_off);

    uint8_t gcfg = map_gyro_range_mpu(GYRO_RANGE_DPS);
    uint8_t acfg = map_acc_range_mpu(RANGE_G);
    mpu_write_u8(MPU6886_REG_GYRO_CONFIG, gcfg);
    mpu_write_u8(MPU6886_REG_ACCEL_CONFIG, acfg);
    mpu_write_u8(MPU6886_REG_ACCEL_CONFIG2, dlpf);

    s_acc_range_g = decode_acc_range_mpu(acfg);
    s_gyro_range_dps = decode_gyro_range_mpu(gcfg);
    s_lsb_per_g = 32768.0f / (float)s_acc_range_g;
    s_lsb_per_dps = 32768.0f / (float)s_gyro_range_dps;
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
        if (!mpu_read_xyz16(MPU6886_REG_GYRO_XOUT_H, gx, gy, gz)) {
            delay(10);
            continue;
        }
        sx += gx; sy += gy; sz += gz;
        delay(1000UL / (s_odr_hz ? s_odr_hz : 200));
    }
    s_gbias_x = (int32_t)(sx / (int32_t)samples);
    s_gbias_y = (int32_t)(sy / (int32_t)samples);
    s_gbias_z = (int32_t)(sz / (int32_t)samples);
    s_imu_calibrated = true;
}

inline bool imu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az) {
    return mpu_read_xyz16(MPU6886_REG_ACCEL_XOUT_H, ax, ay, az);
}

inline bool imu_read_gyro_raw(int16_t& gx, int16_t& gy, int16_t& gz) {
    int16_t rx, ry, rz;
    if (!mpu_read_xyz16(MPU6886_REG_GYRO_XOUT_H, rx, ry, rz)) {
        return false;
    }
    gx = (int16_t)(rx - s_gbias_x);
    gy = (int16_t)(ry - s_gbias_y);
    gz = (int16_t)(rz - s_gbias_z);
    return true;
}

// Expose actual configuration for header/INFO
inline uint16_t imu_active_odr_hz() { return s_odr_hz; }
inline uint16_t imu_active_range_g() { return s_acc_range_g; }
inline uint16_t imu_active_gyro_range_dps() { return s_gyro_range_dps; }
inline uint16_t imu_active_dlpf_hz() { return s_dlpf_hz; }
inline float imu_lsb_per_g() { return s_lsb_per_g; }
inline float imu_lsb_per_dps() { return s_lsb_per_dps; }
