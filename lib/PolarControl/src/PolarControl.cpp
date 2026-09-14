#include "PolarControl.hpp"
#include "RhoAcousticProfile.hpp"
#include "PolarUtils.hpp"
#include "MakeUnique.hpp"
#include "Logger.hpp"
#include "ErrorLog.hpp"
#include "StallGuardDetector.hpp"
#include "RhoHomingBounds.hpp"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <SD.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ============================================================================
// Constructor / Destructor
// ============================================================================

PolarControl::PolarControl() {
    // Initialize default driver settings
    // Operator-selected provisional quiet theta profile (2026-09-13).
    m_tDriverSettings.runCurrent = 700;
    m_tDriverSettings.holdCurrent = 200;
    m_tDriverSettings.microsteps = 16;
    m_tDriverSettings.chopperOffTime = 5;
    m_tDriverSettings.automaticCurrentScaling = false;
    m_tDriverSettings.automaticGradientAdaptation = false;
    m_tDriverSettings.pwmOffset = 76;
    m_tDriverSettings.pwmGradient = 23;
    // Preserve the measured complete register surface even while CoolStep is
    // off, so a fallback boot matches the recorded 2026-09-11 trial.
    m_tDriverSettings.coolStepLowerThreshold = 5;
    m_tDriverSettings.coolStepUpperThreshold = 2;
    m_tDriverSettings.coolStepCurrentIncrement = 1;
    m_tDriverSettings.coolStepMeasurementCount = 1;
    m_tDriverSettings.coolStepThreshold = 2000;
    // Loaded RHO reliability profile. The 150 mA acoustic candidate passed
    // steady travel but lost physical synchronization during the reversal
    // stress profile, so keep both run and standstill torque at 200 mA until
    // the loaded mechanism has completed its full motion qualification.
    m_rDriverSettings.runCurrent = 200;
    m_rDriverSettings.holdCurrent = 200;
    m_rDriverSettings.microsteps = 8;
    m_rDriverSettings.highSensitivityCurrentScale = true;
    m_rDriverSettings.pwmFrequency = 0;
    m_rDriverSettings.pwmRegulation = 15;
    m_rDriverSettings.pwmLimit = 8;
    m_rDriverSettings.automaticCurrentScaling = false;
    m_rDriverSettings.automaticGradientAdaptation = false;
    m_rDriverSettings.pwmOffset = 128;
    m_rDriverSettings.pwmGradient = 2;
}

PolarControl::~PolarControl() {
}

constexpr float PolarControl::R_MAX;
static TMC2209::SerialAddress toSerialAddress(uint8_t address) {
    switch (address) {
        case 0:
            return TMC2209::SERIAL_ADDRESS_0;
        case 1:
            return TMC2209::SERIAL_ADDRESS_1;
        case 2:
            return TMC2209::SERIAL_ADDRESS_2;
        case 3:
            return TMC2209::SERIAL_ADDRESS_3;
        default:
            return TMC2209::SERIAL_ADDRESS_0;
    }
}

// The upstream library returns zero on a timeout and does not validate reply
// framing or CRC. Homing cannot safely distinguish that from a true low
// SG_RESULT, so use a checked read for load detection and confidence telemetry.
static uint8_t tmcCrc(const uint8_t* bytes, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        uint8_t current = bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = ((crc >> 7) ^ (current & 0x01))
                ? static_cast<uint8_t>((crc << 1) ^ 0x07)
                : static_cast<uint8_t>(crc << 1);
            current >>= 1;
        }
    }
    return crc;
}

struct TmcReadProbe {
    size_t echoCount = 0;
    bool echoMatches = false;
    size_t replyCount = 0;
    bool frameMatches = false;
    bool crcMatches = false;
    uint8_t reply[8] = {};
};

static bool readTmcRegisterCheckedOnce(uint8_t driverAddress,
                                       uint8_t registerAddress,
                                       uint32_t& value,
                                       TmcReadProbe* probe = nullptr) {
    constexpr uint8_t kSync = 0x05;
    constexpr uint8_t kMasterAddress = 0xFF;
    constexpr uint32_t kEchoTimeoutUs = 4000;
    constexpr uint32_t kReplyTimeoutUs = 10000;
    uint8_t request[4] = {kSync, driverAddress,
                          static_cast<uint8_t>(registerAddress & 0x7F), 0};
    request[3] = tmcCrc(request, 3);

    Serial1.flush();
    while (Serial1.available() > 0) Serial1.read();
    if (Serial1.write(request, sizeof(request)) != sizeof(request)) return false;
    Serial1.flush();

    uint8_t echo[sizeof(request)] = {};
    size_t echoCount = 0;
    uint32_t started = micros();
    while (echoCount < sizeof(echo) && (micros() - started) < kEchoTimeoutUs) {
        if (Serial1.available() > 0) {
            echo[echoCount++] = static_cast<uint8_t>(Serial1.read());
        }
    }
    const bool echoMatches = echoCount == sizeof(echo) &&
        memcmp(echo, request, sizeof(request)) == 0;
    if (probe != nullptr) {
        probe->echoCount = echoCount;
        probe->echoMatches = echoMatches;
    }
    if (!echoMatches) {
        return false;
    }

    uint8_t reply[8] = {};
    size_t replyCount = 0;
    started = micros();
    while (replyCount < sizeof(reply) && (micros() - started) < kReplyTimeoutUs) {
        if (Serial1.available() > 0) {
            reply[replyCount++] = static_cast<uint8_t>(Serial1.read());
        }
    }
    const bool frameMatches = replyCount == sizeof(reply) &&
        reply[0] == kSync && reply[1] == kMasterAddress &&
        (reply[2] & 0x7F) == (registerAddress & 0x7F);
    const bool crcMatches = frameMatches && tmcCrc(reply, 7) == reply[7];
    if (probe != nullptr) {
        probe->replyCount = replyCount;
        probe->frameMatches = frameMatches;
        probe->crcMatches = crcMatches;
        memcpy(probe->reply, reply, sizeof(reply));
    }
    if (!crcMatches) {
        return false;
    }

    value = (static_cast<uint32_t>(reply[3]) << 24) |
            (static_cast<uint32_t>(reply[4]) << 16) |
            (static_cast<uint32_t>(reply[5]) << 8) |
            static_cast<uint32_t>(reply[6]);
    return true;
}

static bool readTmcRegisterChecked(uint8_t driverAddress,
                                   uint8_t registerAddress,
                                   uint32_t& value) {
    constexpr uint8_t kAttempts = 3;
    for (uint8_t attempt = 0; attempt < kAttempts; ++attempt) {
        if (readTmcRegisterCheckedOnce(driverAddress, registerAddress, value)) {
            return true;
        }
        delayMicroseconds(250);
    }
    return false;
}

// The upstream TMC2209 library does not expose CHOPCONF.VSENSE or the full
// PWMCONF tuning surface. Use the same datagram/CRC format here and rely on
// IFCNT plus masked register readback in verifyDriverSettings().
static bool writeTmcRegister(uint8_t driverAddress, uint8_t registerAddress,
                             uint32_t value) {
    constexpr uint8_t kSync = 0x05;
    constexpr uint32_t kEchoTimeoutUs = 4000;
    uint8_t datagram[8] = {
        kSync,
        driverAddress,
        static_cast<uint8_t>((registerAddress & 0x7F) | 0x80),
        static_cast<uint8_t>(value >> 24),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value),
        0,
    };
    datagram[7] = tmcCrc(datagram, 7);
    Serial1.flush();
    while (Serial1.available() > 0) Serial1.read();
    if (Serial1.write(datagram, sizeof(datagram)) != sizeof(datagram)) {
        return false;
    }
    Serial1.flush();
    uint8_t echo[sizeof(datagram)] = {};
    size_t echoCount = 0;
    const uint32_t started = micros();
    while (echoCount < sizeof(echo) && (micros() - started) < kEchoTimeoutUs) {
        if (Serial1.available() > 0) {
            echo[echoCount++] = static_cast<uint8_t>(Serial1.read());
        }
    }
    return echoCount == sizeof(echo) &&
        memcmp(echo, datagram, sizeof(datagram)) == 0;
}

static bool writeTmcRegisterAcknowledged(uint8_t driverAddress,
                                         uint8_t registerAddress,
                                         uint32_t value) {
    uint32_t interfaceCountBefore = 0;
    uint32_t interfaceCountAfter = 0;
    if (!readTmcRegisterChecked(driverAddress, 0x02, interfaceCountBefore) ||
        !writeTmcRegister(driverAddress, registerAddress, value) ||
        !readTmcRegisterChecked(driverAddress, 0x02, interfaceCountAfter)) {
        return false;
    }
    return static_cast<uint8_t>(interfaceCountAfter) ==
        static_cast<uint8_t>(static_cast<uint8_t>(interfaceCountBefore) + 1U);
}

static bool setDriverEnabled(TMC2209& driver, uint8_t driverAddress,
                             const DriverSettings& settings, bool enabled) {
    (void)driver;
    // Never call the library's enable()/disable(): its cached CHOPCONF has
    // VSENSE=0, which would create an over-current transient before a second
    // corrective write. Change TOFF, VSENSE, and interpolation atomically.
    // A lost local UART echo does not tell us whether the driver accepted the
    // write. Read back the target bits before retrying this idempotent update.
    // Three bounded attempts cover a transient without accepting an unknown
    // bridge state.
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        uint32_t chopconf = 0;
        if (!readTmcRegisterChecked(driverAddress, 0x6C, chopconf)) {
            delayMicroseconds(250);
            continue;
        }
        chopconf = (chopconf & ~0x0FUL) |
            (enabled ? static_cast<uint32_t>(settings.chopperOffTime) : 0UL);
        chopconf = settings.highSensitivityCurrentScale
            ? (chopconf | (1UL << 17))
            : (chopconf & ~(1UL << 17));
        chopconf = settings.interpolationEnabled
            ? (chopconf | (1UL << 28))
            : (chopconf & ~(1UL << 28));
        const bool echoValid = writeTmcRegister(
            driverAddress, 0x6C, chopconf);
        uint32_t verified = 0;
        const bool readbackValid = readTmcRegisterChecked(
            driverAddress, 0x6C, verified);
        const bool stateVerified = readbackValid &&
            (verified & 0x0FU) ==
                (enabled ? settings.chopperOffTime : 0U) &&
            (((verified & (1UL << 17)) != 0) ==
             settings.highSensitivityCurrentScale) &&
            (((verified & (1UL << 28)) != 0) ==
             settings.interpolationEnabled);
        if (stateVerified) {
            if (!echoValid) {
                LOG("Driver address %u CHOPCONF echo lost; readback verified\r\n",
                    driverAddress);
            }
            return true;
        }
        delayMicroseconds(250);
    }
    LOG("Driver address %u CHOPCONF state verification failed\r\n",
        driverAddress);
    return false;
}

static bool disableDriverMotion(TMC2209& driver, uint8_t driverAddress,
                                const DriverSettings& settings) {
    // An OTA reboot does not reset the TMC2209. Stop a possibly active UART
    // velocity command as well as turning off the power stage, then verify the
    // bridge is disabled. This keeps a deliberately unavailable motor from
    // reacting to the shared RHO STEP/DIR signals.
    const bool velocityStopped = writeTmcRegisterAcknowledged(
        driverAddress, 0x22, 0);
    const bool bridgeCommanded = setDriverEnabled(
        driver, driverAddress, settings, false);
    uint32_t chopconf = 0;
    const bool bridgeDisabled =
        readTmcRegisterChecked(driverAddress, 0x6C, chopconf) &&
        (chopconf & 0x0FU) == 0;
    return velocityStopped && bridgeCommanded && bridgeDisabled;
}

static bool tmcDriverPresent(uint8_t driverAddress) {
    uint32_t ioInput = 0;
    return readTmcRegisterChecked(driverAddress, 0x06, ioInput) &&
        ((ioInput >> 24) & 0xFF) == 0x21;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool PolarControl::begin() {
    if (m_mutex == NULL) {
        m_mutex = xSemaphoreCreateMutex();
    }
    if (m_mutex == NULL) {
        LOG("ERROR: Failed to create motor mutex\r\n");
        return false;
    }

    // Machine tuning is controller configuration, so keep it in internal
    // flash rather than on the removable pattern SD card.
#ifdef SISYPHUS_SKIP_INTERNAL_FS
    LOG("DIAGNOSTIC: LittleFS initialization is disabled; using tuning defaults.\r\n");
#else
    const bool littleFsReady = LittleFS.begin(true);
    m_tuningStorageReady.store(littleFsReady);
    if (!littleFsReady) {
        LOG("ERROR: LittleFS Mount Failed\r\n");
        ErrorLog::instance().log("ERROR", "FS", "LITTLEFS_MOUNT_FAILED",
                                 "LittleFS mount failed");
    } else {
        LOG("LittleFS Mounted\r\n");
        loadTuningSettings();
    }
#endif

#ifdef SISYPHUS_THETA_COMMISSIONING
    // Never inherit an aggressive saved value when a single motor is being
    // attached and commissioned. Runtime changes remain possible after the
    // operator supplies the actual motor rating to the measurement harness.
    m_tDriverSettings = DriverSettings{};
    m_tDriverSettings.runCurrent = Config::kThetaCommissioningStartupCurrentMa;
    m_tDriverSettings.holdCurrent = Config::kThetaCommissioningStartupHoldCurrentMa;
    m_tDriverSettings.microsteps = 64;
    m_tDriverSettings.stealthChopEnabled = true;
    m_tDriverSettings.coolStepEnabled = false;
    m_motionSettings.tMaxVelocity = 0.05f;
    m_motionSettings.tMaxAccel = 0.10f;
    m_motionSettings.tMaxJerk = 0.50f;
    LOG("THETA COMMISSIONING: forced safe boot envelope (250mA, 0.05rad/s)\r\n");
#endif
#ifdef SISYPHUS_RHO_COMMISSIONING
    // Do not inherit an aggressive saved value before the paired rho motors
    // and mechanism have been observed. A trial can apply a higher value only
    // after the operator supplies the motor rating to the host-side tool.
    // Rebuild the complete driver profile so an extreme-but-valid chopper or
    // PWM trial cannot become the next commissioning boot configuration.
    m_rDriverSettings = DriverSettings{};
    m_rDriverSettings.runCurrent = Config::kRhoCommissioningStartupCurrentMa;
    m_rDriverSettings.holdCurrent = Config::kRhoCommissioningStartupHoldCurrentMa;
    m_rDriverSettings.holdDelay = 8;
    m_rDriverSettings.powerDownDelay = 20;
    m_rDriverSettings.chopperOffTime = 3;
    m_rDriverSettings.hysteresisStart = 5;
    m_rDriverSettings.hysteresisEnd = 0;
    m_rDriverSettings.blankTime = 2;
    m_rDriverSettings.microsteps = 8;
    m_rDriverSettings.interpolationEnabled = true;
    m_rDriverSettings.highSensitivityCurrentScale = true;
    m_rDriverSettings.stealthChopEnabled = true;
    m_rDriverSettings.stealthChopThreshold = 0;
    m_rDriverSettings.pwmFrequency = 0;
    m_rDriverSettings.pwmRegulation = 15;
    m_rDriverSettings.pwmLimit = 8;
    m_rDriverSettings.standstillMode = 0;
    m_rDriverSettings.automaticCurrentScaling = false;
    m_rDriverSettings.automaticGradientAdaptation = false;
    m_rDriverSettings.pwmOffset = 128;
    m_rDriverSettings.pwmGradient = 2;
    m_rDriverSettings.coolStepEnabled = false;
    m_motionSettings.rMaxVelocity = 1.0f;
    m_motionSettings.rMaxAccel = 2.0f;
    m_motionSettings.rMaxJerk = 10.0f;
    LOG("RHO COMMISSIONING: forced safe envelope (VSENSE=1, CS6, 200mA requested, u8, 1mm/s)\r\n");
#endif

#ifndef SISYPHUS_SKIP_MOTOR_HARDWARE
    // Setup TMC2209 drivers with serial connection
    // Using ESP32 variant of setup() with alternate pins
    m_tDriver.setup(Serial1, 115200, toSerialAddress(T_ADDR), RX_PIN, TX_PIN);
    m_rDriver.setup(Serial1, 115200, toSerialAddress(R_ADDR), RX_PIN, TX_PIN);
    m_rCDriver.setup(Serial1, 115200, toSerialAddress(RC_ADDR), RX_PIN, TX_PIN);
#ifndef SISYPHUS_TMC_UART_SCAN_ONLY
    // Datasheet SENDDELAY requirement for multiple addressed nodes sharing a
    // single-wire UART. Apply it before the first bidirectional status read.
    m_tDriver.setReplyDelay(2);
    m_rDriver.setReplyDelay(2);
    m_rCDriver.setReplyDelay(2);
#endif
    m_driverBusInitialized.store(true);

    delay(100);  // Allow drivers to initialize
#endif

    // Create queues for async file reading
    m_coordQueue = xQueueCreate(256, sizeof(PolarCord_t));
    m_cmdQueue = xQueueCreate(5, sizeof(FileCommand));
    if (m_coordQueue == NULL || m_cmdQueue == NULL) {
        ErrorLog::instance().log("ERROR", "MOTOR", "QUEUE_CREATE_FAILED",
                                 "Could not allocate motor command queues");
        return false;
    }

    // Create file reader task on Core 0 (System Core)
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        fileReadTask,
        "FileReadTask",
        8192,
        this,
        1,
        &m_fileTaskHandle,
        0 // Core 0
    );
    if (taskCreated != pdPASS) {
        ErrorLog::instance().log("ERROR", "MOTOR", "FILE_TASK_CREATE_FAILED",
                                 "Could not create file reader task");
        m_fileTaskHandle = NULL;
        return false;
    }

    // Initialize motion planner with separate axis limits
    m_planner.init(
        getStepsPerMm(),
        getStepsPerRadian(),
        R_MAX,
        // Rho limits (mm)
        m_motionSettings.rMaxVelocity,
        m_motionSettings.rMaxAccel,
        m_motionSettings.rMaxJerk,
        // Theta limits (rad)
        m_motionSettings.tMaxVelocity,
        m_motionSettings.tMaxAccel,
        m_motionSettings.tMaxJerk
    );

    updateSpeedSettings();

    delay(10);
    LOG("Motor Setup Complete\r\n");
    return true;
}

void PolarControl::updateSpeedSettings() {
    float speedFactor = m_speed.load() / 10.0f;
    m_planner.setSpeedMultiplier(speedFactor);
}

