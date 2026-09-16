#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <atomic>
#include "Logger.hpp"
#include "ErrorLog.hpp"

// SD Card SPI pins
#define SD_CS_PIN   5
#define SD_MOSI_PIN 23
#define SD_CLK_PIN  18
#define SD_MISO_PIN 19

inline std::atomic<bool>& sdCardReadyFlag() {
    static std::atomic<bool> ready{false};
    return ready;
}

inline bool isSDCardReady() {
    return sdCardReadyFlag().load();
}

// Initialize SD card - call once in setup()
inline bool initSDCard() {
    sdCardReadyFlag().store(false);

    // Explicitly initialize SPI with defined pins
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    // The pinned Arduino SD driver limits supported cards to 25 MHz.
    if (!SD.begin(SD_CS_PIN, SPI, 40000000)) {
        LOG("ERROR: SD card mount failed!\r\n");
        ErrorLog::instance().log("ERROR", "SD", "MOUNT_FAILED", "SD card mount failed");
        SD.end();
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        LOG("No SD card attached\r\n");
        ErrorLog::instance().log("ERROR", "SD", "NO_CARD", "No SD card attached");
        SD.end();
        return false;
    }

    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC) typeStr = "MMC";
    else if (cardType == CARD_SD) typeStr = "SDSC";
    else if (cardType == CARD_SDHC) typeStr = "SDHC";
    LOG("SD Card Type: %s\r\n", typeStr);

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    LOG("SD Card Size: %llu MB\r\n", cardSize);

    // Ensure directory structure exists
    if (!SD.exists("/patterns")) {
        LOG("Creating /patterns directory\r\n");
        if (!SD.mkdir("/patterns")) {
            ErrorLog::instance().log("ERROR", "SD", "DIRECTORY_FAILED",
                                     "Could not create directory", "/patterns");
            SD.end();
            return false;
        }
    }
    if (!SD.exists("/playlists")) {
        LOG("Creating /playlists directory\r\n");
        if (!SD.mkdir("/playlists")) {
            ErrorLog::instance().log("ERROR", "SD", "DIRECTORY_FAILED",
                                     "Could not create directory", "/playlists");
            SD.end();
            return false;
        }
    }

    sdCardReadyFlag().store(true);
    return true;
}

// Helper to list files on SD card
inline void listSDFiles(const char* dirname = "/") {
    LOG("\r\n=== Files on SD Card (%s) ===\r\n", dirname);

    File root = SD.open(dirname);
    if (!root) {
        LOG("ERROR: Failed to open directory\r\n");
        ErrorLog::instance().log("ERROR", "SD", "OPEN_DIR_FAILED",
                                 "Failed to open directory", dirname);
        return;
    }

    if (!root.isDirectory()) {
        LOG("ERROR: Not a directory\r\n");
        ErrorLog::instance().log("ERROR", "SD", "NOT_DIRECTORY",
                                 "Path is not a directory", dirname);
        return;
    }

    File file = root.openNextFile();
    int count = 0;
    while (file) {
        if (!file.isDirectory()) {
            LOG("  %s (%lu bytes)\r\n", file.name(), (unsigned long)file.size());
            count++;
        }
        file.close();
        file = root.openNextFile();
    }
    
    LOG("Total files: %d\r\n\r\n", count);
}

// Recursively delete a directory and all its contents
inline bool removeDirectoryRecursive(const char* path) {
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return false;
    }

    File entry = dir.openNextFile();
    while (entry) {
        String entryPath = String(path) + "/" + entry.name();
        if (entry.isDirectory()) {
            entry.close();
            removeDirectoryRecursive(entryPath.c_str());
        } else {
            entry.close();
            SD.remove(entryPath.c_str());
        }
        entry = dir.openNextFile();
    }
    dir.close();
    return SD.rmdir(path);
}
