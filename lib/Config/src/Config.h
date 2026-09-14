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
// Daughterboard socket mapping: main RHO uses address 0 and the counterweight
// companion uses address 1. Both receive the shared RHO STEP/DIR signals.
static constexpr uint8_t kRhoDriverAddress = 0;
static constexpr uint8_t kRhoCDriverAddress = 1;
static constexpr uint8_t kThetaDriverAddress = 2;
// The address-1 driver is fitted, but its motor is not connected. Keep its
// bridge off during all motion, including main-only sensorless homing.
static constexpr bool kRhoCompanionMotorEnabled = false;

// FYSETC TMC2209 V3.0 uses 0.11 ohm external sense resistors. UART operation
// disables analog current scaling, so the onboard VREF potentiometer does not
// set coil current. Keep this value aligned with the fitted module revision.
static constexpr float kDriverSenseResistorOhms = 0.11f;
static constexpr bool kDriverSenseResistorVerified = true;

// Project safety ceiling for the theta motor. This is an RMS phase-current
// limit, not a tuning target; commissioning works upward from the quietest
// reliable setting. Keep the firmware check independent of the browser UI.
static constexpr uint16_t kThetaMaxRunCurrentMa = 1500;

// The rho motor is intentionally operated at a lower project ceiling. Keep
// this in firmware as well as the browser so direct API calls cannot bypass
// the hardware limit.
static constexpr uint16_t kRhoMaxRunCurrentMa = 500;
// Until coil current or the fitted shunt tolerance is measured, cap the raw
// TMC2209 current setting at CS=14. On 0.11 ohm shunts with VSENSE=1 this is
// about 459 mA nominal and remains below 500 mA with the 6% firmware margin.
static constexpr uint8_t kRhoMaxUnmeasuredCurrentRegister = 14;

// Dedicated sensorless-homing profile, independent of quiet normal motion.
// The 150 and 200 mA eight-microstep profiles produced mid-travel SG false
// triggers. At 350 mA with full steps, SG sometimes missed the physical stop.
// Test 250 mA at 6 mm/s with faster SG sampling and the same known-origin cap.
static constexpr uint16_t kRhoHomingRunCurrentMa = 250;
static constexpr uint16_t kRhoHomingHoldCurrentMa = 250;
// Full external steps make each polled SG_RESULT correspond to one complete
// commanded electrical step. Compare against the previous 8-microstep trace.
static constexpr uint16_t kRhoHomingMicrosteps = 1;
static constexpr float kRhoHomingVelocityMmPerSecond = 6.0f;
static constexpr float kRhoHomingRunwayMm = 8.0f;
static constexpr float kRhoHomingVerificationBackoffMm = 4.0f;
static constexpr float kRhoHomingMaximumOverrunMm = 2.0f;
static constexpr float kRhoHomingCoarseRunwayMarginMm = 1.0f;
static constexpr float kRhoHomingApproachAgreementMm = 0.5f;

// Theta-only commissioning always boots at a conservative current and motion
// envelope, regardless of settings saved by a previous production run.
static constexpr uint16_t kThetaCommissioningStartupCurrentMa = 250;
static constexpr uint16_t kThetaCommissioningStartupHoldCurrentMa = 100;

// Rho-only commissioning also ignores persisted motion/current values on boot.
// The operator establishes the physical start point before power-up; firmware
// treats it as a temporary logical zero and only permits outward-positive test
// trajectories that return to that point.
// CS=8 is the lowest current code recommended for StealthChop automatic
// tuning. With VSENSE=1 and the V3.0 module's 0.11 ohm shunts it is about
// 275 mA RMS.
static constexpr uint16_t kRhoCommissioningStartupCurrentMa = 200;
static constexpr uint16_t kRhoCommissioningStartupHoldCurrentMa = 200;
}
