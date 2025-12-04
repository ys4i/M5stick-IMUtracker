#pragma once
#include <M5StickC.h>
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "board_hal.h"

// Minimal SH200Q access. We avoid M5.IMU read helpers to keep readings raw
// and prevent any library-side auto-calibration from altering values.

// Runtime configuration (actual values after rounding to supported settings)
static uint16_t s_odr_hz = ODR_HZ;
static uint16_t s_acc_range_g = RANGE_G;
static uint16_t s_gyro_range_dps = GYRO_RANGE_DPS;
static uint16_t s_dlpf_hz = DLPF_HZ;
static float s_lsb_per_g = 32768.0f / (float)RANGE_G;
static float s_lsb_per_dps = 32768.0f / (float)GYRO_RANGE_DPS;

// SH200Q register map (7-bit address from M5Unified defaults)
#define SH200I_ACC_CONFIG  0x0E
#define SH200I_GYRO_CONFIG 0x0F
#define SH200I_GYRO_DLPF   0x11
#define SH200I_FIFO_CONFIG 0x12
#define SH200I_ACC_RANGE   0x16
#define SH200I_GYRO_RANGE  0x2B

inline void sh200q_write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(hal_imu_addr());
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

inline uint8_t sh200q_read_u8(uint8_t reg) {
    Wire1.beginTransmission(hal_imu_addr());
    Wire1.write(reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom(hal_imu_addr(), (uint8_t)1);
    if (Wire1.available()) return Wire1.read();
    return 0;
}

inline void sh200q_read_xyz16(uint8_t start_reg, int16_t& x, int16_t& y, int16_t& z) {
    uint8_t buf[6] = {0};
    Wire1.beginTransmission(hal_imu_addr());
    Wire1.write(start_reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom(hal_imu_addr(), (uint8_t)6);
    for (int i = 0; i < 6 && Wire1.available(); ++i) buf[i] = Wire1.read();
    x = (int16_t)((buf[1] << 8) | buf[0]);
    y = (int16_t)((buf[3] << 8) | buf[2]);
    z = (int16_t)((buf[5] << 8) | buf[4]);
}

inline uint8_t map_acc_range_value(uint16_t range_g) {
    // 0x00: ±4g, 0x01: ±8g, 0x02: ±16g
    if (range_g >= 16) return 0x02;
    if (range_g >= 8)  return 0x01;
    return 0x00; // default to ±4g
}

inline uint16_t decode_acc_range_value(uint8_t v) {
    switch (v & 0x03) {
        case 0x02: return 16;
        case 0x01: return 8;
        default: return 4;
    }
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

inline uint16_t decode_gyro_range_value(uint8_t v) {
    switch (v & 0x07) {
        case 0x00: return 2000;
        case 0x01: return 1000;
        case 0x02: return 500;
        case 0x03: return 250;
        case 0x04: return 125;
    }
    return 2000;
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

inline uint16_t decode_acc_odr_reg(uint8_t reg) {
    const struct { uint16_t hz; uint8_t reg; } lut[] = {
        {1024, 0x81}, {512, 0x89}, {256, 0x91}, {128, 0x99},
        { 64, 0xA1}, { 32, 0xA9}, { 16, 0xB1}, {  8, 0xB9},
    };
    for (auto &e : lut) if (e.reg == reg) return e.hz;
    return 0;
}

inline uint16_t decode_gyro_odr_reg(uint8_t reg) {
    const struct { uint16_t hz; uint8_t reg; } lut[] = {
        {1000, 0x11}, {500, 0x13}, {256, 0x15}, {128, 0x17}, { 64, 0x19},
    };
    for (auto &e : lut) if (e.reg == reg) return e.hz;
    return 0;
}

inline uint8_t map_gyro_dlpf_reg(uint16_t hz) {
    if (hz == 0) return 0x00; // off
    if (hz <= 50) return 0x03; // ~50Hz
    return 0x02; // ~92Hz
}

inline uint16_t decode_gyro_dlpf_reg(uint8_t reg) {
    if (reg == 0x03) return 50;
    if (reg == 0x02) return 92;
    return 0; // treat others as off
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
    const uint8_t gyr_dlpf = map_gyro_dlpf_reg(DLPF_HZ);

    sh200q_write(SH200I_ACC_CONFIG, acc_cfg);
    sh200q_write(SH200I_GYRO_CONFIG, gyr_cfg);
    sh200q_write(SH200I_GYRO_DLPF, gyr_dlpf);
    sh200q_write(SH200I_FIFO_CONFIG, 0x00); // no FIFO buffer
    sh200q_write(SH200I_ACC_RANGE, acc_rng);
    sh200q_write(SH200I_GYRO_RANGE, gyr_rng);

    uint16_t acc_odr = decode_acc_odr_reg(acc_cfg);
    uint16_t gyro_odr = decode_gyro_odr_reg(gyr_cfg);
    s_odr_hz = (acc_odr && gyro_odr) ? min(acc_odr, gyro_odr) : (acc_odr ? acc_odr : (gyro_odr ? gyro_odr : ODR_HZ));
    s_acc_range_g = decode_acc_range_value(acc_rng);
    s_gyro_range_dps = decode_gyro_range_value(gyr_rng);
    s_dlpf_hz = decode_gyro_dlpf_reg(gyr_dlpf);
    s_lsb_per_g = 32768.0f / (float)s_acc_range_g;
    s_lsb_per_dps = 32768.0f / (float)s_gyro_range_dps;

    return true;
}

// One-time manual calibration: estimate gyro bias while device is stationary.
static int32_t s_gbias_x = 0, s_gbias_y = 0, s_gbias_z = 0;
static bool s_imu_calibrated = false;
inline bool imu_is_calibrated() { return s_imu_calibrated; }
inline void imu_calibrate_once(uint16_t samples = 512) {
    if (s_imu_calibrated) return;
    // Turn screen on and show calibration screen (full blue background)
    // Keep UI minimal to avoid timing impact.
    M5.Axp.SetLDO2(true);
    M5.Axp.ScreenBreath(LCD_BRIGHT_ACTIVE);
    M5.Lcd.fillScreen(TFT_BLUE);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.print("CALIBRATING...\n");
    M5.Lcd.print("Keep device still");
    int64_t sx = 0, sy = 0, sz = 0;
    for (uint16_t i = 0; i < samples; ++i) {
        int16_t gx, gy, gz;
        sh200q_read_xyz16(0x06 /* SH200I_OUTPUT_GYRO */, gx, gy, gz);
        sx += gx; sy += gy; sz += gz;
        // Roughly follow ODR pacing to avoid bias from transient startup
        delay(1000UL / (s_odr_hz ? s_odr_hz : 128));
    }
    s_gbias_x = (int32_t)(sx / (int32_t)samples);
    s_gbias_y = (int32_t)(sy / (int32_t)samples);
    s_gbias_z = (int32_t)(sz / (int32_t)samples);
    s_imu_calibrated = true;
}

// Read raw accelerometer values directly from device (ADC units)
inline bool imu_read_accel_raw(int16_t& ax, int16_t& ay, int16_t& az) {
    sh200q_read_xyz16(0x00 /* SH200I_OUTPUT_ACC */, ax, ay, az);
    return true;
}

// Read gyroscope from device (ADC units), subtract bias, convert to dps and cast
inline bool imu_read_gyro_raw(int16_t& gx, int16_t& gy, int16_t& gz) {
    int16_t rx, ry, rz;
    sh200q_read_xyz16(0x06 /* SH200I_OUTPUT_GYRO */, rx, ry, rz);
    int32_t bx = rx - s_gbias_x;
    int32_t by = ry - s_gbias_y;
    int32_t bz = rz - s_gbias_z;
    float fx = (float)bx * ((float)s_gyro_range_dps / 32768.0f);
    float fy = (float)by * ((float)s_gyro_range_dps / 32768.0f);
    float fz = (float)bz * ((float)s_gyro_range_dps / 32768.0f);
    gx = (int16_t)fx; gy = (int16_t)fy; gz = (int16_t)fz;
    return true;
}

// Expose actual configuration for header/INFO
inline uint16_t imu_active_odr_hz() { return s_odr_hz; }
inline uint16_t imu_active_range_g() { return s_acc_range_g; }
inline uint16_t imu_active_gyro_range_dps() { return s_gyro_range_dps; }
inline uint16_t imu_active_dlpf_hz() { return s_dlpf_hz; }
inline float imu_lsb_per_g() { return s_lsb_per_g; }
inline float imu_lsb_per_dps() { return s_lsb_per_dps; }
