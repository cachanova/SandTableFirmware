#pragma once

// Commanded-step estimate, never encoder feedback. Logical recentering for
// unhomed jogging must not move either cursor; only executed deltas do.
class RhoPositionTracker {
public:
    enum class Motor { BOTH, MAIN, COMPANION };
    struct Sample { double mainRho, companionRho; bool available, referenced; };
    explicit RhoPositionTracker(double midpoint) : midpoint_(midpoint), main_(midpoint), companion_(midpoint) {}
    void sample(double plannerRho, Motor motor, bool mainConnected, bool companionConnected) {
        const double delta = plannerRho - last_;
        last_ = plannerRho;
        if (!available_) return;
        if (mainConnected && motor != Motor::COMPANION) main_ += delta;
        if (companionConnected && motor != Motor::MAIN) companion_ += delta;
    }
    void rebase(double plannerRho) { last_ = plannerRho; }
    void home() { main_=companion_=last_=0; available_=referenced_=true; }
    void invalidate() { available_=referenced_=false; }
    void beginRelative(double plannerRho) {
        if (!available_) { main_=companion_=midpoint_; available_=true; }
        last_=plannerRho;
    }
    Sample get() const { return {main_,companion_,available_,referenced_}; }
private:
    double midpoint_, main_, companion_, last_=0;
    bool available_=true, referenced_=false;
};
