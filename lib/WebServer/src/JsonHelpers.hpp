#pragma once
#include <ArduinoJson.h>
#include <new>
#include <SD.h>
#include <Print.h>
#include <esp_heap_caps.h>
#include "PolarControl.hpp"
#include "LEDController.hpp"
#include "PresenceSensor.hpp"
#include "SDCard.hpp"

class JsonHelpers {
public:
    static int brightnessPercent(uint8_t brightness) {
        // Round the PWM value back to percent so setting 50 reports 50, not 49.
        return (static_cast<unsigned>(brightness) * 100U + 127U) / 255U;
    }

    static void writeStatusJSON(Print& out, PolarControl* polarControl,
                                LEDController* ledController,
                                PresenceSensor* presenceSensor,
                                const String& currentPattern,
                                const String& clearingPattern,
                                const String& queuedPattern, uint32_t fileListRevision) {
        String state = getStateString(polarControl->getState());
        int progress = polarControl->getProgressPercent();
        uint8_t brightness = ledController->getBrightness();
        int brightnessPercent = JsonHelpers::brightnessPercent(brightness);
        uint8_t speed = polarControl->getSpeed();
        uint32_t heap = ESP.getFreeHeap();
        uint32_t uptime = millis() / 1000;
        int clearingProgress = (state == "CLEARING") ? progress : -1;
        HomingStatus homing = polarControl->getHomingStatus();
        DriverAvailability drivers = polarControl->getDriverAvailability();

        out.print("{\"state\":\"");
        out.print(state);
        out.print("\",\"currentPattern\":\"");
        out.print(currentPattern);
        out.print("\",\"clearingPattern\":\"");
        out.print(clearingPattern);
        out.print("\",\"queuedPattern\":\"");
        out.print(queuedPattern);
        out.print("\",\"progress\":");
        out.print(progress);
        out.print(",\"clearingProgress\":");
        out.print(clearingProgress);
        out.print(",\"etaSeconds\":");
        out.print(polarControl->getEtaSeconds());
        out.print(",\"ledBrightness\":");
        out.print(brightnessPercent);
        out.print(",\"ledTargetBrightness\":");
        out.print(JsonHelpers::brightnessPercent(ledController->getTargetBrightness()));
        out.print(",\"speed\":");
        out.print(speed);
        out.print(",\"heap\":");
        out.print(heap);
        out.print(",\"uptime\":");
        out.print(uptime);
        out.print(",\"resetReason\":");
        out.print(static_cast<int>(esp_reset_reason()));
        out.print(",\"storageAvailable\":");
        out.print(isSDCardReady() ? "true" : "false");
        out.print(",\"fileListRevision\":");
        out.print(fileListRevision);
        out.print(",\"presence\":");
        writePresenceJSON(out, presenceSensor);
        out.print(",\"drivers\":{\"theta\":");
        out.print(drivers.theta ? "true" : "false");
        out.print(",\"rho\":");
        out.print(drivers.rho ? "true" : "false");
        out.print(",\"rhoCompanion\":");
        out.print(drivers.rhoCompanion ? "true" : "false");
        out.print(",\"thetaAxis\":");
        out.print(drivers.thetaAxis() ? "true" : "false");
        out.print(",\"rhoAxis\":");
        out.print(drivers.rhoAxis() ? "true" : "false");
        out.print("}");
        out.print(",\"homing\":{\"cycle\":");
        out.print(homing.cycle);
        out.print(",\"stepsPerMm\":");
        out.print(homing.stepsPerMm);
        out.print(",\"phase\":");
        out.print(homing.phase);
        out.print(",\"phasePulseCount\":");
        out.print(homing.phasePulseCount);
        out.print(",\"fastApproachMs\":");
        out.print(homing.fastApproachMs);
        out.print(",\"slowApproachMs\":");
        out.print(homing.slowApproachMs);
        out.print(",\"fastApproachSteps\":");
        out.print(homing.fastApproachSteps);
        out.print(",\"slowApproachSteps\":");
        out.print(homing.slowApproachSteps);
        out.print(",\"fastBaseline\":");
        out.print(homing.fastBaseline);
        out.print(",\"fastTrigger\":");
        out.print(homing.fastTrigger);
        out.print(",\"baseline\":");
        out.print(homing.baseline);
        out.print(",\"trigger\":");
        out.print(homing.trigger);
        out.print(",\"companionFastApproachMs\":");
        out.print(homing.companionFastApproachMs);
        out.print(",\"companionSlowApproachMs\":");
        out.print(homing.companionSlowApproachMs);
        out.print(",\"companionFastApproachSteps\":");
        out.print(homing.companionFastApproachSteps);
        out.print(",\"companionSlowApproachSteps\":");
        out.print(homing.companionSlowApproachSteps);
        out.print(",\"companionFastBaseline\":");
        out.print(homing.companionFastBaseline);
        out.print(",\"companionFastTrigger\":");
        out.print(homing.companionFastTrigger);
        out.print(",\"companionBaseline\":");
        out.print(homing.companionBaseline);
        out.print(",\"companionTrigger\":");
        out.print(homing.companionTrigger);
        out.print(",\"uartSamples\":");
        out.print(homing.uartSamples);
        out.print(",\"validUartSamples\":");
        out.print(homing.validUartSamples);
        out.print(",\"failure\":");
        out.print(homing.failure);
        out.print(",\"failedAxis\":");
        out.print(homing.failedAxis);
        out.print("}");
        out.print("}");
    }

