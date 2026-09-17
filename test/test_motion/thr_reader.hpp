#pragma once

#include <algorithm>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <iostream>
#include "ThrParser.hpp"

// THR file format: theta rho (one pair per line)
// theta: radians (can exceed 2*PI for multiple rotations)
// rho: 0-1 scaled (multiply by maxRho for actual mm)

struct ThrPosition {
    double theta;  // radians
    double rho;    // 0-1 normalized
};

class ThrReader {
public:
    ThrReader() : m_maxRho(450.0f), m_currentIndex(0) {}

    void setMaxRho(float maxRho) {
        m_maxRho = maxRho;
    }

    bool load(const std::string& filepath) {
        m_positions.clear();
        m_currentIndex = 0;

        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filepath << std::endl;
            return false;
        }

        std::string line;
        int lineNum = 0;
        while (std::getline(file, line)) {
            lineNum++;

            double theta=0,rho=0;
            const ThrLine parsed=parseThrLine(line.c_str(),1.0,theta,rho);
            if (line.size()>127 || line.find('\0')!=std::string::npos || parsed==ThrLine::Invalid) {
                std::cerr << "Parse error at line " << lineNum << ": " << line << std::endl;
                m_positions.clear(); return false;
            }
            if (parsed==ThrLine::Ignore) continue;

            m_positions.push_back({theta, rho});
        }

        std::cout << "Loaded " << m_positions.size() << " positions from " << filepath << std::endl;
        return !m_positions.empty();
    }

    // Get next position in physical units (theta in rad, rho in mm)
    // Returns NaN when end is reached
    bool getNextPosition(double& theta, double& rho) {
        if (m_currentIndex >= m_positions.size()) {
            theta = std::nanf("");
            rho = std::nanf("");
            return false;
        }

        const ThrPosition& pos = m_positions[m_currentIndex++];
        theta = pos.theta;
        rho = pos.rho * m_maxRho;
        return true;
    }

    void reset() {
        m_currentIndex = 0;
    }

    size_t size() const {
        return m_positions.size();
    }

    size_t currentIndex() const {
        return m_currentIndex;
    }

    // Get position at specific index
    bool getPositionAt(size_t index, double& theta, double& rho) const {
        if (index >= m_positions.size()) {
            return false;
        }
        theta = m_positions[index].theta;
        rho = m_positions[index].rho * m_maxRho;
        return true;
    }

private:
    std::vector<ThrPosition> m_positions;
    double m_maxRho;
    size_t m_currentIndex;
};
