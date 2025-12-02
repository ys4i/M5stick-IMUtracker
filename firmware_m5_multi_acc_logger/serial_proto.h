#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "board_hal.h"
#include "fs_format.h"
// For IMU register access
#include <Wire.h>
#if !HAL_IMU_IS_SH200Q
#include "imu_mpu6886_unified.h"
#endif

// start/stop functions provided by main sketch
void start_logging();
void stop_logging();
extern bool recording;

#if HAL_IMU_IS_SH200Q
// --- IMU register dump helpers (SH200Q) ---
// These are inline to keep header-only and avoid separate TU.
inline uint8_t _sh200q_read_u8(uint8_t reg) {
    Wire1.beginTransmission(hal_imu_addr());
    Wire1.write(reg);
    Wire1.endTransmission(false);
    Wire1.requestFrom(hal_imu_addr(), (uint8_t)1);
    if (Wire1.available()) return Wire1.read();
    return 0xFF;
}
inline uint16_t _decode_acc_odr(uint8_t v) {
    struct E { uint8_t reg; uint16_t hz; } lut[] = {
        {0x81,1024}, {0x89,512}, {0x91,256}, {0x99,128}, {0xA1,64}, {0xA9,32}, {0xB1,16}, {0xB9,8},
    };
    for (auto &e : lut) if (e.reg == v) return e.hz;
    return 0;
}
inline uint16_t _decode_gyro_odr(uint8_t v) {
    // Known values from M5 driver; 0x17/0x19 are best-effort
    struct E { uint8_t reg; uint16_t hz; } lut[] = {
        {0x11,1000}, {0x13,500}, {0x15,256}, {0x17,128}, {0x19,64},
    };
    for (auto &e : lut) if (e.reg == v) return e.hz;
    return 0;
}
inline uint16_t _decode_acc_range_g(uint8_t v) {
    switch (v & 0x03) { // 0x00:±4g, 0x01:±8g, 0x02:±16g
        case 0x00: return 4;
        case 0x01: return 8;
        case 0x02: return 16;
    }
    return 0;
}
inline uint16_t _decode_gyro_range_dps(uint8_t v) {
    // 0x00:±2000dps in M5 driver. Others mapped best-effort.
    switch (v & 0x07) {
        case 0x00: return 2000;
        case 0x01: return 1000;
        case 0x02: return 500;
        case 0x03: return 250;
        case 0x04: return 125;
    }
    return 0;
}
#endif