    static void writePresenceJSON(Print& out, PresenceSensor* presenceSensor) {
        const PresenceStatus status = presenceSensor == nullptr
            ? PresenceStatus{} : presenceSensor->getStatus();
        out.print("{\"available\":");
        out.print(status.available ? "true" : "false");
        out.print(",\"receiving\":");
        out.print(status.receiving ? "true" : "false");
        out.print(",\"calibrated\":");
        out.print(status.calibrated ? "true" : "false");
        out.print(",\"calibrating\":");
        out.print(status.calibrating ? "true" : "false");
        out.print(",\"suppressed\":");
        out.print(status.suppressed ? "true" : "false");
        out.print(",\"memoryLimited\":");
        out.print(status.memoryLimited ? "true" : "false");
        out.print(",\"maxProcessingUs\":");
        out.print(status.maxProcessingUs);
        out.print(",\"motion\":");
        out.print(status.motion ? "true" : "false");
        out.print(",\"occupied\":");
        out.print(status.occupied ? "true" : "false");
        out.print(",\"calibrationProgress\":");
        out.print(status.calibrationProgress);
        out.print(",\"score\":");
        out.print(status.score, 3);
        out.print(",\"threshold\":");
        out.print(status.threshold, 4);
        out.print(",\"rssi\":");
        out.print(status.rssi);
        out.print(",\"packets\":");
        out.print(status.packets);
        out.print(",\"dropped\":");
        out.print(status.dropped);
        out.print(",\"acceptedSamples\":");
        out.print(status.acceptedSamples);
        out.print(",\"sampleAgeMs\":");
        out.print(status.sampleAgeMs);
        out.print(",\"lastMotionMs\":");
        out.print(status.lastMotionMs);
        out.print("}");
    }

    static void writeFileListJSON(Print& out) {
        out.print("{\"storageAvailable\":");
        out.print(isSDCardReady() ? "true" : "false");
        out.print(",\"files\":[");
        bool first = true;

        File root;
        if (isSDCardReady()) root = SD.open("/patterns");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                String name = String(file.name());
                if (name.startsWith("/")) name = name.substring(1);

                if (!file.isDirectory()) {
                    if (name.endsWith(".thr")) {
                        JsonDocument entry;
                        entry["name"] = name;
                        entry["size"] = file.size();
                        entry["time"] = file.getLastWrite();
                        if (!first) out.print(',');
                        if (entry.overflowed()) throw std::bad_alloc();
                        serializeJson(entry, out);
                        first = false;
                    }
                } else {
                    String patternFile = name + ".thr";
                    String innerPath = "/patterns/" + name + "/" + patternFile;
                    if (SD.exists(innerPath)) {
                        File innerFile = SD.open(innerPath);
                        JsonDocument entry;
                        entry["name"] = patternFile;
                        entry["size"] = innerFile.size();
                        entry["time"] = innerFile.getLastWrite();
                        if (!first) out.print(',');
                        if (entry.overflowed()) throw std::bad_alloc();
                        serializeJson(entry, out);
                        first = false;
                        innerFile.close();
                    }
                }
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }
        out.print("]}");
    }

    static void writeSystemInfoJSON(Print& out) {
        JsonDocument doc;
        doc["heap"] = ESP.getFreeHeap();
        doc["largestFreeBlock"] = ESP.getMaxAllocHeap();
        doc["minimumFreeHeap"] = ESP.getMinFreeHeap();
        // Arduino's legacy heap figures include internal 32-bit-only memory.
        // Network buffers and CSI admission need byte-addressable memory;
        // expose that pool separately without changing the existing API fields.
        doc["heap8Bit"] = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        doc["largestFree8BitBlock"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        doc["minimumFree8BitHeap"] = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        doc["uptime"] = millis() / 1000;

        JsonObject wifi = doc["wifi"].to<JsonObject>();
        wifi["ssid"] = WiFi.SSID();
        wifi["ip"] = WiFi.localIP().toString();
        wifi["rssi"] = WiFi.RSSI();
        wifi["sleep"] = static_cast<int>(WiFi.getSleep());
        if (doc.overflowed()) throw std::bad_alloc();
        serializeJson(doc, out);
    }

    static String getStateString(PolarControl::State_t state) {
        switch (static_cast<uint8_t>(state)) {
            case 0: return "UNINITIALIZED";
            case 1: return "INITIALIZED";
            case 2: return "IDLE";
            case 3: return "RUNNING";
            case 4: return "PAUSED";
            case 5: return "STOPPING";
            case 6: return "CLEARING";
            case 7: return "PREPARING";
            case 8: return "HOMING";
            case 9: return "HOMING_REVIEW";
            case 10: return "HOMING_FAILED";
            default: return "UNKNOWN";
        }
    }
};
