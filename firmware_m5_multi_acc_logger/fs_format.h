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

// File system capacity helpers
inline size_t fs_total_bytes() {
    return LittleFS.totalBytes();
}

inline size_t fs_used_bytes() {
    return LittleFS.usedBytes();
}

inline size_t fs_free_bytes() {
    size_t total = fs_total_bytes();
    size_t used = fs_used_bytes();
    return (used <= total) ? (total - used) : 0;
}

inline uint8_t fs_used_pct() {
    size_t total = fs_total_bytes();
    if (total == 0) return 0;
    return (uint8_t)((fs_used_bytes() * 100) / total);
}