bool PolarControl::setupDrivers() {
    if (!m_driverBusInitialized.load()) {
        ErrorLog::instance().log("ERROR", "MOTOR", "DRIVER_BUS_DISABLED",
                                 "TMC2209 UART initialization is disabled");
        return false;
    }
    if (m_state != UNINITIALIZED) {
        LOG("Setup was already done\r\n");
        return false;
    }

#ifdef SISYPHUS_TMC_UART_SCAN_ONLY
    // Non-motion address discovery: do not call the general TMC configuration
    // methods or setDriverEnabled(), because either could energize a bridge.
    // Multiple passes distinguish a valid addressed reply from intermittent
    // bus noise; readTmcRegisterChecked() already validates framing and CRC.
    // First verify the board-level 1 kOhm TX-to-RX link without sending any
    // TMC datagram. A healthy link must let RX follow TX high, low, then high.
    Serial1.end();
    pinMode(TX_PIN, OUTPUT);
    pinMode(RX_PIN, INPUT_PULLUP);
    digitalWrite(TX_PIN, HIGH);
    delayMicroseconds(50);
    const bool linkHighBefore = digitalRead(RX_PIN) == HIGH;
    digitalWrite(TX_PIN, LOW);
    delayMicroseconds(50);
    const bool linkLow = digitalRead(RX_PIN) == LOW;
    digitalWrite(TX_PIN, HIGH);
    delayMicroseconds(50);
    const bool linkHighAfter = digitalRead(RX_PIN) == HIGH;
    LOG("TMC UART GPIO link TX26->RX27: high=%u low=%u high=%u result=%s\r\n",
        linkHighBefore, linkLow, linkHighAfter,
        (linkHighBefore && linkLow && linkHighAfter) ? "PASS" : "FAIL");
    Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(10);

    uint8_t validReplies[4] = {};
    uint8_t validEchoes[4] = {};
    uint8_t fullReplies[4] = {};
    uint8_t framedReplies[4] = {};
    uint8_t crcReplies[4] = {};
    uint32_t lastIoInput[4] = {};
    TmcReadProbe lastProbe[4] = {};
    constexpr uint8_t kScanPasses = 5;
    LOG("TMC UART read-only scan starting on RX=%u TX=%u at 115200 baud\r\n",
        RX_PIN, TX_PIN);
    for (uint8_t pass = 0; pass < kScanPasses; ++pass) {
        for (uint8_t address = 0; address < 4; ++address) {
            uint32_t ioInput = 0;
            TmcReadProbe probe;
            const bool readValid = readTmcRegisterCheckedOnce(
                address, 0x06, ioInput, &probe);
            if (probe.echoMatches) ++validEchoes[address];
            if (probe.replyCount == sizeof(probe.reply)) ++fullReplies[address];
            if (probe.frameMatches) ++framedReplies[address];
            if (probe.crcMatches) ++crcReplies[address];
            lastProbe[address] = probe;
            if (readValid &&
                ((ioInput >> 24) & 0xFFU) == 0x21U) {
                ++validReplies[address];
                lastIoInput[address] = ioInput;
            }
        }
        delay(5);
    }
    for (uint8_t address = 0; address < 4; ++address) {
        if (validReplies[address] > 0) {
            LOG("TMC UART scan address %u: %u/%u valid, IOIN=0x%08lX\r\n",
                address, validReplies[address], kScanPasses,
                static_cast<unsigned long>(lastIoInput[address]));
        } else {
            LOG("TMC UART scan address %u: 0/%u valid\r\n",
                address, kScanPasses);
        }
        LOG("  echo=%u/%u full-reply=%u/%u frame=%u/%u crc=%u/%u "
            "last-counts=%u/%u last-reply="
            "%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            validEchoes[address], kScanPasses,
            fullReplies[address], kScanPasses,
            framedReplies[address], kScanPasses,
            crcReplies[address], kScanPasses,
            static_cast<unsigned>(lastProbe[address].echoCount),
            static_cast<unsigned>(lastProbe[address].replyCount),
            lastProbe[address].reply[0], lastProbe[address].reply[1],
            lastProbe[address].reply[2], lastProbe[address].reply[3],
            lastProbe[address].reply[4], lastProbe[address].reply[5],
            lastProbe[address].reply[6], lastProbe[address].reply[7]);
    }

    // Repeat after a deliberately inert bootstrap. Every possible target gets
    // zero phase current and TOFF=0 before GCONF selects UART operation. This
    // mirrors the production library's safe initialization ordering without
    // enabling a bridge or producing a STEP pulse.
    LOG("TMC UART safe bootstrap scan starting (IRUN=0, TOFF=0)\r\n");
    for (uint8_t address = 0; address < 4; ++address) {
        const bool zeroCurrentEcho = writeTmcRegister(address, 0x10, 0x00000000UL);
        const bool bridgeOffEcho = writeTmcRegister(address, 0x6C, 0x00000000UL);
        const bool velocityZeroEcho = writeTmcRegister(address, 0x22, 0x00000000UL);
        const bool uartModeEcho = writeTmcRegister(address, 0x00, 0x000001C0UL);
        const bool replyDelayEcho = writeTmcRegister(address, 0x03, 0x00000200UL);
        delay(2);
        uint32_t ioInput = 0;
        const bool readValid = readTmcRegisterChecked(address, 0x06, ioInput);
        const bool versionValid = readValid &&
            ((ioInput >> 24) & 0xFFU) == 0x21U;
        LOG("TMC UART bootstrap address %u: echo=%u%u%u%u%u read=%u "
            "version=%u IOIN=0x%08lX\r\n",
            address, zeroCurrentEcho, bridgeOffEcho, velocityZeroEcho,
            uartModeEcho, replyDelayEcho, readValid, versionValid,
            static_cast<unsigned long>(ioInput));
    }
    m_thetaDriverConnected.store(false);
    m_rhoDriverConnected.store(false);
    m_rhoCompanionDriverConnected.store(false);
    m_planner.setAxisAvailability(false, false);
    m_state.store(INITIALIZED);
    LOG("TMC UART scan complete; all motion axes remain locked\r\n");
    return true;
#endif

    auto configureConnectedDriver = [&](TMC2209& driver,
                                        const DriverSettings& settings,
                                        uint8_t address,
                                        const char* name) {
        if (!tmcDriverPresent(address)) {
            LOG("Motor driver %s disconnected; skipping configuration\r\n", name);
            ErrorLog::instance().log("WARN", "MOTOR", "DRIVER_OFFLINE",
                                     "Motor driver is disconnected", name);
            return false;
        }
        if (!applyDriverSettings(driver, settings, address, name)) {
            LOG("Motor driver %s failed verification; leaving it disabled\r\n", name);
            ErrorLog::instance().log("ERROR", "MOTOR", "DRIVER_CONFIG",
                                     "Driver configuration did not verify", name);
            return false;
        }
        return setDriverEnabled(driver, address, settings, true);
    };

    bool thetaReady = false;
    bool thetaSafe = true;
#ifdef SISYPHUS_RHO_COMMISSIONING
    // OTA reboot does not power-cycle a TMC2209. Explicitly turn theta off and
    // read it back so a previously running production image cannot leave the
    // stationary axis energized during rho acoustic measurements.
    setDriverEnabled(m_tDriver, T_ADDR, m_tDriverSettings, false);
    uint32_t thetaChopconf = 0;
    thetaSafe = readTmcRegisterChecked(T_ADDR, 0x6C, thetaChopconf) &&
        (thetaChopconf & 0x0FU) == 0;
    LOG("RHO COMMISSIONING: theta driver %s; theta STEP remains low\r\n",
        thetaSafe ? "disabled and verified" : "disable verification failed");
#else
    thetaReady = configureConnectedDriver(
        m_tDriver, m_tDriverSettings, T_ADDR, "theta");
#endif
    bool rhoReady = false;
    bool rhoCompanionReady = false;
    bool rhoCompanionSafe = true;
#ifdef SISYPHUS_THETA_COMMISSIONING
    LOG("THETA COMMISSIONING: rho drivers are not probed; rho STEP remains low\r\n");
#else
    rhoReady = configureConnectedDriver(
        m_rDriver, m_rDriverSettings, R_ADDR, "rho");
    if (Config::kRhoCompanionMotorEnabled) {
        rhoCompanionReady = configureConnectedDriver(
            m_rCDriver, m_rDriverSettings, RC_ADDR, "rho-companion");
    } else {
        rhoCompanionSafe = disableDriverMotion(
            m_rCDriver, RC_ADDR, m_rDriverSettings);
        LOG("Rho companion driver address %u intentionally disabled%s\r\n",
            RC_ADDR, rhoCompanionSafe ? " and verified" :
            "; disable verification failed");
        if (!rhoCompanionSafe) {
            ErrorLog::instance().log(
                "ERROR", "MOTOR", "RHO_COMPANION_DISABLE_FAILED",
                "Could not verify intentionally disabled rho companion driver");
        }
    }
#endif

    m_thetaDriverConnected.store(thetaReady);
    m_rhoDriverConnected.store(rhoReady);
    m_rhoCompanionDriverConnected.store(rhoCompanionReady);
    m_planner.setAxisAvailability(thetaReady, rhoReady || rhoCompanionReady);

    LOG("Driver availability: theta=%s rho=%s rho-companion=%s\r\n",
        thetaReady ? "connected" : "disconnected",
        rhoReady ? "connected" : "disconnected",
        rhoCompanionReady ? "connected" : "disconnected");

#ifdef SISYPHUS_RHO_COMMISSIONING
    if (!thetaSafe || !rhoReady || !rhoCompanionSafe ||
        (Config::kRhoCompanionMotorEnabled && !rhoCompanionReady)) {
        setDriverEnabled(m_rDriver, R_ADDR, m_rDriverSettings, false);
        setDriverEnabled(m_rCDriver, RC_ADDR, m_rDriverSettings, false);
        m_state = INITIALIZED;
        ErrorLog::instance().log(
            "ERROR", "MOTOR", "RHO_COMMISSIONING_PREFLIGHT",
            "Rho commissioning preflight could not verify the requested driver state");
        return false;
    }
#endif
    m_state = INITIALIZED;
    return true;
}

bool PolarControl::home(bool confirmedOriginBounded) {
#if defined(SISYPHUS_THETA_COMMISSIONING)
    LOG("COMMISSIONING: physical homing rejected\r\n");
    return false;
#elif defined(SISYPHUS_RHO_COMMISSIONING)
    if (!confirmedOriginBounded) {
        if (!m_knownPositionHomingActive.load()) {
            LOG("RHO COMMISSIONING: unbounded physical homing rejected\r\n");
            return false;
        }
        LOG("RHO COMMISSIONING: independently capped known-position homing\r\n");
    }
#else
    if (confirmedOriginBounded) {
        LOG("Confirmed-origin bounded homing is commissioning-only\r\n");
        return false;
    }
#endif
    if (!m_rhoDriverConnected.load() ||
        (Config::kRhoCompanionMotorEnabled &&
         !m_rhoCompanionDriverConnected.load())) {
        LOG("Cannot home: a configured rho motor driver is offline\r\n");
        ErrorLog::instance().log("WARN", "HOME", "DRIVER_OFFLINE",
                                 "A configured rho motor driver is offline");
        return false;
    }
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    State_t state = m_state.load();
    if ((state != IDLE && state != INITIALIZED && state != HOMING_FAILED) ||
        m_homingTaskHandle != NULL) {
        LOG("Cannot home: system is busy\r\n");
        xSemaphoreGive(m_mutex);
        return false;
    }

    // A previous logical position cannot be trusted once separately controlled
    // homing motion begins. Keep pattern execution locked out until a person
    // confirms the resulting physical position.
    m_planner.stop();
    m_homingFailure.store(0);
    m_homingStepsPerMm.store(static_cast<uint16_t>(getStepsPerMm()));
    m_homingFastApproachMs.store(0);
    m_homingSlowApproachMs.store(0);
    m_homingFastApproachSteps.store(0);
    m_homingSlowApproachSteps.store(0);
    m_homingFastBaseline.store(0);
    m_homingFastTrigger.store(0);
    m_homingBaseline.store(0);
    m_homingTrigger.store(0);
    m_homingCompanionFastApproachMs.store(0);
    m_homingCompanionSlowApproachMs.store(0);
    m_homingCompanionFastApproachSteps.store(0);
    m_homingCompanionSlowApproachSteps.store(0);
    m_homingCompanionFastBaseline.store(0);
    m_homingCompanionFastTrigger.store(0);
    m_homingCompanionBaseline.store(0);
    m_homingCompanionTrigger.store(0);
    m_homingUartSamples.store(0);
    m_homingValidUartSamples.store(0);
    m_homingFailedAxis.store(0);
    m_confirmedOriginBoundedHoming.store(confirmedOriginBounded);
    m_homingTraceAxis.store(0);
    m_homingTracePhase.store(0);
    m_homingTraceCount.store(0, std::memory_order_release);
    m_homingTraceTotal.store(0, std::memory_order_release);
    m_homingTraceStartedAtMs.store(millis());
    m_homingCycle.fetch_add(1);
    m_state.store(HOMING);

    BaseType_t created = xTaskCreatePinnedToCore(
        homingTask,
        "HomingTask",
        4096,
        this,
        1,
        &m_homingTaskHandle,
        Config::kMotorCore
    );
    if (created != pdPASS) {
        m_homingTaskHandle = NULL;
        const bool disabled = disableRhoDriversLocked();
        m_homingFailure.store(5);
#ifdef SISYPHUS_RHO_COMMISSIONING
        m_knownPositionHomingActive.store(false);
        m_knownRhoStartSteps.store(0);
        m_knownCompanionStartSteps.store(0);
#endif
        m_state.store(HOMING_FAILED);
        ErrorLog::instance().log("ERROR", "HOME", "TASK_CREATE_FAILED",
                                 "Could not start homing task");
        if (!disabled) {
            ErrorLog::instance().log("ERROR", "HOME", "DISABLE_VERIFY_FAILED",
                                     "Could not verify both rho drivers disabled after task creation failure");
        }
        xSemaphoreGive(m_mutex);
        return false;
    }

    xSemaphoreGive(m_mutex);
    LOG("Homing task started\r\n");
    return true;
}

void PolarControl::homingTask(void* arg) {
    PolarControl* self = static_cast<PolarControl*>(arg);

    bool success = self->homeDrivers();
    self->m_confirmedOriginBoundedHoming.store(false);
#ifdef SISYPHUS_RHO_COMMISSIONING
    self->m_knownPositionHomingActive.store(false);
    self->m_knownRhoStartSteps.store(0);
    self->m_knownCompanionStartSteps.store(0);
#endif
    xSemaphoreTake(self->m_mutex, portMAX_DELAY);
    self->m_homingTaskHandle = NULL;
    State_t expected = HOMING;
    const bool completedNormally = self->m_state.compare_exchange_strong(
        expected, success ? HOMING_REVIEW : HOMING_FAILED);
    xSemaphoreGive(self->m_mutex);

    if (!completedNormally) {
        LOG("Homing cancelled by emergency stop\r\n");
    } else if (success) {
        LOG("Automatic homing pass complete; waiting for visual confirmation\r\n");
    } else {
        LOG("Homing failed; motors stopped\r\n");
    }

    vTaskDelete(NULL);
}

bool PolarControl::confirmHome(bool successful) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_state.load() != HOMING_REVIEW) {
        xSemaphoreGive(m_mutex);
        return false;
    }

    if (successful) {
        // Sensorless homing establishes rho=0. Theta has no absolute reference,
        // and at the center its angular origin is arbitrary, so reset both axes.
        m_planner.resetPosition(0.0f, 0.0f);
        m_homingFailure.store(0);
        m_state.store(IDLE);
        LOG("Homing visually confirmed; logical position reset\r\n");
    } else {
        // A rejected sensorless result is not safe to hold as a trusted paired
        // position. Keep both rho power stages off until the next homing try.
        const bool disabled = disableRhoDriversLocked();
        m_homingFailure.store(6);
        m_state.store(HOMING_FAILED);
        ErrorLog::instance().log("WARN", "HOME", "USER_REJECTED",
                                 "User reported that sensorless homing stopped at the wrong position");
        if (!disabled) {
            ErrorLog::instance().log("ERROR", "HOME", "DISABLE_VERIFY_FAILED",
                                     "Could not verify both rho drivers disabled after rejection");
        }
    }

    xSemaphoreGive(m_mutex);
    return true;
}

bool PolarControl::setCurrentPositionAsHome() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    const State_t state = m_state.load();
    if ((state != INITIALIZED && state != IDLE && state != HOMING_FAILED) ||
        m_homingTaskHandle != NULL) {
        xSemaphoreGive(m_mutex);
        return false;
    }
    m_planner.stop();
    m_planner.resetPosition(0.0f, 0.0f);
    m_homingFailure.store(0);
    m_state.store(IDLE);
    xSemaphoreGive(m_mutex);
    LOG("Operator accepted current physical position as theta=0, rho=0\r\n");
    return true;
}

#if defined(SISYPHUS_BENCH_MOTION_TEST) || defined(SISYPHUS_THETA_COMMISSIONING) || defined(SISYPHUS_RHO_COMMISSIONING)
void PolarControl::assumeBenchTestOrigin() {
    if (!setCurrentPositionAsHome()) {
        LOG("Could not assign test origin while motion or homing is active\r\n");
        return;
    }
#ifdef SISYPHUS_THETA_COMMISSIONING
    LOG("THETA COMMISSIONING: logical theta origin assumed; rho motion locked out\r\n");
#elif defined(SISYPHUS_RHO_COMMISSIONING)
    LOG("RHO COMMISSIONING: physical start assigned rho=0; only outward-return tests are allowed\r\n");
#else
    LOG("BENCH MOTION TEST: logical origin assumed without physical homing\r\n");
#endif
}
#endif

#ifdef SISYPHUS_RHO_COMMISSIONING
void PolarControl::enterRhoManualServiceMode() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_planner.stop();
    // A midpoint is only a representational convenience. INITIALIZED is the
    // authoritative indication that this is not a trusted absolute position;
    // jogRelative() re-centers rho before every unhomed relative command.
    m_planner.resetPosition(0.0f, R_MAX * 0.5f);
    m_homingFailure.store(0);
    m_knownPositionHomingActive.store(false);
    m_knownRhoStartSteps.store(0);
    m_knownCompanionStartSteps.store(0);
    m_state.store(INITIALIZED);
    xSemaphoreGive(m_mutex);
    LOG("RHO SERVICE: manual relative-jog mode; absolute position invalidated\r\n");
}

bool PolarControl::homeFromKnownRhoPositions(float rhoStartMm,
                                             float companionStartMm) {
    if (!std::isfinite(rhoStartMm) || !std::isfinite(companionStartMm) ||
        rhoStartMm < -1.0f || rhoStartMm > 400.0f ||
        companionStartMm < -1.0f || companionStartMm > 400.0f) {
        return false;
    }
    const uint32_t stepsPerMm = static_cast<uint32_t>(getStepsPerMm());
    m_knownRhoStartSteps.store(static_cast<int32_t>(
        std::lround(rhoStartMm * stepsPerMm)));
    m_knownCompanionStartSteps.store(static_cast<int32_t>(
        std::lround(companionStartMm * stepsPerMm)));
    m_knownPositionHomingActive.store(true);
    if (home(false)) return true;
    m_knownPositionHomingActive.store(false);
    m_knownRhoStartSteps.store(0);
    m_knownCompanionStartSteps.store(0);
    return false;
}
#endif

HomingStatus PolarControl::getHomingStatus() const {
    HomingStatus status;
    status.cycle = m_homingCycle.load();
    status.stepsPerMm = m_homingStepsPerMm.load();
    status.fastApproachMs = m_homingFastApproachMs.load();
    status.slowApproachMs = m_homingSlowApproachMs.load();
    status.fastApproachSteps = m_homingFastApproachSteps.load();
    status.slowApproachSteps = m_homingSlowApproachSteps.load();
    status.fastBaseline = m_homingFastBaseline.load();
    status.fastTrigger = m_homingFastTrigger.load();
    status.baseline = m_homingBaseline.load();
    status.trigger = m_homingTrigger.load();
    status.companionFastApproachMs =
        m_homingCompanionFastApproachMs.load();
    status.companionSlowApproachMs =
        m_homingCompanionSlowApproachMs.load();
    status.companionFastApproachSteps =
        m_homingCompanionFastApproachSteps.load();
    status.companionSlowApproachSteps =
        m_homingCompanionSlowApproachSteps.load();
    status.companionFastBaseline = m_homingCompanionFastBaseline.load();
    status.companionFastTrigger = m_homingCompanionFastTrigger.load();
    status.companionBaseline = m_homingCompanionBaseline.load();
    status.companionTrigger = m_homingCompanionTrigger.load();
    status.uartSamples = m_homingUartSamples.load();
    status.validUartSamples = m_homingValidUartSamples.load();
    status.failure = m_homingFailure.load();
    status.failedAxis = m_homingFailedAxis.load();
    return status;
}

size_t PolarControl::getHomingTrace(HomingTraceSample* output,
                                    size_t capacity, size_t* totalOutput) const {
    if (output == nullptr || capacity == 0) return 0;
    const size_t total = m_homingTraceTotal.load(std::memory_order_acquire);
    if (totalOutput != nullptr) *totalOutput = total;
    const size_t available = std::min(kHomingTraceCapacity, total);
    const size_t count = std::min(capacity, available);
    size_t source = total < kHomingTraceCapacity
        ? total - count
        : (total % kHomingTraceCapacity + available - count) %
            kHomingTraceCapacity;
    for (size_t index = 0; index < count; ++index) {
        output[index] = m_homingTrace[source];
        source = (source + 1U) % kHomingTraceCapacity;
    }
    return count;
}

DriverAvailability PolarControl::getDriverAvailability() const {
    DriverAvailability availability;
    availability.theta = m_thetaDriverConnected.load();
    availability.rho = m_rhoDriverConnected.load();
    availability.rhoCompanion = m_rhoCompanionDriverConnected.load();
    return availability;
}

static float driverFullScaleCurrentMa(bool highSensitivityCurrentScale) {
    const float vsenseVolts = highSensitivityCurrentScale ? 0.180f : 0.325f;
    return 1000.0f * vsenseVolts /
        (Config::kDriverSenseResistorOhms + 0.020f) / std::sqrt(2.0f);
}

static uint8_t currentMaToDriverRegister(uint16_t currentMa,
                                         bool highSensitivityCurrentScale) {
    const float rawCs = static_cast<float>(currentMa) /
        driverFullScaleCurrentMa(highSensitivityCurrentScale) * 32.0f - 1.0f;
    return static_cast<uint8_t>(
        std::max(0, std::min(31, static_cast<int>(std::lround(rawCs)))));
}

static float driverRegisterCurrentMa(uint8_t currentRegister,
                                     bool highSensitivityCurrentScale) {
    return (static_cast<float>(currentRegister) + 1.0f) / 32.0f *
        driverFullScaleCurrentMa(highSensitivityCurrentScale);
}

// The TMC2209 library accepts percentages, then floors them into register
// values. A ceiling conversion is required to reproduce a chosen register
// value exactly (notably for IHOLDDELAY).
static constexpr uint8_t driverRegisterToLibraryPercent(uint8_t value,
                                                         uint8_t maxValue) {
    return static_cast<uint8_t>((value * 100U + maxValue - 1U) / maxValue);
}

static constexpr uint8_t microstepsToMres(uint16_t microsteps) {
    return microsteps <= 1
        ? 8U
        : static_cast<uint8_t>(microstepsToMres(microsteps >> 1) - 1U);
}

static_assert((driverRegisterToLibraryPercent(8, 15) * 15U) / 100U == 8U,
              "IHOLDDELAY conversion must survive the library's floor mapping");
static_assert(microstepsToMres(256) == 0 && microstepsToMres(64) == 2 &&
              microstepsToMres(1) == 8,
              "MRES conversion must match the TMC2209 register encoding");

static bool setDriverMicrostepsChecked(uint8_t driverAddress,
                                       uint16_t microsteps) {
    uint32_t chopconf = 0;
    if (!readTmcRegisterChecked(driverAddress, 0x6C, chopconf)) return false;
    chopconf = (chopconf & ~0x0F000000UL) |
        (static_cast<uint32_t>(microstepsToMres(microsteps)) << 24);
    if (!writeTmcRegister(driverAddress, 0x6C, chopconf)) return false;
    uint32_t verified = 0;
    return readTmcRegisterChecked(driverAddress, 0x6C, verified) &&
        (verified & 0x0F000000UL) == (chopconf & 0x0F000000UL);
}

