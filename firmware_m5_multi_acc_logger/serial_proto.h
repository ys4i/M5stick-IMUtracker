#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "fs_format.h"

// start/stop functions provided by main sketch
void start_logging();
void stop_logging();
extern bool recording;

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
            "{\"uid\":\"0x%016llX\",\"odr\":%u,\"range_g\":%u,\"gyro_dps\":%u,\"file_size\":%u,\"fs_total\":%u,\"fs_used\":%u,\"fs_free\":%u,\"fs_used_pct\":%u,\"has_head\":%u}\n",
            (unsigned long long)uid, ODR_HZ, RANGE_G, GYRO_RANGE_DPS, (unsigned)size,
            (unsigned)fs_total_bytes(), (unsigned)fs_used_bytes(), (unsigned)fs_free_bytes(), (unsigned)fs_used_pct(),
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
            uint8_t buf[64];
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
    } else {
        Serial.println("UNKNOWN");
    }
}
