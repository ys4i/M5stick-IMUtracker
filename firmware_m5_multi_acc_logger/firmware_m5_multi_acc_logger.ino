// Build Marker: 2025-12-04 19:35:45 (Local, Last Updated)
// Note: Update this timestamp whenever agents modifies this file.

#include <LittleFS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include "config.h"
#include "board_hal.h"
#include "fs_format.h"
#if HAL_IMU_IS_SH200Q
#include "imu_sh200q.h"
#else
#include "imu_mpu6886_unified.h"
#endif

bool recording = false;
static bool screen_on = true;
static uint32_t screen_on_until_ms = 0;
File logFile;
static uint8_t ring_buf[4096];
static size_t ring_pos = 0;
static uint32_t total_samples = 0;
static uint32_t last_idle_ms = 0; // for auto power-off when idle
static int16_t dbg_ax = 0, dbg_ay = 0, dbg_az = 0;
static int16_t dbg_gx = 0, dbg_gy = 0, dbg_gz = 0;
static bool dbg_has_sample = false;

// forward declaration for serial protocol
void start_logging();
void stop_logging();
// Runtime IMU config (actual values after rounding)
static uint16_t active_odr_hz = ODR_HZ;
static uint16_t active_range_g = RANGE_G;
static uint16_t active_gyro_dps = GYRO_RANGE_DPS;
static uint16_t active_dlpf_hz = DLPF_HZ;
static float active_lsb_per_g = 32768.0f / (float)RANGE_G;
static float active_lsb_per_dps = 32768.0f / (float)GYRO_RANGE_DPS;

// Ensure exact 64-byte layout without padding
struct __attribute__((packed)) LogHeader {
    char magic[8];
    uint16_t format_ver;
    uint64_t device_uid;
    uint64_t start_unix_ms;
    uint16_t odr_hz;
    uint16_t range_g;
    // New in v2: gyro range (dps).
    uint16_t gyro_range_dps;
    // New in v2.1: IMU meta
    uint16_t imu_type;
    uint16_t device_model;
    float lsb_per_g;
    float lsb_per_dps;
    uint32_t total_samples;
    uint32_t dropped_samples;
    uint8_t reserved[64 - 8 - 2 - 8 - 8 - 2 - 2 - 2 - 2 - 2 - 4 - 4 - 4 - 4];
};

#include "serial_proto.h"

void lcd_draw_debug_overlay(uint16_t bg) {
    if (!DEBUG_MODE) return;
    const int margin = 2;
    const int bar_h = 8;
    const int bar_y = hal_lcd().height() - bar_h - margin;
    int line_h = hal_lcd().fontHeight();
    if (line_h <= 0) line_h = 16;
    const int line_gap = 2;
    const int lines = 4;
    const int overlay_h = lines * line_h + line_gap * (lines - 1);
    int y = bar_y - overlay_h - margin;
    if (y < 0) y = 0;
    hal_lcd().fillRect(0, y, hal_lcd().width(), overlay_h, bg);
    hal_lcd().setTextColor(TFT_YELLOW, bg);
    hal_lcd().setCursor(0, y);
#if HAL_IMU_IS_SH200Q
    const char* imu_name = "SH200Q";
    const uint8_t imu_addr = hal_imu_addr();
    const char* bus_name = hal_i2c_bus_name();
#else
    const char* imu_name = "MPU6886";
    const uint8_t imu_addr = hal_imu_addr();
    const char* bus_name = hal_i2c_bus_name();
#endif
    hal_lcd().printf("IMU:%s 0x%02X %s", imu_name, imu_addr, bus_name);
    hal_lcd().setCursor(0, y + line_h + line_gap);
    hal_lcd().printf("SDA:%d SCL:%d cal:%c rec:%c", hal_i2c_sda_pin(), hal_i2c_scl_pin(), imu_is_calibrated() ? 'Y' : 'N', recording ? 'Y' : 'N');
    hal_lcd().setCursor(0, y + (line_h + line_gap) * 2);
    if (dbg_has_sample) {
        hal_lcd().printf("ax:%d ay:%d az:%d", dbg_ax, dbg_ay, dbg_az);
    } else {
        hal_lcd().print("ax:-- ay:-- az:--");
    }
    hal_lcd().setCursor(0, y + (line_h + line_gap) * 3);
    if (dbg_has_sample) {
        hal_lcd().printf("gx:%d gy:%d gz:%d", dbg_gx, dbg_gy, dbg_gz);
    } else {
        hal_lcd().print("gx:-- gy:-- gz:--");
    }
}