inline void serial_proto_poll() {
    if (!Serial.available()) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "PING") {
        Serial.println("PONG");
    } else if (cmd == "INFO") {
        size_t size = 0;
        bool has_head = false;
        uint64_t uid = ESP.getEfuseMac();
        if (LittleFS.exists(LOG_FILE_NAME)) {
            File f = LittleFS.open(LOG_FILE_NAME, "r");
            size = f.size();
            // Check magic
            uint8_t m[8] = {0};
            if (f.read(m, 8) == 8) {
                has_head = (m[0]=='A' && m[1]=='C' && m[2]=='C' && m[3]=='L' && m[4]=='O' && m[5]=='G');
            }
            f.close();
        }
        Serial.printf(
            "{\"uid\":\"0x%016llX\",\"odr\":%u,\"range_g\":%u,\"gyro_dps\":%u,\"imu_type\":%u,\"device_model\":%u,\"format\":\"0x0201\",\"lsb_per_g\":%.3f,\"lsb_per_dps\":%.3f,\"file_size\":%u,\"fs_total\":%u,\"fs_used\":%u,\"fs_free\":%u,\"fs_used_pct\":%u,\"has_head\":%u}\n",
            (unsigned long long)uid, ODR_HZ, RANGE_G, GYRO_RANGE_DPS, (unsigned)HAL_IMU_TYPE, (unsigned)HAL_DEVICE_MODEL,
            (float)(32768.0f / (float)RANGE_G), (float)(32768.0f / (float)GYRO_RANGE_DPS),
            (unsigned)size, (unsigned)fs_total_bytes(), (unsigned)fs_used_bytes(), (unsigned)fs_free_bytes(), (unsigned)fs_used_pct(),
            (unsigned)has_head
        );
    } else if (cmd == "HEAD") {
        if (LittleFS.exists(LOG_FILE_NAME)) {
            File f = LittleFS.open(LOG_FILE_NAME, "r");
            uint8_t buf[64];
            size_t n = f.read(buf, sizeof(buf));
            f.close();
            // Print as hex
            Serial.print("HEAD ");
            const char hex[] = "0123456789abcdef";
            for (size_t i = 0; i < n; ++i) {
                uint8_t b = buf[i];
                Serial.write(hex[(b >> 4) & 0xF]);
                Serial.write(hex[b & 0xF]);
            }
            Serial.print("\n");
        } else {
            Serial.println("HEAD");
        }
    } else if (cmd == "DUMP") {
        if (LittleFS.exists(LOG_FILE_NAME)) {
            File f = LittleFS.open(LOG_FILE_NAME, "r");
            size_t size = f.size();
            // Include device current millis in OK line for PC-side timestamping
            Serial.printf("OK %u %u\n", (unsigned)size, (unsigned)millis());
            // Larger buffer reduces per-call overhead during transfer
            static uint8_t buf[1024];
            while (f.available()) {
                size_t n = f.read(buf, sizeof(buf));
                Serial.write(buf, n);
            }
            f.close();
            Serial.print("\nDONE\n");
        } else {
            Serial.println("ERR");
        }
    } else if (cmd == "ERASE") {
        if (LittleFS.exists(LOG_FILE_NAME)) {
            LittleFS.remove(LOG_FILE_NAME);
        }
        Serial.println("OK");
    } else if (cmd == "START") {
        if (!recording) start_logging();
        Serial.println("OK");
    } else if (cmd == "STOP") {
        if (recording) stop_logging();
        Serial.println("OK");
    } else if (cmd == "I2CSCAN") {
        Serial.println("I2C scan start");
        for (uint8_t addr = 3; addr < 0x78; ++addr) {
            Wire.beginTransmission(addr);
            uint8_t err = Wire.endTransmission();
            if (err == 0) {
                Serial.printf("I2C found: 0x%02X\n", addr);
            } else if (err == 4) {
                Serial.printf("I2C unknown error at 0x%02X\n", addr);
            }
        }
        Serial.println("I2C scan done");
    } else if (cmd == "REGS") {
#if HAL_IMU_IS_SH200Q
        uint8_t r0E = _sh200q_read_u8(0x0E);
        uint8_t r0F = _sh200q_read_u8(0x0F);
        uint8_t r16 = _sh200q_read_u8(0x16);
        uint8_t r2B = _sh200q_read_u8(0x2B);
        uint16_t a_odr = _decode_acc_odr(r0E);
        uint16_t g_odr = _decode_gyro_odr(r0F);
        uint16_t a_rng = _decode_acc_range_g(r16);
        uint16_t g_rng = _decode_gyro_range_dps(r2B);
        Serial.printf(
            "REGS 0E=0x%02X 0F=0x%02X 16=0x%02X 2B=0x%02X ACC_ODR=%uHz GYRO_ODR=%uHz ACC_RANGE=%ug GYRO_RANGE=%udps\n",
            r0E, r0F, r16, r2B, (unsigned)a_odr, (unsigned)g_odr, (unsigned)a_rng, (unsigned)g_rng
        );
#else
        // Minimal MPU6886 dump
        auto mpu_read = [](uint8_t reg) {
            Wire.beginTransmission(MPU6886_ADDR);
            Wire.write(reg);
            Wire.endTransmission(false);
            Wire.requestFrom((int)MPU6886_ADDR, 1);
            if (Wire.available()) return Wire.read();
            return 0xFF;
        };
        auto mpu_whoami = [&]() {
            Wire.beginTransmission(MPU6886_ADDR);
            Wire.write(0x75);
            Wire.endTransmission(false);
            Wire.requestFrom((int)MPU6886_ADDR, 1);
            if (Wire.available()) return Wire.read();
            return 0xFF;
        };
        Wire.beginTransmission(MPU6886_ADDR);
        int ack = Wire.endTransmission();
        uint8_t cfg = mpu_read(MPU6886_REG_CONFIG);
        uint8_t smpl = mpu_read(MPU6886_REG_SMPLRT_DIV);
        uint8_t gcfg = mpu_read(MPU6886_REG_GYRO_CONFIG);
        uint8_t acfg = mpu_read(MPU6886_REG_ACCEL_CONFIG);
        uint8_t a2 = mpu_read(MPU6886_REG_ACCEL_CONFIG2);
        uint8_t pm1 = mpu_read(MPU6886_REG_PWR_MGMT_1);
        uint8_t pm2 = mpu_read(MPU6886_REG_PWR_MGMT_2);
        Serial.printf(
            "REGS ACK=%d CONFIG=0x%02X SMPLRT_DIV=%u GYRO_CONFIG=0x%02X ACCEL_CONFIG=0x%02X ACCEL_CONFIG2=0x%02X PWR_MGMT_1=0x%02X PWR_MGMT_2=0x%02X WHOAMI=0x%02X\n",
            ack, cfg, smpl, gcfg, acfg, a2, pm1, pm2, mpu_whoami()
        );
#endif
    } else {
        Serial.println("UNKNOWN");
    }
}
