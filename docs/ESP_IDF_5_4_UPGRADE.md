# ESP-IDF 5.4 upgrade assessment

## Decision

Do not upgrade the production motion-controller firmware solely for presence
sensing yet. The current Arduino-ESP32 2.0.17 / ESP-IDF 4.4.7 SDK already ships
the low-level CSI receive APIs and enables them in its prebuilt SDK. A small
local detector can prove whether Wi-Fi sensing works in the installed room
without simultaneously changing the Wi-Fi, OTA, filesystem, compiler, GPIO,
and PWM foundations under the motor controller.

Revisit the upgrade after field data demonstrates that CSI is useful. IDF 5.4
then becomes attractive for Espressif's packaged `esp_wifi_sensing` state
machine, maintained-IDF fixes, and a supported calibration/presence algorithm.

## Measured migration probe

The complete project was compiled in an isolated PlatformIO environment using:

- community `pioarduino` platform `54.03.21-2`;
- Arduino-ESP32 `3.2.1`;
- ESP-IDF `5.4.2` libraries;
- Xtensa GCC `14.2.0`.

All project and third-party library sources compiled after three source changes:

1. select the Arduino 3 pin-based LEDC API while retaining the Arduino 2
   channel-based implementation;
2. explicitly include `soc/gpio_struct.h` for direct STEP GPIO registers;
3. make an `int32_t` clamp literal explicit for the newer C++ toolchain.

The resulting firmware linked at **1,590,130 bytes**, exceeding each current
1,572,864-byte OTA application slot by **17,266 bytes before adding Espressif's
sensing component**. RAM use was 85,588/327,680 bytes (26.1%). The compatibility
changes above remain in this branch and also build on the production stack.

## Build-system work

Official PlatformIO `platform-espressif32` continues to package Arduino-ESP32
2.0.17 on ESP-IDF 4.4.7 even when its standalone ESP-IDF framework is newer.
Arduino-ESP32 3.x therefore requires one of these routes:

1. pin the community `pioarduino` platform;
2. migrate to ESP-IDF tooling and use Arduino as an IDF component; or
3. convert the firmware away from Arduino APIs to native ESP-IDF.

The first route is the least code churn but adds a new supply-chain and support
dependency. On this Arch/Python 3.14 host, its automatic Python dependency step
failed under the externally managed system interpreter. A local package target
was needed for `rich-click`, `PyYAML`, `intelhex`, `rich`, and `esp-idf-size`.
The current builder also invokes an `esp-idf-size --ng` option removed by the
latest `esp-idf-size`, so the Python tool versions need an explicit lock.

Espressif's `esp_wifi_sensing` package requires ESP-IDF 5.4 or newer and an
`esp-radar` dependency. In an Arduino-only PlatformIO build it is not a normal
Arduino library: its IDF component metadata and prebuilt target library must be
integrated explicitly. A hybrid Arduino-as-IDF-component build would be the
cleanest supported route, but it is a build-system migration rather than a
single dependency addition.

## Flash layout

The current 4 MB layout reserves two 1.5 MiB OTA slots and 960 KiB for
LittleFS. Patterns live on SD and LittleFS currently holds small tuning data,
so a future migration could trade filesystem capacity for two larger app slots.
For example, two 1.75 MiB slots leave roughly 448 KiB for LittleFS and provide
about 245 KiB of headroom over the measured IDF 5.4 base image.

Changing the partition table must be performed by serial flashing rather than
an ordinary OTA update and must be treated as a data migration: preserve tuning
settings, flash the new layout, restore settings, and verify both OTA slots.

## Hardware qualification required

A successful compile is not sufficient for this controller. Before adopting
5.4, qualify at least:

- boot, Wi-Fi provisioning/reconnect, web UI, SSE, and authenticated OTA;
- SD and LittleFS mount/read/write/rename behavior;
- LED PWM duty and frequency;
- all three TMC2209 UART addresses and driver enable behavior;
- STEP/DIR pulse width, rate, jitter, and direct-register writes under load;
- sensorless homing cancellation and failure paths;
- long pattern runs with concurrent web traffic and CSI enabled;
- heap, stack high-water marks, watchdogs, and reset behavior.

The newer compiler also emitted warnings for volatile delay loops and conflicting
IRAM section attributes around the step-timer handler. Those are not compile
failures, but they make timing capture on real hardware mandatory.

## When it becomes worth doing

Upgrade when at least one of these is true:

- field testing shows the local detector is useful but needs Espressif's
  packaged presence/training algorithm;
- another required feature or supported dependency needs Arduino-ESP32 3.x;
- IDF 4.4 maintenance/security exposure becomes unacceptable; or
- the firmware is already scheduled for a full electronics requalification.

Until then, presence sensing alone does not justify combining a radio experiment
with a motion-platform migration and partition-table change.

## Primary references

- [Arduino-ESP32 2.x to 3.0 migration guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html)
- [Espressif Wi-Fi CSI configuration](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/wifi-driver/wifi-vendor-features.html#wi-fi-channel-state-information-configure)
- [`esp_wifi_sensing` requirements and API](https://components.espressif.com/components/espressif/esp_wifi_sensing/versions/0.1.1~3/readme)
- [Official PlatformIO Arduino-ESP32 3.x support status](https://github.com/platformio/platform-espressif32/issues/1225)
- [`pioarduino` PlatformIO compatibility platform](https://github.com/pioarduino/platform-espressif32)
