#pragma once
#include <TMC2209.h>
#include <Print.h>
#include "MotionPlanner.hpp"
#include "Profiler.hpp"
#include <PosGen.hpp>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <memory>
#include <atomic>
#include <array>
#include <vector>

#ifndef NATIVE_BUILD
#include <Config.h>
#endif

// Pin/address definitions (Config.h in firmware, defaults for native builds).
#ifndef NATIVE_BUILD
#define R_STEP_PIN Config::kRhoStepPin
#define R_DIR_PIN Config::kRhoDirPin
#define T_STEP_PIN Config::kThetaStepPin
#define T_DIR_PIN Config::kThetaDirPin
#define RX_PIN Config::kUartRxPin
#define TX_PIN Config::kUartTxPin
#define R_ADDR Config::kRhoDriverAddress
#define RC_ADDR Config::kRhoCDriverAddress
#define T_ADDR Config::kThetaDriverAddress
#elif !defined(R_STEP_PIN)
#define R_STEP_PIN 33
#define R_DIR_PIN 25
#define T_STEP_PIN 32
#define T_DIR_PIN 22
#define RX_PIN 27
#define TX_PIN 26
#define R_ADDR 0
#define RC_ADDR 1
#define T_ADDR 2
#endif

// Tuning settings structures
struct MotionSettings {
  // Historical paired-RHO acoustic speed at a -60 dBFS ceiling (2026-09-09).
  // Requalify for the assembled main-only hardware before claiming that tier.
  float rMaxVelocity = 4.25f;   // mm/s
  float rMaxAccel = 20.0f;      // mm/s²
  float rMaxJerk = 100.0f;      // mm/s³
  // Operator-selected provisional quiet theta profile (2026-09-13).
  // Commissioning builds override this with their quiet-first boot envelope.
  float tMaxVelocity = 0.225f;  // rad/s
  float tMaxAccel = 2.0f;       // rad/s²
  float tMaxJerk = 10.0f;       // rad/s³
};

struct DriverSettings {
  // Persistent UART direction compensation for the installed motor wiring.
  // This is a hardware property, not an acoustic tuning parameter.
  bool inverseMotorDirection = false;

  // Current settings (in mA)
  uint16_t runCurrent = 800;          // Run current in mA
  uint16_t holdCurrent = 400;         // Hold current in mA
  uint8_t holdDelay = 8;              // Delay before switching to hold current (0-15)
  uint8_t powerDownDelay = 20;        // Standstill delay before hold reduction (0-255)
  bool highSensitivityCurrentScale = false; // CHOPCONF.VSENSE: 180mV instead of 325mV

  // Chopper timing. Hysteresis values use the raw CHOPCONF field encoding.
  uint8_t chopperOffTime = 3;       // TOFF while enabled (1-15)
  uint8_t hysteresisStart = 5;      // HSTRT (0-7)
  uint8_t hysteresisEnd = 0;        // HEND register value (0-15 means -3..12)
  uint8_t blankTime = 2;            // TBL (0-3)

  // Microstepping
  uint16_t microsteps = 2;            // Microsteps per full step (1,2,4,8,16,32,64,128,256)
  bool interpolationEnabled = true;   // Interpolate external microsteps to 256

  // StealthChop settings
  bool stealthChopEnabled = true;     // true=StealthChop, false=SpreadCycle
  uint32_t stealthChopThreshold = 0;  // Velocity threshold for StealthChop (0=always)
  uint8_t pwmFrequency = 1;           // PWM_FREQ (0-3)
  uint8_t pwmRegulation = 1;          // PWM_REG (1-15)
  uint8_t pwmLimit = 12;              // PWM_LIM (0-15)
  uint8_t standstillMode = 0;         // FREEWHEEL (0=normal, 1-3 freewheel/braking)
  bool automaticCurrentScaling = true;
  bool automaticGradientAdaptation = true;
  uint8_t pwmOffset = 36;              // Feed-forward offset (0-255)
  uint8_t pwmGradient = 0;             // Feed-forward gradient (0-255)

  // CoolStep settings (current reduction at low load)
  bool coolStepEnabled = false;       // Enable CoolStep
  uint8_t coolStepLowerThreshold = 1; // Lower StallGuard threshold (0-15)
  uint8_t coolStepUpperThreshold = 0; // Upper StallGuard threshold (0-15)
  uint8_t coolStepCurrentIncrement = 0;  // Current increment (0-3: 1,2,4,8)
  uint8_t coolStepMeasurementCount = 0;  // Measurement count (0-3: 32,8,2,1)
  uint32_t coolStepThreshold = 0;     // Velocity threshold for CoolStep
};

