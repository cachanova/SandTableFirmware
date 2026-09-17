#pragma once
#include <Arduino.h>
#include <cmath>

struct PolarCord_t {
    double theta;
    double rho;

    bool isNan() const {
        return std::isnan(theta) || std::isnan(rho);
    }

    PolarCord_t operator - (const PolarCord_t& other) const {
        return {theta - other.theta, rho - other.rho};
    }

    bool operator == (const PolarCord_t& other) const {
        return (theta == other.theta) && (rho == other.rho);
    }

    PolarCord_t operator * (float mult) const {
        return {theta * mult, rho * mult};
    }

    PolarCord_t operator + (const PolarCord_t& other) const {
        return {theta + other.theta, rho + other.rho};
    }

    PolarCord_t operator / (float div) const {
        return {theta / div, rho / div};
    }

    PolarCord_t operator / (int div) const {
        return {theta / (float)div, rho / (float)div};
    }
};

struct PolarVelocity_t {
    float theta;
    float rho;
};

struct CartesianCord_t {
    double x;
    double y;
};

class PolarUtils {
public:
    static CartesianCord_t toCartesian(const PolarCord_t& polar) {
        return {
            polar.rho * std::cos(polar.theta),
            polar.rho * std::sin(polar.theta)
        };
    }

    static CartesianCord_t toNormalizedCartesian(const PolarCord_t& polar, float maxRho) {
        if (maxRho <= 0) return {0.5, 0.5};
        CartesianCord_t cart = toCartesian(polar);
        return {
            (cart.x / maxRho + 1.0f) / 2.0f,
            (cart.y / maxRho + 1.0f) / 2.0f
        };
    }
};