static bool verifyDriverSettings(uint8_t driverAddress,
                                 const DriverSettings& settings,
                                 const char* driverName,
                                 uint8_t interfaceCountBefore,
                                 uint8_t expectedWrites) {
    // GCONF, CHOPCONF, and PWMCONF are readable and can be compared directly.
    // Current and threshold registers are write-only on the TMC2209, so IFCNT
    // is the datasheet-defined acknowledgement that those UART writes landed.
    constexpr uint8_t kRegGconf = 0x00;
    constexpr uint8_t kRegIfcnt = 0x02;
    constexpr uint8_t kRegChopconf = 0x6C;
    constexpr uint8_t kRegPwmconf = 0x70;

    uint32_t gconf = 0;
    uint32_t interfaceCount = 0;
    uint32_t chopconf = 0;
    uint32_t pwmconf = 0;
    if (!readTmcRegisterChecked(driverAddress, kRegGconf, gconf) ||
        !readTmcRegisterChecked(driverAddress, kRegIfcnt, interfaceCount) ||
        !readTmcRegisterChecked(driverAddress, kRegChopconf, chopconf) ||
        !readTmcRegisterChecked(driverAddress, kRegPwmconf, pwmconf)) {
        LOG("Driver %s settings readback failed (invalid UART reply)\r\n",
            driverName);
        return false;
    }

    const uint32_t expectedMres =
        static_cast<uint32_t>(microstepsToMres(settings.microsteps)) << 24;
    const uint8_t writesObserved = static_cast<uint8_t>(
        static_cast<uint8_t>(interfaceCount) - interfaceCountBefore);
    // The library rewrites COOLCONF after each current field when CoolStep was
    // previously enabled. Those three additional acknowledged writes are
    // harmless and depend on the prior in-memory driver state.
    const bool writesOk = writesObserved == expectedWrites ||
        writesObserved == static_cast<uint8_t>(expectedWrites + 3U);
    const bool microstepsOk = (chopconf & 0x0F000000U) == expectedMres;
    const bool chopperTimingOk =
        ((chopconf >> 4) & 0x07U) == settings.hysteresisStart &&
        ((chopconf >> 7) & 0x0FU) == settings.hysteresisEnd &&
        ((chopconf >> 15) & 0x03U) == settings.blankTime;
    const bool interpolationOk = (chopconf & (1UL << 28)) != 0;
    const bool expectedInterpolation = settings.interpolationEnabled;
    const bool currentScaleOk = ((chopconf & (1UL << 17)) != 0) ==
        settings.highSensitivityCurrentScale;
    const bool modeOk = ((gconf & (1U << 2)) == 0) == settings.stealthChopEnabled;
    const bool directionOk = ((gconf & (1U << 3)) != 0) ==
        settings.inverseMotorDirection;
    const bool uartCurrentScaleOk = (gconf & (1U << 0)) == 0;
    const bool senseOk = (gconf & (1U << 1)) == 0;
    constexpr uint32_t kPwmSettingsMask = 0xFF3FFFFFU;
    const uint32_t expectedPwmconf =
        static_cast<uint32_t>(settings.pwmOffset) |
        (static_cast<uint32_t>(settings.pwmGradient) << 8) |
        (static_cast<uint32_t>(settings.pwmFrequency) << 16) |
        (static_cast<uint32_t>(settings.automaticCurrentScaling) << 18) |
        (static_cast<uint32_t>(settings.automaticGradientAdaptation) << 19) |
        (static_cast<uint32_t>(settings.standstillMode) << 20) |
        (static_cast<uint32_t>(settings.pwmRegulation) << 24) |
        (static_cast<uint32_t>(settings.pwmLimit) << 28);
    const bool pwmOk =
        (pwmconf & kPwmSettingsMask) == (expectedPwmconf & kPwmSettingsMask);

    const bool verified = writesOk && microstepsOk && chopperTimingOk &&
        (interpolationOk == expectedInterpolation) && currentScaleOk &&
        modeOk && directionOk && uartCurrentScaleOk && senseOk && pwmOk;
    if (!verified) {
        LOG("Driver %s readback mismatch: writes=%u/%u GCONF=%08lX "
            "CHOPCONF=%08lX PWMCONF=%08lX\r\n",
            driverName, writesObserved, expectedWrites,
            static_cast<unsigned long>(gconf),
            static_cast<unsigned long>(chopconf),
            static_cast<unsigned long>(pwmconf));
    }
    return verified;
}

bool PolarControl::applyDriverSettings(TMC2209 &driver,
                                       const DriverSettings &settings,
                                       uint8_t driverAddress,
                                       const char* driverName) {
    uint32_t interfaceCount = 0;
    if (!readTmcRegisterChecked(driverAddress, 0x02, interfaceCount)) {
        LOG("Driver %s IFCNT pre-write read failed\r\n", driverName);
        return false;
    }

    // Set MRES, VSENSE, and interpolation together while the bridge is still
    // disabled. The library's cached CHOPCONF may contain TOFF>0 after a
    // diagnostic read, so using its microstep setter here could briefly
    // re-enable the bridge with the wrong current range.
    uint32_t chopconf = 0;
    if (!readTmcRegisterChecked(driverAddress, 0x6C, chopconf)) {
        LOG("Driver %s CHOPCONF pre-write read failed\r\n", driverName);
        return false;
    }
    chopconf = (chopconf & ~0x0F0187F0UL) |
        (static_cast<uint32_t>(settings.hysteresisStart) << 4) |
        (static_cast<uint32_t>(settings.hysteresisEnd) << 7) |
        (static_cast<uint32_t>(settings.blankTime) << 15) |
        (static_cast<uint32_t>(microstepsToMres(settings.microsteps)) << 24);
    chopconf = settings.highSensitivityCurrentScale
        ? (chopconf | (1UL << 17))
        : (chopconf & ~(1UL << 17));
    chopconf = settings.interpolationEnabled
        ? (chopconf | (1UL << 28))
        : (chopconf & ~(1UL << 28));
    if (!writeTmcRegister(driverAddress, 0x6C, chopconf)) {
        LOG("Driver %s CHOPCONF write echo failed\r\n", driverName);
        return false;
    }

    // TMC2209 datasheet equation (325 mV or high-sensitivity 180 mV VFS):
    // I_RMS = (CS + 1) / 32 * VFS / (R_SENSE + 20mOhm) / sqrt(2).
    const float maxCurrentMa = driverFullScaleCurrentMa(
        settings.highSensitivityCurrentScale);
    const uint8_t runRegister = currentMaToDriverRegister(
        settings.runCurrent, settings.highSensitivityCurrentScale);
    const uint8_t holdRegister = currentMaToDriverRegister(
        settings.holdCurrent, settings.highSensitivityCurrentScale);
    const uint8_t runPercent = driverRegisterToLibraryPercent(
        runRegister, 31);
    const uint8_t holdPercent = driverRegisterToLibraryPercent(
        holdRegister, 31);

    driver.setRunCurrent(runPercent);
    driver.setHoldCurrent(holdPercent);

    LOG("Driver current request run=%umA hold=%umA; actual run=%.0fmA hold=%.0fmA "
        "(VSENSE=%u, R_sense=%.3fohm, full-scale=%.0fmA RMS)\r\n",
        settings.runCurrent, settings.holdCurrent,
        driverRegisterCurrentMa(runRegister, settings.highSensitivityCurrentScale),
        driverRegisterCurrentMa(holdRegister, settings.highSensitivityCurrentScale),
        settings.highSensitivityCurrentScale ? 1U : 0U,
        Config::kDriverSenseResistorOhms, maxCurrentMa);

    // Hold delay (0-15 mapped to 0-100%)
    const uint8_t delayPercent = driverRegisterToLibraryPercent(
        settings.holdDelay, 15);
    driver.setHoldDelay(delayPercent);
    driver.setPowerDownDelay(settings.powerDownDelay);

    // Use external sense resistors
    driver.useExternalSenseResistors();

    // Reapply GCONF.SHAFT on every profile transition. Direction must remain
    // deterministic across reboot, homing, acoustic tuning, and recovery.
    if (settings.inverseMotorDirection) {
        driver.enableInverseMotorDirection();
    } else {
        driver.disableInverseMotorDirection();
    }

    // StealthChop / SpreadCycle mode
    if (settings.stealthChopEnabled) {
        driver.enableStealthChop();
        driver.setStealthChopDurationThreshold(settings.stealthChopThreshold);

    } else {
        // SpreadCycle mode (louder but more torque at high speeds)
        driver.disableStealthChop();
    }

    // CoolStep settings (reduces current at low load for efficiency)
    if (settings.coolStepEnabled) {
        driver.enableCoolStep(settings.coolStepLowerThreshold, settings.coolStepUpperThreshold);
        driver.setCoolStepCurrentIncrement(static_cast<TMC2209::CurrentIncrement>(settings.coolStepCurrentIncrement));
        driver.setCoolStepMeasurementCount(static_cast<TMC2209::MeasurementCount>(settings.coolStepMeasurementCount));
        driver.setCoolStepDurationThreshold(settings.coolStepThreshold);
    } else {
        driver.disableCoolStep();
    }

    // Use step/dir interface for motion (not UART velocity mode)
    driver.moveUsingStepDirInterface();

    // Apply the complete StealthChop PWM surface and standstill mode in one
    // register write. Reserved bits stay clear during commissioning.
    const uint32_t pwmconf =
        static_cast<uint32_t>(settings.pwmOffset) |
        (static_cast<uint32_t>(settings.pwmGradient) << 8) |
        (static_cast<uint32_t>(settings.pwmFrequency) << 16) |
        (static_cast<uint32_t>(settings.automaticCurrentScaling) << 18) |
        (static_cast<uint32_t>(settings.automaticGradientAdaptation) << 19) |
        (static_cast<uint32_t>(settings.standstillMode) << 20) |
        (static_cast<uint32_t>(settings.pwmRegulation) << 24) |
        (static_cast<uint32_t>(settings.pwmLimit) << 28);
    if (!writeTmcRegister(driverAddress, 0x70, pwmconf)) {
        LOG("Driver %s PWMCONF write echo failed\r\n", driverName);
        return false;
    }

    const uint8_t expectedWrites = static_cast<uint8_t>(
        9U + (settings.stealthChopEnabled ? 2U : 1U) +
        (settings.coolStepEnabled ? 4U : 1U));
    const bool verified = verifyDriverSettings(
        driverAddress, settings, driverName,
        static_cast<uint8_t>(interfaceCount), expectedWrites);
    if (verified) {
        LOG("Driver %s settings verified over UART\r\n", driverName);
    } else {
        ErrorLog::instance().log("ERROR", "TUNING", "UART_VERIFY_FAILED",
                                 "Driver settings write/readback verification failed",
                                 driverName);
    }
    return verified;
}

bool PolarControl::disableRhoDriversLocked() {
    const bool primaryCommanded = setDriverEnabled(
        m_rDriver, R_ADDR, m_rDriverSettings, false);
    const bool companionCommanded = setDriverEnabled(
        m_rCDriver, RC_ADDR, m_rDriverSettings, false);

    uint32_t primaryChopconf = 0;
    uint32_t companionChopconf = 0;
    const bool primaryDisabled =
        readTmcRegisterChecked(R_ADDR, 0x6C, primaryChopconf) &&
        (primaryChopconf & 0x0FU) == 0;
    const bool companionDisabled =
        readTmcRegisterChecked(RC_ADDR, 0x6C, companionChopconf) &&
        (companionChopconf & 0x0FU) == 0;
    return primaryCommanded && companionCommanded &&
        primaryDisabled && companionDisabled;
}

bool PolarControl::startInactiveRhoHoldLocked(uint8_t driverAddress,
                                              uint16_t targetPhase) {
    // TMC2209 VACTUAL=0 selects STEP/DIR; any non-zero value selects the
    // addressed internal pulse generator instead. At u256, VACTUAL=+/-1 is
    // only about 0.715 microsteps/s, allowing this driver to keep holding
    // torque while ignoring the STEP pulses sent to the other RHO driver.
    // Service the saved MSCNT phase below so even that tiny commanded motion
    // cannot accumulate during a full-range homing pass.
    constexpr uint32_t kVactualPositiveOne = 1U;
    if (!setDriverMicrostepsChecked(driverAddress, 256) ||
        !writeTmcRegisterAcknowledged(
            driverAddress, 0x22, kVactualPositiveOne)) {
        return false;
    }
    m_inactiveRhoHoldAddress = driverAddress;
    m_inactiveRhoHoldTargetPhase = targetPhase & 0x03FFU;
    m_inactiveRhoHoldDirection = 1;
    m_inactiveRhoHoldLastToggleMs = millis();
    return true;
}

bool PolarControl::serviceInactiveRhoHoldLocked() {
    constexpr uint32_t kCheckIntervalMs = 250;
    constexpr uint32_t kVactualPositiveOne = 1U;
    constexpr uint32_t kVactualNegativeOne = 0x00FFFFFFU;
    if (m_inactiveRhoHoldAddress == UINT8_MAX ||
        millis() - m_inactiveRhoHoldLastToggleMs < kCheckIntervalMs) {
        return true;
    }

    uint32_t phaseRegister = 0;
    if (!readTmcRegisterChecked(
            m_inactiveRhoHoldAddress, 0x6A, phaseRegister)) {
        return false;
    }
    const uint16_t phase = static_cast<uint16_t>(phaseRegister & 0x03FFU);
    const uint16_t forward = static_cast<uint16_t>(
        (m_inactiveRhoHoldTargetPhase + 1024U - phase) & 0x03FFU);
    const int8_t desiredDirection =
        phase == m_inactiveRhoHoldTargetPhase || forward <= 512U ? 1 : -1;
    if (desiredDirection != m_inactiveRhoHoldDirection &&
        !writeTmcRegisterAcknowledged(
            m_inactiveRhoHoldAddress, 0x22,
            desiredDirection > 0
                ? kVactualPositiveOne : kVactualNegativeOne)) {
        return false;
    }
    m_inactiveRhoHoldDirection = desiredDirection;
    m_inactiveRhoHoldLastToggleMs = millis();
    return true;
}

void PolarControl::clearInactiveRhoHoldLocked() {
    m_inactiveRhoHoldAddress = UINT8_MAX;
    m_inactiveRhoHoldTargetPhase = 0;
    m_inactiveRhoHoldLastToggleMs = 0;
    m_inactiveRhoHoldDirection = 1;
}

bool PolarControl::rampRhoStepRate(int8_t direction,
                                   uint32_t targetStepsPerSecond,
                                   uint32_t maxSteps, uint32_t rampMs) {
    constexpr uint32_t kRampIntervalMs = 20;
    const uint32_t rampIncrements =
        std::max<uint32_t>(1, rampMs / kRampIntervalMs);
    const uint32_t initialRate = std::max<uint32_t>(
        1, targetStepsPerSecond / rampIncrements);

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    const bool mayStart = m_state.load() == HOMING &&
        m_planner.startRhoHoming(direction, initialRate, maxSteps);
    xSemaphoreGive(m_mutex);
    if (!mayStart) return false;

    for (uint32_t increment = 2; increment <= rampIncrements; ++increment) {
        vTaskDelay(pdMS_TO_TICKS(kRampIntervalMs));
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        if (m_state.load() != HOMING) {
            m_planner.stopRhoHoming();
            xSemaphoreGive(m_mutex);
            return false;
        }
        if (!serviceInactiveRhoHoldLocked()) {
            m_planner.stopRhoHoming();
            xSemaphoreGive(m_mutex);
            return false;
        }
        if (!m_planner.isRhoHoming()) {
            xSemaphoreGive(m_mutex);
            return m_planner.getRhoHomingStepCount() >= maxSteps;
        }
        const uint32_t rate = std::max<uint32_t>(
            1, static_cast<uint32_t>(
                static_cast<uint64_t>(targetStepsPerSecond) * increment /
                rampIncrements));
        const bool updated = m_planner.setRhoHomingStepRate(rate);
        xSemaphoreGive(m_mutex);
        if (!updated) return false;
    }
    return true;
}

void PolarControl::recordHomingSample(bool valid, uint16_t stallGuard) {
    const size_t total = m_homingTraceTotal.load(std::memory_order_relaxed);
    const size_t index = total % kHomingTraceCapacity;
    HomingTraceSample& sample = m_homingTrace[index];
    sample.elapsedMs = millis() - m_homingTraceStartedAtMs.load();
    sample.steps = m_planner.getRhoHomingStepCount();
    sample.stallGuard = stallGuard;
    sample.axis = m_homingTraceAxis.load();
    sample.phase = m_homingTracePhase.load();
    sample.valid = valid;
    m_homingTraceTotal.store(total + 1U, std::memory_order_release);
    m_homingTraceCount.store(
        std::min(kHomingTraceCapacity, total + 1U),
        std::memory_order_release);
}

PolarControl::HomingMove PolarControl::moveRhoBySteps(
    uint8_t driverAddress, int8_t direction, uint32_t stepsPerSecond,
    uint32_t stepCount, uint16_t recoveryThreshold) {
    HomingMove result;
    if (stepCount == 0) {
        result.success = true;
        result.loadRecovered = true;
        return result;
    }

    constexpr uint32_t kSampleIntervalMs = 20;
    constexpr uint8_t kMaxConsecutiveUartErrors = 3;
    const uint8_t recoverySamples = std::max<uint8_t>(
        3, static_cast<uint8_t>(m_homingSettings.consecutiveSamples / 3));
    uint8_t consecutiveUartErrors = 0;
    uint8_t consecutiveRecovered = 0;
    const uint32_t startedAt = millis();
    const uint32_t timeoutMs = std::max<uint32_t>(
        5000, static_cast<uint32_t>(
            static_cast<uint64_t>(stepCount) * 2000U / stepsPerSecond) + 3000U);

    if (!rampRhoStepRate(direction, stepsPerSecond, stepCount, 500)) {
        return result;
    }

    while (m_planner.getRhoHomingStepCount() < stepCount &&
           (millis() - startedAt) < timeoutMs) {
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        if (m_state.load() != HOMING) {
            m_planner.stopRhoHoming();
            xSemaphoreGive(m_mutex);
            return result;
        }
        if (!serviceInactiveRhoHoldLocked()) {
            m_planner.stopRhoHoming();
            xSemaphoreGive(m_mutex);
            result.communicationError = true;
            result.elapsedMs = millis() - startedAt;
            result.steps = m_planner.getRhoHomingStepCount();
            return result;
        }

        uint32_t registerValue = 0;
        const bool validSample = readTmcRegisterChecked(
            driverAddress, 0x41, registerValue);
        m_homingUartSamples.fetch_add(1);
        if (validSample) m_homingValidUartSamples.fetch_add(1);
        recordHomingSample(validSample,
            static_cast<uint16_t>(registerValue & 0x03FF));
        if (!validSample) {
            if (consecutiveUartErrors < UINT8_MAX) ++consecutiveUartErrors;
            if (consecutiveUartErrors >= kMaxConsecutiveUartErrors) {
                m_planner.stopRhoHoming();
                xSemaphoreGive(m_mutex);
                result.communicationError = true;
                result.elapsedMs = millis() - startedAt;
                result.steps = m_planner.getRhoHomingStepCount();
                return result;
            }
            xSemaphoreGive(m_mutex);
            vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
            continue;
        }

        consecutiveUartErrors = 0;
        const uint16_t sample = static_cast<uint16_t>(registerValue & 0x03FF);
        result.peakStallGuard = std::max(result.peakStallGuard, sample);
        if (m_planner.getRhoHomingStepCount() >= stepCount / 2U &&
            sample >= 4U && result.healthySecondHalfSamples < UINT8_MAX) {
            ++result.healthySecondHalfSamples;
        }
        if (recoveryThreshold == 0 || sample > recoveryThreshold) {
            if (consecutiveRecovered < UINT8_MAX) ++consecutiveRecovered;
            if (consecutiveRecovered >= recoverySamples) {
                result.loadRecovered = true;
            }
        } else {
            consecutiveRecovered = 0;
        }
        xSemaphoreGive(m_mutex);
        vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
    }

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    result.steps = m_planner.getRhoHomingStepCount();
    m_planner.stopRhoHoming();
    xSemaphoreGive(m_mutex);
    result.elapsedMs = millis() - startedAt;
    result.success = result.steps >= stepCount &&
        (recoveryThreshold == 0 || result.loadRecovered);
    return result;
}

PolarControl::HomingAttempt PolarControl::approachHome(
    uint8_t driverAddress, uint32_t stepsPerSecond, uint32_t maxSteps,
    uint32_t minimumTravelSteps, uint8_t requiredSamples,
    float triggerRatio, uint16_t externalStepsPerFullStep) {
    HomingAttempt result;
    constexpr uint32_t kSampleIntervalMs = 5;
    constexpr uint8_t kMaxConsecutiveUartErrors = 3;
    const uint32_t startedAt = millis();
    const uint32_t timeoutMs = std::max<uint32_t>(
        5000, static_cast<uint32_t>(
            static_cast<uint64_t>(maxSteps) * 1500U / stepsPerSecond) + 3000U);

    if (!rampRhoStepRate(-1, stepsPerSecond, maxSteps, 500)) return result;
    const uint32_t stepsAtDetectorStart = m_planner.getRhoHomingStepCount();
    const uint32_t remainingIgnoreSteps = minimumTravelSteps > stepsAtDetectorStart
        ? minimumTravelSteps - stepsAtDetectorStart
        : 0;
    const uint32_t ignoreMs = static_cast<uint32_t>(
        (static_cast<uint64_t>(remainingIgnoreSteps) * 1000U +
         stepsPerSecond - 1U) / stepsPerSecond);
    const uint32_t detectorStartedAt = millis();
    StallGuardDetector detector(ignoreMs, requiredSamples, triggerRatio);
    uint8_t consecutiveUartErrors = 0;
    uint32_t lastDetectorSampleSteps = 0;
    bool haveDetectorSample = false;

    while (m_planner.getRhoHomingStepCount() < maxSteps &&
           (millis() - startedAt) < timeoutMs) {
        const uint32_t detectorElapsed = millis() - detectorStartedAt;
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        if (m_state.load() != HOMING) {
            m_planner.stopRhoHoming();
            xSemaphoreGive(m_mutex);
            return result;
        }
        if (!serviceInactiveRhoHoldLocked()) {
            m_planner.stopRhoHoming();
            xSemaphoreGive(m_mutex);
            result.communicationError = true;
            result.elapsedMs = millis() - startedAt;
            result.steps = m_planner.getRhoHomingStepCount();
            result.baseline = detector.baseline();
            result.threshold = detector.threshold();
            return result;
        }

        uint32_t registerValue = 0;
        const bool validSample = readTmcRegisterChecked(
            driverAddress, 0x41, registerValue);
        m_homingUartSamples.fetch_add(1);
        if (validSample) m_homingValidUartSamples.fetch_add(1);
        recordHomingSample(validSample,
            static_cast<uint16_t>(registerValue & 0x03FF));
        if (!validSample) {
            if (consecutiveUartErrors < UINT8_MAX) ++consecutiveUartErrors;
            if (consecutiveUartErrors >= kMaxConsecutiveUartErrors) {
                m_planner.stopRhoHoming();
                xSemaphoreGive(m_mutex);
                result.communicationError = true;
                result.elapsedMs = millis() - startedAt;
                result.steps = m_planner.getRhoHomingStepCount();
                result.baseline = detector.baseline();
                result.threshold = detector.threshold();
                return result;
            }
            xSemaphoreGive(m_mutex);
            vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
            continue;
        }

        consecutiveUartErrors = 0;
        const uint16_t sample = static_cast<uint16_t>(registerValue & 0x03FF);
        const uint32_t sampleSteps = m_planner.getRhoHomingStepCount();
        // SG_RESULT updates once per full step. At the fixed MRES=8 homing
        // resolution, reject duplicate polls until at least eight external
        // STEP pulses have advanced the driver's electrical sequencer.
        if (haveDetectorSample &&
            sampleSteps - lastDetectorSampleSteps <
                std::max<uint16_t>(1, externalStepsPerFullStep)) {
            xSemaphoreGive(m_mutex);
            vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
            continue;
        }
        lastDetectorSampleSteps = sampleSteps;
        haveDetectorSample = true;
        if (detector.update(sample, detectorElapsed)) {
            result.steps = m_planner.getRhoHomingStepCount();
            m_planner.stopRhoHoming();
            xSemaphoreGive(m_mutex);
            result.success = true;
            result.elapsedMs = millis() - startedAt;
            result.baseline = detector.baseline();
            result.trigger = sample;
            result.threshold = detector.threshold();
            return result;
        }
        xSemaphoreGive(m_mutex);
        vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
    }

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    result.steps = m_planner.getRhoHomingStepCount();
    m_planner.stopRhoHoming();
    xSemaphoreGive(m_mutex);
    result.elapsedMs = millis() - startedAt;
    result.baseline = detector.baseline();
    result.threshold = detector.threshold();
    return result;
}

