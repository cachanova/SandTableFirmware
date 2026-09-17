#pragma once
#include <cstdint>
#include <cmath>

// S-curve (7-segment) velocity profile for jerk-limited motion
//
// The profile consists of 7 phases:
// 1: Jerk+ (acceleration increasing)
// 2: Constant acceleration
// 3: Jerk- (acceleration decreasing to 0)
// 4: Cruise (constant velocity)
// 5: Jerk- (deceleration increasing)
// 6: Constant deceleration
// 7: Jerk+ (deceleration decreasing to 0)
//
// Some phases may have zero duration depending on the move.

class SCurve {
public:
    struct Profile {
        // Phase durations (seconds)
        double t[7];

        // Phase end times (cumulative)
        double tEnd[7];

        // Phase end positions (cumulative)
        double posEnd[7];

        // Velocities at phase boundaries
        double v[8]; // v[0] = start, v[7] = end

        // Accelerations at phase boundaries
        double a[8];

        // Total time and distance
        double totalTime;
        double totalDistance;

        // Constraints used
        double jerk;
        double maxAccel;
        double maxVelocity;
    };

    // Calculate a profile for a move
    // Returns true if successful, false if constraints can't be satisfied
    static bool calculate(double distance, // Total distance to travel (positive)
                          double vStart,   // Starting velocity
                          double vEnd,     // Ending velocity
                          double vMax,     // Maximum velocity
                          double aMax,     // Maximum acceleration
                          double jMax,     // Maximum jerk
                          Profile& out     // Output profile
    );

    // Get velocity at time t within the profile
    static double getVelocity(const Profile& p, double t);

    // Get position at time t within the profile
    static double getPosition(const Profile& p, double t);

    // Optimized access with phase caching
    static double getPosition(const Profile& p, double t, int& phaseIdx);

    // Get acceleration at time t within the profile
    static double getAcceleration(const Profile& p, double t);

    // Calculate maximum achievable entry velocity given distance, exit velocity, and limits
    // Returns the highest vStart that can decelerate to vEnd within the given distance
    static double maxAchievableEntryVelocity(double distance, double vEnd, double vMax, double aMax,
                                             double jMax);

    // Calculate maximum achievable exit velocity given distance, entry velocity, and limits
    // Returns the highest vEnd that can be reached from vStart within the given distance
    static double maxAchievableExitVelocity(double distance, double vStart, double vMax,
                                            double aMax, double jMax);

    // Calculate distance required to decelerate from vStart to vEnd
    static double decelerationDistance(double vStart, double vEnd, double aMax, double jMax);

  private:
    // Calculate distance covered during a jerk phase
    static double jerkPhaseDistance(double v0, double a0, double j, double t);

    // Calculate distance covered during constant accel phase
    static double constAccelDistance(double v0, double a, double t);

    // Calculate velocity after jerk phase
    static double jerkPhaseVelocity(double v0, double a0, double j, double t);
};
