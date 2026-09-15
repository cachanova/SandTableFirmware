#include "Arduino.h"
#include <SDCard.hpp>
#include <ErrorLog.hpp>

#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <esp_heap_caps.h>
#include <freertos/task.h>
#include <PolarControl.hpp>
#include <SisyphusWebServer.hpp>
#include <LEDController.hpp>

#include <Config.h>

PolarControl polarControl;
SisyphusWebServer webServer(Config::kWebServerPort);
LEDController ledController(Config::kLedPin);

TaskHandle_t motorTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;

void motorTask(void *parameter) {
    LOG("Motor task started on Core %d\r\n", xPortGetCoreID());

    unsigned long lastPrint = millis();
    unsigned long loopCount = 0;
    uint32_t lastUnderruns = 0;

    while (true) {
        bool isBusy = polarControl.processNextMove();
        loopCount++;

        unsigned long now = millis();
        if (now - lastPrint >= 1000) {
            PolarCord_t pos = polarControl.getCurrentPosition();
            float loopsPerSec = (float)loopCount * 1000.0 / (now - lastPrint);
            auto state = polarControl.getState();
            
            PlannerTelemetry telemetry;
            polarControl.getTelemetry(telemetry);

            uint32_t maxProc, maxInt, avgGen;
            polarControl.getProfileData(maxProc, maxInt, avgGen);
            uint32_t maxMutexWait, avgMutexWait;
            polarControl.getMutexWaitProfile(maxMutexWait, avgMutexWait);

            if (state == PolarControl::RUNNING || state == PolarControl::CLEARING || state == PolarControl::PREPARING) {
                LOG("[MOTOR Core%d] Pos: ρ=%.0f θ=%.0f° | Q: %u | UR: %u | Loop: %.0f/s | MaxProc: %uus | MaxInt: %uus | AvgGen: %uus | MutexW max/avg: %u/%u\r\n",
                              xPortGetCoreID(),
                              pos.rho,
                              pos.theta * 180.0 / PI,
                              telemetry.queueDepth,
                              telemetry.underruns,
                              loopsPerSec,
                              maxProc,
                              maxInt,
                              avgGen,
                              maxMutexWait,
                              avgMutexWait);
            }

            if (telemetry.underruns > lastUnderruns) {
                uint32_t delta = telemetry.underruns - lastUnderruns;
                uint32_t coordDepth = polarControl.getCoordQueueDepth();
                uint32_t lastFileLine = polarControl.getLastFileLine();
                LOG("[MOTOR Core%d] Underrun burst +%u | Q cur/min: %u/%u | maxConsec: %u | coordQ: %u | timer:%u running:%u\r\n",
                    xPortGetCoreID(),
                    delta,
                    telemetry.queueDepth,
                    telemetry.minQueueDepth,
                    telemetry.maxConsecutiveUnderruns,
                    coordDepth,
                    telemetry.timerActive ? 1 : 0,
                    telemetry.running ? 1 : 0);
                char context[160];
                snprintf(context, sizeof(context),
                         "delta=%u q=%u min=%u maxConsec=%u coordQ=%u",
                         delta, telemetry.queueDepth, telemetry.minQueueDepth,
                         telemetry.maxConsecutiveUnderruns, coordDepth);
                ErrorLog::instance().log("ERROR", "MOTION", "QUEUE_UNDERRUN",
                                         "Queue underrun burst", context);
                LOG("[MOTOR Core%d] Underrun context | segCompleted: %u | fileLine: %u\r\n",
                    xPortGetCoreID(),
                    telemetry.completedCount,
                    lastFileLine);
            }

            lastUnderruns = telemetry.underruns;
            loopCount = 0;
            lastPrint = now;
        }

        if (!isBusy) {
            vTaskDelay(1);
        } else {
            vTaskDelay(0); // Yield to same-priority tasks on Core 1
        }
    }
}

