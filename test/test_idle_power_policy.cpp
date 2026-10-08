// Build with g++ -std=c++17 -Wall -Wextra -Werror -Ilib/PowerPolicy/src
// test/test_idle_power_policy.cpp -o /tmp/test_idle_power_policy
#include <IdlePowerPolicy.hpp>
#include <cassert>
#include <cstdint>

int main() {
    IdlePowerPolicy policy(1000);
    assert(policy.fullPower(false, 0, 0, 1000));
    assert(!policy.fullPower(false, 0, 0, 6000));
    assert(policy.fullPower(false, 1, 0, 6001));
    assert(policy.fullPower(false, 1, 1, 11000));
    assert(policy.fullPower(false, 1, 0, 15999));
    assert(!policy.fullPower(false, 1, 0, 16000));
    assert(policy.fullPower(true, 1, 0, 16000));
    assert(policy.fullPower(false, 1, 0, 20999));
    assert(!policy.fullPower(false, 1, 0, 21000));

    IdlePowerPolicy rollover(UINT32_MAX - 1000U);
    assert(rollover.fullPower(false, 0, 0, 3000));
    assert(!rollover.fullPower(false, 0, 0, 4000));
}
