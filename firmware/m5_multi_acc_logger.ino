#include <M5StickC.h>
#include <LittleFS.h>
#include "config.h"
#include "fs_format.h"
#include "imu_sh200q.h"

bool recording = false;
File logFile;
static uint8_t ring_buf[4096];
static size_t ring_pos = 0;
static uint32_t total_samples = 0;

// forward declaration for serial protocol
void start_logging();
void stop_logging();
#include "serial_proto.h"

struct LogHeader {
    char magic[8];
    uint16_t format_ver;
    uint64_t device_uid;
    uint64_t start_unix_ms;
    uint16_t odr_hz;
    uint16_t range_g;
    uint32_t total_samples;
    uint32_t dropped_samples;
    uint8_t reserved[64 - 8 - 2 - 8 - 8 - 2 - 2 - 4 - 4];
};

void lcd_show_state() {
    if (recording) {
        M5.Lcd.fillScreen(TFT_RED);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.setTextColor(TFT_WHITE);
        M5.Lcd.print("REC");
    } else {
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.setTextColor(TFT_WHITE);
        M5.Lcd.print("IDLE");
    }
}

void start_logging() {
    logFile = fs_create_log();
    if (!logFile) return;
    LogHeader hdr = {};
    memcpy(hdr.magic, "ACCLOG\0", 7);
    hdr.format_ver = 0x0100;
    hdr.device_uid = ESP.getEfuseMac();
    hdr.start_unix_ms = 0;
    hdr.odr_hz = ODR_HZ;
    hdr.range_g = RANGE_G;
    hdr.total_samples = 0xFFFFFFFF;
    hdr.dropped_samples = 0;
    logFile.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
    ring_pos = 0;
    total_samples = 0;
    recording = true;
    lcd_show_state();
}

void stop_logging() {
    if (!recording) return;
    if (ring_pos > 0) {
        logFile.write(ring_buf, ring_pos);
        ring_pos = 0;
    }
    if (logFile) {
        logFile.flush();
        LogHeader hdr;
        logFile.seek(0);
        logFile.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
        hdr.total_samples = total_samples;
        logFile.seek(0);
        logFile.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
        logFile.close();
    }
    recording = false;
    lcd_show_state();
}

void setup() {
    M5.begin();
    M5.Lcd.setRotation(3);
    Serial.begin(SERIAL_BAUD);
    fs_init();
    imu_init();
    lcd_show_state();
}

void loop() {
    M5.update();
    if (M5.BtnA.wasPressed()) {
        if (recording) stop_logging();
        else start_logging();
    }
    serial_proto_poll();
    if (!recording) {
        delay(10);
        return;
    }

    static uint32_t last_us = micros();
    uint32_t now = micros();
    if (now - last_us < (1000000UL / ODR_HZ)) return;
    last_us += 1000000UL / ODR_HZ;

    int16_t ax, ay, az;
    if (!imu_read_accel_raw(ax, ay, az)) return;
    ring_buf[ring_pos++] = ax >> 8;
    ring_buf[ring_pos++] = ax & 0xFF;
    ring_buf[ring_pos++] = ay >> 8;
    ring_buf[ring_pos++] = ay & 0xFF;
    ring_buf[ring_pos++] = az >> 8;
    ring_buf[ring_pos++] = az & 0xFF;
    total_samples++;
    if (ring_pos >= sizeof(ring_buf)) {
        logFile.write(ring_buf, sizeof(ring_buf));
        ring_pos = 0;
    }
}