void webTask(void *parameter) {
    LOG("Web logic task started on Core %d\r\n", xPortGetCoreID());
    unsigned long lastStats = millis();
    uint32_t loopCount = 0;
    uint64_t totalLoopUs = 0;
    uint32_t maxLoopUs = 0;
    while (true) {
        uint32_t startUs = micros();
        webServer.loop();
#ifndef SISYPHUS_SKIP_OTA
        ArduinoOTA.handle();
#endif
        uint32_t loopUs = micros() - startUs;
        totalLoopUs += loopUs;
        loopCount++;
        if (loopUs > maxLoopUs) maxLoopUs = loopUs;

        unsigned long now = millis();
        if (now - lastStats >= 5000) {
            uint32_t reqTotal = 0;
            uint32_t reqInflight = 0;
            webServer.getRequestStats(reqTotal, reqInflight);

            uint32_t fileStack = polarControl.getFileTaskHighWater();
            UBaseType_t webStack = uxTaskGetStackHighWaterMark(webTaskHandle);
            UBaseType_t motorStack = uxTaskGetStackHighWaterMark(motorTaskHandle);

            uint32_t freeHeap = ESP.getFreeHeap();
            uint32_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            float avgLoopUs = loopCount ? static_cast<float>(totalLoopUs) / loopCount : 0.0f;

            LOG("[WEB Core%d] Loop avg: %.0fus max: %uus | Heap: %u | Largest: %u | Stack W/M/F: %u/%u/%u | Req total: %u inflight: %u\r\n",
                xPortGetCoreID(),
                avgLoopUs,
                maxLoopUs,
                freeHeap,
                largestBlock,
                static_cast<unsigned int>(webStack),
                static_cast<unsigned int>(motorStack),
                static_cast<unsigned int>(fileStack),
                reqTotal,
                reqInflight);


            lastStats = now;
            loopCount = 0;
            totalLoopUs = 0;
            maxLoopUs = 0;
        }
        vTaskDelay(10); // Run at ~100Hz, sufficient for UI updates
    }
}

