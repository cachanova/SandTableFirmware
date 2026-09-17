#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Geometry is independent of its time law. Theta remains unwrapped and double
// precision; every evaluation uses one scalar distance for both axes.
struct PathPoint {
    double theta, rho;
    constexpr PathPoint(double t = 0, double r = 0) : theta(t), rho(r) {}
    PathPoint operator+(PathPoint p) const { return {theta + p.theta, rho + p.rho}; }
    PathPoint operator-(PathPoint p) const { return {theta - p.theta, rho - p.rho}; }
    PathPoint operator*(double k) const { return {theta * k, rho * k}; }
};

struct PolarPath {
    PathPoint start, end, entry, exit;
    double length = 0;
    using Coefficients = std::array<PathPoint, 6>;

    static void boundControls(const PathPoint *controls, int count, int depth, PathPoint &maximum) {
        if (depth == 0) {
            for (int i = 0; i < count; ++i) {
                maximum.theta = std::max(maximum.theta, std::abs(controls[i].theta));
                maximum.rho = std::max(maximum.rho, std::abs(controls[i].rho));
            }
            return;
        }
        PathPoint work[5], left[5], right[5];
        for (int i = 0; i < count; ++i)
            work[i] = controls[i];
        left[0] = work[0];
        right[count - 1] = work[count - 1];
        for (int level = 1; level < count; ++level) {
            for (int i = 0; i < count - level; ++i)
                work[i] = (work[i] + work[i + 1]) * .5;
            left[level] = work[0];
            right[count - 1 - level] = work[count - 1 - level];
        }
        boundControls(left, count, depth - 1, maximum);
        boundControls(right, count, depth - 1, maximum);
    }

    static double norm(PathPoint p, double radius) { return std::hypot(radius * p.theta, p.rho); }
    void line(PathPoint a, PathPoint b, double radius) {
        start = a;
        end = b;
        length = norm(b - a, radius);
        entry = exit = length > 0 ? (b - a) * (1 / length) : PathPoint{};
    }
    Coefficients coefficients() const {
        const PathPoint d = end - start, a = entry * length, b = exit * length;
        return {start,
                a,
                PathPoint{},
                d * 10 - a * 6 - b * 4,
                d * (-15) + a * 8 + b * 7,
                d * 6 - a * 3 - b * 3};
    }
    PathPoint position(double s) const { return position(s, coefficients()); }
    PathPoint position(double s, const Coefficients &coefficients) const {
        if (s <= 0)
            return start;
        if (s >= length)
            return end;
        const double u = s / length;
        PathPoint value = coefficients[5];
        for (int i = 4; i >= 1; --i)
            value = value * u + coefficients[i];
        return start + value * u;
    }
    PathPoint tangent(double s) const {
        if (length <= 0)
            return {};
        if (s <= 0)
            return entry;
        if (s >= length)
            return exit;
        const double u = s / length;
        const auto c = coefficients();
        PathPoint value = c[5] * 5;
        for (int i = 4; i >= 1; --i)
            value = value * u + c[i] * i;
        return value * (1 / length);
    }
    // Convex-hull bounds on derivatives with respect to scalar distance.
    // They cover the complete curve, including between validation samples.
    void derivativeBounds(PathPoint &first, PathPoint &second, PathPoint &third) const {
        first = second = third = {};
        if (length <= 0)
            return;
        const PathPoint d = end - start;
        PathPoint c[6] = {{},
                          entry * (length / 5),
                          entry * (2 * length / 5),
                          d - exit * (2 * length / 5),
                          d - exit * (length / 5),
                          d};
        for (int order = 1; order <= 3; ++order) {
            for (int i = 0; i < 6 - order; ++i) {
                c[i] = (c[i + 1] - c[i]) * ((6 - order) / length);
            }
            // Exact subdivision tightens the convex hull without relying on
            // time samples that could miss a derivative extremum.
            boundControls(c, 6 - order, 3, order == 1 ? first : (order == 2 ? second : third));
        }
        // Suppress roundoff-only curvature for an exactly linear segment.
        const PathPoint direction = d * (1 / length);
        if (norm(entry - direction, 1) < 1e-14 && norm(exit - direction, 1) < 1e-14) {
            second = third = {};
        }
    }
};

// One common tangent at a junction, with zero geometric second derivative.
// The quintic Bezier controls remain inside a convex capsule about each THR
// line. In (radius*theta,rho), epsilon/sqrt(2) implies <=epsilon Cartesian
// deviation because |dx| <= |dr| + radius*|dtheta|. Controls also stay within
// each segment's radial range, so blending cannot overshoot the table edge.
inline PathPoint boundedJunction(const PolarPath &a, const PolarPath &b, double radius,
                                 double epsilon) {
    const PathPoint da = (a.end - a.start) * (1 / a.length);
    const PathPoint db = (b.end - b.start) * (1 / b.length);
    if (PolarPath::norm(da - db, radius) < 1e-10)
        return da;
    if (epsilon <= 0)
        return {};
    PathPoint direction = da + db;
    if (da.rho * db.rho <= 0)
        direction.rho = 0;
    if (da.theta * db.theta <= 0)
        direction.theta = 0;
    const double size = PolarPath::norm(direction, radius);
    if (size < 1e-12)
        return {};
    direction = direction * (1 / size);
    double scale = 1;
    for (const PolarPath *p : {&a, &b}) {
        const PathPoint d = (p->end - p->start) * (1 / p->length);
        const double dot = radius * radius * direction.theta * d.theta + direction.rho * d.rho;
        if (dot <= 0)
            return {};
        const double cross = std::abs(radius * (direction.theta * d.rho - direction.rho * d.theta));
        if (cross > 1e-14)
            scale = std::min(scale, epsilon / std::sqrt(2.0) / (.4 * p->length * cross));
        scale = std::min(scale, 1 / (.4 * dot));
        if (std::abs(direction.theta) > 1e-14)
            scale = std::min(scale, std::abs(p->end.theta - p->start.theta) /
                                        (.4 * p->length * std::abs(direction.theta)));
        if (std::abs(direction.rho) > 1e-14)
            scale = std::min(scale, std::abs(p->end.rho - p->start.rho) /
                                        (.4 * p->length * std::abs(direction.rho)));
    }
    return direction * scale;
}
