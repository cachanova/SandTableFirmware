#pragma once

#include "Arduino.h"

namespace Config {
// Preferred station network. WiFiManager still opens the fallback portal if
// this network is unavailable.
static constexpr const char kWifiSsid[] = "DUKE";
static constexpr const char kWifiPassword[] = "DUKE5000";

// WiFi AP fallback credentials for captive portal mode.
static constexpr const char kApSsid[] = "SisyphusTable";
static constexpr const char kApPassword[] = "sandpatterns";

// Core affinity for the FreeRTOS tasks.
static constexpr int kMotorCore = 1;
static constexpr int kWebCore = 0;

// Config portal timeout for WiFiManager in seconds.
static constexpr uint32_t kWifiPortalTimeoutSec = 180;

// Keep automatic motion disabled while developing on the bench. Sensorless
// homing must be started from the UI and visually confirmed before patterns
// are allowed to run.
static constexpr bool kAutoHomeOnBoot = false;

// Static IP defaults for STA mode.
static const IPAddress kStaticIpBase(100, 76, 149, 200);
static const IPAddress kStaticGateway(100, 76, 149, 1);
static const IPAddress kStaticSubnet(255, 255, 255, 0);
static const IPAddress kStaticDns(100, 76, 149, 1);

// OTA identity and auth.
static constexpr const char kOtaHostname[] = "sisyphus";
static constexpr const char kOtaPassword[] = "sandpatterns";

// Web server port and attached LED count.
static constexpr uint16_t kWebServerPort = 80;
static constexpr uint8_t kLedPin = 2;

// Step/dir pin mapping.
static constexpr uint8_t kRhoStepPin = 33;
static constexpr uint8_t kRhoDirPin = 25;
static constexpr uint8_t kThetaStepPin = 32;
static constexpr uint8_t kThetaDirPin = 22;

// TMC2209 UART pins and driver addresses.
static constexpr uint8_t kUartRxPin = 27;
static constexpr uint8_t kUartTxPin = 26;
static constexpr uint8_t kRhoDriverAddress = 1;
static constexpr uint8_t kRhoCDriverAddress = 0;
static constexpr uint8_t kThetaDriverAddress = 2;

// Verify this against the R-sense marking fitted to the actual TMC2209 module.
// It determines the conversion between the driver's IRUN/IHOLD registers and
// RMS coil current.
static constexpr float kDriverSenseResistorOhms = 0.12f;

// Project safety ceiling for the theta motor. This is an RMS phase-current
// limit, not a tuning target; commissioning works upward from the quietest
// reliable setting. Keep the firmware check independent of the browser UI.
static constexpr uint16_t kThetaMaxRunCurrentMa = 1500;

// The rho motor is intentionally operated at a lower project ceiling. Keep
// this in firmware as well as the browser so direct API calls cannot bypass
// the hardware limit.
static constexpr uint16_t kRhoMaxRunCurrentMa = 500;

// Theta-only commissioning always boots at a conservative current and motion
// envelope, regardless of settings saved by a previous production run.
static constexpr uint16_t kThetaCommissioningStartupCurrentMa = 250;
static constexpr uint16_t kThetaCommissioningStartupHoldCurrentMa = 100;
}
