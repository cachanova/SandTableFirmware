// g++ -std=c++17 -Wall -Wextra -Werror -O2 -Itest/support/clearing -Ilib/PolarControl/src test/test_clearing_patterns.cpp -o /tmp/test_clearing_patterns
// Every clearing pattern must sweep the whole disc for a 1/2" ball: no table
// point may lie farther than half a groove from the ball's path.
#include "ClearingPatternGen.cpp"
#include <cassert>
#include <cfloat>
#include <cstdio>
#include <queue>
#include <vector>

namespace {
constexpr float R = 425.0f;  // PolarControl::R_MAX

std::vector<PolarCord_t> points(ClearingPattern pattern) {
    ClearingPatternGen gen(pattern, R);
    std::vector<PolarCord_t> out;
    for (PolarCord_t p = gen.getNextPos(); !p.isNan(); p = gen.getNextPos()) out.push_back(p);
    return out;
}

// Largest distance (mm, 1 mm grid) from any point of the disc to the path,
// following the planner's linear interpolation in polar coordinates.
double maxGap(const std::vector<PolarCord_t>& path) {
    const int n = 2 * static_cast<int>(R) + 3, c = n / 2;
    std::vector<float> dist(n * n, FLT_MAX);
    std::vector<int> nearest(n * n, -1);
    std::queue<int> frontier;
    for (size_t i = 1; i < path.size(); ++i) {
        for (int k = 0; k <= 64; ++k) {
            const double u = k / 64.0;
            const double t = path[i - 1].theta + (path[i].theta - path[i - 1].theta) * u;
            const double r = path[i - 1].rho + (path[i].rho - path[i - 1].rho) * u;
            const int id = (c + static_cast<int>(lround(r * sin(t)))) * n +
                           c + static_cast<int>(lround(r * cos(t)));
            if (dist[id] > 0) { dist[id] = 0; nearest[id] = id; frontier.push(id); }
        }
    }
    while (!frontier.empty()) {
        const int id = frontier.front(); frontier.pop();
        const int x = id % n, y = id / n, sx = nearest[id] % n, sy = nearest[id] / n;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
            const float d = hypotf(nx - sx, ny - sy);
            if (d < dist[ny * n + nx] - 1e-4f) {
                dist[ny * n + nx] = d; nearest[ny * n + nx] = nearest[id]; frontier.push(ny * n + nx);
            }
        }
    }
    double worst = 0;
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x)
        if (hypot(x - c, y - c) <= R) worst = std::max(worst, static_cast<double>(dist[y * n + x]));
    return worst;
}
}  // namespace

int main() {
    for (int p = SPIRAL_OUTWARD; p <= PETAL_FLOWER; ++p) {
        const auto pattern = static_cast<ClearingPattern>(p);
        const auto path = points(pattern);
        assert(path.size() > 100);
        for (const auto& point : path) assert(point.rho >= 0 && point.rho <= R + 1e-6);
        assert(path.front().rho == R);  // starts on the rim
        // Neighbouring points stay close, so no long uncontrolled chords.
        for (size_t i = 1; i < path.size(); ++i)
            assert(std::fabs(path[i].theta - path[i - 1].theta) < 0.5);
        const double gap = maxGap(path);
        const double minutes = ClearingPatternGen(pattern, R).nominalSeconds(
            path.front(), 0.225, 5.5, 30.0) * ClearingPatternGen::plannerOverhead(pattern) / 60.0;
        std::printf("%-20s %6zu points, max gap %.1f mm, about %.0f min\n",
                    ClearingPatternGen::displayName(pattern), path.size(), gap, minutes);
        // Half of a 1/2" ball's ~10 mm groove; the 1 mm grid adds up to ~0.7 mm.
        assert(gap <= 5.2);
        if (pattern != ZIGZAG_RADIAL) assert(path.back().rho == 0.0);  // ends at the centre
    }
    std::printf("PASS: every clearing pattern sweeps the whole disc for a 1/2\" ball\n");
}