void lcd_show_state() {
    uint16_t bg = recording ? TFT_RED : TFT_BLACK;
    hal_lcd().fillScreen(bg);
    hal_lcd().setCursor(0, 0);
    hal_lcd().setTextColor(TFT_WHITE, bg);
    if (recording) {
        hal_lcd().print("REC");
    } else if (!imu_is_calibrated()) {
        hal_lcd().print("CALIBRATION STANDBY\n");
        hal_lcd().print("Hold button to start\n");
        hal_lcd().printf("Starts %us after hold\n", (unsigned)CALIB_DELAY_SEC);
    } else {
        hal_lcd().print("IDLE");
    }
    lcd_draw_debug_overlay(bg);
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
    const int bar_y = hal_lcd().height() - bar_h - margin;
    const int bar_x = margin;
    const int bar_w = hal_lcd().width() - margin * 2;

    // Compute FS stats
    uint8_t pct = fs_used_pct();
    size_t used = fs_used_bytes();
    size_t total = fs_total_bytes();

    // Clear text area line with sufficient height to avoid overlap
    int th = hal_lcd().fontHeight();
    if (th <= 0) th = 16;
    hal_lcd().fillRect(0, text_y - 2, hal_lcd().width(), th + 4, bg);
    hal_lcd().setCursor(0, text_y);
    // Use background color to overwrite previous text fully
    hal_lcd().setTextColor(fg, bg);
    // Display bytes with B (bytes) unit per request
    hal_lcd().printf("FS: %3u%%(%uB / %uB) used", pct, (unsigned)used, (unsigned)total);

    // Third line: ODR and estimated remaining time based on free space
    int th2 = hal_lcd().fontHeight();
    if (th2 <= 0) th2 = 16;
    int text2_y = text_y + th2 + 4;
    hal_lcd().fillRect(0, text2_y - 2, hal_lcd().width(), th2 + 4, bg);
    hal_lcd().setCursor(0, text2_y);
    hal_lcd().setTextColor(fg, bg);
    // Data rate: 6 channels * int16 = 12 bytes per sample at active ODR
    const float bytes_per_sec = 12.0f * (float)active_odr_hz;
    float eta_sec = 0.0f;
    if (bytes_per_sec > 0.0f) {
        eta_sec = (float)fs_free_bytes() / bytes_per_sec;
    }
    // Compact human-readable ETA
    if (eta_sec >= 3600.0f) {
        float hrs = eta_sec / 3600.0f;
        hal_lcd().printf("ODR:%uHz ETA: %.1fhour", (unsigned)active_odr_hz, hrs);
    } else if (eta_sec >= 60.0f) {
        float mins = eta_sec / 60.0f;
        hal_lcd().printf("ODR:%uHz ETA: %.1fmin", (unsigned)active_odr_hz, mins);
    } else {
        hal_lcd().printf("ODR:%uHz ETA: %usec", (unsigned)active_odr_hz, (unsigned)eta_sec);
    }

    // Draw usage bar background and fill
    hal_lcd().fillRect(bar_x, bar_y, bar_w, bar_h, TFT_DARKGREY);
    int fill_w = (int)((uint32_t)bar_w * pct / 100);
    if (fill_w > 0) {
        hal_lcd().fillRect(bar_x, bar_y, fill_w, bar_h, TFT_RED);
    }
    lcd_draw_debug_overlay(bg);
}

// Screen power/brightness control helpers
void screen_set(bool on) {
    if (on == screen_on) return;
    if (on) {
        // Power on LCD/backlight
        hal_screen_on(LCD_BRIGHT_ACTIVE, LCD_BRIGHT_OFF);
        screen_on = true;
    } else {
        // Dim and power off
        hal_screen_off(LCD_BRIGHT_OFF);    // backlight off
        screen_on = false;
    }
}

void ui_wake_for(uint32_t ms) {
    screen_on_until_ms = millis() + ms;
    screen_set(true);
    lcd_show_state();
    lcd_draw_fs_usage();
    last_idle_ms = millis();
}

