#pragma once

// Minimal board/IMU abstraction.
// - SH200Q 搭載デバイス: M5StickC 系など。レジスタ直叩きを行う。
// - 非SH200Q (例: MPU6886/Core2): M5Unified を利用する。

#include <Arduino.h>
#include "config.h"

// 利用可能なら M5Unified 由来の定義 (IMUアドレスやピン名) を参照する
#if __has_include(<M5Unified.h>)
  #include <M5Unified.h>
  #define HAS_M5UNIFIED 1
#else
  #define HAS_M5UNIFIED 0
#endif

// --- IMU種別ID ---
#define IMU_TYPE_UNKNOWN 0
#define IMU_TYPE_SH200Q 1
#define IMU_TYPE_MPU6886 2

// --- デバイスモデルID（例示。未知は0） ---
#define DEVICE_MODEL_UNKNOWN 0
#define DEVICE_MODEL_STICKC 1
#define DEVICE_MODEL_STICKC_PLUS 2
#define DEVICE_MODEL_STICKC_PLUS2 3
#define DEVICE_MODEL_CORE2_16MB 10
#define DEVICE_MODEL_CORE2_UNKNOWN 11

// --- ボード/IMU検出 ---
// 判定マクロは agents/arduinoide_board.md のルール（build.board に ARDUINO_ を付与）を参照。
#if defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_C_PLUS) || defined(ARDUINO_M5STACK_STICKC)
#  define HAL_BOARD_IS_STICKC 1
#else
#  define HAL_BOARD_IS_STICKC 0
#endif

#if defined(ARDUINO_M5STACK_STICKC_PLUS2) || defined(ARDUINO_M5Stick_C_PLUS2) || defined(ARDUINO_M5Stick_Plus2)
#  define HAL_BOARD_IS_PLUS2 1
#else
#  define HAL_BOARD_IS_PLUS2 0
#endif

#if defined(ARDUINO_M5STACK_CORE2) || defined(ARDUINO_M5STACK_Core2) || defined(M5STACK_CORE2) || defined(M5STACK_M5Core2)
#  define HAL_BOARD_IS_CORE2 1
#else
#  define HAL_BOARD_IS_CORE2 0
#endif

#if defined(IMU_FORCE_SH200Q)
#  define HAL_IMU_IS_SH200Q 1
#elif HAL_BOARD_IS_STICKC
#  define HAL_IMU_IS_SH200Q 1
#elif HAL_BOARD_IS_PLUS2 || HAL_BOARD_IS_CORE2
#  define HAL_IMU_IS_SH200Q 0
#else
#  error "Unsupported board: define IMU_FORCE_SH200Q or use a supported M5Stick/Core2 board macro"
#endif

#if HAL_IMU_IS_SH200Q
  #include <M5StickC.h>
  #define HAL_IMU_TYPE IMU_TYPE_SH200Q
  // Board推定（わからなければUNKNOWN）
  #if HAL_BOARD_IS_STICKC
    #if defined(ARDUINO_M5Stick_C_PLUS)
      #define HAL_DEVICE_MODEL DEVICE_MODEL_STICKC_PLUS
    #elif defined(ARDUINO_M5Stick_C)
      #define HAL_DEVICE_MODEL DEVICE_MODEL_STICKC
    #else
      #define HAL_DEVICE_MODEL DEVICE_MODEL_STICKC
    #endif
  #else
    #define HAL_DEVICE_MODEL DEVICE_MODEL_UNKNOWN
  #endif
#else
  #include <M5Unified.h>
  #define HAL_IMU_TYPE IMU_TYPE_MPU6886
  // Plus2/Core2 を識別、その他は UNKNOWN
  #if HAL_BOARD_IS_PLUS2
    #define HAL_DEVICE_MODEL DEVICE_MODEL_STICKC_PLUS2
  #elif HAL_BOARD_IS_CORE2
    #define HAL_DEVICE_MODEL DEVICE_MODEL_CORE2_16MB
  #else
    #define HAL_DEVICE_MODEL DEVICE_MODEL_CORE2_UNKNOWN
  #endif
  // M5Unified で M5.Lcd 互換になるように alias
  #define Lcd Display
#endif

// --- 画面/電源制御 ---
inline void hal_screen_on(uint8_t brightness_active, uint8_t brightness_off) {
#if HAL_IMU_IS_SH200Q
    uint8_t axp_b = brightness_active > 12 ? 12 : brightness_active;
    M5.Axp.SetLDO2(true);
    M5.Axp.ScreenBreath(axp_b);
#else
    M5.Display.setBrightness(brightness_active);
#endif
    (void)brightness_off;
}

inline void hal_screen_off(uint8_t brightness_off) {
#if HAL_IMU_IS_SH200Q
    uint8_t axp_b = brightness_off > 12 ? 12 : brightness_off;
    M5.Axp.ScreenBreath(axp_b);
    M5.Axp.SetLDO2(false);
#else
    M5.Display.setBrightness(brightness_off);
#endif
}

