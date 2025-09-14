#pragma once
#include <LittleFS.h>
#include "config.h"

// Initialize LittleFS; format if mounting fails
inline bool fs_init() {
    if (!LittleFS.begin(false)) {
        return LittleFS.begin(true);
    }
    return true;
}

// Format LittleFS explicitly
inline bool fs_format() {
    return LittleFS.format();
}

// Create log file for writing; existing file will be truncated
inline File fs_create_log() {
    return LittleFS.open(LOG_FILE_NAME, "w");
}
