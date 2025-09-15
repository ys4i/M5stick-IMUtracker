// Build Marker: 2025-09-15 11:24:10 (Local, Last Updated)
// Note: Update this timestamp whenever agents modifies this file.

#include <M5StickC.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include "config.h"
#include "fs_format.h"
#include "imu_sh200q.h"

bool recording = false;
static bool screen_on = true;
static uint32_t screen_on_until_ms = 0;
File logFile;
static uint8_t ring_buf[4096];
static size_t ring_pos = 0;
static uint32_t total_samples = 0;

// forward declaration for serial protocol
void start_logging();
void stop_logging();
#include "serial_proto.h"

// Ensure exact 64-byte layout without padding
struct __attribute__((packed)) LogHeader {
    char magic[8];
    uint16_t format_ver;
    uint64_t device_uid;
    uint64_t start_unix_ms;
    uint16_t odr_hz;
    uint16_t range_g;
    // New in v2: gyro range (dps). To keep header 64B, this occupies
    // the first 2 bytes of the previous reserved area.
    uint16_t gyro_range_dps;
    uint32_t total_samples;
    uint32_t dropped_samples;
    uint8_t reserved[64 - 8 - 2 - 8 - 8 - 2 - 2 - 2 - 4 - 4];
};

void lcd_show_state() {
    if (recording) {
        M5.Lcd.fillScreen(TFT_RED);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.setTextColor(TFT_WHITE);
        M5.Lcd.print("REC");
        return;
    }
    if (!imu_is_calibrated()) {
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Lcd.print("CALIBRATION STANDBY\n");
        M5.Lcd.print("Hold button to start\n");
        M5.Lcd.printf("Starts %us after hold\n", (unsigned)CALIB_DELAY_SEC);
        return;
    }
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.print("IDLE");
}

void lcd_draw_fs_usage() {
    if (!screen_on) return; // skip drawing when screen is off
    // Determine background color by state
    uint16_t bg = recording ? TFT_RED : TFT_BLACK;
    uint16_t fg = TFT_WHITE;
    // Layout
    const int margin = 2;
    const int text_y = 12; // second line
    const int bar_h = 8;
    const int bar_y = M5.Lcd.height() - bar_h - margin;
    const int bar_x = margin;
    const int bar_w = M5.Lcd.width() - margin * 2;

    // Compute FS stats
    uint8_t pct = fs_used_pct();
    size_t used = fs_used_bytes();
    size_t total = fs_total_bytes();

    // Clear text area line with sufficient height to avoid overlap
    int th = M5.Lcd.fontHeight();
    if (th <= 0) th = 16;
    M5.Lcd.fillRect(0, text_y - 2, M5.Lcd.width(), th + 4, bg);
    M5.Lcd.setCursor(0, text_y);
    // Use background color to overwrite previous text fully
    M5.Lcd.setTextColor(fg, bg);
    // Display bytes with B (bytes) unit per request
    M5.Lcd.printf("FS: %3u%%(%uB / %uB) used", pct, (unsigned)used, (unsigned)total);

    // Third line: ODR and estimated remaining time based on free space
    int th2 = M5.Lcd.fontHeight();
    if (th2 <= 0) th2 = 16;
    int text2_y = text_y + th2 + 4;
    M5.Lcd.fillRect(0, text2_y - 2, M5.Lcd.width(), th2 + 4, bg);
    M5.Lcd.setCursor(0, text2_y);
    M5.Lcd.setTextColor(fg, bg);
    // Data rate: 6 channels * int16 = 12 bytes per sample at ODR_HZ
    const float bytes_per_sec = 12.0f * (float)ODR_HZ;
    float eta_sec = 0.0f;
    if (bytes_per_sec > 0.0f) {
        eta_sec = (float)fs_free_bytes() / bytes_per_sec;
    }
    // Compact human-readable ETA
    if (eta_sec >= 3600.0f) {
        float hrs = eta_sec / 3600.0f;
        M5.Lcd.printf("ODR:%uHz ETA: %.1fhour", (unsigned)ODR_HZ, hrs);
    } else if (eta_sec >= 60.0f) {
        float mins = eta_sec / 60.0f;
        M5.Lcd.printf("ODR:%uHz ETA: %.1fmin", (unsigned)ODR_HZ, mins);
    } else {
        M5.Lcd.printf("ODR:%uHz ETA: %usec", (unsigned)ODR_HZ, (unsigned)eta_sec);
    }

    // Draw usage bar background and fill
    M5.Lcd.fillRect(bar_x, bar_y, bar_w, bar_h, TFT_DARKGREY);
    int fill_w = (int)((uint32_t)bar_w * pct / 100);
    if (fill_w > 0) {
        M5.Lcd.fillRect(bar_x, bar_y, fill_w, bar_h, TFT_RED);
    }
}

