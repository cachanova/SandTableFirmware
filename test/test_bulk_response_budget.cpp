#include <BulkResponseBudget.hpp>
#include <cassert>
#include <iostream>

int main() {
    using namespace BulkResponseBudget;
    for (const char* path : {"/", "/settings", "/manual", "/files", "/api/pattern/image", "/api/pattern/download"})
        assert(isBulkPath(path));
    for (const char* path : {"/api/status", "/api/system/info", "/api/presence",
                            "/api/pattern/stop", "/api/stream", "/tuning"})
        assert(!isBulkPath(path));
    assert(canStart(0, 28672, 8192));
    assert(!canStart(0, 28671, 8192));
    assert(!canStart(0, 28672, 8191));
    assert(!canStart(1, 100000, 50000));
    assert(!canStart(2, 100000, 50000));
    std::cout << "PASS: single bulk response, byte-heap reserve and fragmentation boundaries; controls excluded\n";
}
