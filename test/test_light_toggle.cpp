#include <LEDController.hpp>
#include <PresenceAutomation.hpp>
#include <driver/gpio.h>
#include <cassert>
#include <iostream>

int main() {
    LEDController led;
    assert(!led.isReady() && !led.setOn(true));
    led.begin();
    assert(led.isReady() && !led.isOn() && !led.targetOn());
    assert(testConfiguredLow && testOutput == 0);
    assert(testConfig.pin_bit_mask == (1ULL << 4));
    assert(testConfig.mode == GPIO_MODE_OUTPUT);
    assert(testConfig.pull_down_en == GPIO_PULLDOWN_ENABLE);
    assert(testConfig.pull_up_en == GPIO_PULLUP_DISABLE);
    assert(led.setOn(true) && testOutput == 1 && led.targetOn());
    const auto writes = testWrites;
    assert(led.setOn(true) && testWrites == writes);
    testGpioFailure = true;
    assert(!led.setOn(false));
    assert(led.isOn() && led.targetOn() && testOutput == 1);
    testGpioFailure = false;
    assert(led.setOn(false) && testOutput == 0 && !led.targetOn());

    PresenceAutomation automation;
    automation.setAction(PresenceAction::RESTORE_LIGHT);
    bool next = false;
    assert(!automation.update(true, false, led.targetOn(), next));
    assert(led.setOn(true));
    assert(led.setOutputOn(false) && led.targetOn());
    automation.update(false, false, true, next);
    assert(automation.update(true, false, true, next) && next);
    assert(led.setOutputOn(next) && led.isOn());
    assert(led.setOn(false));
    automation.manualOverride();
    assert(!automation.update(true, false, false, next));
    automation.update(false, false, false, next);
    assert(!automation.update(true, false, false, next));

    testConfigFailure = true;
    LEDController failed;
    failed.begin();
    assert(!failed.isReady() && !failed.setOn(true));
    testConfigFailure = false;
    testGpioFailure = true;
    failed.begin();
    assert(!failed.isReady());
    std::cout << "PASS: binary GPIO, startup low, pull-down, failures, and manual Off respected\n";
}