struct HomingStatus {
  uint32_t cycle = 0;
  uint16_t stepsPerMm = 0;
  uint8_t phase = 0; // 5=entry probe; 1=outward; 2=coarse; 3=backoff; 4=return
  uint32_t phasePulseCount = 0; // Diagnostic command count, not physical position
  // Primary rho results (legacy field names retained for API compatibility).
  uint32_t fastApproachMs = 0;
  uint32_t slowApproachMs = 0;
  uint32_t fastApproachSteps = 0;
  uint32_t slowApproachSteps = 0;
  uint16_t fastBaseline = 0;
  uint16_t fastTrigger = 0;
  uint16_t baseline = 0;
  uint16_t trigger = 0;
  uint32_t companionFastApproachMs = 0;
  uint32_t companionSlowApproachMs = 0;
  uint32_t companionFastApproachSteps = 0;
  uint32_t companionSlowApproachSteps = 0;
  uint16_t companionFastBaseline = 0;
  uint16_t companionFastTrigger = 0;
  uint16_t companionBaseline = 0;
  uint16_t companionTrigger = 0;
  uint32_t uartSamples = 0;
  uint32_t validUartSamples = 0;
  uint8_t failure = 0;
  uint8_t failedAxis = 0;  // 0=none, 1=rho, 2=rho-companion
};

struct HomingSettings {
  // Lower percentages require a larger SG_RESULT drop and are less sensitive.
  uint8_t triggerPercent = 85;
  // Number of low SG_RESULT votes required in a 2*N-1 fresh-full-step window.
  uint8_t consecutiveSamples = 5;
  uint16_t minimumTravelMs = 450;
  uint16_t verificationBackoffMm = 6;
};

struct HomingTraceSample {
  uint32_t elapsedMs = 0;
  uint32_t steps = 0;
  int32_t legOriginSteps = 0; // Inward-positive ledger origin for this leg.
  uint16_t stallGuard = 0;
  uint16_t contactBaseline = 0; // Nonzero only on a detector stop marker.
  uint16_t contactThreshold = 0;
  uint8_t axis = 0;   // 1=rho, 2=rho-companion
  uint8_t pass = 0;   // 1=coarse, 2..maximumContactAttempts=verification
  uint8_t phase = 0;  // 1=runway, 2=coarse, 3=backoff, 4=precision
  bool valid = false;
};

struct DriverAvailability {
  bool theta = false;
  bool rho = false;
  bool rhoCompanion = false;

  bool thetaAxis() const { return theta; }
  bool rhoAxis() const { return rho || rhoCompanion; }
};

enum class TuningUpdateResult : uint8_t {
  UPDATED,
  REJECTED,
  DRIVER_VERIFY_FAILED,
  SAVE_FAILED
};

class PolarControl {
public:
  enum State_t : uint8_t {
    UNINITIALIZED,
    INITIALIZED,
    IDLE,
    RUNNING,
    PAUSED,
    STOPPING,
    CLEARING,
    PREPARING,
    HOMING,
    HOMING_REVIEW,
    HOMING_FAILED
  };

  PolarControl();
  ~PolarControl();

  // Lifecycle
  bool begin();
  bool setupDrivers();
  // confirmedOriginBounded is commissioning-only. It limits each approach to
  // the known outward runway plus the configured per-pass allowance, so a missed SG trigger cannot grind
  // against the stop for an entire unknown-position travel span.
  bool home(bool confirmedOriginBounded = false, bool automaticBoot = false);
  bool confirmHome(bool successful);
  // Accept the current physical location as theta=0, rho=0 without moving.
  // The caller must require explicit operator confirmation first.
  bool setCurrentPositionAsHome();
  HomingStatus getHomingStatus() const;
  size_t getHomingTrace(HomingTraceSample* output, size_t capacity,
                        size_t* total = nullptr) const;
  DriverAvailability getDriverAvailability() const;
#if defined(SISYPHUS_BENCH_MOTION_TEST) || defined(SISYPHUS_THETA_COMMISSIONING) || defined(SISYPHUS_RHO_COMMISSIONING)
  // Test/commissioning convenience wrapper around setCurrentPositionAsHome().
  void assumeBenchTestOrigin();
#endif
#ifdef SISYPHUS_RHO_COMMISSIONING
  // Enter relative-jog service mode without claiming an absolute rho origin.
  // The web layer must ensure motion is already stopped before calling this.
  void enterRhoManualServiceMode();
  // Commissioning-only recovery/qualification entrypoint. Each motor gets an
  // independent known-distance cap plus the normal one-millimetre tolerance.
  bool homeFromKnownRhoPositions(float rhoStartMm,
                                 float companionStartMm);
#endif

