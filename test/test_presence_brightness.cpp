#include <LEDController.hpp>
#include <PresenceAutomation.hpp>
#include <cassert>
#include <iostream>

int main() {
    LEDController led;
    led.begin();
    assert(led.getBrightness() == 128 && led.getTargetBrightness() == 128);
    led.setBrightness(102, 0); // User selects 40%; the output fades in via update().
    assert(led.getTargetBrightness() == 102 && led.getBrightness() == 128);
    led.update(LEDController::kFadeDurationMs);
    assert(led.getBrightness() == 102 && !led.isFading());
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
    led.setBrightness(0, 2600);
    led.update(2600 + LEDController::kFadeDurationMs);
    assert(led.getBrightness() == 0 && !led.isFading());
    automation.cancelFade();
    uint8_t next = 0;
    automation.update(false, 0, led.getTargetBrightness(), 3000, next);
    assert(!automation.update(true, 0, led.getTargetBrightness(), 4000, next));
    assert(!automation.isFading() && led.getBrightness() == 0 && testPwmDuty == 0);
    // Confirm full-on mapping, and that a rejected driver write never
    // publishes its brightness: the fade retries until the write succeeds.
    assert(led.setBrightness(255, 5000));
    led.update(5000 + LEDController::kFadeDurationMs);
    auto diagnostics = led.getDiagnostics();
    assert(diagnostics.ready && diagnostics.duty == 256 && diagnostics.frequencyHz == 5000);
    testPwmFailure = true;
    assert(led.setBrightness(20, 7000));
    led.update(7000 + LEDController::kFadeDurationMs);
    assert(led.isFading() && led.getBrightness() == 255 && led.getTargetBrightness() == 20);
    assert(led.getDiagnostics().errors == 1 && led.getDiagnostics().duty == 256);
    testPwmFailure = false;
    led.update(7000 + 2 * LEDController::kFadeDurationMs);
    assert(!led.isFading() && led.getBrightness() == 20);
    assert(led.setBrightness(0, 9000));
    led.update(9000 + LEDController::kFadeDurationMs);
    assert(led.getDiagnostics().duty == 0 && led.getDiagnostics().frequencyHz == 5000);
    std::cout << "PASS: real LED target survives fade writes, 40% caps PWM, and zero stays off\n";
}