bool PolarControl::restoreHeldDriverPhase(TMC2209& driver,
                                          uint8_t driverAddress,
                                          uint16_t targetPhase,
                                          uint16_t microsteps,
                                          const char* driverName) {
    (void)driver;
    // The inactive bridge stayed energized at u256 while VACTUAL +/-1 kept it
    // isolated from shared STEP/DIR. Return its sequencer to the saved MSCNT
    // phase before writing VACTUAL=0 and restoring the normal homing MRES.
    // The physical correction is bounded to the tiny dither accumulated
    // around the saved phase, rather than an ambiguous electrical revolution.
    constexpr int32_t kFastPhaseVelocity = 128;
    constexpr int32_t kSlowPhaseVelocity = 16;
    constexpr uint16_t kSlowWindow = 64;
    constexpr uint16_t kAcceptedPhaseError = 2;
    constexpr uint32_t kPhaseTimeoutMs = 30000;
    constexpr uint8_t kMaxConsecutiveUartErrors = 3;
    uint8_t consecutiveUartErrors = 0;
    uint32_t phaseRegister = 0;

    auto circularError = [targetPhase](uint16_t phase) {
        const uint16_t forward = static_cast<uint16_t>(
            (targetPhase + 1024U - phase) & 0x03FFU);
        const uint16_t backward = static_cast<uint16_t>(
            (phase + 1024U - targetPhase) & 0x03FFU);
        return std::min(forward, backward);
    };
    auto signedVelocityTowardTarget = [targetPhase](uint16_t phase,
                                                     int32_t magnitude) {
        const uint16_t forward = static_cast<uint16_t>(
            (targetPhase + 1024U - phase) & 0x03FFU);
        return forward <= 512U ? magnitude : -magnitude;
    };
    auto velocityRegisterValue = [](int32_t velocity) {
        return static_cast<uint32_t>(velocity) & 0x00FFFFFFU;
    };

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    bool valid = readTmcRegisterChecked(driverAddress, 0x6A, phaseRegister);
    if (!valid) {
        xSemaphoreGive(m_mutex);
        return false;
    }
    uint16_t phase = static_cast<uint16_t>(phaseRegister & 0x03FFU);
    if (circularError(phase) <= kAcceptedPhaseError) {
        const bool stopped = writeTmcRegisterAcknowledged(
            driverAddress, 0x22, 0U);
        const bool resolutionRestored = stopped &&
            setDriverMicrostepsChecked(driverAddress, microsteps);
        xSemaphoreGive(m_mutex);
        return resolutionRestored;
    }
    if (!writeTmcRegisterAcknowledged(
            driverAddress, 0x22,
            velocityRegisterValue(signedVelocityTowardTarget(
                phase, kFastPhaseVelocity)))) {
        xSemaphoreGive(m_mutex);
        return false;
    }
    xSemaphoreGive(m_mutex);

    const uint32_t startedAt = millis();
    bool slowStage = false;
    while ((millis() - startedAt) < kPhaseTimeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(1));
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        valid = readTmcRegisterChecked(driverAddress, 0x6A, phaseRegister);
        if (!valid) {
            if (consecutiveUartErrors < UINT8_MAX) ++consecutiveUartErrors;
            if (consecutiveUartErrors >= kMaxConsecutiveUartErrors) {
                writeTmcRegisterAcknowledged(driverAddress, 0x22, 0U);
                xSemaphoreGive(m_mutex);
                return false;
            }
            xSemaphoreGive(m_mutex);
            continue;
        }
        consecutiveUartErrors = 0;
        phase = static_cast<uint16_t>(phaseRegister & 0x03FFU);
        const uint16_t error = circularError(phase);
        if (!slowStage && error <= kSlowWindow) {
            if (!writeTmcRegisterAcknowledged(driverAddress, 0x22, 0U)) {
                xSemaphoreGive(m_mutex);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            if (!readTmcRegisterChecked(driverAddress, 0x6A, phaseRegister)) {
                xSemaphoreGive(m_mutex);
                return false;
            }
            phase = static_cast<uint16_t>(phaseRegister & 0x03FFU);
            if (circularError(phase) <= kAcceptedPhaseError) {
                xSemaphoreGive(m_mutex);
                LOG("Restored disabled %s sequencer phase to %u (target %u)\r\n",
                    driverName, phase, targetPhase);
                xSemaphoreTake(m_mutex, portMAX_DELAY);
                const bool restored = setDriverMicrostepsChecked(
                    driverAddress, microsteps);
                xSemaphoreGive(m_mutex);
                return restored;
            }
            if (!writeTmcRegisterAcknowledged(
                    driverAddress, 0x22,
                    velocityRegisterValue(signedVelocityTowardTarget(
                        phase, kSlowPhaseVelocity)))) {
                xSemaphoreGive(m_mutex);
                return false;
            }
            slowStage = true;
            xSemaphoreGive(m_mutex);
            continue;
        }
        if (slowStage && error <= kAcceptedPhaseError) {
            if (!writeTmcRegisterAcknowledged(driverAddress, 0x22, 0U)) {
                xSemaphoreGive(m_mutex);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
            if (!readTmcRegisterChecked(driverAddress, 0x6A, phaseRegister)) {
                xSemaphoreGive(m_mutex);
                return false;
            }
            phase = static_cast<uint16_t>(phaseRegister & 0x03FFU);
            const uint16_t stoppedError = circularError(phase);
            xSemaphoreGive(m_mutex);
            if (stoppedError <= kAcceptedPhaseError) {
                LOG("Restored disabled %s sequencer phase to %u (target %u)\r\n",
                    driverName, phase, targetPhase);
                xSemaphoreTake(m_mutex, portMAX_DELAY);
                const bool restored = setDriverMicrostepsChecked(
                    driverAddress, microsteps);
                xSemaphoreGive(m_mutex);
                return restored;
            }
            xSemaphoreTake(m_mutex, portMAX_DELAY);
            if (!writeTmcRegisterAcknowledged(
                    driverAddress, 0x22,
                    velocityRegisterValue(signedVelocityTowardTarget(
                        phase, kSlowPhaseVelocity)))) {
                xSemaphoreGive(m_mutex);
                return false;
            }
            xSemaphoreGive(m_mutex);
            continue;
        }
        xSemaphoreGive(m_mutex);
    }

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    writeTmcRegisterAcknowledged(driverAddress, 0x22, 0U);
    xSemaphoreGive(m_mutex);
    LOG("Timed out restoring held %s sequencer phase\r\n", driverName);
    return false;
}

bool PolarControl::homeAxis(TMC2209& activeDriver, uint8_t activeAddress,
                            const char* activeName,
                            TMC2209& inactiveDriver,
                            uint8_t inactiveAddress,
                            const char* inactiveName,
                            bool companionAxis,
                            const DriverSettings& homingSettings) {
    // SG_RESULT is updated once per full step and becomes unstable at very
    // low motor speeds. With this mechanism's 50 full steps/mm, 6 mm/s is
    // 1.5 revolutions/s: above the TMC2209's documented problematic region
    // below roughly one revolution/s. Use the same characterized velocity
    // for both passes so one threshold describes one operating condition.
    constexpr float kCoarseMmPerSecond =
        Config::kRhoHomingVelocityMmPerSecond;
    constexpr float kPrecisionMmPerSecond =
        Config::kRhoHomingVelocityMmPerSecond;
    constexpr float kRunwayMm = Config::kRhoHomingRunwayMm;
    constexpr float kVerificationBackoffMm =
        Config::kRhoHomingVerificationBackoffMm;
    constexpr float kVerificationToleranceMm =
        Config::kRhoHomingMaximumOverrunMm;
    constexpr float kImpossibleContactTravelMm = 3.0f;
    constexpr uint32_t kSettleMs = 150;
    const uint32_t stepsPerMm = 50U * homingSettings.microsteps;
    const uint32_t maxStepRate = 1000000U / STEP_TIMER_PERIOD_US;
    const uint32_t coarseRate = std::min<uint32_t>(
        maxStepRate, std::max<uint32_t>(1,
            static_cast<uint32_t>(std::lround(kCoarseMmPerSecond * stepsPerMm))));
    const uint32_t precisionRate = std::min<uint32_t>(
        maxStepRate, std::max<uint32_t>(1,
            static_cast<uint32_t>(std::lround(kPrecisionMmPerSecond * stepsPerMm))));
    const uint32_t runwaySteps = static_cast<uint32_t>(
        std::lround(kRunwayMm * stepsPerMm));
    const uint32_t backoffSteps = static_cast<uint32_t>(
        std::lround(kVerificationBackoffMm * stepsPerMm));
    // This is a physical safety limit, not a detector-dependent allowance.
    // A longer debounce must fail at the cap rather than silently increasing
    // how far the mechanism may be commanded into the hard stop.
    const uint32_t toleranceSteps = static_cast<uint32_t>(
        std::lround(kVerificationToleranceMm * stepsPerMm));
    uint32_t maximumTravelSteps = static_cast<uint32_t>(
        std::lround(R_MAX * stepsPerMm));
    uint32_t expectedContactSteps = 0;
    if (m_confirmedOriginBoundedHoming.load()) {
        expectedContactSteps = runwaySteps;
        maximumTravelSteps = runwaySteps + toleranceSteps;
    }
#ifdef SISYPHUS_RHO_COMMISSIONING
    else {
        const int32_t knownStartNormalSteps = companionAxis
            ? m_knownCompanionStartSteps.load()
            : m_knownRhoStartSteps.load();
        if (m_knownPositionHomingActive.load()) {
            const int32_t knownStartSteps = static_cast<int32_t>(std::lround(
                static_cast<double>(knownStartNormalSteps) * stepsPerMm /
                getStepsPerMm()));
            const int64_t boundedTravel =
                static_cast<int64_t>(knownStartSteps) + runwaySteps +
                toleranceSteps;
            expectedContactSteps = static_cast<uint32_t>(
                std::max<int64_t>(1, boundedTravel - toleranceSteps));
            maximumTravelSteps = static_cast<uint32_t>(
                std::max<int64_t>(1, boundedTravel));
        }
    }
#endif
    const float triggerRatio = m_homingSettings.triggerPercent / 100.0f;
    const uint32_t coarseMinimumTravelSteps = static_cast<uint32_t>(
        std::lround(kImpossibleContactTravelMm * stepsPerMm));
    const uint8_t coarseSamples = std::max<uint8_t>(
        5, static_cast<uint8_t>(m_homingSettings.consecutiveSamples / 3));
    uint32_t inactivePhaseRegister = 0;
    uint32_t precisionMaximumSteps = backoffSteps + toleranceSteps;
    uint32_t coarseContactSteps = 0;
    const bool holdInactiveMotor =
        companionAxis || Config::kRhoCompanionMotorEnabled;

    m_homingFailedAxis.store(companionAxis ? 2 : 1);
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    const bool inactiveReady = m_state.load() == HOMING &&
        (holdInactiveMotor
            ? readTmcRegisterChecked(inactiveAddress, 0x6A,
                                     inactivePhaseRegister)
            : disableDriverMotion(inactiveDriver, inactiveAddress,
                                  homingSettings));
    bool holdStarted = false;
    bool motorsEnabled = false;
    if (inactiveReady) {
        activeDriver.moveUsingStepDirInterface();
        holdStarted = !holdInactiveMotor || startInactiveRhoHoldLocked(
            inactiveAddress,
            static_cast<uint16_t>(inactivePhaseRegister & 0x03FFU));
        motorsEnabled = holdStarted &&
            (!holdInactiveMotor || setDriverEnabled(
                inactiveDriver, inactiveAddress, homingSettings, true)) &&
            setDriverEnabled(activeDriver, activeAddress,
                             homingSettings, true);
    }
    uint32_t activeChopconf = 0;
    uint32_t inactiveChopconf = 0;
    const bool enablesVerified = inactiveReady && holdStarted && motorsEnabled &&
        readTmcRegisterChecked(activeAddress, 0x6C, activeChopconf) &&
        readTmcRegisterChecked(inactiveAddress, 0x6C, inactiveChopconf) &&
        (activeChopconf & 0x0FU) != 0 &&
        ((inactiveChopconf & 0x0FU) != 0) == holdInactiveMotor &&
        (activeChopconf & 0x0F000000UL) ==
            (static_cast<uint32_t>(
                microstepsToMres(homingSettings.microsteps)) << 24) &&
        (!holdInactiveMotor ||
         (inactiveChopconf & 0x0F000000UL) == 0);
    if (!enablesVerified) {
        clearInactiveRhoHoldLocked();
        writeTmcRegisterAcknowledged(inactiveAddress, 0x22, 0U);
        setDriverEnabled(activeDriver, activeAddress, homingSettings, false);
        setDriverEnabled(inactiveDriver, inactiveAddress,
                         homingSettings, false);
        if (holdInactiveMotor) {
            setDriverMicrostepsChecked(
                inactiveAddress, homingSettings.microsteps);
        }
    }
    xSemaphoreGive(m_mutex);
    if (!enablesVerified) {
        m_homingFailure.store(7);
        ErrorLog::instance().log("ERROR", "HOME", "AXIS_SELECT_FAILED",
                                 "Could not select one rho driver for homing",
                                 activeName);
        return false;
    }

    if (holdInactiveMotor) {
        LOG("Homing %s by STEP/DIR with %s held in u256 VACTUAL mode\r\n",
            activeName, inactiveName);
    } else {
        LOG("Homing %s by STEP/DIR with %s bridge disabled\r\n",
            activeName, inactiveName);
    }
    vTaskDelay(pdMS_TO_TICKS(kSettleMs));

    bool axisSuccess = false;
    m_homingTraceAxis.store(companionAxis ? 2 : 1);
    m_homingTracePhase.store(1);
    HomingMove runway = moveRhoBySteps(
        activeAddress, +1, coarseRate, runwaySteps);
    if (!runway.success) {
        m_homingFailure.store(runway.communicationError ? 7 : 8);
        ErrorLog::instance().log("ERROR", "HOME", "RUNWAY_FAILED",
                                 "Initial bounded outward move failed",
                                 activeName);
        goto axis_cleanup;
    }
    // A disconnected or jammed motor can consume the outward STEP budget
    // without moving the carriage. Do not issue the inward command unless
    // StallGuard showed sustained healthy load at cruising speed.
    if (runway.healthySecondHalfSamples < 8U) {
        m_homingFailure.store(4);
        ErrorLog::instance().log("ERROR", "HOME", "RUNWAY_LOAD_INVALID",
                                 "Outward motion lacked healthy SG_RESULT samples",
                                 activeName);
        goto axis_cleanup;
    }
    vTaskDelay(pdMS_TO_TICKS(kSettleMs));

    {
        m_homingTracePhase.store(2);
        HomingAttempt coarse = approachHome(
            activeAddress, coarseRate, maximumTravelSteps,
            coarseMinimumTravelSteps,
            coarseSamples, triggerRatio, homingSettings.microsteps);
        if (companionAxis) {
            m_homingCompanionFastApproachMs.store(coarse.elapsedMs);
            m_homingCompanionFastApproachSteps.store(coarse.steps);
            m_homingCompanionFastBaseline.store(coarse.baseline);
            m_homingCompanionFastTrigger.store(coarse.trigger);
        } else {
            m_homingFastApproachMs.store(coarse.elapsedMs);
            m_homingFastApproachSteps.store(coarse.steps);
            m_homingFastBaseline.store(coarse.baseline);
            m_homingFastTrigger.store(coarse.trigger);
        }
        if (!coarse.success) {
            m_homingFailure.store(coarse.communicationError ? 7 : 2);
            ErrorLog::instance().log("ERROR", "HOME", "COARSE_FAILED",
                                     "Coarse approach did not find a sustained stall",
                                     activeName);
            goto axis_cleanup;
        }
        if (expectedContactSteps > 0) {
            const RhoBoundedReturn bound = rhoBoundedReturn(
                expectedContactSteps, coarse.steps, backoffSteps,
                toleranceSteps);
            if (!bound.contactWithinWindow) {
                m_homingFailure.store(4);
                ErrorLog::instance().log("ERROR", "HOME",
                                         "COARSE_OUTSIDE_KNOWN_WINDOW",
                                         "Coarse trigger fell outside the confirmed-origin window",
                                         activeName);
                goto axis_cleanup;
            }
            precisionMaximumSteps = bound.precisionCapSteps;
            coarseContactSteps = coarse.steps;
            LOG("%s shared contact budget: coarse overrun=%lu steps, "
                "precision cap=%lu steps\r\n", activeName,
                static_cast<unsigned long>(bound.coarseOverrunSteps),
                static_cast<unsigned long>(precisionMaximumSteps));
        }
        LOG("%s coarse home: steps=%lu baseline=%u trigger=%u\r\n",
            activeName, static_cast<unsigned long>(coarse.steps),
            coarse.baseline, coarse.trigger);

        m_homingTracePhase.store(3);
        HomingMove backoff = moveRhoBySteps(
            activeAddress, +1, coarseRate, backoffSteps, coarse.threshold);
        if (!backoff.success || !backoff.loadRecovered) {
            m_homingFailure.store(backoff.communicationError ? 7 : 4);
            ErrorLog::instance().log("ERROR", "HOME", "LOAD_NOT_RECOVERED",
                                     "StallGuard did not recover during verification backoff",
                                     activeName);
            goto axis_cleanup;
        }
        LOG("%s backoff: steps=%lu SG recovered to %u\r\n",
            activeName, static_cast<unsigned long>(backoff.steps),
            backoff.peakStallGuard);
    }
    vTaskDelay(pdMS_TO_TICKS(kSettleMs));

    {
        m_homingTracePhase.store(4);
        const uint32_t configuredMinimumSteps = static_cast<uint32_t>(
            static_cast<uint64_t>(precisionRate) *
            m_homingSettings.minimumTravelMs / 1000U);
        HomingAttempt precision = approachHome(
            activeAddress, precisionRate, precisionMaximumSteps,
            configuredMinimumSteps,
            m_homingSettings.consecutiveSamples, triggerRatio,
            homingSettings.microsteps);
        if (companionAxis) {
            m_homingCompanionSlowApproachMs.store(precision.elapsedMs);
            m_homingCompanionSlowApproachSteps.store(precision.steps);
            m_homingCompanionBaseline.store(precision.baseline);
            m_homingCompanionTrigger.store(precision.trigger);
        } else {
            m_homingSlowApproachMs.store(precision.elapsedMs);
            m_homingSlowApproachSteps.store(precision.steps);
            m_homingBaseline.store(precision.baseline);
            m_homingTrigger.store(precision.trigger);
        }
        if (!precision.success) {
            m_homingFailure.store(precision.communicationError ? 7 : 3);
            ErrorLog::instance().log("ERROR", "HOME", "PRECISION_FAILED",
                                     "Precision return did not reproduce the stall",
                                     activeName);
            goto axis_cleanup;
        }

        const uint32_t minimumReturnSteps = std::max<uint32_t>(
            configuredMinimumSteps,
            backoffSteps > toleranceSteps ? backoffSteps - toleranceSteps : 0);
        if (precision.steps < minimumReturnSteps ||
            precision.steps > precisionMaximumSteps) {
            m_homingFailure.store(4);
            ErrorLog::instance().log("ERROR", "HOME", "INCONSISTENT_RETURN",
                                     "Precision trigger fell outside the verification window",
                                     activeName);
            goto axis_cleanup;
        }
        if (expectedContactSteps > 0 && !rhoReturnWithinWindow(
                expectedContactSteps, coarseContactSteps, backoffSteps,
                precision.steps, toleranceSteps)) {
            m_homingFailure.store(4);
            ErrorLog::instance().log("ERROR", "HOME",
                                     "FINAL_OUTSIDE_KNOWN_WINDOW",
                                     "Combined return ended outside the confirmed-origin window",
                                     activeName);
            goto axis_cleanup;
        }
        LOG("%s precision home verified: steps=%lu expected=%lu+-%lu "
            "baseline=%u trigger=%u\r\n",
            activeName, static_cast<unsigned long>(precision.steps),
            static_cast<unsigned long>(backoffSteps),
            static_cast<unsigned long>(toleranceSteps),
            precision.baseline, precision.trigger);
    }
    axisSuccess = true;

axis_cleanup:
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_planner.stopRhoHoming();
    setDriverEnabled(activeDriver, activeAddress, homingSettings, false);
    clearInactiveRhoHoldLocked();
    const bool inactiveStillDisabled = holdInactiveMotor ||
        disableDriverMotion(inactiveDriver, inactiveAddress, homingSettings);
    xSemaphoreGive(m_mutex);
    if (!inactiveStillDisabled) {
        m_homingFailure.store(9);
        ErrorLog::instance().log("ERROR", "HOME", "INACTIVE_DISABLE_FAILED",
                                 "Could not verify unused rho bridge disabled",
                                 inactiveName);
        return false;
    }
    if (!holdInactiveMotor) return axisSuccess;
    if (!restoreHeldDriverPhase(
            inactiveDriver, inactiveAddress,
            static_cast<uint16_t>(inactivePhaseRegister & 0x03FFU),
            homingSettings.microsteps,
            inactiveName)) {
        m_homingFailure.store(9);
        ErrorLog::instance().log("ERROR", "HOME", "PHASE_RESTORE_FAILED",
                                 "Could not restore disabled driver phase",
                                 inactiveName);
        return false;
    }
    return axisSuccess;
}

bool PolarControl::homeDrivers() {
    LOG("Preparing %s STEP/DIR sensorless homing\r\n",
        Config::kRhoCompanionMotorEnabled ? "paired" : "main-only");
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    uint32_t rhoIo = 0;
    uint32_t companionIo = 0;
    const bool driversReady = m_state.load() == HOMING &&
        m_rDriver.isSetupAndCommunicating() &&
        m_rCDriver.isSetupAndCommunicating() &&
        readTmcRegisterChecked(R_ADDR, 0x06, rhoIo) &&
        readTmcRegisterChecked(RC_ADDR, 0x06, companionIo) &&
        ((rhoIo >> 24) & 0xFF) == 0x21 &&
        ((companionIo >> 24) & 0xFF) == 0x21;
    xSemaphoreGive(m_mutex);
    if (!driversReady) {
        if (m_state.load() != HOMING) return false;
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        const bool disabled = disableRhoDriversLocked();
        xSemaphoreGive(m_mutex);
        m_homingFailure.store(1);
        ErrorLog::instance().log("ERROR", "HOME", "DRIVER_OFFLINE",
                                 "One or both rho drivers are not communicating");
        if (!disabled) {
            ErrorLog::instance().log("ERROR", "HOME", "DISABLE_VERIFY_FAILED",
                                     "Could not verify both rho drivers disabled after readiness failure");
        }
        return false;
    }

    const DriverSettings normalSettings = m_rDriverSettings;
    DriverSettings homingSettings = normalSettings;
    // Homing is brief and may be louder than normal motion. Use a dedicated
    // current high enough to cross every normal rail section; otherwise a
    // persistent constriction is electrically indistinguishable from the real
    // hard stop. Autoscaled StealthChop also conditions StallGuard more
    // consistently than the fixed-PWM acoustic profile. The 150 ms enabled
    // settle below performs AT#1, and the runway performs AT#2 before SG
    // classification begins.
    homingSettings.runCurrent = Config::kRhoHomingRunCurrentMa;
    homingSettings.holdCurrent = Config::kRhoHomingHoldCurrentMa;
    homingSettings.highSensitivityCurrentScale = true;
    homingSettings.holdDelay = 8;
    homingSettings.powerDownDelay = 20;
    homingSettings.chopperOffTime = 3;
    homingSettings.hysteresisStart = 5;
    homingSettings.hysteresisEnd = 0;
    homingSettings.blankTime = 2;
    homingSettings.microsteps = Config::kRhoHomingMicrosteps;
    homingSettings.interpolationEnabled = true;
    homingSettings.stealthChopEnabled = true;
    homingSettings.stealthChopThreshold = 0;
    homingSettings.pwmFrequency = 0;
    homingSettings.pwmRegulation = 15;
    homingSettings.pwmLimit = 8;
    homingSettings.standstillMode = 0;
    homingSettings.automaticCurrentScaling = true;
    homingSettings.automaticGradientAdaptation = true;
    homingSettings.pwmOffset = 36;
    homingSettings.pwmGradient = 14;
    homingSettings.coolStepEnabled = false;

    m_homingStepsPerMm.store(static_cast<uint16_t>(
        50U * homingSettings.microsteps));

    // A single lost UART write can leave a write-only register (for example
    // IHOLD_IRUN) unknown even when readable profile fields match. Reapply
    // the whole profile, but only while the bridge is verified off. Never
    // proceed to STEP pulses until one complete application verifies.
    auto applyDisabledSettingsWithRetry = [&](TMC2209& driver,
                                               const DriverSettings& settings,
                                               uint8_t address,
                                               const char* name) {
        for (uint8_t attempt = 0; attempt < 3; ++attempt) {
            uint32_t chopconf = 0;
            if (!readTmcRegisterChecked(address, 0x6C, chopconf) ||
                (chopconf & 0x0FU) != 0) {
                LOG("Driver %s bridge not verified off before profile retry\r\n",
                    name);
                return false;
            }
            if (applyDriverSettings(driver, settings, address, name)) {
                return true;
            }
            if (attempt < 2) {
                LOG("Driver %s profile verification retry %u/2\r\n",
                    name, static_cast<unsigned>(attempt + 1));
                delayMicroseconds(250);
            }
        }
        return false;
    };

    auto restoreNormalSettings = [&](bool enableDrivers) {
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        m_planner.stopRhoHoming();
        // Keep both power stages off while restoring configuration. A failed
        // attempt stays de-energized; success re-enables only fitted motors.
        const bool initiallyDisabled = disableRhoDriversLocked();
        if (!initiallyDisabled) {
            xSemaphoreGive(m_mutex);
            return false;
        }
        m_rDriver.moveUsingStepDirInterface();
        if (Config::kRhoCompanionMotorEnabled) {
            m_rCDriver.moveUsingStepDirInterface();
        }
        const bool primaryApplied = applyDisabledSettingsWithRetry(
            m_rDriver, normalSettings, R_ADDR, "rho");
        const bool companionApplied = !Config::kRhoCompanionMotorEnabled ||
            applyDisabledSettingsWithRetry(
                m_rCDriver, normalSettings, RC_ADDR, "rho-companion");
        bool stateVerified = primaryApplied && companionApplied;
        if (stateVerified && enableDrivers) {
            const bool primaryEnabled = setDriverEnabled(
                m_rDriver, R_ADDR, normalSettings, true);
            const bool companionEnabled = Config::kRhoCompanionMotorEnabled
                ? setDriverEnabled(m_rCDriver, RC_ADDR, normalSettings, true)
                : disableDriverMotion(m_rCDriver, RC_ADDR, normalSettings);

            uint32_t primaryChopconf = 0;
            uint32_t companionChopconf = 0;
            stateVerified = primaryEnabled && companionEnabled &&
                readTmcRegisterChecked(R_ADDR, 0x6C, primaryChopconf) &&
                readTmcRegisterChecked(RC_ADDR, 0x6C, companionChopconf) &&
                (primaryChopconf & 0x0FU) != 0 &&
                (((companionChopconf & 0x0FU) != 0) ==
                 Config::kRhoCompanionMotorEnabled);
        }

        if (!stateVerified || !enableDrivers) {
            const bool disabled = disableRhoDriversLocked();
            stateVerified = stateVerified && disabled;
        }
        xSemaphoreGive(m_mutex);
        return stateVerified;
    };

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    const bool homingConfigApplied = disableRhoDriversLocked() &&
        applyDisabledSettingsWithRetry(
            m_rDriver, homingSettings, R_ADDR, "rho") &&
        (Config::kRhoCompanionMotorEnabled
            ? applyDisabledSettingsWithRetry(
                  m_rCDriver, homingSettings, RC_ADDR, "rho-companion")
            : disableDriverMotion(m_rCDriver, RC_ADDR, normalSettings)) &&
        setDriverEnabled(m_rDriver, R_ADDR, homingSettings, false) &&
        setDriverEnabled(m_rCDriver, RC_ADDR,
                         Config::kRhoCompanionMotorEnabled
                             ? homingSettings : normalSettings, false);
    xSemaphoreGive(m_mutex);
    if (!homingConfigApplied) {
        m_homingFailure.store(7);
        ErrorLog::instance().log("ERROR", "HOME", "UART_CONFIG_VERIFY_FAILED",
                                 "Homing driver configuration did not verify");
        restoreNormalSettings(false);
        return false;
    }

    // With a fitted counterweight motor, home it first. With an empty socket,
    // keep its bridge off and home only the main motor.
    const bool companionHomed = !Config::kRhoCompanionMotorEnabled || homeAxis(
        m_rCDriver, RC_ADDR, "rho-companion", m_rDriver, R_ADDR,
        "rho", true, homingSettings);
    const bool primaryHomed = companionHomed && m_state.load() == HOMING &&
        homeAxis(m_rDriver, R_ADDR, "rho",
                 m_rCDriver, RC_ADDR, "rho-companion", false,
                 homingSettings);
    const bool homingSucceeded = primaryHomed && companionHomed;
    const bool restored = restoreNormalSettings(homingSucceeded);
    if (!restored) {
        m_homingFailure.store(7);
        ErrorLog::instance().log("ERROR", "HOME", "RESTORE_FAILED",
                                 "Normal rho driver configuration did not restore");
        return false;
    }
    if (!homingSucceeded) return false;

    m_homingFailedAxis.store(0);
    return true;
}

// ============================================================================
// Pattern Control
// ============================================================================

class SingleTargetGen : public PosGen {
public:
    SingleTargetGen(float theta, float rho)
        : m_target{theta, rho} {}

    PolarCord_t getNextPos() override {
        if (m_sent) return {std::nan(""), std::nan("")};
        m_sent = true;
        return m_target;
    }

private:
    PolarCord_t m_target;
    bool m_sent = false;
};

bool PolarControl::start(std::unique_ptr<PosGen> posGen) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);