  // Pattern control
  bool start(std::unique_ptr<PosGen> posGen);
  // Move directly to one polar coordinate using the same coordinated planner
  // as patterns. The supplied angle may be wrapped; the shortest equivalent
  // theta move is selected from the current position.
  bool moveTo(float theta, float rho);
  // Relative single-axis jogs are also allowed before homing. Unhomed rho
  // moves use a temporary logical midpoint so both directions remain usable;
  // completing the jog does not promote the controller to IDLE/homed.
  bool jogRelative(float thetaDelta, float rhoDelta);
  bool startClearing(std::unique_ptr<PosGen> posGen);
  bool loadAndRunFile(String filePath);
  bool loadAndRunFile(String filePath, float maxRho);
  bool pause();
  bool resume();
  bool stop();
  void emergencyStop(bool disableRho = false);

  // Main processing loop - call from motor task
  bool processNextMove();

  // Getters/Setters
  State_t getState();
  void setSpeed(uint8_t speed);
  uint8_t getSpeed() const { return m_speed.load(); }
  PolarCord_t getCurrentPosition() const;
  PolarCord_t getActualPosition();
  PolarVelocity_t getActualVelocity();
  uint32_t getSegmentsCompleted() const;
  float getMaxRho() const { return R_MAX; }
  int getProgressPercent() const;

  void getDiagnostics(uint32_t& queueDepth, uint32_t& underruns) const;
  void getProfileData(uint32_t& maxProcessUs, uint32_t& maxIntervalUs, uint32_t& avgGenUs);
  void getTelemetry(PlannerTelemetry& telemetry);
  void getMutexWaitProfile(uint32_t& maxWaitUs, uint32_t& avgWaitUs) {
    maxWaitUs = m_mutexWaitProfiler.getMax();
    avgWaitUs = m_mutexWaitProfiler.getAvg();
    m_mutexWaitProfiler.reset();
  }
  uint32_t getCoordQueueDepth() const {
    return m_coordQueue ? static_cast<uint32_t>(uxQueueMessagesWaiting(m_coordQueue)) : 0;
  }
  uint32_t getLastFileLine() const { return m_lastFileLine.load(); }
  uint32_t getFileTaskHighWater() const;

  // Reset theta to zero (current position becomes new origin)
  void resetTheta();

  // Tuning getters/setters
  MotionSettings getMotionSettings() const;

  DriverSettings getThetaDriverSettings() const;
  DriverSettings getRhoDriverSettings() const;
  HomingSettings getHomingSettings() const;

  // Validate and atomically persist a settings group. Driver updates are
  // UART-verified first and rolled back if verification or flash commit fails.
  TuningUpdateResult saveMotionSettings(const MotionSettings& settings);
  TuningUpdateResult saveThetaDriverSettings(const DriverSettings& settings);
  TuningUpdateResult saveRhoDriverSettings(const DriverSettings& settings);
  TuningUpdateResult saveHomingSettings(const HomingSettings& settings);

  // Settings persistence
  bool saveTuningSettings();
  bool loadTuningSettings();
  bool tuningPersistenceAvailable() const { return m_tuningStorageReady.load(); }

  // Motor stress tests (blocking calls - run from main task)
  bool testThetaContinuous();
  bool testThetaStress();
  bool testThetaSegment(float targetThetaRad);
  bool testRhoContinuous();
  bool testRhoStress();
  bool testRhoSegment(float targetRhoMm);

  // Driver diagnostics
  void writeThetaDriverSettings(Print& out);
  void writeRhoDriverSettings(Print& out);
  void writeRhoCompanionDriverSettings(Print& out);

private:
  struct FileCommand {
      enum Type { CMD_LOAD, CMD_STOP } type;
      char filename[64];
      float maxRho;
  };

  static void fileReadTask(void* arg);
  static void homingTask(void* arg);

