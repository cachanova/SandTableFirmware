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

// Main-only SG startup uses the qualified dedicated profile and bounded
// retries. Serve the abort endpoint before starting; failures keep zero untrusted.
// Service builds still boot without motion. See RHO_HOMING_TUNING.md for evidence
// and SG/command-ledger limitations; these flags do not provide position sensing.
static constexpr bool kAutoHomeOnBoot = true;
static constexpr bool kEnableUnknownPositionRhoHoming = true;

// Static IP defaults for STA mode.
static const IPAddress kStaticIpBase(100, 76, 149, 200);
static const IPAddress kStaticGateway(100, 76, 149, 1);
static const IPAddress kStaticSubnet(255, 255, 255, 0);
static const IPAddress kStaticDns(100, 76, 149, 1);

// OTA identity and auth.
static constexpr const char kOtaHostname[] = "sisyphus";
static constexpr const char kOtaPassword[] = "sandpatterns";

// Web server port and LED PWM pin.
static constexpr uint16_t kWebServerPort = 80;
static constexpr uint8_t kLedPin = 4; // ESP32 D4 / GPIO4

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
// The multipass profile uses rated current at 12 mm/s. Preserve these dedicated
// settings while retuning ordinary motion; changing them requires requalification.
static constexpr uint16_t kRhoHomingRunCurrentMa = 500;
static constexpr uint16_t kRhoHomingHoldCurrentMa = 500;
// Keep eight external microsteps; gate SG sampling by full-step advancement.
static constexpr uint16_t kRhoHomingMicrosteps = 8;
static constexpr float kRhoHomingVelocityMmPerSecond = 12.0f;
static constexpr float kRhoHomingRunwayMm = 8.0f;
// Qualification build exercises production entry with an independent known
// position cap. Ordinary service tests retain the earlier bounded entry.
#if defined(SISYPHUS_RHO_COMMISSIONING) && !defined(SISYPHUS_RHO_STARTUP_TRIAL)
static constexpr bool kRhoStartupEntry = false;
#else
static constexpr bool kRhoStartupEntry = true;
#endif
static constexpr float kRhoStartupProbeMm = 1.0f;
static constexpr float kRhoStartupRunwayMm = 6.0f;
static constexpr float kRhoKnownStartMaximumMm = kRhoStartupEntry ? 425.0f : 400.0f;
static constexpr uint32_t kRhoHomingCycleTimeoutMs = 90000;
static constexpr float kRhoHomingMaximumOverrunMm = 2.0f;
static constexpr float kRhoHomingMaximumTotalOverrunMm = 5.0f;
static constexpr float kRhoHomingCoarseRunwayMarginMm = 1.0f;
static constexpr float kRhoHomingApproachAgreementMm = 0.4f;
static constexpr uint8_t kRhoHomingMaximumContactAttempts = 12;

// Theta-only commissioning always boots at a conservative current and motion
// envelope, regardless of settings saved by a previous production run.
static constexpr uint16_t kThetaCommissioningStartupCurrentMa = 250;
static constexpr uint16_t kThetaCommissioningStartupHoldCurrentMa = 100;

// Rho service ignores persisted motion/current values on boot. It starts in
// unhomed manual mode; an explicit origin confirmation enables bounded tests.
// The 200 mA fixed-PWM baseline avoids relying on automatic StealthChop
// calibration below CS8. Homing uses its separate profile above.
static constexpr uint16_t kRhoCommissioningStartupCurrentMa = 200;
static constexpr uint16_t kRhoCommissioningStartupHoldCurrentMa = 200;
}