void setup() {
    // ... (rest of setup unchanged until task creation)
#ifdef SISYPHUS_SKIP_MOTOR_HARDWARE
    // Reduce peak current during electronics-only bring-up. This is especially
    // useful when the ESP32 and attached driver logic share a USB supply.
    setCpuFrequencyMhz(80);
#endif
    Serial.begin(115200);
    delay(500);

    LOG("\n\n=== Sisyphus Table Starting ===\r\n");
    LOG("ESP reset reason: %d\r\n", static_cast<int>(esp_reset_reason()));

    // Initialize SD Card
#ifdef SISYPHUS_SKIP_SD_HARDWARE
    LOG("DIAGNOSTIC: SD bus initialization is disabled.\r\n");
#else
    if (!initSDCard()) {
        LOG("WARNING: Running without SD card storage!\r\n");
    } else {
        listSDFiles();
    }
#endif

    // WiFi Setup
    LOG("Setting up WiFi...\r\n");
    WiFi.mode(WIFI_STA);
#ifdef SISYPHUS_SKIP_MOTOR_HARDWARE
    WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
#endif
    WiFiManager wm;
    wm.setConfigPortalTimeout(Config::kWifiPortalTimeoutSec);
    wm.preloadWiFi(Config::kWifiSsid, Config::kWifiPassword);

    // Configure static IP
    wm.setSTAStaticIPConfig(Config::kStaticIpBase, Config::kStaticGateway, Config::kStaticSubnet, Config::kStaticDns);
    LOG("Requesting static IP: %s\r\n", Config::kStaticIpBase.toString().c_str());

    if (!wm.autoConnect(Config::kApSsid, Config::kApPassword)) {
        LOG("Failed to connect to WiFi\r\n");
        ErrorLog::instance().log("ERROR", "WIFI", "CONNECT_FAILED",
                                 "Failed to connect to WiFi", Config::kApSsid);
        ESP.restart();
    }

    LOG("WiFi connected!\r\n");
    LOG("IP Address: %s\r\n", WiFi.localIP().toString().c_str());
    LOG("SSID: %s\r\n", WiFi.SSID().c_str());

    // Setup OTA updates
#ifdef SISYPHUS_SKIP_OTA
    LOG("DIAGNOSTIC: OTA initialization is disabled.\r\n");
#else
    ArduinoOTA.setHostname(Config::kOtaHostname);
    ArduinoOTA.setPassword(Config::kOtaPassword);
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        LOG("OTA Start: %s\r\n", type.c_str());
        // Stop motors during OTA update
        polarControl.emergencyStop();
    });
    ArduinoOTA.onEnd([]() {
        LOG("\nOTA End - Rebooting...\r\n");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static unsigned int lastReported = 101;
        const unsigned int percent = total ? (progress * 100U / total) : 0U;
        if (lastReported == 101 || percent >= lastReported + 5 || percent == 100) {
            lastReported = percent;
            LOG("OTA Progress: %u%%\r\n", percent);
        }
    });
    ArduinoOTA.onError([](ota_error_t error) {
        LOG("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            LOG("OTA Auth Failed\r\n");
            ErrorLog::instance().log("ERROR", "OTA", "AUTH_FAILED", "OTA auth failed");
        } else if (error == OTA_BEGIN_ERROR) {
            LOG("OTA Begin Failed\r\n");
            ErrorLog::instance().log("ERROR", "OTA", "BEGIN_FAILED", "OTA begin failed");
        } else if (error == OTA_CONNECT_ERROR) {
            LOG("OTA Connect Failed\r\n");
            ErrorLog::instance().log("ERROR", "OTA", "CONNECT_FAILED", "OTA connect failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            LOG("OTA Receive Failed\r\n");
            ErrorLog::instance().log("ERROR", "OTA", "RECEIVE_FAILED", "OTA receive failed");
        } else if (error == OTA_END_ERROR) {
            LOG("OTA End Failed\r\n");
            ErrorLog::instance().log("ERROR", "OTA", "END_FAILED", "OTA end failed");
        }
    });
    ArduinoOTA.begin();
    LOG("OTA updates enabled\r\n");
#endif

    // Initialize hardware
    LOG("Initializing motors...\r\n");
    bool motorSubsystemReady = polarControl.begin();
#ifdef SISYPHUS_SKIP_MOTOR_HARDWARE
    motorSubsystemReady = false;
    LOG("DIAGNOSTIC: Motor UART and driver initialization are disabled.\r\n");
#else
    motorSubsystemReady = motorSubsystemReady && polarControl.setupDrivers();
#endif
    if (!motorSubsystemReady) {
        LOG("Motor subsystem failed to initialize; pattern motion is locked out.\r\n");
    } else {
#if defined(SISYPHUS_BENCH_MOTION_TEST) || defined(SISYPHUS_THETA_COMMISSIONING)
        polarControl.assumeBenchTestOrigin();
#ifdef SISYPHUS_THETA_COMMISSIONING
        LOG("WARNING: THETA-ONLY COMMISSIONING firmware is active.\r\n");
#else
        LOG("WARNING: BENCH MOTION TEST firmware is active; physical position is not known.\r\n");
#endif
#elif defined(SISYPHUS_RHO_COMMISSIONING)
        // The unified rho-service image always boots fail-closed in manual
        // relative-jog mode. Commissioning origin assignment requires an
        // explicit API/UI confirmation after the operator positions the axis.
        polarControl.enterRhoManualServiceMode();
        LOG("WARNING: RHO SERVICE firmware is active in manual mode; theta and patterns remain locked out.\r\n");
#else
        if (Config::kAutoHomeOnBoot) {
            polarControl.home();
        } else {
            LOG("Automatic homing disabled; use the UI to home and confirm position.\r\n");
        }
#endif
    }

    // Initialize LED controller
    LOG("Initializing LED controller...\r\n");
    ledController.begin();

    // Start web server
    LOG("Starting web server...\r\n");
    webServer.begin(&polarControl, &ledController);

    LOG("\n=== System Ready ===\r\n");
    LOG("Access web interface at: http://%s\r\n", WiFi.localIP().toString().c_str());

    // Create motor task on Core 1
    LOG("Starting motor task on Core 1...\r\n");
    const BaseType_t motorTaskCreated = xTaskCreatePinnedToCore(
        motorTask,
        "MotorTask",
        4096,
        NULL,
        1,
        &motorTaskHandle,
        Config::kMotorCore
    );

    // Create web logic task on Core 0
    LOG("Starting web logic task on Core 0...\r\n");
    const BaseType_t webTaskCreated = xTaskCreatePinnedToCore(
        webTask,
        "WebTask",
        8192,
        NULL,
        1,
        &webTaskHandle,
        Config::kWebCore
    );
    if (motorTaskCreated != pdPASS) {
        motorTaskHandle = NULL;
        ErrorLog::instance().log("ERROR", "SYSTEM", "MOTOR_TASK_CREATE_FAILED",
                                 "Could not create motor processing task");
    }
    if (webTaskCreated != pdPASS) {
        webTaskHandle = NULL;
        ErrorLog::instance().log("ERROR", "SYSTEM", "WEB_TASK_CREATE_FAILED",
                                 "Could not create web processing task");
    }

    // Initial speed
    polarControl.setSpeed(5);

    LOG("Main setup done on Core %d\r\n", xPortGetCoreID());
}

void loop() {
    vTaskDelete(NULL); // Delete the default loop task
}
