// Host harness for the production controller methods extracted by test_rho_jog.py.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "../../../lib/PolarControl/src/RhoPositionTracker.hpp"
#define PI 3.14159265358979323846
#define R_MAX 425
#define R_ADDR 0
#define RC_ADDR 1
#define LOG(...) ((void)0)
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
static uint32_t nowMs = 200000;
uint32_t millis() { return nowMs; }
uint32_t micros() { return nowMs * 1000; }
int xSemaphoreTake(int,int) { return 1; }
void xSemaphoreGive(int) {}
void xQueueOverwrite(int,const void*) {}
struct SemaphoreGuard { explicit SemaphoreGuard(int) {} };
namespace Config { constexpr uint32_t kRhoHomingCycleTimeoutMs=90000; }
struct ErrorLog { static ErrorLog& instance() { static ErrorLog e; return e; }
    template<class... T> void log(T...) {} };
struct Profiler { void addSample(uint32_t) {} };
struct SingleTargetGen { double theta,rho; SingleTargetGen(double t,double r):theta(t),rho(r){} };
namespace std_patch { using std::make_unique; }
struct Planner {
    double theta=0.2,rho=50,targetTheta=0,targetRho=0;
    bool idle=true, complete=false;
    void getCurrentPosition(double& t,double& r) { t=theta;r=rho; }
    void resetPosition(double t,double r) { assert(idle);theta=t;rho=r; }
    void stop(){idle=true;}
    void start(){idle=false;}
    void stopGracefully(){idle=true;}
    void discardResume(){}
    void resetCompletedCount(){}
    void process(){if(complete){theta=targetTheta;rho=targetRho;idle=true;complete=false;}}
    bool isIdle(){return idle;}
    bool isRunning(){return !idle;}
};
static uint32_t regs[2][128]{};
static int readFail=-1, writeFail=-1;
static unsigned writes=0;
bool readTmcRegisterChecked(uint8_t a,uint8_t r,uint32_t& v) {
    if (r==readFail) return false; v=regs[a][r];return true;
}
bool writeTmcRegisterAcknowledged(uint8_t a,uint8_t r,uint32_t v) {
    ++writes;if(r==writeFail)return false;regs[a][r]=v;return true;
}
bool setDriverMicrostepsChecked(uint8_t a,uint16_t steps) {
    return writeTmcRegisterAcknowledged(a,0x6c,steps);
}
struct FakeDriver { void moveAtVelocity(int) {} };
struct FileCommand { enum {CMD_STOP};int type; };
struct PolarControl {
    enum State_t {UNINITIALIZED,INITIALIZED,IDLE,RUNNING,PAUSED,STOPPING,CLEARING,PREPARING,HOMING,HOMING_REVIEW,HOMING_FAILED};
    using RhoJogMotor=RhoPositionTracker::Motor;
    RhoPositionTracker m_rhoPosition{212.5};
    RhoJogMotor m_jogMotor=RhoJogMotor::BOTH;
    void sampleRhoPositionLocked();
    RhoPositionTracker::Sample getRhoPositionEstimate();
    bool jogRelative(float,float,RhoJogMotor=RhoJogMotor::BOTH);
    bool startInactiveRhoHoldLocked(uint8_t,uint16_t);
    bool serviceInactiveRhoHoldLocked();
    bool restoreInactiveRhoInterfaceLocked(uint8_t,uint16_t);
    bool finishIndependentRhoJogLocked();
    void clearInactiveRhoHoldLocked();
    bool processNextMove();bool stop();void emergencyStop(bool=false);
    bool prepareRhoDriversForManualJogLocked(){return true;}
    bool disableRhoDriversLocked(){disabled=true;return true;}
    void feedPlanner(){if(m_posGen){m_planner.targetTheta=m_posGen->theta;m_planner.targetRho=m_posGen->rho;}}
    void updateSpeedSettings(){}
    int m_mutex=0,m_cmdQueue=1; void* m_homingTaskHandle=nullptr;
    Planner m_planner;
    FakeDriver m_tDriver,m_rDriver,m_rCDriver;
    std::unique_ptr<SingleTargetGen> m_posGen;
    std::vector<int> m_resumePoints;size_t m_resumePointIndex=0;
    std::atomic<State_t> m_state{INITIALIZED},m_motionCompletionState{IDLE};
    std::atomic<bool> m_independentRhoJog{false},m_thetaDriverConnected{true},m_rhoDriverConnected{true},m_rhoCompanionDriverConnected{true},m_bootHomingCancelled{false},m_driverBusInitialized{true};
    bool m_rhoManualProfileDirty=false,disabled=false,m_speedUpdatePending=false,m_restartAfterSpeedChange=false,m_pauseAfterStop=false,m_clearingSpeedActive=false,m_fileLoading=false;
    std::atomic<int> m_fileReadyGeneration{0},m_fileGeneration{0};
    Profiler m_mutexWaitProfiler;
    struct {uint16_t microsteps=8;} m_rDriverSettings;
    double m_jogSavedTheta=0,m_jogSavedRho=0;
    uint8_t m_inactiveRhoHoldAddress=UINT8_MAX;
    uint16_t m_inactiveRhoHoldTargetPhase=0;
    uint32_t m_inactiveRhoHoldGconf=0,m_inactiveRhoHoldLastToggleMs=0;
    int8_t m_inactiveRhoHoldDirection=1;
    std::atomic<uint32_t> m_homingTraceStartedAtMs{0};
};
#include "controller_methods.inc"
static void resetBus(){readFail=writeFail=-1;writes=0;for(auto& motor:regs){for(auto& r:motor)r=0;motor[0]=0x1c8;motor[0x6a]=100;motor[0x6c]=8;}}
int main(){
    using C=PolarControl;using M=C::RhoJogMotor;
    {
        RhoPositionTracker tracker(212.5);
        assert(!tracker.get().referenced);
        tracker.rebase(212.5);
        tracker.sample(222.5,M::COMPANION,true,true);
        assert(tracker.get().mainRho==212.5 && tracker.get().companionRho==222.5);
        tracker.rebase(50); // end of independent jog: logical reset is not motion
        tracker.sample(40,M::MAIN,true,true);
        assert(tracker.get().mainRho==202.5 && tracker.get().companionRho==222.5);
        tracker.sample(45,M::BOTH,true,true);
        assert(tracker.get().mainRho==207.5 && tracker.get().companionRho==227.5);
        tracker.invalidate();assert(!tracker.get().available);
        tracker.sample(90,M::BOTH,true,true); // homing has its own unknown origin
        tracker.home();assert(tracker.get().referenced);
        tracker.sample(20,M::BOTH,true,true);
        assert(tracker.get().mainRho==20 && tracker.get().companionRho==20);
        tracker.sample(25,M::BOTH,true,false);
        assert(tracker.get().companionRho==20);
    }
    for(auto motor:{M::MAIN,M::COMPANION}) {
        for(auto state:{C::IDLE,C::INITIALIZED,C::HOMING_FAILED}) {
            for(int end=0;end<5;++end) {
                resetBus();C c;c.m_state=state;
                assert(c.jogRelative(0,-10,motor));
                int inactive=motor==M::MAIN?1:0,active=1-inactive;
                assert(c.m_inactiveRhoHoldAddress==inactive);
                assert(regs[inactive][0x22]==1 && regs[active][0x22]==0);
                assert(regs[inactive][0x6c]==256 && regs[active][0x6c]==8);
                assert((regs[inactive][0]&8)==0);
                assert(c.m_planner.targetRho==202.5);
                // Manual hold must work long after the boot homing deadline.
                assert(c.serviceInactiveRhoHoldLocked());
                if(end==0){c.m_planner.complete=true;c.processNextMove();}
                if(end==1){c.stop();}
                if(end==2){c.emergencyStop(true);}
                if(end==3){c.m_state=C::STOPPING;c.m_planner.idle=true;c.processNextMove();}
                if(end==4){c.m_state=C::PAUSED;c.m_planner.idle=true;c.stop();}
                assert(!c.m_independentRhoJog && c.m_state==C::INITIALIZED);
                assert(regs[inactive][0x22]==0 && regs[inactive][0x6c]==8);
                assert(regs[inactive][0]==0x1c8);
                assert(c.m_planner.rho==50 && c.m_planner.theta==0.2);
            }
        }
    }
    // A paired jog never isolates a motor or invalidates an existing home.
    resetBus();{C c;c.m_state=C::IDLE;assert(c.jogRelative(0,10));assert(!c.m_independentRhoJog);assert(writes==0);c.m_planner.complete=true;c.processNextMove();assert(c.m_state==C::IDLE);assert(c.m_planner.rho==60);}
    // Wrong axis, missing target driver, and busy state cannot start motion.
    resetBus();{C c;assert(!c.jogRelative(1,0,M::MAIN));c.m_rhoCompanionDriverConnected=false;assert(!c.jogRelative(0,1,M::COMPANION));c.m_state=C::HOMING;assert(!c.jogRelative(0,1,M::MAIN));assert(writes==0);}
    // Failed isolation writes do not start the planner and force profile recovery.
    for(int reg:{0,0x6c,0x22}){resetBus();C c;writeFail=reg;assert(!c.jogRelative(0,1,M::MAIN));assert(c.disabled && c.m_planner.idle && !c.m_independentRhoJog && c.m_rhoManualProfileDirty);}
    // Hold read failure aborts an active jog and disables both bridges.
    resetBus();{C c;assert(c.jogRelative(0,1,M::MAIN));readFail=0x6a;nowMs+=300;c.processNextMove();assert(c.disabled && c.m_planner.idle && c.m_state==C::INITIALIZED && !c.m_independentRhoJog);}
    // Excessive inactive phase drift must not silently restore normal operation.
    resetBus();{C c;assert(c.jogRelative(0,1,M::MAIN));regs[1][0x6a]+=10;c.m_planner.complete=true;c.processNextMove();assert(c.disabled && c.m_rhoManualProfileDirty);}
    // Microstep/direction restoration failures also force full profile recovery.
    for(int reg:{0,0x6c,0x22}){resetBus();C c;assert(c.jogRelative(0,1,M::MAIN));writeFail=reg;c.stop();assert(c.disabled && c.m_rhoManualProfileDirty && c.m_state==C::INITIALIZED);}
}
