#include <LEDController.hpp>
#include <cassert>
#include <iostream>

int main() {
    LEDController led;
    led.begin();
    assert(led.getBrightness() == 128 && testPwmDuty == 128);

    // A user brightness change starts a fade: the target moves immediately,
    // the output stays where it is until update() ticks it.
    assert(led.setBrightness(200, 1000));
    assert(led.getTargetBrightness() == 200);
    assert(led.getBrightness() == 128 && testPwmDuty == 128);
    assert(led.isFading());

    // Halfway through the fade the output is halfway to the target.
    led.update(1000 + LEDController::kFadeDurationMs / 2);
    assert(led.getBrightness() == 164 && testPwmDuty == 164);
    assert(led.isFading());

    // Ticks that land on the same interpolated value must not rewrite PWM.
    const uint32_t writesBefore = led.getDiagnostics().writes;
    led.update(1000 + LEDController::kFadeDurationMs / 2);
    assert(led.getDiagnostics().writes == writesBefore);

    // The fade ends exactly at the target and stops ticking.
    led.update(1000 + LEDController::kFadeDurationMs);
    assert(led.getBrightness() == 200 && testPwmDuty == 200);
    assert(!led.isFading());
    const uint32_t writesDone = led.getDiagnostics().writes;
    led.update(1000 + 2 * LEDController::kFadeDurationMs);
    assert(led.getDiagnostics().writes == writesDone);

    // Fading down works symmetrically.
    assert(led.setBrightness(50, 10000));
    led.update(10000 + LEDController::kFadeDurationMs / 2);
    assert(led.getBrightness() == 125 && testPwmDuty == 125);

    // A new request mid-fade restarts from the current output level.
    assert(led.setBrightness(0, 10500));
    assert(led.getTargetBrightness() == 0 && led.getBrightness() == 125);
    led.update(10500 + LEDController::kFadeDurationMs / 2);
    assert(led.getBrightness() == 63 && testPwmDuty == 63);
    led.update(10500 + LEDController::kFadeDurationMs);
    assert(led.getBrightness() == 0 && testPwmDuty == 0 && !led.isFading());

    // A failed PWM write mid-fade is retried by the next tick and recovers.
    assert(led.setBrightness(100, 20000));
    testPwmFailure = true;
    led.update(20000 + LEDController::kFadeDurationMs / 2);
    assert(led.getBrightness() == 0 && led.getDiagnostics().errors == 1);
    testPwmFailure = false;
    led.update(20000 + LEDController::kFadeDurationMs);
    assert(led.getBrightness() == 100 && testPwmDuty == 100 && !led.isFading());

    // Re-selecting the level the output already shows never opens a fade
    // window (which would needlessly suppress presence automation).
    assert(led.setBrightness(100, 30000));
    assert(!led.isFading() && led.getBrightness() == 100);
    assert(led.setBrightness(0, 31000));
    led.update(31000 + LEDController::kFadeDurationMs / 2);
    assert(led.getBrightness() == 50);
    assert(led.setBrightness(50, 31500)); // Re-target to the current output.
    assert(!led.isFading() && led.getTargetBrightness() == 50);

    // A fade that spans the millis() wrap still lands on time.
    assert(led.setBrightness(150, 0xFFFFFF00u));
    led.update(0xFFFFFF00u + LEDController::kFadeDurationMs / 2);
    assert(led.getBrightness() == 100);
    led.update(0xFFFFFF00u + LEDController::kFadeDurationMs);
    assert(led.getBrightness() == 150 && !led.isFading());

    std::cout << "PASS: manual brightness changes fade over "
              << LEDController::kFadeDurationMs << "ms without redundant PWM writes\n";
}
