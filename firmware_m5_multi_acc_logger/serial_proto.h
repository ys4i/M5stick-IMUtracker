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
        if (LittleFS.exists(LOG_FILE_NAME)) {
            File f = LittleFS.open(LOG_FILE_NAME, "r");
            size = f.size();
            f.close();
        }
        Serial.printf(
            "{\"odr\":%u,\"range_g\":%u,\"gyro_dps\":%u,\"file_size\":%u,\"fs_total\":%u,\"fs_used\":%u,\"fs_free\":%u,\"fs_used_pct\":%u}\n",
            ODR_HZ, RANGE_G, GYRO_RANGE_DPS, (unsigned)size,
            (unsigned)fs_total_bytes(), (unsigned)fs_used_bytes(), (unsigned)fs_free_bytes(), (unsigned)fs_used_pct()
        );
    } else if (cmd == "DUMP") {
        if (LittleFS.exists(LOG_FILE_NAME)) {
            File f = LittleFS.open(LOG_FILE_NAME, "r");
            size_t size = f.size();
            Serial.printf("OK %u\n", (unsigned)size);
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
