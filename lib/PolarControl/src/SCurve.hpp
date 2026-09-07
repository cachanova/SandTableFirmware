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
        float t[7];

        // Phase end times (cumulative)
        float tEnd[7];

        // Phase end positions (cumulative)
        float posEnd[7];

        // Velocities at phase boundaries
        float v[8];  // v[0] = start, v[7] = end

        // Accelerations at phase boundaries
        float a[8];

        // Total time and distance
        float totalTime;
        float totalDistance;

        // Constraints used
        float jerk;
        float maxAccel;
        float maxVelocity;
    };

    // Calculate a profile for a move
    // Returns true if successful, false if constraints can't be satisfied
    static bool calculate(
        float distance,       // Total distance to travel (positive)
        float vStart,         // Starting velocity
        float vEnd,           // Ending velocity
        float vMax,           // Maximum velocity
        float aMax,           // Maximum acceleration
        float jMax,           // Maximum jerk
        Profile& out          // Output profile
    );

    // Get velocity at time t within the profile
    static float getVelocity(const Profile& p, float t);

    // Get position at time t within the profile
    static float getPosition(const Profile& p, float t);

    // Optimized access with phase caching
    static float getPosition(const Profile& p, float t, int& phaseIdx);

    // Get acceleration at time t within the profile
    static float getAcceleration(const Profile& p, float t);

    // Calculate maximum achievable entry velocity given distance, exit velocity, and limits
    // Returns the highest vStart that can decelerate to vEnd within the given distance
    static float maxAchievableEntryVelocity(
        float distance,
        float vEnd,
        float vMax,
        float aMax,
        float jMax
    );

    // Calculate maximum achievable exit velocity given distance, entry velocity, and limits
    // Returns the highest vEnd that can be reached from vStart within the given distance
    static float maxAchievableExitVelocity(
        float distance,
        float vStart,
        float vMax,
        float aMax,
        float jMax
    );

    // Calculate distance required to decelerate from vStart to vEnd
    static float decelerationDistance(
        float vStart,
        float vEnd,
        float aMax,
        float jMax
    );

private:
    // Calculate distance covered during a jerk phase
    static float jerkPhaseDistance(float v0, float a0, float j, float t);

    // Calculate distance covered during constant accel phase
    static float constAccelDistance(float v0, float a, float t);

    // Calculate velocity after jerk phase
    static float jerkPhaseVelocity(float v0, float a0, float j, float t);
};
