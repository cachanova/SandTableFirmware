#pragma once
#include <algorithm>
#include <cmath>
#include "PathLimits.hpp"
#include "PolarPath.hpp"

// Run-time estimate for one THR segment b->c that follows a->b, at speed 1.
// Preflight sums it over a file and the planner accumulates it per completed
// waypoint, so both must call it with the same points. It uses the planner's
// own velocity limits for the line and for the corner blend at b, cruises at
// the line's limit (or what its length allows), and adds the extra time of
// jerk-limited slowing for the corner. Lookahead coupling across several
// short segments is approximated, not simulated.
struct EtaModel {
    MotionLimits limits;
    double radius = 425;            // mm; scales theta in the path metric
    double cornerTolerance = 0.10;  // mm
    double minSegmentDuration = 0.010;

    struct Line {
        double length = 0, v = 0, a = 0, j = 0;
    };

    Line line(PathPoint from, PathPoint to) const {
        Line out;
        PolarPath path;
        path.line(from, to, radius);
        out.length = path.length;
        if (out.length <= 0)
            return out;
        pathLimits(path, out.length, minSegmentDuration, limits, out.v, out.a, out.j);
        return out;
    }

    // Extra seconds, beyond cruising at `cruise`, to change speed from v to w.
    static double rampPenalty(double v, double w, double a, double j, double cruise) {
        const double dv = std::abs(v - w);
        if (dv <= 0 || a <= 0 || j <= 0 || cruise <= 0)
            return 0;
        const double t = dv >= a * a / j ? dv / a + a / j : 2 * std::sqrt(dv / j);
        return std::max(0.0, t - 0.5 * (v + w) * t / cruise);
    }

    struct Corner {
        double speed = 0;  // 0 where the planner stops exactly at the waypoint
        double reach = 0;  // blend length taken from each adjacent line
    };

    // The planner's blend at b between a->b and b->c, where `available` is the
    // part of a->b not already taken by the blend at a.
    Corner corner(PathPoint a, PathPoint b, PathPoint c, const Line& in, const Line& out,
                  double available) const {
        Corner result;
        if (in.length <= 1e-9 || out.length <= 1e-9 || cornerTolerance <= 0)
            return result;
        const PathPoint d1 = (b - a) * (1 / in.length), d2 = (c - b) * (1 / out.length);
        const double cosine = radius * radius * d1.theta * d2.theta + d1.rho * d2.rho;
        if (cosine > 1 - 1e-12) {
            result.speed = std::min(in.v, out.v);
            return result;
        }
        if (cosine < -0.5)
            return result;
        const double sine = std::sqrt(std::max(0.0, 1 - cosine * cosine));
        double reach = std::min(0.5 * available, 0.5 * out.length);
        if (sine > 1e-12)
            reach = std::min(reach, std::sqrt(2.0) * cornerTolerance / sine);
        if (reach < 1e-6)
            return result;
        PolarPath blend;
        blend.start = b - d1 * reach;
        blend.end = b + d2 * reach;
        blend.length = PolarPath::norm(blend.end - blend.start, radius);
        blend.entry = d1;
        blend.exit = d2;
        double v, acc, jerk;
        pathLimits(blend, blend.length, minSegmentDuration, limits, v, acc, jerk);
        result.speed = std::min({v, in.v, out.v});
        result.reach = reach;
        return result;
    }

    // Seconds for waypoint segment b->c, given the speed and blend reach at
    // each of its corners. Each segment owns the speed changes at both ends
    // and half of each blend.
    double segmentSeconds(const Line& here, const Corner& enter, const Corner& leave) const {
        if (here.length <= 0 || here.v <= 0)
            return 0;
        const double straight = std::max(0.0, here.length - enter.reach - leave.reach);
        // A short line cannot reach its limit between its corner speeds.
        const double cruise = std::min(
            here.v, std::sqrt(0.5 * (enter.speed * enter.speed + leave.speed * leave.speed) +
                              here.a * std::max(straight, 1e-9)));
        double seconds = straight / cruise +
                         rampPenalty(enter.speed, cruise, here.a, here.j, cruise) +
                         rampPenalty(cruise, leave.speed, here.a, here.j, cruise);
        for (const Corner* k : {&enter, &leave})
            if (k->reach > 0)
                seconds += k->reach / std::max(k->speed, 1e-3);
        return seconds;
    }
};

// Streams waypoints through EtaModel, one line and one corner per point.
// add() returns the seconds of the segment that ended at the previous
// waypoint, which only becomes known once the following corner is; finish()
// returns the last segment, which stops. Preflight and the planner must feed
// identical point sequences to agree.
class EtaStream {
public:
    void reset(const EtaModel& model) {
        m_model = model;
        m_count = 0;
    }
    double add(PathPoint p) {
        if (m_count > 0 && p.theta == m_points[1].theta && p.rho == m_points[1].rho)
            return 0;
        double seconds = 0;
        if (m_count >= 1) {
            const EtaModel::Line next = m_model.line(m_points[1], p);
            if (m_count >= 2) {
                // Corner at the previous point closes the segment before it.
                const EtaModel::Corner leave =
                    m_model.corner(m_points[0], m_points[1], p, m_line, next,
                                   m_line.length - m_enter.reach);
                seconds = m_model.segmentSeconds(m_line, m_enter, leave);
                m_enter = leave;
            } else {
                m_enter = {};
            }
            m_line = next;
        }
        m_points[0] = m_points[1];
        m_points[1] = p;
        ++m_count;
        return seconds;
    }
    double finish() {
        if (m_count < 2)
            return 0;
        m_count = 0;
        return m_model.segmentSeconds(m_line, m_enter, {});
    }

private:
    EtaModel m_model;
    PathPoint m_points[2];
    EtaModel::Line m_line;
    EtaModel::Corner m_enter;
    size_t m_count = 0;
};

// The estimate always models speed 1; ETA divides by the current speed.
inline EtaModel makeEtaModel(MotionLimits limits, double radius, double cornerTolerance,
                             double minSegmentDuration) {
    EtaModel model;
    limits.speedMultiplier = 1;
    model.limits = limits;
    model.radius = radius;
    model.cornerTolerance = cornerTolerance;
    model.minSegmentDuration = minSegmentDuration;
    return model;
}
