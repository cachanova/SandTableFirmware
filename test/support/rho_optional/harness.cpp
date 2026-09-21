#include <atomic>
#include <cassert>
#include <cstdint>
#include <vector>
#define R_ADDR 0
#define RC_ADDR 1
#define LOG(...) ((void)0)
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
void xSemaphoreTake(int, int) {}
void xSemaphoreGive(int) {}
void delayMicroseconds(int) {}
void vTaskDelay(int) {}
struct ErrorLog {
    static ErrorLog& instance() { static ErrorLog e; return e; }
    template<class... T> void log(T...) {}
};
namespace Config {
#ifdef TEST_CW_DISABLED
constexpr bool kRhoCompanionMotorEnabled = false;
#else
constexpr bool kRhoCompanionMotorEnabled = true;
#endif
constexpr uint16_t kRhoHomingRunCurrentMa = 500, kRhoHomingHoldCurrentMa = 500;
constexpr uint16_t kRhoHomingMicrosteps = 8;
}
#include "settings.inc"
static bool responding[2];
static unsigned reads[2], writes[2];
static uint32_t regs[2][128];
static int failAxis = -1, failProfile = -1, failReadAddress = -1, failReadRegister = -1;
static bool cancelAfterContact = false;
static std::vector<int> order;
static uint8_t microstepsToMres(uint16_t steps) {
    uint8_t result = 8;
    while (steps > 1) { --result; steps >>= 1; }
    return result;
}
bool readTmcRegisterChecked(uint8_t a, uint8_t r, uint32_t& v) {
    ++reads[a];
    if (!responding[a] || (a == failReadAddress && r == failReadRegister)) return false;
    v = regs[a][r]; return true;
}
bool writeTmcRegisterAcknowledged(uint8_t a, uint8_t r, uint32_t v) {
    ++writes[a]; if (!responding[a]) return false;
    regs[a][r] = v; return true;
}
struct TMC2209 {
    uint8_t address;
    bool isSetupAndCommunicating() { ++reads[address]; return responding[address]; }
    void moveUsingStepDirInterface() { writeTmcRegisterAcknowledged(address, 0x22, 0); }
};
bool setDriverEnabled(TMC2209&, uint8_t a, const DriverSettings&, bool enabled) {
    return writeTmcRegisterAcknowledged(a, 0x6c,
        (regs[a][0x6c] & ~15U) | (enabled ? 3U : 0U));
}
bool disableDriverMotion(TMC2209& driver, uint8_t a, const DriverSettings& settings) {
    bool stopped = writeTmcRegisterAcknowledged(a, 0x22, 0);
    bool off = setDriverEnabled(driver, a, settings, false);
    uint32_t value = 0;
    return readTmcRegisterChecked(a, 0x6c, value) && stopped && off && !(value & 15U);
}
bool setDriverMicrostepsChecked(uint8_t a, uint16_t steps) {
    return writeTmcRegisterAcknowledged(a, 0x6c,
        (regs[a][0x6c] & ~0x0f000000U) | (uint32_t(microstepsToMres(steps)) << 24));
}
struct PolarControl {
    enum State { INITIALIZED, HOMING, IDLE };
    int m_mutex = 0;
    std::atomic<State> m_state{HOMING};
    std::atomic<bool> m_rhoDriverPresent{true}, m_rhoDriverConnected{true};
    std::atomic<bool> m_rhoCompanionDriverPresent{false}, m_rhoCompanionDriverConnected{false};
    std::atomic<unsigned> m_homingFailure{0}, m_homingFailedAxis{0}, m_homingStepsPerMm{0};
    bool m_rhoManualProfileDirty = false;
    DriverSettings m_rDriverSettings;
    TMC2209 m_rDriver{0}, m_rCDriver{1};
    struct { void stopRhoHoming() {} } m_planner;
    bool disableRhoDriversLocked();
    bool prepareRhoDriversForManualJogLocked();
    bool homeDrivers();
    bool homeAxis(TMC2209&, uint8_t, const char*, TMC2209&, uint8_t,
                  const char*, bool, const DriverSettings&);
    bool applyDriverSettings(TMC2209&, const DriverSettings& s, uint8_t a, const char*) {
        if (a == failProfile) return false;
        return setDriverMicrostepsChecked(a, s.microsteps);
    }
    bool startInactiveRhoHoldLocked(uint8_t a, uint16_t) {
        return setDriverMicrostepsChecked(a, 256) && writeTmcRegisterAcknowledged(a, 0x22, 1);
    }
    void clearInactiveRhoHoldLocked() {}
    bool restoreHeldDriverPhase(TMC2209&, uint8_t a, uint16_t, uint16_t steps, const char*) {
        return writeTmcRegisterAcknowledged(a, 0x22, 0) && setDriverMicrostepsChecked(a, steps);
    }
};
#include "controller_methods.inc"
static void reset(bool cw) {
    failAxis = failProfile = failReadAddress = failReadRegister = -1;
    cancelAfterContact = false; order.clear();
    for (int a = 0; a < 2; ++a) {
        responding[a] = a == 0 || cw;
        reads[a] = writes[a] = 0;
        for (auto& reg : regs[a]) reg = 0;
        regs[a][6] = 0x21000000;
        regs[a][0x6c] = (5U << 24) | 3U;
        regs[a][0x6a] = 16;
    }
}
static void fitted(PolarControl& c, bool cw) {
    c.m_rhoCompanionDriverPresent = cw;
    c.m_rhoCompanionDriverConnected = cw && Config::kRhoCompanionMotorEnabled;
    c.m_rDriverSettings.microsteps = 8;
    if (cw && !Config::kRhoCompanionMotorEnabled) regs[1][0x6c] &= ~15U;
}
int main() {
    // An empty CW socket never participates in homing, restoration, or stop.
    reset(false); { PolarControl c; fitted(c, false);
        assert(c.homeDrivers()); assert((order == std::vector<int>{0}));
        assert(reads[1] == 0 && writes[1] == 0); assert(regs[0][0x6c] & 15U);
        assert(c.disableRhoDriversLocked()); assert(!(regs[0][0x6c] & 15U));
        assert(reads[1] == 0 && writes[1] == 0);
    }
    // When fitted, CW homes first; an intentionally disabled CW stays off.
    reset(true); { PolarControl c; fitted(c, true);
        assert(c.homeDrivers());
        assert(order == (Config::kRhoCompanionMotorEnabled
            ? std::vector<int>{1, 0} : std::vector<int>{0}));
        assert(bool(regs[1][0x6c] & 15U) == Config::kRhoCompanionMotorEnabled);
    }
    // Present-at-startup drivers cannot silently disappear during a cycle.
    reset(true); { PolarControl c; fitted(c, true); responding[1] = false;
        assert(!c.homeDrivers()); assert(order.empty());
    }
    if (Config::kRhoCompanionMotorEnabled) {
        reset(true); { PolarControl c; fitted(c, true);
            c.m_rhoCompanionDriverConnected = false;
            assert(!c.homeDrivers()); assert(order.empty());
            assert(!c.prepareRhoDriversForManualJogLocked());
        }
        reset(true); { PolarControl c; fitted(c, true); failReadAddress=0; failReadRegister=0x6a;
            assert(!c.homeDrivers()); assert(order.empty()); // Inactive phase read failed.
        }
    }
    for (bool cw : {false, true}) {
        // Normal jog and recovery after aborted/failed homing ignore only absent drivers.
        for (bool dirty : {false, true}) {
            reset(cw); PolarControl c; fitted(c, cw); c.m_rhoManualProfileDirty = dirty;
            assert(c.prepareRhoDriversForManualJogLocked()); assert(!c.m_rhoManualProfileDirty);
            if (!cw) assert(reads[1] == 0 && writes[1] == 0);
        }
        reset(cw); { PolarControl c; fitted(c, cw); failAxis = 0;
            assert(!c.homeDrivers()); assert(!(regs[0][0x6c] & 15U));
            if (cw) assert(!(regs[1][0x6c] & 15U));
        }
        reset(cw); { PolarControl c; fitted(c, cw); failProfile = 0;
            assert(!c.homeDrivers()); assert(order.empty()); assert(!(regs[0][0x6c] & 15U));
        }
        reset(cw); { PolarControl c; fitted(c, cw); cancelAfterContact = true;
            c.homeDrivers(); assert(!(regs[0][0x6c] & 15U));
            if (cw) assert(!(regs[1][0x6c] & 15U));
        }
    }
}
