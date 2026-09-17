#pragma once
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

enum class ThrLine { Ignore, Coordinate, Invalid };

// Shared by production streaming, preflight, and native corpus tests. Never
// confuse corrupt coordinates with comments or silently replace them by zero.
inline ThrLine parseThrLine(const char *line, double maxRho, double &theta, double &rho) {
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    const char *p = line;
    while (space(*p))
        ++p;
    if (!*p || *p == '#' || (*p == '/' && p[1] == '/'))
        return ThrLine::Ignore;
    char *end = nullptr;
    errno = 0;
    const double t = std::strtod(p, &end);
    if (end == p || errno == ERANGE || (!space(*end) && *end != ','))
        return ThrLine::Invalid;
    p = end;
    while (space(*p))
        ++p;
    if (*p == ',')
        ++p;
    while (space(*p))
        ++p;
    errno = 0;
    const double r = std::strtod(p, &end);
    if (end == p || errno == ERANGE)
        return ThrLine::Invalid;
    while (space(*end))
        ++end;
    if ((*end && *end != '#' && !(*end == '/' && end[1] == '/')) || !std::isfinite(t) ||
        !std::isfinite(r) || r < 0 || r > 1 || !std::isfinite(maxRho) || maxRho <= 0)
        return ThrLine::Invalid;
    theta = t;
    rho = r * maxRho;
    return ThrLine::Coordinate;
}

class ThrValidator {
  public:
    ThrValidator(double radius, double thetaScale) : m_radius(radius), m_thetaScale(thetaScale) {}
    ThrLine accept(const char *line, size_t length, bool overflow, double &theta, double &rho) {
        if (overflow || length > 127 || std::memchr(line, 0, length))
            return ThrLine::Invalid;
        const ThrLine result = parseThrLine(line, m_radius, theta, rho);
        if (result != ThrLine::Coordinate)
            return result;
        const double target = std::round(theta * m_thetaScale);
        if (!std::isfinite(target) || target < INT32_MIN || target > INT32_MAX)
            return ThrLine::Invalid;
        const int64_t steps = static_cast<int64_t>(target);
        if (m_points && std::abs(steps - m_previousSteps) > INT32_MAX)
            return ThrLine::Invalid;
        m_previousSteps = steps;
        ++m_points;
        return result;
    }
    uint32_t points() const { return m_points; }

  private:
    double m_radius, m_thetaScale;
    int64_t m_previousSteps = 0;
    uint32_t m_points = 0;
};