#if defined(SISYPHUS_THETA_COMMISSIONING) || defined(SISYPHUS_RHO_COMMISSIONING)
    if (!m_commissioningStartPermit.exchange(false)) {
        LOG("COMMISSIONING: non-test motion rejected\r\n");
        xSemaphoreGive(m_mutex);
        return false;
    }
#endif

    if (m_state != IDLE) {
        LOG("Not IDLE. Start Failed\r\n");
        xSemaphoreGive(m_mutex);
        return false;
    }

    LOG("Starting new PosGen\r\n");

    m_posGen = std::move(posGen);

    // Ensure planner is stopped and empty
    m_planner.stop();

    // Reset planner stats for new pattern
    m_planner.resetCompletedCount();

    // Feed initial segments to planner
    feedPlanner();

    // Start the motion planner
    m_planner.start();

    m_motionCompletionState.store(IDLE);
    m_state = RUNNING;
    xSemaphoreGive(m_mutex);
    return true;
}

bool PolarControl::moveTo(float theta, float rho) {
    if (!std::isfinite(theta) || !std::isfinite(rho) || rho < 0.0f || rho > R_MAX) {
        return false;
    }

    // A polar angle has no physical meaning at the center. Keeping the current
    // theta there avoids an unnecessary rotation while rho converges to zero.
    const PolarCord_t current = getCurrentPosition();
    float targetTheta = current.theta;
    if (rho > 0.01f) {
        const float shortestDelta = remainderf(theta - current.theta, 2.0f * PI);
        targetTheta = current.theta + shortestDelta;
    }

    return start(std_patch::make_unique<SingleTargetGen>(targetTheta, rho));
}

bool PolarControl::jogRelative(float thetaDelta, float rhoDelta) {
    constexpr float kMinimumDelta = 0.000001f;
    const bool jogTheta = std::isfinite(thetaDelta) &&
        fabsf(thetaDelta) > kMinimumDelta;
    const bool jogRho = std::isfinite(rhoDelta) &&
        fabsf(rhoDelta) > kMinimumDelta;
    if (jogTheta == jogRho ||
        (jogTheta && fabsf(thetaDelta) > 100.0f * PI / 180.0f + kMinimumDelta) ||
        (jogRho && fabsf(rhoDelta) > 100.0f + kMinimumDelta)) {
        return false;
    }

    xSemaphoreTake(m_mutex, portMAX_DELAY);
    const State_t entryState = m_state.load();
    if (entryState != IDLE && entryState != INITIALIZED &&
        entryState != HOMING_FAILED) {
        xSemaphoreGive(m_mutex);
        return false;
    }
    if ((jogTheta && !m_thetaDriverConnected.load()) ||
        (jogRho && !m_rhoDriverConnected.load() &&
         !m_rhoCompanionDriverConnected.load())) {
        xSemaphoreGive(m_mutex);
        return false;
    }

    float currentTheta = 0.0f;
    float currentRho = 0.0f;
    m_planner.getCurrentPosition(currentTheta, currentRho);
    if (entryState != IDLE && jogRho) {
        // Rho's absolute position is unknown before homing. Re-center only the
        // logical coordinate before every relative jog so +/-100 mm remains
        // representable without claiming a physical absolute position.
        currentRho = R_MAX * 0.5f;
        m_planner.resetPosition(currentTheta, currentRho);
    }

    const float targetTheta = currentTheta + (jogTheta ? thetaDelta : 0.0f);
    const float targetRho = std::max(0.0f, std::min(
        R_MAX, currentRho + (jogRho ? rhoDelta : 0.0f)));
    if ((jogTheta && fabsf(targetTheta - currentTheta) <= kMinimumDelta) ||
        (jogRho && fabsf(targetRho - currentRho) <= kMinimumDelta)) {
        xSemaphoreGive(m_mutex);
        return false;
    }

    m_posGen = std_patch::make_unique<SingleTargetGen>(targetTheta, targetRho);
    m_planner.stop();
    m_planner.resetCompletedCount();
    feedPlanner();
    m_planner.start();
    m_motionCompletionState.store(entryState);
    m_state.store(RUNNING);
    xSemaphoreGive(m_mutex);
    LOG("Manual %s jog started while %s\r\n",
        jogTheta ? "theta" : "rho",
        entryState == IDLE ? "homed" : "unhomed");
    return true;
}

bool PolarControl::startClearing(std::unique_ptr<PosGen> posGen) {
#if defined(SISYPHUS_THETA_COMMISSIONING) || defined(SISYPHUS_RHO_COMMISSIONING)
    (void)posGen;
    LOG("COMMISSIONING: clearing motion rejected\r\n");
    return false;
#endif
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    if (m_state != IDLE) {
        LOG("Not IDLE. Clearing Start Failed\r\n");
        xSemaphoreGive(m_mutex);
        return false;
    }

    m_clearingSpeedActive = true;
    m_planner.setSpeedMultiplier(1.0f);

    LOG("Starting Clearing Pattern\r\n");

    m_posGen = std::move(posGen);

    // Ensure planner is stopped and empty
    m_planner.stop();

    // Reset planner stats for new pattern
    m_planner.resetCompletedCount();

    // Feed initial segments to planner
    feedPlanner();

    // Start the motion planner
    m_planner.start();

    m_motionCompletionState.store(IDLE);
    m_state = CLEARING;
    xSemaphoreGive(m_mutex);
    return true;
}

bool PolarControl::loadAndRunFile(String filePath) {
    return loadAndRunFile(filePath, R_MAX);
}

bool PolarControl::loadAndRunFile(String filePath, float maxRho) {
#if defined(SISYPHUS_THETA_COMMISSIONING) || defined(SISYPHUS_RHO_COMMISSIONING)
    (void)filePath;
    (void)maxRho;
    LOG("COMMISSIONING: pattern motion rejected\r\n");
    return false;
#endif
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    if (m_state != IDLE) {
        LOG("ERROR: Cannot load file - system not IDLE\r\n");
        ErrorLog::instance().log("ERROR", "FILE", "LOAD_NOT_IDLE",
                                 "Cannot load file - system not IDLE");
        xSemaphoreGive(m_mutex);
        return false;
    }

    LOG("Loading pattern file (Async): %s\r\n", filePath.c_str());

    // Clear any existing generator
    m_posGen.reset();

    // Send load command to file task
    FileCommand cmd;
    cmd.type = FileCommand::CMD_LOAD;
    strncpy(cmd.filename, filePath.c_str(), sizeof(cmd.filename) - 1);
    cmd.filename[sizeof(cmd.filename) - 1] = '\0';
    cmd.maxRho = maxRho;

    // Set flag explicitly BEFORE command to prevent race condition with feedPlanner
    m_fileLoading = true;

    // Ensure planner is stopped and empty
    m_planner.stop();

    // Reset planner stats
    m_planner.resetCompletedCount();

    // Send command
    if (xQueueSend(m_cmdQueue, &cmd, 100) != pdTRUE) {
        LOG("ERROR: Failed to send load command\r\n");
        ErrorLog::instance().log("ERROR", "FILE", "QUEUE_SEND_FAILED",
                                 "Failed to send load command");
        m_fileLoading = false; // Reset if send fails
        xSemaphoreGive(m_mutex);
        return false;
    }
    LOG("Load command queued\r\n");

    // Wait briefly for file task to start filling queue
    vTaskDelay(10);
    feedPlanner();

    m_state = PREPARING;
    m_motionCompletionState.store(IDLE);
    xSemaphoreGive(m_mutex);
    return true;
}

bool PolarControl::pause() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_state == RUNNING) {
        capturePendingTargetsForResume();
        m_pauseAfterStop = true;
        m_planner.stopGracefully();
        m_state = STOPPING;
        xSemaphoreGive(m_mutex);
        return true;
    }
    xSemaphoreGive(m_mutex);
    return false;
}

bool PolarControl::resume() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_state == PAUSED) {
        m_state = RUNNING;
        feedPlanner();
        m_planner.start();
        xSemaphoreGive(m_mutex);
        return true;
    }
    xSemaphoreGive(m_mutex);
    return false;
}

bool PolarControl::stop() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);

    if (m_state == PAUSED || m_state == RUNNING || m_state == CLEARING ||
        m_state == PREPARING || m_state == STOPPING) {
        m_posGen.reset();
        m_resumePoints.clear();
        m_resumePointIndex = 0;
        m_pauseAfterStop = false;
        m_restartAfterSpeedChange = false;

        // Send stop command to file task
        FileCommand cmd;
        cmd.type = FileCommand::CMD_STOP;
        xQueueSend(m_cmdQueue, &cmd, 0);

        if (m_state == PAUSED) {
            m_planner.stop();
        } else if (m_state != STOPPING) {
            m_planner.stopGracefully();
        }

        if (m_state == CLEARING && m_clearingSpeedActive) {
            m_clearingSpeedActive = false;
            if (m_planner.isIdle()) {
                updateSpeedSettings();
            } else {
                // Keep the full-speed limits used to construct the controlled
                // clearing brake until it has finished.
                m_speedUpdatePending = true;
            }
        }

        m_state = m_planner.isIdle() ? IDLE : STOPPING;
        LOG("Stop requested\r\n");

        xSemaphoreGive(m_mutex);
        return true;
    }
    xSemaphoreGive(m_mutex);
    return false;
}

void PolarControl::setSpeed(uint8_t speed) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    const uint8_t newSpeed = std::max<uint8_t>(1, std::min<uint8_t>(speed, 10));
    if (newSpeed == m_speed.load()) {
        xSemaphoreGive(m_mutex);
        return;
    }
    m_speed.store(newSpeed);

    if (m_clearingSpeedActive) {
        // Clearing deliberately runs at full speed. The selected value takes
        // effect when clearing completes or is stopped.
    } else if (m_state == RUNNING) {
        // Generated events are immutable. Preserve their remaining targets,
        // brake with the current limits, and apply the newest requested speed
        // only after reaching zero velocity.
        capturePendingTargetsForResume();
        m_restartAfterSpeedChange = true;
        m_speedUpdatePending = true;
        m_planner.stopGracefully();
        m_state = STOPPING;
    } else if (m_state == STOPPING) {
        // Do not re-plan a braking segment in flight. Multiple slider changes
        // collapse to the last stored value and are applied once stationary.
        m_speedUpdatePending = true;
    } else {
        updateSpeedSettings();
    }
    xSemaphoreGive(m_mutex);
}

MotionSettings PolarControl::getMotionSettings() const {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    MotionSettings settings = m_motionSettings;
    xSemaphoreGive(m_mutex);
    return settings;
}

DriverSettings PolarControl::getThetaDriverSettings() const {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    DriverSettings settings = m_tDriverSettings;
    xSemaphoreGive(m_mutex);
    return settings;
}

DriverSettings PolarControl::getRhoDriverSettings() const {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    DriverSettings settings = m_rDriverSettings;
    xSemaphoreGive(m_mutex);
    return settings;
}

HomingSettings PolarControl::getHomingSettings() const {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    HomingSettings settings = m_homingSettings;
    xSemaphoreGive(m_mutex);
    return settings;
}

void PolarControl::resetTheta() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_state.load() == IDLE) {
        m_planner.resetTheta();
    }
    xSemaphoreGive(m_mutex);
}

PolarControl::State_t PolarControl::getState() {
    return m_state;
}

PolarCord_t PolarControl::getCurrentPosition() const {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    float theta, rho;
    m_planner.getCurrentPosition(theta, rho);
    xSemaphoreGive(m_mutex);
    return {theta, rho};
}

PolarCord_t PolarControl::getActualPosition() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    float theta, rho;
    m_planner.getCurrentPosition(theta, rho);
    xSemaphoreGive(m_mutex);
    return {theta, rho};
}

PolarVelocity_t PolarControl::getActualVelocity() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    float thetaVel, rhoVel;
    m_planner.getCurrentVelocity(thetaVel, rhoVel);
    xSemaphoreGive(m_mutex);
    return {thetaVel, rhoVel};
}

uint32_t PolarControl::getSegmentsCompleted() const {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    uint32_t completed = m_planner.getCompletedCount();
    xSemaphoreGive(m_mutex);
    return completed;
}

void PolarControl::getDiagnostics(uint32_t& queueDepth, uint32_t& underruns) const {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_planner.getDiagnostics(queueDepth, underruns);
    xSemaphoreGive(m_mutex);
}

void PolarControl::getProfileData(uint32_t& maxProcessUs, uint32_t& maxIntervalUs,
                                  uint32_t& avgGenUs) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_planner.getProfileData(maxProcessUs, maxIntervalUs, avgGenUs);
    xSemaphoreGive(m_mutex);
}

void PolarControl::getTelemetry(PlannerTelemetry& telemetry) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_planner.getTelemetry(telemetry);
    xSemaphoreGive(m_mutex);
}

int PolarControl::getProgressPercent() const {
    // Cast away constness to use mutex (it's safe as we don't modify logical state)
    // Alternatively, make m_mutex mutable, but C++ style cast is easier here for existing code
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    int progress = -1;
    if (m_posGen) {
        progress = m_posGen->getProgressPercent();
    } else {
        uint32_t size = m_lastFileSize.load();
        if (size > 0) {
            uint32_t pos = m_lastFilePos.load();
            if (pos > size) pos = size;
            progress = static_cast<int>((pos * 100) / size);
        }
    }
    xSemaphoreGive(m_mutex);
    return progress;
}

void PolarControl::emergencyStop() {
    // Publish cancellation before waiting for the UART/motion lock. The homing
    // task checks this state while holding the same lock before every command.
    m_state.store(INITIALIZED);
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_driverBusInitialized.load()) {
        if (m_thetaDriverConnected.load()) m_tDriver.moveAtVelocity(0);
        if (m_rhoDriverConnected.load()) {
            m_rDriver.moveAtVelocity(0);
        }
        if (m_rhoCompanionDriverConnected.load()) m_rCDriver.moveAtVelocity(0);
    }
    m_planner.stop();
    m_posGen.reset();
    m_resumePoints.clear();
    m_resumePointIndex = 0;
    m_pauseAfterStop = false;
    m_restartAfterSpeedChange = false;
    m_speedUpdatePending = false;
    m_clearingSpeedActive = false;
    updateSpeedSettings();
    if (m_cmdQueue) {
        FileCommand cmd{};
        cmd.type = FileCommand::CMD_STOP;
        xQueueSend(m_cmdQueue, &cmd, 0);
    }
    xSemaphoreGive(m_mutex);
    LOG("Emergency stop; homing is required before motion resumes\r\n");
}

