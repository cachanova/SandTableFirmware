#pragma once

#ifndef NATIVE_BUILD
#include <Arduino.h>
#include <soc/gpio_struct.h>
#else
#include "esp32_mock.hpp"
#endif

/**
 * FastGPIO provides direct port manipulation for ESP32.
 * This is significantly faster than digitalWrite() as it avoids pin mapping 
 * and validation overhead, and uses the W1TS (Write 1 to Set) and 
 * W1TC (Write 1 to Clear) registers which are atomic.
 */
namespace FastGPIO {

    /**
     * Sets a pin HIGH using direct register access.
     * For constant pin numbers, the branch and bit shifts are optimized out.
     */
    static inline __attribute__((always_inline)) void setHigh(uint8_t pin) {
        if (pin < 32) {
            GPIO.out_w1ts = (1UL << pin);
        } else {
            GPIO.out1_w1ts.val = (1UL << (pin - 32));
        }
    }

    /**
     * Sets a pin LOW using direct register access.
     */
    static inline __attribute__((always_inline)) void setLow(uint8_t pin) {
        if (pin < 32) {
            GPIO.out_w1tc = (1UL << pin);
        } else {
            GPIO.out1_w1tc.val = (1UL << (pin - 32));
        }
    }

    /**
     * Writes a level to a pin using direct register access.
     */
    static inline __attribute__((always_inline)) void write(uint8_t pin, bool level) {
        if (level) setHigh(pin);
        else setLow(pin);
    }
}