  // Physical constants
  // Measured usable radial stroke of the final loaded mechanism (2026-09-09).
  static constexpr float R_MAX = 425.0f;

  // These are calculated based on current microstep settings
  inline int getStepsPerMm() const { return 50 * m_rDriverSettings.microsteps; }
  inline int getStepsPerRadian() const { return (int)((200.0 * m_tDriverSettings.microsteps / (2.0 * PI)) * (60.0 / 16.0)); }

  // Tuning settings (runtime changeable)
  MotionSettings m_motionSettings;
  DriverSettings m_tDriverSettings;  // Theta driver
  DriverSettings m_rDriverSettings;  // Rho driver
  HomingSettings m_homingSettings;
  std::atomic<bool> m_tuningStorageReady{false};

  // Motion planner with lookahead and S-curves
  MotionPlanner m_planner;

  // TMC2209 drivers for configuration and homing
  TMC2209 m_tDriver;
  TMC2209 m_rDriver;
  TMC2209 m_rCDriver;

  mutable SemaphoreHandle_t m_mutex = NULL;
  Profiler m_mutexWaitProfiler;
  
  // Async File Reading
  QueueHandle_t m_coordQueue = NULL;
  QueueHandle_t m_cmdQueue = NULL;
  TaskHandle_t m_fileTaskHandle = NULL;
  std::atomic<bool> m_fileLoading{false};
  std::atomic<uint32_t> m_lastFileLine{0};
  std::atomic<uint32_t> m_lastFilePos{0};
  std::atomic<uint32_t> m_lastFileSize{0};

  std::atomic<State_t> m_state{UNINITIALIZED};
  std::atomic<State_t> m_motionCompletionState{IDLE};
  std::atomic<bool> m_driverBusInitialized{false};
  std::atomic<bool> m_thetaDriverConnected{false};
  std::atomic<bool> m_rhoDriverConnected{false};
  std::atomic<bool> m_rhoCompanionDriverConnected{false};
#if defined(SISYPHUS_THETA_COMMISSIONING) || defined(SISYPHUS_RHO_COMMISSIONING)
  std::atomic<bool> m_commissioningStartPermit{false};
#endif
  std::atomic<uint32_t> m_homingCycle{0};
  std::atomic<uint16_t> m_homingStepsPerMm{0};
  std::atomic<uint32_t> m_homingFastApproachMs{0};
  std::atomic<uint32_t> m_homingSlowApproachMs{0};
  std::atomic<uint32_t> m_homingFastApproachSteps{0};
  std::atomic<uint32_t> m_homingSlowApproachSteps{0};
  std::atomic<uint16_t> m_homingFastBaseline{0};
  std::atomic<uint16_t> m_homingFastTrigger{0};
  std::atomic<uint16_t> m_homingBaseline{0};
  std::atomic<uint16_t> m_homingTrigger{0};
  std::atomic<uint32_t> m_homingCompanionFastApproachMs{0};
  std::atomic<uint32_t> m_homingCompanionSlowApproachMs{0};
  std::atomic<uint32_t> m_homingCompanionFastApproachSteps{0};
  std::atomic<uint32_t> m_homingCompanionSlowApproachSteps{0};
  std::atomic<uint16_t> m_homingCompanionFastBaseline{0};
  std::atomic<uint16_t> m_homingCompanionFastTrigger{0};
  std::atomic<uint16_t> m_homingCompanionBaseline{0};
  std::atomic<uint16_t> m_homingCompanionTrigger{0};
  std::atomic<uint32_t> m_homingUartSamples{0};
  std::atomic<uint32_t> m_homingValidUartSamples{0};
  std::atomic<uint8_t> m_homingFailure{0};
  std::atomic<uint8_t> m_homingFailedAxis{0};
  static constexpr size_t kHomingTraceCapacity = 768;
  std::array<HomingTraceSample, kHomingTraceCapacity> m_homingTrace{};
  std::atomic<size_t> m_homingTraceCount{0};
  std::atomic<size_t> m_homingTraceTotal{0};
  std::atomic<uint32_t> m_homingTraceStartedAtMs{0};
  std::atomic<uint8_t> m_homingTraceAxis{0};
  std::atomic<uint8_t> m_homingTracePhase{0};
  std::atomic<uint8_t> m_homingTracePass{0};
  std::atomic<int32_t> m_homingTraceLegOrigin{0};
  std::atomic<bool> m_confirmedOriginBoundedHoming{false};
  std::atomic<bool> m_bootHomingCancelled{false};
#ifdef SISYPHUS_RHO_COMMISSIONING
  std::atomic<bool> m_knownPositionHomingActive{false};
  std::atomic<int32_t> m_knownRhoStartSteps{0};
  std::atomic<int32_t> m_knownCompanionStartSteps{0};
#endif
  TaskHandle_t m_homingTaskHandle = NULL;
  std::unique_ptr<PosGen> m_posGen;

