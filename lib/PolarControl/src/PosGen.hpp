#pragma once
#include <Arduino.h>
#include <cmath>
#include "PolarUtils.hpp"

class PosGen {
  public:
    virtual ~PosGen() = default;
    virtual PolarCord_t getNextPos() = 0;

    // Progress tracking: returns 0-100 percentage, -1 if unknown
    virtual int getProgressPercent() const { return -1; }
};