uint32_t PolarControl::getFileTaskHighWater() const {
    if (!m_fileTaskHandle) {
        return 0;
    }
    return static_cast<uint32_t>(uxTaskGetStackHighWaterMark(m_fileTaskHandle));
}

// ============================================================================
// Feed segments to planner
// ============================================================================

void PolarControl::capturePendingTargetsForResume() {
    float theta[SEGMENT_BUFFER_SIZE];
    float rho[SEGMENT_BUFFER_SIZE];
    const size_t pending = m_planner.copyPendingTargets(
        theta, rho, SEGMENT_BUFFER_SIZE);
    const size_t savedRemaining = m_resumePoints.size() - m_resumePointIndex;
    std::vector<PolarCord_t> targets;
    targets.reserve(pending + savedRemaining);
    for (size_t i = 0; i < pending; ++i) {
        targets.push_back({theta[i], rho[i]});
    }
    // A second pause or speed change can arrive before a previous replay has
    // drained. Those not-yet-requeued points follow the planner's targets.
    for (size_t i = m_resumePointIndex; i < m_resumePoints.size(); ++i) {
        targets.push_back(m_resumePoints[i]);
    }
    m_resumePoints = std::move(targets);
    m_resumePointIndex = 0;
}

void PolarControl::feedPlanner() {
    bool replayAdded = false;
    while (m_resumePointIndex < m_resumePoints.size() && m_planner.hasSpace()) {
        const PolarCord_t& point = m_resumePoints[m_resumePointIndex];
        if (!m_planner.addSegment(point.theta, point.rho)) {
            ErrorLog::instance().log("ERROR", "MOTION", "RESUME_POINT_REJECTED",
                                     "A saved resume waypoint was invalid");
            m_resumePoints.clear();
            m_resumePointIndex = 0;
            m_planner.setEndOfPattern(true);
            break;
        }
        ++m_resumePointIndex;
        replayAdded = true;
    }
    if (m_resumePointIndex >= m_resumePoints.size()) {
        m_resumePoints.clear();
        m_resumePointIndex = 0;
    }
    if (replayAdded) {
        m_planner.setEndOfPattern(false);
        m_planner.recalculate();
    }

    // Preserve replay ordering: do not pull newer source points until all
    // planner targets displaced by the braking move have been restored.
    if (!m_resumePoints.empty()) {
        return;
    }

    // Mode 1: Generator (Testing/Clear)
    if (m_posGen) {
        bool addedAny = false;
        while (m_planner.hasSpace()) {
            PolarCord_t next = m_posGen->getNextPos();

            if (next.isNan()) {
                m_planner.setEndOfPattern(true);
                break;
            }

            m_planner.setEndOfPattern(false);
            if (!m_planner.addSegment(next.theta, next.rho)) {
                ErrorLog::instance().log("ERROR", "MOTION", "GENERATOR_POINT_REJECTED",
                                         "A generated waypoint was invalid or outside planner range");
                m_posGen.reset();
                m_planner.setEndOfPattern(true);
                break;
            }
            addedAny = true;
        }
        if (addedAny) {
            m_planner.recalculate();
            if (!m_planner.isRunning()) {
                m_planner.start();
            }
        }
        return;
    }

    // Mode 2: File Queue (Async)
    // Consume as much as possible from the queue - popping is fast!
    bool addedAny = false;
    while (m_planner.hasSpace()) {
        PolarCord_t next;
        if (xQueueReceive(m_coordQueue, &next, 0) == pdTRUE) {
            if (m_state == PREPARING) {
                m_state = RUNNING;
                LOG("feedPlanner: First coord received, state -> RUNNING\r\n");
            }
            m_planner.setEndOfPattern(false);
            if (!m_planner.addSegment(next.theta, next.rho)) {
                ErrorLog::instance().log("ERROR", "MOTION", "FILE_POINT_REJECTED",
                                         "A streamed pattern waypoint was rejected by the planner");
                FileCommand stopCmd{};
                stopCmd.type = FileCommand::CMD_STOP;
                xQueueSend(m_cmdQueue, &stopCmd, 0);
                xQueueReset(m_coordQueue);
                m_fileLoading.store(false);
                m_planner.setEndOfPattern(true);
                break;
            }
            addedAny = true;
        } else {
            // Queue empty. The file task publishes completion atomically.
            if (!m_fileLoading.load()) {
                // File done and queue empty -> End of Pattern
                LOG("feedPlanner: Queue empty and fileLoading=false -> End of Pattern\r\n");
                m_planner.setEndOfPattern(true);
            }
            break;
        }
    }
    if (addedAny) {
        m_planner.recalculate();
        if (!m_planner.isRunning()) {
            m_planner.start();
        }
    }
}

// ============================================================================
// Main Processing Loop
// ============================================================================

bool PolarControl::processNextMove() {
    uint32_t waitStart = micros();
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        m_mutexWaitProfiler.addSample(micros() - waitStart);
        return false;
    }
    m_mutexWaitProfiler.addSample(micros() - waitStart);

    // Let planner process (handles timer internally)
    m_planner.process();

    if (m_state == STOPPING) {
        if (m_planner.isIdle()) {
            if (m_speedUpdatePending) {
                m_speedUpdatePending = false;
                updateSpeedSettings();
            }
            if (m_restartAfterSpeedChange) {
                m_restartAfterSpeedChange = false;
                m_state = RUNNING;
                feedPlanner();
                if (!m_planner.isIdle()) {
                    m_planner.start();
                    LOG("Motion resumed after speed change\r\n");
                } else {
                    m_posGen.reset();
                    m_state = m_motionCompletionState.exchange(IDLE);
                    LOG("Speed changed after motion completed\r\n");
                }
            } else if (m_pauseAfterStop) {
                m_pauseAfterStop = false;
                m_state = PAUSED;
                LOG("Paused after controlled deceleration\r\n");
            } else {
                m_state = m_motionCompletionState.exchange(IDLE);
                LOG("Stopped after controlled deceleration\r\n");
            }
        }
        xSemaphoreGive(m_mutex);
        return false;
    }

    if (m_state == RUNNING || m_state == CLEARING || m_state == PREPARING) {
        // Feed more segments to the planner
        feedPlanner();

        // Check if pattern is complete
        if (m_planner.isIdle()) {
            if (m_state == PREPARING && m_fileLoading) {
                xSemaphoreGive(m_mutex);
                return false;
            }
            if (m_state == CLEARING && m_clearingSpeedActive) {
                m_clearingSpeedActive = false;
                updateSpeedSettings();
            }
            m_posGen.reset();
            m_state = m_motionCompletionState.exchange(IDLE);
            LOG("Pattern Complete (idle state detected in processNextMove)\r\n");
            xSemaphoreGive(m_mutex);
            return false;
        }

        xSemaphoreGive(m_mutex);
        return m_planner.isRunning();
    }

    xSemaphoreGive(m_mutex);
    return false;
}

// ============================================================================
// Tuning Settings
// ============================================================================

static bool validMotionSettings(const MotionSettings& settings) {
    const bool finite = std::isfinite(settings.rMaxVelocity) &&
        std::isfinite(settings.rMaxAccel) && std::isfinite(settings.rMaxJerk) &&
        std::isfinite(settings.tMaxVelocity) && std::isfinite(settings.tMaxAccel) &&
        std::isfinite(settings.tMaxJerk);
    return finite &&
        settings.rMaxVelocity >= 0.1f && settings.rMaxVelocity <= 50.0f &&
        settings.rMaxAccel >= 0.1f && settings.rMaxAccel <= 200.0f &&
        settings.rMaxJerk >= 0.1f && settings.rMaxJerk <= 2000.0f &&
        settings.tMaxVelocity >= 0.01f && settings.tMaxVelocity <= 5.0f &&
        settings.tMaxAccel >= 0.01f && settings.tMaxAccel <= 20.0f &&
        settings.tMaxJerk >= 0.01f && settings.tMaxJerk <= 200.0f;
}

// The software executor services at most one step event per 50 us. Keep a
// substantial margin for dual-axis motion and Wi-Fi task jitter until pulse
// generation moves to a hardware peripheral.
static constexpr float kSafeAxisStepRate = 10000.0f;

static bool motionStepRatesAreSafe(const MotionSettings& motion,
                                   uint16_t thetaMicrosteps,
                                   uint16_t rhoMicrosteps) {
    const float thetaStepsPerRadian =
        (200.0f * thetaMicrosteps / (2.0f * PI)) * (60.0f / 16.0f);
    const float rhoStepsPerMm = 50.0f * rhoMicrosteps;
    return motion.tMaxVelocity * thetaStepsPerRadian <= kSafeAxisStepRate &&
           motion.rMaxVelocity * rhoStepsPerMm <= kSafeAxisStepRate;
}

static bool validMicrosteps(uint16_t microsteps) {
    switch (microsteps) {
        case 1: case 2: case 4: case 8: case 16:
        case 32: case 64: case 128: case 256:
            return true;
        default:
            return false;
    }
}

static bool validDriverSettings(const DriverSettings& settings,
                                uint16_t configuredMaxCurrentMa) {
    constexpr float kCurrentToleranceMargin = 1.06f;
    const uint8_t runRegister = currentMaToDriverRegister(
        settings.runCurrent, settings.highSensitivityCurrentScale);
    const uint8_t holdRegister = currentMaToDriverRegister(
        settings.holdCurrent, settings.highSensitivityCurrentScale);
    const float actualRunCurrentMa = driverRegisterCurrentMa(
        runRegister, settings.highSensitivityCurrentScale);
    const float actualHoldCurrentMa = driverRegisterCurrentMa(
        holdRegister, settings.highSensitivityCurrentScale);
    // The board revision identifies the nominal shunt value but does not state
    // its tolerance. Keep rho below CS=15 until current is measured.
    if (configuredMaxCurrentMa == Config::kRhoMaxRunCurrentMa &&
        runRegister > Config::kRhoMaxUnmeasuredCurrentRegister) {
        return false;
    }
    return settings.runCurrent >= 100 &&
        actualRunCurrentMa * kCurrentToleranceMargin <=
            static_cast<float>(configuredMaxCurrentMa) &&
        settings.holdCurrent <= settings.runCurrent &&
        actualHoldCurrentMa <= actualRunCurrentMa &&
        settings.holdDelay <= 15 && settings.powerDownDelay >= 12 &&
        settings.chopperOffTime >= 1 && settings.chopperOffTime <= 15 &&
        settings.hysteresisStart <= 7 && settings.hysteresisEnd <= 15 &&
        settings.hysteresisStart + settings.hysteresisEnd <= 18 &&
        settings.blankTime <= 3 &&
        (settings.chopperOffTime != 1 || settings.blankTime >= 2) &&
        settings.standstillMode <= 3 &&
        validMicrosteps(settings.microsteps) &&
        settings.pwmFrequency <= 3 && settings.pwmRegulation >= 1 &&
        settings.pwmRegulation <= 15 && settings.pwmLimit <= 15 &&
        (!settings.automaticGradientAdaptation ||
            settings.automaticCurrentScaling) &&
        (!settings.stealthChopEnabled || settings.automaticCurrentScaling ||
            settings.pwmOffset > 0) &&
        settings.stealthChopThreshold <= 0x000FFFFFU &&
        (!settings.coolStepEnabled || settings.coolStepLowerThreshold >= 1) &&
        settings.coolStepLowerThreshold <= 15 &&
        settings.coolStepUpperThreshold <= 15 &&
        settings.coolStepCurrentIncrement <= 3 &&
        settings.coolStepMeasurementCount <= 3 &&
        settings.coolStepThreshold <= 0x000FFFFFU;
}

static bool sameDriverSettings(const DriverSettings& lhs,
                               const DriverSettings& rhs) {
    return lhs.inverseMotorDirection == rhs.inverseMotorDirection &&
        lhs.runCurrent == rhs.runCurrent &&
        lhs.holdCurrent == rhs.holdCurrent &&
        lhs.holdDelay == rhs.holdDelay &&
        lhs.powerDownDelay == rhs.powerDownDelay &&
        lhs.highSensitivityCurrentScale == rhs.highSensitivityCurrentScale &&
        lhs.chopperOffTime == rhs.chopperOffTime &&
        lhs.hysteresisStart == rhs.hysteresisStart &&
        lhs.hysteresisEnd == rhs.hysteresisEnd &&
        lhs.blankTime == rhs.blankTime &&
        lhs.microsteps == rhs.microsteps &&
        lhs.interpolationEnabled == rhs.interpolationEnabled &&
        lhs.stealthChopEnabled == rhs.stealthChopEnabled &&
        lhs.stealthChopThreshold == rhs.stealthChopThreshold &&
        lhs.pwmFrequency == rhs.pwmFrequency &&
        lhs.pwmRegulation == rhs.pwmRegulation &&
        lhs.pwmLimit == rhs.pwmLimit &&
        lhs.standstillMode == rhs.standstillMode &&
        lhs.automaticCurrentScaling == rhs.automaticCurrentScaling &&
        lhs.automaticGradientAdaptation == rhs.automaticGradientAdaptation &&
        lhs.pwmOffset == rhs.pwmOffset &&
        lhs.pwmGradient == rhs.pwmGradient &&
        lhs.coolStepEnabled == rhs.coolStepEnabled &&
        lhs.coolStepLowerThreshold == rhs.coolStepLowerThreshold &&
        lhs.coolStepUpperThreshold == rhs.coolStepUpperThreshold &&
        lhs.coolStepCurrentIncrement == rhs.coolStepCurrentIncrement &&
        lhs.coolStepMeasurementCount == rhs.coolStepMeasurementCount &&
        lhs.coolStepThreshold == rhs.coolStepThreshold;
}

static bool validHomingSettings(const HomingSettings& settings) {
    return settings.triggerPercent >= 40 && settings.triggerPercent <= 85 &&
        settings.consecutiveSamples >= 5 && settings.consecutiveSamples <= 50 &&
        settings.minimumTravelMs >= 100 && settings.minimumTravelMs <= 2500;
}

static bool tuningAllowed(PolarControl::State_t state) {
    const bool normallyAllowed =
        state == PolarControl::IDLE || state == PolarControl::INITIALIZED ||
        state == PolarControl::HOMING_FAILED;
#ifdef SISYPHUS_SKIP_MOTOR_HARDWARE
    return normallyAllowed || state == PolarControl::UNINITIALIZED;
#else
    return normallyAllowed;
#endif
}

TuningUpdateResult PolarControl::saveMotionSettings(const MotionSettings& settings) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_homingTaskHandle != NULL ||
        !tuningAllowed(m_state.load()) || !validMotionSettings(settings) ||
        !motionStepRatesAreSafe(settings, m_tDriverSettings.microsteps,
                               m_rDriverSettings.microsteps)) {
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::REJECTED;
    }
    if (!writeTuningSettingsLocked(settings, m_tDriverSettings,
                                   m_rDriverSettings, m_homingSettings)) {
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::SAVE_FAILED;
    }
    m_motionSettings = settings;

    // Update the motion planner with new limits (doesn't reset positions)
    m_planner.setMotionLimits(
        m_motionSettings.rMaxVelocity,
        m_motionSettings.rMaxAccel,
        m_motionSettings.rMaxJerk,
        m_motionSettings.tMaxVelocity,
        m_motionSettings.tMaxAccel,
        m_motionSettings.tMaxJerk
    );

    LOG("Motion settings updated\r\n");
    xSemaphoreGive(m_mutex);
    return TuningUpdateResult::UPDATED;
}

TuningUpdateResult PolarControl::saveThetaDriverSettings(const DriverSettings& settings) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_homingTaskHandle != NULL || !tuningAllowed(m_state.load()) ||
        !validDriverSettings(settings, Config::kThetaMaxRunCurrentMa) ||
        !motionStepRatesAreSafe(m_motionSettings, settings.microsteps,
                               m_rDriverSettings.microsteps)) {
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::REJECTED;
    }

    const DriverSettings previousSettings = m_tDriverSettings;
    const bool driverReady = m_thetaDriverConnected.load();
    auto restoreDriver = [&]() {
        if (!driverReady) return true;
        const bool disabled = setDriverEnabled(
            m_tDriver, T_ADDR, settings, false);
        const bool restored = applyDriverSettings(
            m_tDriver, previousSettings, T_ADDR, "theta");
        const bool enabled = restored && setDriverEnabled(
            m_tDriver, T_ADDR, previousSettings, true);
        if (!disabled || !restored || !enabled) {
            m_state.store(INITIALIZED);
            ErrorLog::instance().log("ERROR", "TUNING", "THETA_ROLLBACK_FAILED",
                                     "Could not restore theta settings after a failed update");
        }
        return restored;
    };

    if (driverReady) {
        const bool disabled = setDriverEnabled(
            m_tDriver, T_ADDR, previousSettings, false);
        const bool applied = applyDriverSettings(
            m_tDriver, settings, T_ADDR, "theta");
        const bool enabled = applied && setDriverEnabled(
            m_tDriver, T_ADDR, settings, true);
        if (!disabled || !applied || !enabled) {
            restoreDriver();
            xSemaphoreGive(m_mutex);
            return TuningUpdateResult::DRIVER_VERIFY_FAILED;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!writeTuningSettingsLocked(m_motionSettings, settings,
                                   m_rDriverSettings, m_homingSettings)) {
        restoreDriver();
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::SAVE_FAILED;
    }
    const bool scaleChanged = settings.microsteps != m_tDriverSettings.microsteps;
    m_tDriverSettings = settings;

    // Reinitialize planner if microsteps changed (keep position)
    m_planner.init(
        getStepsPerMm(),
        getStepsPerRadian(),
        R_MAX,
        m_motionSettings.rMaxVelocity,
        m_motionSettings.rMaxAccel,
        m_motionSettings.rMaxJerk,
        m_motionSettings.tMaxVelocity,
        m_motionSettings.tMaxAccel,
        m_motionSettings.tMaxJerk,
        scaleChanged
    );

    if (scaleChanged) {
#ifdef SISYPHUS_THETA_COMMISSIONING
        // There is deliberately no physical homing in single-axis mode. The
        // present shaft angle becomes the fresh logical origin after a scale
        // change so another guarded theta test can run immediately.
        m_planner.resetPosition(0.0f, 0.0f);
        m_state.store(IDLE);
#else
        m_state.store(INITIALIZED);
#endif
    }

    LOG("Theta driver settings updated\r\n");
    xSemaphoreGive(m_mutex);
    return TuningUpdateResult::UPDATED;
}

TuningUpdateResult PolarControl::saveRhoDriverSettings(const DriverSettings& settings) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_homingTaskHandle != NULL || !tuningAllowed(m_state.load()) ||
        !validDriverSettings(settings, Config::kRhoMaxRunCurrentMa) ||
        !motionStepRatesAreSafe(m_motionSettings, m_tDriverSettings.microsteps,
                               settings.microsteps)) {
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::REJECTED;
    }

    // Acoustic and commissioning clients deliberately send a complete driver
    // surface before every trial. Do not drop either loaded bridge when that
    // surface is already active: there is nothing to recalibrate or persist.
    if (sameDriverSettings(settings, m_rDriverSettings)) {
        LOG("Rho driver settings unchanged; skipping bridge cycle\r\n");
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::UPDATED;
    }

    const DriverSettings previousSettings = m_rDriverSettings;
    const bool primaryReady = m_rhoDriverConnected.load();
    const bool companionReady = m_rhoCompanionDriverConnected.load();
    const bool driversReady = primaryReady || companionReady;
    auto restoreDrivers = [&]() {
        if (!driversReady) return true;
        const bool primaryDisabled = !primaryReady || setDriverEnabled(
            m_rDriver, R_ADDR, settings, false);
        const bool companionDisabled = !companionReady || setDriverEnabled(
            m_rCDriver, RC_ADDR, settings, false);
        const bool primaryRestored = !primaryReady || applyDriverSettings(
            m_rDriver, previousSettings, R_ADDR, "rho");
        const bool companionRestored = !companionReady || applyDriverSettings(
            m_rCDriver, previousSettings, RC_ADDR, "rho-companion");
        const bool primaryEnabled = !primaryReady || (primaryRestored &&
            setDriverEnabled(m_rDriver, R_ADDR, previousSettings, true));
        const bool companionEnabled = !companionReady || (companionRestored &&
            setDriverEnabled(m_rCDriver, RC_ADDR, previousSettings, true));
        if (!primaryDisabled || !companionDisabled || !primaryRestored ||
            !companionRestored || !primaryEnabled || !companionEnabled) {
            m_state.store(INITIALIZED);
            ErrorLog::instance().log("ERROR", "TUNING", "RHO_ROLLBACK_FAILED",
                                     "Could not restore rho settings after a failed update");
            return false;
        }
        return true;
    };

    if (driversReady) {
        // A fresh disable/apply/enable cycle gives StealthChop a repeatable
        // AT#1 standstill calibration at IRUN. TPOWERDOWN remains longer than
        // the 200 ms wait, so the driver cannot drop to IHOLD prematurely.
        const bool primaryDisabled = !primaryReady || setDriverEnabled(
            m_rDriver, R_ADDR, previousSettings, false);
        const bool companionDisabled = !companionReady || setDriverEnabled(
            m_rCDriver, RC_ADDR, previousSettings, false);
        const bool primaryApplied = !primaryReady || applyDriverSettings(
            m_rDriver, settings, R_ADDR, "rho");
        const bool companionApplied = !companionReady || applyDriverSettings(
            m_rCDriver, settings, RC_ADDR, "rho-companion");
        const bool primaryEnabled = !primaryReady || (primaryApplied &&
            setDriverEnabled(m_rDriver, R_ADDR, settings, true));
        const bool companionEnabled = !companionReady || (companionApplied &&
            setDriverEnabled(m_rCDriver, RC_ADDR, settings, true));
        if (!primaryDisabled || !companionDisabled || !primaryApplied ||
            !companionApplied || !primaryEnabled || !companionEnabled) {
            restoreDrivers();
            xSemaphoreGive(m_mutex);
            return TuningUpdateResult::DRIVER_VERIFY_FAILED;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!writeTuningSettingsLocked(m_motionSettings, m_tDriverSettings,
                                   settings, m_homingSettings)) {
        restoreDrivers();
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::SAVE_FAILED;
    }
    const bool scaleChanged = settings.microsteps != m_rDriverSettings.microsteps;
    m_rDriverSettings = settings;

    // Reinitialize planner if microsteps changed (keep position)
    m_planner.init(
        getStepsPerMm(),
        getStepsPerRadian(),
        R_MAX,
        m_motionSettings.rMaxVelocity,
        m_motionSettings.rMaxAccel,
        m_motionSettings.rMaxJerk,
        m_motionSettings.tMaxVelocity,
        m_motionSettings.tMaxAccel,
        m_motionSettings.tMaxJerk,
        scaleChanged
    );

    if (scaleChanged) {
#ifdef SISYPHUS_RHO_COMMISSIONING
        // Commissioning motion can only finish at its temporary origin and no
        // other motion mode is available. Re-establish that logical origin so
        // microstep candidates can be compared without pretending to home.
        m_planner.resetPosition(0.0f, 0.0f);
        m_state.store(IDLE);
#else
        m_state.store(INITIALIZED);
#endif
    }

    LOG("Rho driver settings updated\r\n");
    xSemaphoreGive(m_mutex);
    return TuningUpdateResult::UPDATED;
}