  // Speed setting: 1-10
  std::atomic<uint8_t> m_speed{5};
  bool m_clearingSpeedActive = false;
  bool m_pauseAfterStop = false;
  bool m_restartAfterSpeedChange = false;
  bool m_speedUpdatePending = false;
  std::vector<PolarCord_t> m_resumePoints;
  size_t m_resumePointIndex = 0;

  // Helpers
  void updateSpeedSettings();
  void capturePendingTargetsForResume();
  void feedPlanner();
  bool writeTuningSettingsLocked(const MotionSettings& motionSettings,
                                 const DriverSettings& thetaSettings,
                                 const DriverSettings& rhoSettings,
                                 const HomingSettings& homingSettings);

  // Driver setup and homing
  bool applyDriverSettings(TMC2209 &driver, const DriverSettings &settings,
                           uint8_t driverAddress, const char* driverName);
  // Requires m_mutex. Attempts to disable both rho stages and verifies every
  // driver that still answers on UART. A false result is still fail-closed to
  // the extent allowed by the available UART links.
  bool disableRhoDriversLocked();
  // Requires m_mutex. Restores the normal profile after fail-closed homing or
  // an emergency stop, and verifies fitted bridges before a manual jog.
  bool prepareRhoDriversForManualJogLocked();
  // Requires m_mutex. A non-zero VACTUAL makes the addressed TMC2209 ignore
  // shared STEP/DIR pulses while its energized bridge holds the mechanism.
  bool startInactiveRhoHoldLocked(uint8_t driverAddress,
                                  uint16_t targetPhase);
  bool serviceInactiveRhoHoldLocked();
  void clearInactiveRhoHoldLocked();
  void recordHomingSample(bool valid, uint16_t stallGuard,
                         uint16_t contactBaseline = 0,
                         uint16_t contactThreshold = 0);
  struct HomingAttempt {
    bool success = false;
    bool communicationError = false;
    uint32_t elapsedMs = 0;
    uint32_t steps = 0;
    uint16_t baseline = 0;
    uint16_t trigger = 0;
    uint16_t threshold = 0;
  };
  struct HomingMove {
    bool success = false;
    bool communicationError = false;
    bool loadRecovered = false;
    uint32_t elapsedMs = 0;
    uint32_t steps = 0;
    uint16_t peakStallGuard = 0;
    uint8_t healthySecondHalfSamples = 0;
  };
  bool rampRhoStepRate(int8_t direction, uint32_t targetStepsPerSecond,
                       uint32_t maxSteps, uint32_t rampMs);
    HomingAttempt approachHome(uint8_t driverAddress,
                              uint32_t stepsPerSecond, uint32_t maxSteps,
                              uint32_t minimumTravelSteps,
                              uint8_t requiredSamples,
                              float triggerRatio,
                              uint16_t externalStepsPerFullStep);
  HomingMove moveRhoBySteps(uint8_t driverAddress, int8_t direction,
                            uint32_t stepsPerSecond, uint32_t stepCount,
                            uint16_t recoveryThreshold = 0);
  bool restoreHeldDriverPhase(TMC2209& driver, uint8_t driverAddress,
                              uint16_t targetPhase, uint16_t microsteps,
                              const char* driverName);
  bool homeAxis(TMC2209& activeDriver, uint8_t activeAddress,
                const char* activeName, TMC2209& inactiveDriver,
                uint8_t inactiveAddress, const char* inactiveName,
                bool companionAxis,
                const DriverSettings& homingSettings);
  bool homeDrivers();

  uint8_t m_inactiveRhoHoldAddress = UINT8_MAX;
  uint16_t m_inactiveRhoHoldTargetPhase = 0;
  uint32_t m_inactiveRhoHoldLastToggleMs = 0;
  int8_t m_inactiveRhoHoldDirection = 1;
};
