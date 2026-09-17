#include <LEDController.hpp>
#include <PresenceAutomation.hpp>
#include <cassert>
#include <iostream>

int main() {
    LEDController led;
    led.begin();
    assert(led.getBrightness() == 128 && led.getTargetBrightness() == 128);
    led.setBrightness(102); // User selects 40%.
    assert(led.getBrightness() == 102 && led.getTargetBrightness() == 102);
    led.setOutputBrightness(0); // A lower live level must not erase that choice.
    assert(led.getBrightness() == 0 && led.getTargetBrightness() == 102);
    PresenceAutomation automation;
    automation.setAction(PresenceAction::FADE_LIGHT_ON);
    for (uint32_t now = 0; now <= 2500; now += 100) {
        uint8_t next = led.getBrightness();
        if (automation.update(true, next, led.getTargetBrightness(), now, next))
            led.setOutputBrightness(next);
        assert(led.getBrightness() <= 102 && testPwmDuty <= 102);
        assert(led.getTargetBrightness() == 102);
    }
    assert(led.getBrightness() == 102 && !automation.isFading());
    led.setBrightness(0);
    automation.cancelFade();
    uint8_t next = 0;
    automation.update(false, 0, led.getTargetBrightness(), 3000, next);
    assert(!automation.update(true, 0, led.getTargetBrightness(), 4000, next));
    assert(!automation.isFading() && led.getBrightness() == 0 && testPwmDuty == 0);
    // Confirm full-on mapping and failure reporting instead of publishing a
    // brightness the driver rejected. A subsequent valid write must recover.
    assert(led.setBrightness(255));
    auto diagnostics = led.getDiagnostics();
    assert(diagnostics.ready && diagnostics.duty == 256 && diagnostics.frequencyHz == 5000);
    testPwmFailure = true;
    assert(!led.setBrightness(20));
    assert(led.getBrightness() == 255 && led.getTargetBrightness() == 255);
    assert(led.getDiagnostics().errors == 1 && led.getDiagnostics().duty == 256);
    testPwmFailure = false;
    assert(led.setBrightness(0));
    assert(led.getDiagnostics().duty == 0 && led.getDiagnostics().frequencyHz == 5000);
    std::cout << "PASS: real LED target survives fade writes, 40% caps PWM, and zero stays off\n";
}