// Screen power/brightness control helpers
void screen_set(bool on) {
    if (on == screen_on) return;
    if (on) {
        // Power on LCD/backlight
        M5.Axp.SetLDO2(true);            // enable backlight power rail
        M5.Axp.ScreenBreath(LCD_BRIGHT_ACTIVE); // set desired brightness
        screen_on = true;
    } else {
        // Dim and power off
        M5.Axp.ScreenBreath(LCD_BRIGHT_OFF);    // backlight off
        M5.Axp.SetLDO2(false);           // cut backlight power
        screen_on = false;
    }
}

void ui_wake_for(uint32_t ms) {
    screen_on_until_ms = millis() + ms;
    screen_set(true);
    lcd_show_state();
    lcd_draw_fs_usage();
}

void start_logging() {
    // Ensure a fresh file is created
    LittleFS.remove(LOG_FILE_NAME);
    logFile = fs_create_log();
    if (!logFile) return;
    LogHeader hdr = {};
    // Write full 8-byte magic explicitly
    memcpy(hdr.magic, "ACCLOG\0\0", 8);
    // Bump format version: 0x0200 adds gyro channels and gyro_range_dps
    hdr.format_ver = 0x0200;
    hdr.device_uid = ESP.getEfuseMac();
    // Use device monotonic millis at start for later PC-side alignment
    hdr.start_unix_ms = millis();
    hdr.odr_hz = ODR_HZ;
    hdr.range_g = RANGE_G;
    hdr.gyro_range_dps = GYRO_RANGE_DPS;
    hdr.total_samples = 0xFFFFFFFF;
    hdr.dropped_samples = 0;
    logFile.seek(0);
    logFile.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
    logFile.flush();
    // Debug: verify header just written
    {
        uint8_t chk[16] = {0};
        logFile.seek(0);
        logFile.read(chk, sizeof(chk));
        Serial.print("HDRCHK ");
        const char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < sizeof(chk); ++i) {
            uint8_t b = chk[i];
            Serial.write(hex[(b >> 4) & 0xF]);
            Serial.write(hex[b & 0xF]);
        }
        Serial.print('\n');
    }
    // Reopen for append to ensure subsequent payload is not overwriting header
    logFile.close();
    logFile = LittleFS.open(LOG_FILE_NAME, "a");
    if (!logFile) {
        Serial.println("HDRCHK reopen failed");
        recording = false;
        return;
    }
    ring_pos = 0;
    total_samples = 0;
    recording = true;
    lcd_show_state();
    lcd_draw_fs_usage();
}