void start_logging() {
    // Ensure a fresh file is created
    LittleFS.remove(LOG_FILE_NAME);
    logFile = fs_create_log();
    if (!logFile) return;
    LogHeader hdr = {};
    // Write full 8-byte magic explicitly
    memcpy(hdr.magic, "ACCLOG\0\0", 8);
    // Bump format version: 0x0201 adds IMU meta
    hdr.format_ver = 0x0201;
    hdr.device_uid = ESP.getEfuseMac();
    // Use device monotonic millis at start for later PC-side alignment
    hdr.start_unix_ms = millis();
    hdr.odr_hz = active_odr_hz;
    hdr.range_g = active_range_g;
    hdr.gyro_range_dps = active_gyro_dps;
    hdr.imu_type = HAL_IMU_TYPE;
    hdr.device_model = HAL_DEVICE_MODEL;
    hdr.lsb_per_g = active_lsb_per_g;
    hdr.lsb_per_dps = active_lsb_per_dps;
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
    hal_begin();
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    btStop();
    hal_lcd().setRotation(3);
    // Enlarge UART buffers to improve dump throughput (ESP32 HardwareSerial)
    #if defined(ARDUINO_ARCH_ESP32)
    Serial.setTxBufferSize(1024);
    Serial.setRxBufferSize(1024);
    #endif
    Serial.begin(SERIAL_BAUD);
    bool fs_ok = fs_init();
    Serial.printf(
        "BOOT fs_ok:%d total:%u used:%u imu_type:%u addr:0x%02X bus:%s SDA:%d SCL:%d\n",
        (int)fs_ok, (unsigned)fs_total_bytes(), (unsigned)fs_used_bytes(),
        (unsigned)HAL_IMU_TYPE, (unsigned)hal_imu_addr(), hal_i2c_bus_name(),
        hal_i2c_sda_pin(), hal_i2c_scl_pin()
    );
    bool imu_ok = imu_init();
    active_odr_hz = imu_active_odr_hz() ? imu_active_odr_hz() : ODR_HZ;
    active_range_g = imu_active_range_g() ? imu_active_range_g() : RANGE_G;
    active_gyro_dps = imu_active_gyro_range_dps() ? imu_active_gyro_range_dps() : GYRO_RANGE_DPS;
    active_dlpf_hz = imu_active_dlpf_hz();
    active_lsb_per_g = imu_lsb_per_g();
    active_lsb_per_dps = imu_lsb_per_dps();
    Serial.printf("IMU_INIT %d odr:%uHz acc:%ug gyro:%udps dlpf:%uHz\n", (int)imu_ok, (unsigned)active_odr_hz, (unsigned)active_range_g, (unsigned)active_gyro_dps, (unsigned)active_dlpf_hz);
    lcd_show_state();
    screen_on = true;
    screen_on_until_ms = millis() + 5000; // initial wake period
    last_idle_ms = millis();
    delay(1000); // 待機しないとserialが不安定になる
}

void loop() {
    hal_update();
    // State: handle calibration-first workflow
    static bool calib_pending = false;
    static uint32_t calib_due_ms = 0;

    // Long press behavior:
    //  - Before first calibration: schedule calibration after CALIB_DELAY_SEC
    //  - After calibration: toggle recording
    const uint32_t LONG_MS = 800;
    if (hal_btn_long(LONG_MS)) {
        if (!imu_is_calibrated()) {
            calib_pending = true;
            calib_due_ms = millis() + (uint32_t)CALIB_DELAY_SEC * 1000UL;
            screen_set(true);
            hal_lcd().fillScreen(TFT_BLACK);
            hal_lcd().setTextColor(TFT_WHITE, TFT_BLACK);
            hal_lcd().setCursor(0, 0);
            hal_lcd().print("Calibration scheduled\n");
            hal_lcd().printf("Starting in %us...\n", (unsigned)CALIB_DELAY_SEC);
            hal_lcd().print("Keep device still");
        } else {
            if (recording) stop_logging();
            else start_logging();
            ui_wake_for(10000);
        }
    } else if (hal_btn_short_released()) {
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
        // Auto power-off if idle (not recording, not dumping) for 10 minutes
        if ((now_ms - last_idle_ms) > 10UL * 60UL * 1000UL) {
            hal_power_off();
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
    const uint32_t interval_us = 1000000UL / (active_odr_hz ? active_odr_hz : 1);
    if (now - last_us < interval_us) {
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
    last_us += interval_us;

    int16_t ax, ay, az, gx, gy, gz;
    if (!imu_read_accel_raw(ax, ay, az)) return;
    if (!imu_read_gyro_raw(gx, gy, gz)) return;
    dbg_ax = ax; dbg_ay = ay; dbg_az = az;
    dbg_gx = gx; dbg_gy = gy; dbg_gz = gz;
    dbg_has_sample = true;
    if (DEBUG_MODE) {
        static uint32_t last_dbg_ms = 0;
        uint32_t now_ms = millis();
        if (now_ms - last_dbg_ms >= DEBUG_RAW_PRINT_INTERVAL_MS) {
            last_dbg_ms = now_ms;
            Serial.printf("DBG_RAW ax:%d ay:%d az:%d gx:%d gy:%d gz:%d\n", ax, ay, az, gx, gy, gz);
        }
    }
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