TuningUpdateResult PolarControl::saveHomingSettings(const HomingSettings& settings) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (m_homingTaskHandle != NULL ||
        !tuningAllowed(m_state.load()) || !validHomingSettings(settings)) {
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::REJECTED;
    }
    if (!writeTuningSettingsLocked(m_motionSettings, m_tDriverSettings,
                                   m_rDriverSettings, settings)) {
        xSemaphoreGive(m_mutex);
        return TuningUpdateResult::SAVE_FAILED;
    }
    m_homingSettings = settings;
    xSemaphoreGive(m_mutex);
    return TuningUpdateResult::UPDATED;
}

// ============================================================================
// Settings Persistence
// ============================================================================

static const char* TUNING_FILE = "/tuning.json";
static const char* TUNING_TEMP_FILE = "/tuning.tmp";
static const char* TUNING_BACKUP_FILE = "/tuning.bak";

// Helper to save driver settings to JSON object
static void saveDriverSettingsToJson(JsonObject& obj, const DriverSettings& settings) {
    // Current settings (mA)
    obj["runCurrent"] = settings.runCurrent;
    obj["holdCurrent"] = settings.holdCurrent;
    obj["holdDelay"] = settings.holdDelay;
    obj["powerDownDelay"] = settings.powerDownDelay;
    obj["highSensitivityCurrentScale"] = settings.highSensitivityCurrentScale;
    obj["chopperOffTime"] = settings.chopperOffTime;
    obj["hysteresisStart"] = settings.hysteresisStart;
    obj["hysteresisEnd"] = settings.hysteresisEnd;
    obj["blankTime"] = settings.blankTime;

    // Microstepping
    obj["microsteps"] = settings.microsteps;
    obj["interpolationEnabled"] = settings.interpolationEnabled;

    // StealthChop settings
    obj["stealthChopEnabled"] = settings.stealthChopEnabled;
    obj["stealthChopThreshold"] = settings.stealthChopThreshold;
    obj["pwmFrequency"] = settings.pwmFrequency;
    obj["pwmRegulation"] = settings.pwmRegulation;
    obj["pwmLimit"] = settings.pwmLimit;
    obj["standstillMode"] = settings.standstillMode;
    obj["automaticCurrentScaling"] = settings.automaticCurrentScaling;
    obj["automaticGradientAdaptation"] = settings.automaticGradientAdaptation;
    obj["pwmOffset"] = settings.pwmOffset;
    obj["pwmGradient"] = settings.pwmGradient;

    // CoolStep settings
    obj["coolStepEnabled"] = settings.coolStepEnabled;
    obj["coolStepLowerThreshold"] = settings.coolStepLowerThreshold;
    obj["coolStepUpperThreshold"] = settings.coolStepUpperThreshold;
    obj["coolStepCurrentIncrement"] = settings.coolStepCurrentIncrement;
    obj["coolStepMeasurementCount"] = settings.coolStepMeasurementCount;
    obj["coolStepThreshold"] = settings.coolStepThreshold;
}

bool PolarControl::writeTuningSettingsLocked(
    const MotionSettings& motionSettings,
    const DriverSettings& thetaSettings,
    const DriverSettings& rhoSettings,
    const HomingSettings& homingSettings) {
    if (!m_tuningStorageReady.load()) {
        ErrorLog::instance().log("ERROR", "TUNING", "STORAGE_UNAVAILABLE",
                                 "LittleFS is unavailable; settings were not changed");
        return false;
    }

    JsonDocument doc;
    doc["schemaVersion"] = 1;

    // Motion settings
    JsonObject motion = doc["motion"].to<JsonObject>();
    motion["rMaxVelocity"] = motionSettings.rMaxVelocity;
    motion["rMaxAccel"] = motionSettings.rMaxAccel;
    motion["rMaxJerk"] = motionSettings.rMaxJerk;
    motion["tMaxVelocity"] = motionSettings.tMaxVelocity;
    motion["tMaxAccel"] = motionSettings.tMaxAccel;
    motion["tMaxJerk"] = motionSettings.tMaxJerk;

    // Driver settings
    JsonObject theta = doc["thetaDriver"].to<JsonObject>();
    saveDriverSettingsToJson(theta, thetaSettings);

    JsonObject rho = doc["rhoDriver"].to<JsonObject>();
    saveDriverSettingsToJson(rho, rhoSettings);

    JsonObject homing = doc["homing"].to<JsonObject>();
    homing["triggerPercent"] = homingSettings.triggerPercent;
    homing["consecutiveSamples"] = homingSettings.consecutiveSamples;
    homing["minimumTravelMs"] = homingSettings.minimumTravelMs;

    // Stage and atomically replace the old file so loss of power cannot leave
    // a half-written machine configuration.
    if (LittleFS.exists(TUNING_TEMP_FILE) &&
        !LittleFS.remove(TUNING_TEMP_FILE)) {
        ErrorLog::instance().log("ERROR", "TUNING", "TEMP_REMOVE_FAILED",
                                 "Could not remove the stale tuning temp file");
        return false;
    }
    File file = LittleFS.open(TUNING_TEMP_FILE, FILE_WRITE);
    if (!file) {
        LOG("Failed to open tuning file for writing\r\n");
        ErrorLog::instance().log("ERROR", "TUNING", "WRITE_OPEN_FAILED",
                                 "Failed to open tuning file for writing");
        return false;
    }

    const size_t bytesWritten = serializeJsonPretty(doc, file);
    file.flush();
    const bool writeFailed = bytesWritten == 0 || file.getWriteError() != 0;
    if (writeFailed) {
        file.close();
        LittleFS.remove(TUNING_TEMP_FILE);
        ErrorLog::instance().log("ERROR", "TUNING", "WRITE_FAILED",
                                 "Could not completely write the tuning temp file");
        return false;
    }
    file.close();

    if (LittleFS.exists(TUNING_BACKUP_FILE) &&
        !LittleFS.remove(TUNING_BACKUP_FILE)) {
        LittleFS.remove(TUNING_TEMP_FILE);
        ErrorLog::instance().log("ERROR", "TUNING", "BACKUP_REMOVE_FAILED",
                                 "Could not rotate the previous tuning backup");
        return false;
    }
    const bool hadOriginal = LittleFS.exists(TUNING_FILE);
    if (hadOriginal && !LittleFS.rename(TUNING_FILE, TUNING_BACKUP_FILE)) {
        LittleFS.remove(TUNING_TEMP_FILE);
        ErrorLog::instance().log("ERROR", "TUNING", "BACKUP_CREATE_FAILED",
                                 "Could not preserve the previous tuning file");
        return false;
    }
    if (!LittleFS.rename(TUNING_TEMP_FILE, TUNING_FILE)) {
        if (hadOriginal) LittleFS.rename(TUNING_BACKUP_FILE, TUNING_FILE);
        LittleFS.remove(TUNING_TEMP_FILE);
        ErrorLog::instance().log("ERROR", "TUNING", "COMMIT_FAILED",
                                 "Could not commit the staged tuning file");
        return false;
    }
    if (hadOriginal) LittleFS.remove(TUNING_BACKUP_FILE);
    LOG("Tuning settings saved to %s\r\n", TUNING_FILE);
    return true;
}

bool PolarControl::saveTuningSettings() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    const bool saved = writeTuningSettingsLocked(
        m_motionSettings, m_tDriverSettings, m_rDriverSettings,
        m_homingSettings);
    xSemaphoreGive(m_mutex);
    return saved;
}

// Helper to load driver settings from JSON object
static void loadDriverSettingsFromJson(JsonObjectConst obj, DriverSettings& settings) {
    // Current settings (mA)
    settings.runCurrent = obj["runCurrent"] | settings.runCurrent;
    settings.holdCurrent = obj["holdCurrent"] | settings.holdCurrent;
    settings.holdDelay = obj["holdDelay"] | settings.holdDelay;
    settings.powerDownDelay = obj["powerDownDelay"] | settings.powerDownDelay;
    settings.highSensitivityCurrentScale = obj["highSensitivityCurrentScale"] |
        settings.highSensitivityCurrentScale;
    settings.chopperOffTime = obj["chopperOffTime"] | settings.chopperOffTime;
    settings.hysteresisStart = obj["hysteresisStart"] | settings.hysteresisStart;
    settings.hysteresisEnd = obj["hysteresisEnd"] | settings.hysteresisEnd;
    settings.blankTime = obj["blankTime"] | settings.blankTime;

    // Microstepping
    settings.microsteps = obj["microsteps"] | settings.microsteps;
    settings.interpolationEnabled = obj["interpolationEnabled"] |
        settings.interpolationEnabled;

    // StealthChop settings
    settings.stealthChopEnabled = obj["stealthChopEnabled"] | settings.stealthChopEnabled;
    settings.stealthChopThreshold = obj["stealthChopThreshold"] | settings.stealthChopThreshold;
    settings.pwmFrequency = obj["pwmFrequency"] | settings.pwmFrequency;
    settings.pwmRegulation = obj["pwmRegulation"] | settings.pwmRegulation;
    settings.pwmLimit = obj["pwmLimit"] | settings.pwmLimit;
    settings.standstillMode = obj["standstillMode"] | settings.standstillMode;
    settings.automaticCurrentScaling = obj["automaticCurrentScaling"] |
        settings.automaticCurrentScaling;
    settings.automaticGradientAdaptation = obj["automaticGradientAdaptation"] |
        settings.automaticGradientAdaptation;
    settings.pwmOffset = obj["pwmOffset"] | settings.pwmOffset;
    settings.pwmGradient = obj["pwmGradient"] | settings.pwmGradient;

    // CoolStep settings
    settings.coolStepEnabled = obj["coolStepEnabled"] | settings.coolStepEnabled;
    settings.coolStepLowerThreshold = obj["coolStepLowerThreshold"] | settings.coolStepLowerThreshold;
    settings.coolStepUpperThreshold = obj["coolStepUpperThreshold"] | settings.coolStepUpperThreshold;
    settings.coolStepCurrentIncrement = obj["coolStepCurrentIncrement"] | settings.coolStepCurrentIncrement;
    settings.coolStepMeasurementCount = obj["coolStepMeasurementCount"] | settings.coolStepMeasurementCount;
    settings.coolStepThreshold = obj["coolStepThreshold"] | settings.coolStepThreshold;
}

static bool loadTuningSettingsFile(
    const char* sourceFile,
    const MotionSettings& currentMotion,
    const DriverSettings& currentTheta,
    const DriverSettings& currentRho,
    const HomingSettings& currentHoming,
    MotionSettings& loadedMotion,
    DriverSettings& loadedTheta,
    DriverSettings& loadedRho,
    HomingSettings& loadedHoming) {
    File file = LittleFS.open(sourceFile, FILE_READ);
    if (!file) {
        LOG("Failed to open tuning file %s for reading\r\n", sourceFile);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        LOG("Failed to parse tuning file %s: %s\r\n",
            sourceFile, error.c_str());
        return false;
    }

    if (!doc.is<JsonObject>()) {
        LOG("Tuning file %s does not contain a JSON object\r\n", sourceFile);
        return false;
    }
    const uint32_t schemaVersion = doc["schemaVersion"] | 0U;
    if (schemaVersion > 1U) {
        LOG("Tuning file %s uses unsupported schema %lu\r\n",
            sourceFile, static_cast<unsigned long>(schemaVersion));
        return false;
    }

    loadedMotion = currentMotion;
    loadedTheta = currentTheta;
    loadedRho = currentRho;
    loadedHoming = currentHoming;

    // Load into temporary values so a corrupt file cannot partially change
    // the machine configuration.
    if (doc["motion"].is<JsonObjectConst>()) {
        JsonObjectConst motion = doc["motion"];
        loadedMotion.rMaxVelocity = motion["rMaxVelocity"] | loadedMotion.rMaxVelocity;
        loadedMotion.rMaxAccel = motion["rMaxAccel"] | loadedMotion.rMaxAccel;
        loadedMotion.rMaxJerk = motion["rMaxJerk"] | loadedMotion.rMaxJerk;
        loadedMotion.tMaxVelocity = motion["tMaxVelocity"] | loadedMotion.tMaxVelocity;
        loadedMotion.tMaxAccel = motion["tMaxAccel"] | loadedMotion.tMaxAccel;
        loadedMotion.tMaxJerk = motion["tMaxJerk"] | loadedMotion.tMaxJerk;
    }

    // Driver settings
    if (doc["thetaDriver"].is<JsonObjectConst>()) {
        JsonObjectConst theta = doc["thetaDriver"];
        loadDriverSettingsFromJson(theta, loadedTheta);
    }

    if (doc["rhoDriver"].is<JsonObjectConst>()) {
        JsonObjectConst rho = doc["rhoDriver"];
        loadDriverSettingsFromJson(rho, loadedRho);
    }

    if (doc["homing"].is<JsonObjectConst>()) {
        JsonObjectConst homing = doc["homing"];
        loadedHoming.triggerPercent = homing["triggerPercent"] | loadedHoming.triggerPercent;
        loadedHoming.consecutiveSamples = homing["consecutiveSamples"] | loadedHoming.consecutiveSamples;
        loadedHoming.minimumTravelMs = homing["minimumTravelMs"] | loadedHoming.minimumTravelMs;
    }

    if (!validMotionSettings(loadedMotion) ||
        !validDriverSettings(loadedTheta, Config::kThetaMaxRunCurrentMa) ||
        !validDriverSettings(loadedRho, Config::kRhoMaxRunCurrentMa) ||
        !validHomingSettings(loadedHoming) ||
        !motionStepRatesAreSafe(loadedMotion, loadedTheta.microsteps,
                               loadedRho.microsteps)) {
        LOG("Tuning file %s contains unsafe or invalid values\r\n", sourceFile);
        return false;
    }

    return true;
}

bool PolarControl::loadTuningSettings() {
    if (!m_tuningStorageReady.load()) return false;

    MotionSettings loadedMotion;
    DriverSettings loadedTheta;
    DriverSettings loadedRho;
    HomingSettings loadedHoming;
    const char* sourceFile = nullptr;
    const char* candidates[] = {TUNING_FILE, TUNING_BACKUP_FILE};
    for (const char* candidate : candidates) {
        if (!LittleFS.exists(candidate)) continue;
        if (loadTuningSettingsFile(candidate, m_motionSettings,
                                   m_tDriverSettings, m_rDriverSettings,
                                   m_homingSettings, loadedMotion, loadedTheta,
                                   loadedRho, loadedHoming)) {
            sourceFile = candidate;
            break;
        }
    }

    if (sourceFile == nullptr) {
        if (LittleFS.exists(TUNING_FILE) || LittleFS.exists(TUNING_BACKUP_FILE)) {
            ErrorLog::instance().log("ERROR", "TUNING", "INVALID_FILE",
                                     "No valid tuning file was found; using defaults");
        } else {
            LOG("No tuning file found, using defaults\r\n");
        }
        return false;
    }

    m_motionSettings = loadedMotion;
    m_tDriverSettings = loadedTheta;
    m_rDriverSettings = loadedRho;
    m_homingSettings = loadedHoming;

    LOG("Tuning settings loaded from %s\r\n", sourceFile);
    if (strcmp(sourceFile, TUNING_BACKUP_FILE) == 0) {
        ErrorLog::instance().log("WARN", "TUNING", "BACKUP_RECOVERED",
                                 "Primary tuning file was invalid; recovered the backup");
    }
    return true;
}

// ============================================================================
// Motor Stress Tests (using Motion Planner)
// ============================================================================

class TestThetaContinuousGen : public PosGen {
public:
    TestThetaContinuousGen(float fixedRho) : m_rho(fixedRho), m_phase(0) {}

    PolarCord_t getNextPos() override {
        // Phase 0: Rotate one full turn forward in one go
        // Phase 1: Rotate one full turn back in one go

        const float FULL_ROTATION = 2.0 * PI;
        const int ROTATIONS = 1;

        if (m_phase == 0) {
            m_phase = 1;
            return {0, m_rho};
        }

        if (m_phase == 1) {
            m_phase = 2;
            return {FULL_ROTATION * ROTATIONS, m_rho};
        }

        if (m_phase == 2) {
            m_phase = 3;
            return {0, m_rho};
        }
        return {std::nan(""), std::nan("")};
    }
private:
    float m_rho;
    int m_phase;
};

class TestThetaStressGen : public PosGen {
public:
    TestThetaStressGen(float fixedRho) : m_rho(fixedRho), m_phase(0), m_step(0) {}

    PolarCord_t getNextPos() override {
        const float DEGREES_TO_RADIANS = PI / 180.0f;

        // Varying move sizes in degrees
        static const float MOVE_SIZES[] = {
            0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 20.0, 30.0, 45.0, 60.0, 90.0,
            90.0, 60.0, 45.0, 30.0, 20.0, 15.0, 10.0, 5.0, 2.0, 1.0, 0.5
        };
        static const int NUM_MOVE_SIZES = sizeof(MOVE_SIZES) / sizeof(MOVE_SIZES[0]);

        if (m_phase == 0) {
            // Varying size moves - go out and back for each size
            if (m_step >= NUM_MOVE_SIZES * 2) {
                m_phase = 1;
                m_step = 0;
                m_currentTheta = 0;
            } else {
                int sizeIdx = m_step / 2;
                bool goingOut = (m_step % 2 == 0);
                float moveSize = MOVE_SIZES[sizeIdx] * DEGREES_TO_RADIANS;

                if (goingOut) {
                    m_currentTheta = moveSize;
                } else {
                    m_currentTheta = 0;
                }
                m_step++;
                return {m_currentTheta, m_rho};
            }
        }

        if (m_phase == 1) {
            // Quick random-ish reversals at different amplitudes
            static const float QUICK_SIZES[] = {5.0f, 45.0f, 2.0f, 90.0f, 10.0f, 30.0f, 1.0f, 60.0f, 15.0f, 0.5f};
            static const int NUM_QUICK = sizeof(QUICK_SIZES) / sizeof(QUICK_SIZES[0]);

            if (m_step >= NUM_QUICK * 2) {
                return {std::nan(""), std::nan("")};
            }

            int sizeIdx = m_step / 2;
            bool goingOut = (m_step % 2 == 0);
            float moveSize = QUICK_SIZES[sizeIdx] * DEGREES_TO_RADIANS;

            if (goingOut) {
                m_currentTheta = moveSize;
            } else {
                m_currentTheta = 0;
            }
            m_step++;
            return {m_currentTheta, m_rho};
        }
        return {std::nan(""), std::nan("")};
    }
private:
    float m_rho;
    float m_currentTheta = 0.0f;
    int m_phase;
    int m_step;
};

class TestRhoContinuousGen : public PosGen {
public:
    TestRhoContinuousGen(float theta, float startRho)
        : m_theta(theta), m_startRho(startRho) {}

    PolarCord_t getNextPos() override {
        if (m_phase >= RhoAcousticProfile::kContinuousOffsetsMm.size()) {
            return {std::nan(""), std::nan("")};
        }
        return {m_theta, m_startRho +
            RhoAcousticProfile::kContinuousOffsetsMm[m_phase++]};
    }
private:
    float m_theta;
    float m_startRho;
    uint8_t m_phase = 0;
};

class TestRhoStressGen : public PosGen {
public:
    TestRhoStressGen(float theta, float startRho)
        : m_theta(theta), m_startRho(startRho) {}

    PolarCord_t getNextPos() override {
        // Every target is at or outward from the temporary origin. The final
        // zero offset is mandatory so even the jerky profile finishes exactly
        // where it began without ever commanding motion farther inward.
        if (m_step >= RhoAcousticProfile::kStressOffsetsMm.size()) {
            return {std::nan(""), std::nan("")};
        }
        return {m_theta, m_startRho +
            RhoAcousticProfile::kStressOffsetsMm[m_step++]};
    }
private:
    float m_theta;
    float m_startRho;
    size_t m_step = 0;
};

bool PolarControl::testThetaContinuous() {
#ifdef SISYPHUS_RHO_COMMISSIONING
    LOG("RHO COMMISSIONING: theta test rejected\r\n");
    return false;
#endif
    if (m_state != IDLE) return false;
    LOG("Starting theta continuous test...\r\n");

    float currentTheta, currentRho;
    m_planner.getCurrentPosition(currentTheta, currentRho);
    resetTheta();
#ifdef SISYPHUS_THETA_COMMISSIONING
    m_commissioningStartPermit.store(true);
#endif
    return start(std_patch::make_unique<TestThetaContinuousGen>(currentRho));
}

bool PolarControl::testThetaStress() {
#ifdef SISYPHUS_RHO_COMMISSIONING
    LOG("RHO COMMISSIONING: theta test rejected\r\n");
    return false;
#endif
    if (m_state != IDLE) return false;
    LOG("Starting theta stress test...\r\n");

    float currentTheta, currentRho;
    m_planner.getCurrentPosition(currentTheta, currentRho);
    resetTheta();
#ifdef SISYPHUS_THETA_COMMISSIONING
    m_commissioningStartPermit.store(true);
#endif
    return start(std_patch::make_unique<TestThetaStressGen>(currentRho));
}