inline void hal_power_off() {
#if HAL_IMU_IS_SH200Q
    // AXP192 による電源オフ
    M5.Axp.PowerOff();
#else
    M5.Power.powerOff();
#endif
}

// --- 初期化と更新 ---
inline void hal_begin() {
#if HAL_IMU_IS_SH200Q
    M5.begin();
    M5.Lcd.setRotation(LCD_ROTATION);
#else
    M5.begin();
    M5.Display.setRotation(LCD_ROTATION);
#endif
}

// Touch state for non-SH200Q devices
#if !HAL_IMU_IS_SH200Q
static bool s_touch_prev = false;
static uint32_t s_touch_down_ms = 0;
static uint32_t s_touch_last_dur = 0;
static bool s_touch_released = false;
#endif

// Compatibility helper: wasReleaseFor (new) vs wasReleasefor (old)
inline bool hal_btnA_wasReleaseFor(uint32_t ms) {
#if HAS_M5UNIFIED
    return M5.BtnA.wasReleaseFor(ms);
#else
    return M5.BtnA.wasReleasefor(ms);
#endif
}

inline void hal_update() {
    M5.update();
#if !HAL_IMU_IS_SH200Q
    if (HAL_DEVICE_MODEL == DEVICE_MODEL_CORE2_16MB || HAL_DEVICE_MODEL == DEVICE_MODEL_CORE2_UNKNOWN) {
        uint32_t now = millis();
        M5.Touch.update(now);
        bool pressed = M5.Touch.getDetail().isPressed();
        if (pressed && !s_touch_prev) {
            s_touch_down_ms = now;
            s_touch_released = false;
        } else if (!pressed && s_touch_prev) {
            // released
            s_touch_last_dur = now - s_touch_down_ms;
            s_touch_released = true;
        }
        s_touch_prev = pressed;
    }
#endif
}

// --- 入力 ---
inline bool hal_btn_long(uint32_t ms) {
    // StickC/Plus/Plus2: 物理BtnA, Core2: タッチ長押し
#if HAL_IMU_IS_SH200Q
    return hal_btnA_wasReleaseFor(ms);
#else
    if (HAL_DEVICE_MODEL == DEVICE_MODEL_CORE2_16MB || HAL_DEVICE_MODEL == DEVICE_MODEL_CORE2_UNKNOWN) {
        if (s_touch_released && s_touch_last_dur >= ms) {
            s_touch_released = false;
            return true;
        }
        return false;
    }
    return hal_btnA_wasReleaseFor(ms);
#endif
}

inline bool hal_btn_short_released() {
    // StickC/Plus/Plus2: 物理BtnA, Core2: タッチ短押し
#if HAL_IMU_IS_SH200Q
    return M5.BtnA.wasReleased();
#else
    if (HAL_DEVICE_MODEL == DEVICE_MODEL_CORE2_16MB || HAL_DEVICE_MODEL == DEVICE_MODEL_CORE2_UNKNOWN) {
        if (s_touch_released && s_touch_last_dur < 800 /* default long threshold */) {
            s_touch_released = false;
            return true;
        }
        return false;
    }
    return M5.BtnA.wasReleased();
#endif
}

// --- LCD アクセスショートカット ---
inline auto& hal_lcd() {
#if HAL_IMU_IS_SH200Q
    return M5.Lcd;
#else
    return M5.Display;
#endif
}

// --- I2Cピン/IMUアドレス取得 (M5Unifiedのデフォルトに揃える) ---
inline uint8_t hal_imu_addr() {
#if HAL_IMU_IS_SH200Q
    return 0x6C; // SH200Q default (M5Unifiedと同じ)
#else
    return 0x68; // MPU6886 default
#endif
}

inline int hal_i2c_sda_pin() {
#if HAL_IMU_IS_SH200Q
    return SDA;
#else
  int pin = M5.getPin(m5::pin_name_t::in_i2c_sda);
  return (pin >= 0) ? pin : SDA;
#endif
}

inline int hal_i2c_scl_pin() {
#if HAL_IMU_IS_SH200Q
    return SCL;
#else
  int pin = M5.getPin(m5::pin_name_t::in_i2c_scl);
  return (pin >= 0) ? pin : SCL;
#endif
}

inline const char* hal_i2c_bus_name() {
#if HAL_IMU_IS_SH200Q
    return "Wire1";
#else
    return "In_I2C";
#endif
}

// Compatibility helper: wasReleaseFor (new) vs wasReleasefor (old)
// --- Board/IMU name helpers ---
inline const char* hal_board_key() {
    switch (HAL_DEVICE_MODEL) {
        case DEVICE_MODEL_STICKC: return "stickc";
        case DEVICE_MODEL_STICKC_PLUS: return "stickc_plus";
        case DEVICE_MODEL_STICKC_PLUS2: return "plus2";
        case DEVICE_MODEL_CORE2_16MB: return "core2";
        default: return "unknown";
    }
}

inline const char* hal_imu_key() {
    switch (HAL_IMU_TYPE) {
        case IMU_TYPE_SH200Q: return "sh200q";
        case IMU_TYPE_MPU6886: return "mpu6886";
        default: return "unknown";
    }
}
