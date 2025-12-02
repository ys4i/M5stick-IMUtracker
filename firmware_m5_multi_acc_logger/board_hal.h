#pragma once

// Minimal board/IMU abstraction.
// - SH200Q 搭載デバイス: M5StickC 系など。レジスタ直叩きを行う。
// - 非SH200Q (例: MPU6886/Core2): M5Unified を利用する。

#include <Arduino.h>
#include "config.h"

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
#if defined(IMU_FORCE_SH200Q)
#  define HAL_IMU_IS_SH200Q 1
#elif defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_C_PLUS) || defined(ARDUINO_M5Stick_C_PLUS2)
#  define HAL_IMU_IS_SH200Q 1
#else
#  define HAL_IMU_IS_SH200Q 0
#endif

#if HAL_IMU_IS_SH200Q
  #include <M5StickC.h>
  #define HAL_IMU_TYPE IMU_TYPE_SH200Q
  // Board推定（わからなければUNKNOWN）
  #if defined(ARDUINO_M5Stick_C_PLUS2)
    #define HAL_DEVICE_MODEL DEVICE_MODEL_STICKC_PLUS2
  #elif defined(ARDUINO_M5Stick_C_PLUS)
    #define HAL_DEVICE_MODEL DEVICE_MODEL_STICKC_PLUS
  #elif defined(ARDUINO_M5Stick_C)
    #define HAL_DEVICE_MODEL DEVICE_MODEL_STICKC
  #else
    #define HAL_DEVICE_MODEL DEVICE_MODEL_UNKNOWN
  #endif
#else
  #include <M5Unified.h>
  #define HAL_IMU_TYPE IMU_TYPE_MPU6886
  // Core2 を既定とし、それ以外は UNKNOWN
  #if defined(ARDUINO_M5STACK_Core2) || defined(M5STACK_CORE2) || defined(M5STACK_M5Core2)
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
    M5.Axp.SetLDO2(true);
    M5.Axp.ScreenBreath(brightness_active);
#else
    M5.Display.setBrightness(brightness_active);
#endif
    (void)brightness_off;
}

inline void hal_screen_off(uint8_t brightness_off) {
#if HAL_IMU_IS_SH200Q
    M5.Axp.ScreenBreath(brightness_off);
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
#else
    M5.begin();
    M5.Display.setRotation(3);
#endif
}

// Touch state for non-SH200Q devices
#if !HAL_IMU_IS_SH200Q
static bool s_touch_prev = false;
static uint32_t s_touch_down_ms = 0;
static uint32_t s_touch_last_dur = 0;
static bool s_touch_released = false;
#endif

inline void hal_update() {
    M5.update();
#if !HAL_IMU_IS_SH200Q
    M5.Touch.update();
    bool pressed = M5.Touch.isPressed();
    uint32_t now = millis();
    if (pressed && !s_touch_prev) {
        s_touch_down_ms = now;
        s_touch_released = false;
    } else if (!pressed && s_touch_prev) {
        // released
        s_touch_last_dur = now - s_touch_down_ms;
        s_touch_released = true;
    }
    s_touch_prev = pressed;
#endif
}

// --- 入力 ---
inline bool hal_btn_long(uint32_t ms) {
    // SH200Q系は物理BtnA
#if HAL_IMU_IS_SH200Q
    return M5.BtnA.wasReleasefor(ms);
#else
    if (s_touch_released && s_touch_last_dur >= ms) {
        s_touch_released = false;
        return true;
    }
    return false;
#endif
}

inline bool hal_btn_short_released() {
    // SH200Q系は物理BtnA
#if HAL_IMU_IS_SH200Q
    return M5.BtnA.wasReleased();
#else
    if (s_touch_released && s_touch_last_dur < 800 /* default long threshold */) {
        s_touch_released = false;
        return true;
    }
    return false;
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