bool PolarControl::testThetaSegment(float targetThetaRad) {
#ifndef SISYPHUS_THETA_COMMISSIONING
    (void)targetThetaRad;
    return false;
#else
    if (m_state != IDLE || !m_thetaDriverConnected.load() ||
        !std::isfinite(targetThetaRad) || targetThetaRad < 0.0f ||
        targetThetaRad > 2.0f * PI) {
        return false;
    }
    const PolarCord_t current = getCurrentPosition();
    if (current.theta < -0.001f || current.theta > 2.0f * PI + 0.001f) {
        return false;
    }
    LOG("Starting bounded theta segment to %.4frad...\r\n", targetThetaRad);
    m_commissioningStartPermit.store(true);
    return start(std_patch::make_unique<SingleTargetGen>(
        targetThetaRad, current.rho));
#endif
}

bool PolarControl::testRhoContinuous() {
#ifdef SISYPHUS_THETA_COMMISSIONING
    LOG("THETA COMMISSIONING: rho test rejected\r\n");
    return false;
#endif
    if (m_state != IDLE || !m_rhoDriverConnected.load() ||
        (Config::kRhoCompanionMotorEnabled &&
         !m_rhoCompanionDriverConnected.load())) return false;
    LOG("Starting rho continuous test...\r\n");
    const PolarCord_t startPosition = getCurrentPosition();
    if (startPosition.rho < 0.0f ||
        startPosition.rho + RhoAcousticProfile::kExcursionMm > R_MAX) {
        LOG("Rho test origin lacks %.0fmm outward clearance\r\n",
            RhoAcousticProfile::kExcursionMm);
        return false;
    }
#ifdef SISYPHUS_RHO_COMMISSIONING
    m_commissioningStartPermit.store(true);
#endif
    return start(std_patch::make_unique<TestRhoContinuousGen>(
        startPosition.theta, startPosition.rho));
}

bool PolarControl::testRhoStress() {
#ifdef SISYPHUS_THETA_COMMISSIONING
    LOG("THETA COMMISSIONING: rho test rejected\r\n");
    return false;
#endif
    if (m_state != IDLE || !m_rhoDriverConnected.load() ||
        (Config::kRhoCompanionMotorEnabled &&
         !m_rhoCompanionDriverConnected.load())) return false;
    LOG("Starting rho stress test...\r\n");
    const PolarCord_t startPosition = getCurrentPosition();
    if (startPosition.rho < 0.0f ||
        startPosition.rho + RhoAcousticProfile::kExcursionMm > R_MAX) {
        LOG("Rho test origin lacks %.0fmm outward clearance\r\n",
            RhoAcousticProfile::kExcursionMm);
        return false;
    }
#ifdef SISYPHUS_RHO_COMMISSIONING
    m_commissioningStartPermit.store(true);
#endif
    return start(std_patch::make_unique<TestRhoStressGen>(
        startPosition.theta, startPosition.rho));
}

bool PolarControl::testRhoSegment(float targetRhoMm) {
#ifndef SISYPHUS_RHO_COMMISSIONING
    (void)targetRhoMm;
    return false;
#else
    if (m_state != IDLE || !m_rhoDriverConnected.load() ||
        (Config::kRhoCompanionMotorEnabled &&
         !m_rhoCompanionDriverConnected.load()) ||
        !std::isfinite(targetRhoMm) ||
        targetRhoMm < 0.0f || targetRhoMm > RhoAcousticProfile::kExcursionMm) {
        return false;
    }
    const PolarCord_t current = getCurrentPosition();
    if (current.rho < 0.0f ||
        current.rho > RhoAcousticProfile::kExcursionMm) {
        return false;
    }
    LOG("Starting bounded rho segment to %.2fmm...\r\n", targetRhoMm);
    m_commissioningStartPermit.store(true);
    return start(std_patch::make_unique<SingleTargetGen>(
        current.theta, targetRhoMm));
#endif
}

// ============================================================================
// Driver Diagnostics
// ============================================================================

// Helper to dump driver info to JSON
static void fillDriverJson(TMC2209& driver, uint8_t driverAddress,
                           const char* name, JsonDocument& doc) {
    uint32_t ioInput = 0;
    const bool uartResponseValid =
        readTmcRegisterChecked(driverAddress, 0x06, ioInput) &&
        ((ioInput >> 24) & 0xFF) == 0x21;
    doc["name"] = name;
    doc["connected"] = uartResponseValid;
    doc["uartResponseValid"] = uartResponseValid;
    doc["communicating"] = uartResponseValid && driver.isCommunicating();
    doc["setupOk"] = uartResponseValid && driver.isSetupAndCommunicating();

    if (uartResponseValid) {
        // IOIN reflects the actual levels observed at the TMC2209 pins. Keep
        // these in the diagnostic dump so a commanded GPIO direction can be
        // distinguished from a wiring or driver-input fault.
        JsonObject inputsObj = doc["inputs"].to<JsonObject>();
        inputsObj["enableN"] = (ioInput & (1UL << 0)) != 0;
        inputsObj["ms1"] = (ioInput & (1UL << 2)) != 0;
        inputsObj["ms2"] = (ioInput & (1UL << 3)) != 0;
        inputsObj["diag"] = (ioInput & (1UL << 4)) != 0;
        inputsObj["pdnUart"] = (ioInput & (1UL << 6)) != 0;
        inputsObj["step"] = (ioInput & (1UL << 7)) != 0;
        inputsObj["spreadEnable"] = (ioInput & (1UL << 8)) != 0;
        inputsObj["direction"] = (ioInput & (1UL << 9)) != 0;
        inputsObj["raw"] = ioInput;

        // Get settings from driver
        TMC2209::Settings settings = driver.getSettings();
        JsonObject settingsObj = doc["settings"].to<JsonObject>();
        settingsObj["softwareEnabled"] = settings.software_enabled;
        settingsObj["microstepsPerStep"] = settings.microsteps_per_step;
        settingsObj["inverseMotorDirection"] = settings.inverse_motor_direction_enabled;
        settingsObj["stealthChopEnabled"] = settings.stealth_chop_enabled;
        settingsObj["standstillMode"] = settings.standstill_mode;
        settingsObj["irunPercent"] = settings.irun_percent;
        settingsObj["irunRegister"] = settings.irun_register_value;
        settingsObj["iholdPercent"] = settings.ihold_percent;
        settingsObj["iholdRegister"] = settings.ihold_register_value;
        settingsObj["iholdDelayPercent"] = settings.iholddelay_percent;
        settingsObj["iholdDelayRegister"] = settings.iholddelay_register_value;
        settingsObj["coolStepEnabled"] = settings.cool_step_enabled;
        settingsObj["analogCurrentScaling"] = settings.analog_current_scaling_enabled;
        settingsObj["internalSenseResistors"] = settings.internal_sense_resistors_enabled;
        uint32_t otpRead = 0;
        const bool otpReadValid =
            readTmcRegisterChecked(driverAddress, 0x05, otpRead);
        settingsObj["otpReadValid"] = otpReadValid;
        if (otpReadValid) {
            settingsObj["otpInternalSenseResistors"] =
                (otpRead & (1UL << 6)) != 0;
            settingsObj["otpReadRaw"] = otpRead;
        }
        uint32_t chopconf = 0;
        const bool chopconfValid =
            readTmcRegisterChecked(driverAddress, 0x6C, chopconf);
        settingsObj["chopconfReadValid"] = chopconfValid;
        if (chopconfValid) {
            settingsObj["interpolationTo256"] = (chopconf & (1UL << 28)) != 0;
            settingsObj["highSensitivityCurrentScale"] =
                (chopconf & (1UL << 17)) != 0;
            settingsObj["chopperOffTime"] = chopconf & 0x0FU;
            settingsObj["hysteresisStart"] = (chopconf >> 4) & 0x07U;
            settingsObj["hysteresisEnd"] = (chopconf >> 7) & 0x0FU;
            settingsObj["blankTime"] = (chopconf >> 15) & 0x03U;
            settingsObj["chopconfRaw"] = chopconf;
        }
        uint32_t pwmconf = 0;
        const bool pwmconfValid =
            readTmcRegisterChecked(driverAddress, 0x70, pwmconf);
        settingsObj["pwmconfReadValid"] = pwmconfValid;
        if (pwmconfValid) {
            settingsObj["pwmOffset"] = pwmconf & 0xFFU;
            settingsObj["pwmGradient"] = (pwmconf >> 8) & 0xFFU;
            settingsObj["pwmFrequency"] = (pwmconf >> 16) & 0x03U;
            settingsObj["automaticCurrentScaling"] =
                (pwmconf & (1UL << 18)) != 0;
            settingsObj["automaticGradientAdaptation"] =
                (pwmconf & (1UL << 19)) != 0;
            settingsObj["standstillMode"] = (pwmconf >> 20) & 0x03U;
            settingsObj["pwmRegulation"] = (pwmconf >> 24) & 0x0FU;
            settingsObj["pwmLimit"] = (pwmconf >> 28) & 0x0FU;
            settingsObj["pwmconfRaw"] = pwmconf;
        }

        // Get status from driver
        TMC2209::Status status = driver.getStatus();
        JsonObject statusObj = doc["status"].to<JsonObject>();
        statusObj["overTempWarning"] = (bool)status.over_temperature_warning;
        statusObj["overTempShutdown"] = (bool)status.over_temperature_shutdown;
        statusObj["shortToGroundA"] = (bool)status.short_to_ground_a;
        statusObj["shortToGroundB"] = (bool)status.short_to_ground_b;
        statusObj["lowSideShortA"] = (bool)status.low_side_short_a;
        statusObj["lowSideShortB"] = (bool)status.low_side_short_b;
        statusObj["openLoadA"] = (bool)status.open_load_a;
        statusObj["openLoadB"] = (bool)status.open_load_b;
        statusObj["overTemp120c"] = (bool)status.over_temperature_120c;
        statusObj["overTemp143c"] = (bool)status.over_temperature_143c;
        statusObj["overTemp150c"] = (bool)status.over_temperature_150c;
        statusObj["overTemp157c"] = (bool)status.over_temperature_157c;
        statusObj["currentScaling"] = status.current_scaling;
        statusObj["stealthChopMode"] = (bool)status.stealth_chop_mode;
        statusObj["standstill"] = (bool)status.standstill;

        // Get global status
        TMC2209::GlobalStatus gstatus = driver.getGlobalStatus();
        JsonObject globalObj = doc["globalStatus"].to<JsonObject>();
        globalObj["reset"] = (bool)gstatus.reset;
        globalObj["drvErr"] = (bool)gstatus.drv_err;
        globalObj["uvCp"] = (bool)gstatus.uv_cp;

        // Get dynamic values
        JsonObject dynamicObj = doc["dynamic"].to<JsonObject>();
        uint32_t tstep = 0;
        const bool tstepValid =
            readTmcRegisterChecked(driverAddress, 0x12, tstep);
        dynamicObj["tstepValid"] = tstepValid;
        if (tstepValid) dynamicObj["tstep"] = tstep & 0x000FFFFFU;
        uint32_t stallGuard = 0;
        const bool stallGuardValid =
            readTmcRegisterChecked(driverAddress, 0x41, stallGuard);
        dynamicObj["stallGuardValid"] = stallGuardValid;
        if (stallGuardValid) {
            dynamicObj["stallGuardResult"] = stallGuard & 0x03FF;
        }
        uint32_t pwmScale = 0;
        const bool pwmScaleValid =
            readTmcRegisterChecked(driverAddress, 0x71, pwmScale);
        dynamicObj["pwmScaleValid"] = pwmScaleValid;
        if (pwmScaleValid) {
            const uint16_t rawScaleAuto = (pwmScale >> 16) & 0x01FFU;
            dynamicObj["pwmScaleSum"] = pwmScale & 0xFFU;
            dynamicObj["pwmScaleAuto"] = (rawScaleAuto & 0x0100U)
                ? static_cast<int16_t>(rawScaleAuto) - 0x0200
                : static_cast<int16_t>(rawScaleAuto);
            dynamicObj["pwmScaleRaw"] = pwmScale;
        }
        uint32_t pwmAuto = 0;
        const bool pwmAutoValid =
            readTmcRegisterChecked(driverAddress, 0x72, pwmAuto);
        dynamicObj["pwmAutoValid"] = pwmAutoValid;
        if (pwmAutoValid) {
            dynamicObj["pwmOffsetAuto"] = pwmAuto & 0xFFU;
            dynamicObj["pwmGradientAuto"] = (pwmAuto >> 16) & 0xFFU;
            dynamicObj["pwmAutoRaw"] = pwmAuto;
        }
        dynamicObj["microstepCounter"] = driver.getMicrostepCounter();
        uint32_t microstepCounter = 0;
        const bool microstepCounterValid =
            readTmcRegisterChecked(driverAddress, 0x6A, microstepCounter);
        dynamicObj["microstepCounterCheckedValid"] = microstepCounterValid;
        if (microstepCounterValid) {
            dynamicObj["microstepCounterChecked"] = microstepCounter & 0x03FF;
        }
    }
}

static void writeDisconnectedDriver(Print& out, const char* name) {
    out.print("{\"name\":\"");
    out.print(name);
    out.print("\",\"connected\":false,\"uartResponseValid\":false,");
    out.print("\"communicating\":false,\"setupOk\":false}");
}

void PolarControl::writeThetaDriverSettings(Print& out) {
    if (!m_driverBusInitialized.load()) {
        out.print("{\"error\":\"Driver UART is not initialized\"}");
        return;
    }
    if (m_state.load() == HOMING) {
        out.print("{\"error\":\"Driver diagnostics unavailable during homing\"}");
        return;
    }
    if (!m_thetaDriverConnected.load()) {
        writeDisconnectedDriver(out, "theta");
        return;
    }
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    JsonDocument doc;
    fillDriverJson(m_tDriver, T_ADDR, "theta", doc);
    serializeJson(doc, out);
    xSemaphoreGive(m_mutex);
}

void PolarControl::writeRhoDriverSettings(Print& out) {
    if (!m_driverBusInitialized.load()) {
        out.print("{\"error\":\"Driver UART is not initialized\"}");
        return;
    }
    if (m_state.load() == HOMING) {
        out.print("{\"error\":\"Driver diagnostics unavailable during homing\"}");
        return;
    }
    if (!m_rhoDriverConnected.load()) {
        writeDisconnectedDriver(out, "rho");
        return;
    }
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    JsonDocument doc;
    fillDriverJson(m_rDriver, R_ADDR, "rho", doc);
    serializeJson(doc, out);
    xSemaphoreGive(m_mutex);
}

void PolarControl::writeRhoCompanionDriverSettings(Print& out) {
    if (!m_driverBusInitialized.load()) {
        out.print("{\"error\":\"Driver UART is not initialized\"}");
        return;
    }
    if (m_state.load() == HOMING) {
        out.print("{\"error\":\"Driver diagnostics unavailable during homing\"}");
        return;
    }
    if (Config::kRhoCompanionMotorEnabled &&
        !m_rhoCompanionDriverConnected.load()) {
        writeDisconnectedDriver(out, "rhoCompanion");
        return;
    }
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    JsonDocument doc;
    fillDriverJson(m_rCDriver, RC_ADDR, "rhoCompanion", doc);
    doc["motorConfigured"] = Config::kRhoCompanionMotorEnabled;
    serializeJson(doc, out);
    xSemaphoreGive(m_mutex);
}

// Parse a coordinate line (theta, rho format)
static bool parseLine(const char* line, float maxRho, PolarCord_t& out) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '\0' || *p == '#') {
        return false;
    }
    if (*p == '/' && *(p + 1) == '/') {
        return false;
    }

    char* end = nullptr;
    float theta = strtof(p, &end);
    if (end == p) {
        return false;
    }
    const char* q = end;
    while (*q == ' ' || *q == '\t') {
        ++q;
    }
    if (*q == ',') {
        ++q;
    }
    while (*q == ' ' || *q == '\t') {
        ++q;
    }
    if (*q == '\0') {
        return false;
    }

    float rho = strtof(q, &end);
    if (end == q) {
        return false;
    }

    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0' && *end != '#') {
        return false;
    }
    if (!std::isfinite(theta) || !std::isfinite(rho) || rho < 0.0f || rho > 1.0f) {
        return false;
    }

    out.theta = theta;
    out.rho = rho * maxRho;
    return true;
}

static bool readLineFromBuffer(File& file, char* buffer, size_t& bufLen, size_t& bufPos, bool& eof,
                               char* lineBuf, size_t lineCap, size_t& lineLen, bool& overflow) {
    lineLen = 0;
    overflow = false;
    while (true) {
        if (bufPos >= bufLen) {
            if (eof) {
                if (lineLen > 0) {
                    lineBuf[lineLen] = '\0';
                    return true;
                }
                return false;
            }
            int readBytes = file.read(reinterpret_cast<uint8_t*>(buffer), 4096);
            if (readBytes <= 0) {
                eof = true;
                if (lineLen > 0) {
                    lineBuf[lineLen] = '\0';
                    return true;
                }
                return false;
            }
            bufLen = static_cast<size_t>(readBytes);
            bufPos = 0;
        }

        char c = buffer[bufPos++];
        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            return true;
        }
        if (c == '\r') {
            continue;
        }
        if (lineLen + 1 < lineCap) {
            lineBuf[lineLen++] = c;
        } else {
            overflow = true;
        }
    }
}

void PolarControl::fileReadTask(void* arg) {
    PolarControl* pc = static_cast<PolarControl*>(arg);
    FileCommand cmd;
    File directFile;
    float directMaxRho = 0.0f;
    bool directActive = false;
    char directBuffer[4096];
    size_t directBufLen = 0;
    size_t directBufPos = 0;
    bool directEof = false;
    char lineBuffer[128];
    size_t lineLen = 0;
    bool lineOverflow = false;
    bool overflowLogged = false;
    char currentFilename[sizeof(cmd.filename)] = {0};
    uint32_t yieldCounter = 0;

    // State for pending line handling
    PolarCord_t pendingPos;
    bool hasPendingPos = false;

    while (true) {
        // Check for commands (non-blocking if reading, blocking if idle)
        // If we have a pending pos, we MUST check for STOP commands but ignore LOAD
        // (though LOAD shouldn't happen while active usually)
        if (xQueueReceive(pc->m_cmdQueue, &cmd, (directActive || hasPendingPos) ? 0 : portMAX_DELAY) == pdTRUE) {
            LOG("FileTask: Received command %d\r\n", cmd.type);
            if (cmd.type == FileCommand::CMD_LOAD) {
                LOG("FileTask: Opening %s with maxRho=%.2f\r\n", cmd.filename, cmd.maxRho);
                strncpy(currentFilename, cmd.filename, sizeof(currentFilename) - 1);
                currentFilename[sizeof(currentFilename) - 1] = '\0';
                overflowLogged = false;
                directFile = SD.open(cmd.filename, FILE_READ);
                if (directFile) {
                    directActive = true;
                    directMaxRho = cmd.maxRho;
                    pc->m_lastFileLine.store(0);
                    pc->m_lastFilePos.store(0);
                    pc->m_lastFileSize.store(static_cast<uint32_t>(directFile.size()));
                    hasPendingPos = false; // Reset pending
                    directBufLen = 0;
                    directBufPos = 0;
                    directEof = false;
                    LOG("FileTask: Direct file open, active=true\r\n");
                } else {
                    LOG("FileTask: Failed to open file\r\n");
                    ErrorLog::instance().log("ERROR", "FILE", "OPEN_FAILED",
                                             "File task failed to open file", cmd.filename);
                    pc->m_fileLoading = false;
                    directActive = false;
                    pc->m_lastFilePos.store(0);
                    pc->m_lastFileSize.store(0);
                }
            } else if (cmd.type == FileCommand::CMD_STOP) {
                LOG("FileTask: Stopping\r\n");
                if (directFile) {
                    directFile.close();
                }
                directActive = false;
                hasPendingPos = false;
                pc->m_fileLoading = false;
                pc->m_lastFilePos.store(0);
                pc->m_lastFileSize.store(0);
                // Clear queue
                PolarCord_t dummy;
                while (xQueueReceive(pc->m_coordQueue, &dummy, 0) == pdTRUE);
            }
        }

        if (directActive) {
            // Only read next line if we don't have one pending
            if (!hasPendingPos) {
                bool hasLine = readLineFromBuffer(
                    directFile,
                    directBuffer,
                    directBufLen,
                    directBufPos,
                    directEof,
                    lineBuffer,
                    sizeof(lineBuffer),
                    lineLen,
                    lineOverflow);
                size_t unreadBytes = 0;
                if (directBufLen >= directBufPos) {
                    unreadBytes = directBufLen - directBufPos;
                }
                size_t filePos = directFile.position();
                size_t consumedPos = (filePos >= unreadBytes) ? (filePos - unreadBytes) : 0;

                if (!hasLine && directEof) {
                    directFile.close();
                    directActive = false;
                    pc->m_fileLoading = false;
                    pc->m_lastFilePos.store(pc->m_lastFileSize.load());
                    LOG("Direct file: EOF reached\r\n");
                } else {
                    pc->m_lastFilePos.store(static_cast<uint32_t>(consumedPos));
                    if (hasLine && lineOverflow && !overflowLogged) {
                        LOG("FileTask: Line overflow, skipping long line\r\n");
                        ErrorLog::instance().log("ERROR", "FILE", "LINE_OVERFLOW",
                                                 "Pattern line exceeded buffer", currentFilename);
                        overflowLogged = true;
                    }
                    if (!lineOverflow && parseLine(lineBuffer, directMaxRho, pendingPos)) {
                        hasPendingPos = true;
                    }
                    // If parse failed (comment/empty), loop continues to read next line
                }
            }

            // If we have a position, try to send it
            if (hasPendingPos) {
                if (xQueueSend(pc->m_coordQueue, &pendingPos, 0) == pdTRUE) {
                    // Success
                    hasPendingPos = false;
                    pc->m_lastFileLine.fetch_add(1);
                    // Throttle if the queue is near full to keep Core 0 responsive.
                    UBaseType_t spaces = uxQueueSpacesAvailable(pc->m_coordQueue);
                    if (spaces <= 64) {
                        vTaskDelay(2);
                    } else {
                        taskYIELD();
                    }
                } else {
                    // Queue full - yield and retry next loop
                    vTaskDelay(2);
                }
            } else {
                // Yield to allow other tasks on Core 0 (like WebServer) to run
                // Only yield if we didn't just push data (to keep throughput high when queue is open)
                vTaskDelay(1);
            }

            // Periodic cooperative yield to avoid starving the webserver.
            if ((++yieldCounter % 16) == 0) {
                vTaskDelay(1);
            }
        }
    }
}