void stop_logging() {
    if (!recording) return;
    if (ring_pos > 0) {
        logFile.write(ring_buf, ring_pos);
        ring_pos = 0;
    }
    if (logFile) {
        logFile.flush();
        logFile.close();
        // Update total_samples in header
        File f = LittleFS.open(LOG_FILE_NAME, "r+");
        if (f) {
            LogHeader hdr;
            f.seek(0);
            f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
            hdr.total_samples = total_samples;
            f.seek(0);
            f.write(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
            f.flush();
            f.close();
        }
    }
    recording = false;
    lcd_show_state();
    lcd_draw_fs_usage();
}

void setup() {
    M5.begin();
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    btStop();
    M5.Lcd.setRotation(3);
    // Enlarge UART buffers to improve dump throughput (ESP32 HardwareSerial)
    #if defined(ARDUINO_ARCH_ESP32)
    Serial.setTxBufferSize(1024);
    Serial.setRxBufferSize(1024);
    #endif
    Serial.begin(SERIAL_BAUD);
    fs_init();
    imu_init();
    lcd_show_state();
    screen_on = true;
    screen_on_until_ms = millis() + 5000; // initial wake period
    delay(1000); // 待機しないとserialが不安定になる
}

void loop() {
    M5.update();
    // State: handle calibration-first workflow
    static bool calib_pending = false;
    static uint32_t calib_due_ms = 0;

    // Long press behavior:
    //  - Before first calibration: schedule calibration after CALIB_DELAY_SEC
    //  - After calibration: toggle recording
    const uint32_t LONG_MS = 800;
    if (M5.BtnA.wasReleasefor(LONG_MS)) {
        if (!imu_is_calibrated()) {
            calib_pending = true;
            calib_due_ms = millis() + (uint32_t)CALIB_DELAY_SEC * 1000UL;
            screen_set(true);
            M5.Lcd.fillScreen(TFT_BLACK);
            M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Lcd.setCursor(0, 0);
            M5.Lcd.print("Calibration scheduled\n");
            M5.Lcd.printf("Starting in %us...\n", (unsigned)CALIB_DELAY_SEC);
            M5.Lcd.print("Keep device still");
        } else {
            if (recording) stop_logging();
            else start_logging();
            ui_wake_for(10000);
        }
    } else if (M5.BtnA.wasReleased()) {
        // Short press
        ui_wake_for(10000);
    }
    // Kick calibration when due
    if (calib_pending && (int32_t)(millis() - calib_due_ms) >= 0) {
        calib_pending = false;
        imu_calibrate_once();
        lcd_show_state();
        lcd_draw_fs_usage();
    }
    serial_proto_poll();
    if (!recording) {
        static uint32_t last_lcd_ms = 0;
        uint32_t now_ms = millis();
        if (imu_is_calibrated() && now_ms - last_lcd_ms >= 1000) {
            last_lcd_ms = now_ms;
            lcd_draw_fs_usage();
        }
        // Auto screen off
        if (screen_on && (int32_t)(millis() - screen_on_until_ms) > 0) {
            screen_set(false);
        }
        delay(10);
        return;
    }

    static uint32_t last_us = micros();
    uint32_t now = micros();
    if (now - last_us < (1000000UL / ODR_HZ)) {
        // Update LCD FS usage at ~1 Hz while recording as well
        static uint32_t last_lcd_ms_rec = 0;
        uint32_t now_ms = millis();
        if (now_ms - last_lcd_ms_rec >= 1000) {
            last_lcd_ms_rec = now_ms;
            lcd_draw_fs_usage();
        }
        // Auto screen off during recording
        if (screen_on && (int32_t)(millis() - screen_on_until_ms) > 0) {
            screen_set(false);
        }
        return;
    }
    last_us += 1000000UL / ODR_HZ;

    int16_t ax, ay, az, gx, gy, gz;
    if (!imu_read_accel_raw(ax, ay, az)) return;
    if (!imu_read_gyro_raw(gx, gy, gz)) return;
    // Write big-endian (MSB first) like accel
    ring_buf[ring_pos++] = ax >> 8; ring_buf[ring_pos++] = ax & 0xFF;
    ring_buf[ring_pos++] = ay >> 8; ring_buf[ring_pos++] = ay & 0xFF;
    ring_buf[ring_pos++] = az >> 8; ring_buf[ring_pos++] = az & 0xFF;
    ring_buf[ring_pos++] = gx >> 8; ring_buf[ring_pos++] = gx & 0xFF;
    ring_buf[ring_pos++] = gy >> 8; ring_buf[ring_pos++] = gy & 0xFF;
    ring_buf[ring_pos++] = gz >> 8; ring_buf[ring_pos++] = gz & 0xFF;
    total_samples++;
    if (ring_pos >= sizeof(ring_buf)) {
        logFile.write(ring_buf, sizeof(ring_buf));
        ring_pos = 0;
    }
}
