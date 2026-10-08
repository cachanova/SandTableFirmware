# Idle electronics power

The production firmware keeps the ESP32 at its boot clock while motors move,
homing runs, OTA runs, or a web request or live stream is active. After five
seconds without those activities, it lowers the CPU from 240 to 80 MHz and
enables Wi-Fi minimum modem sleep. A new HTTP request, live stream, or motion
state restores the boot clock and disables modem sleep. The motor task keeps
its 1 ms poll so a newly started pattern can refill without added delay.

The Wi-Fi station remains connected. The access point buffers incoming traffic
until the ESP32 wakes on a beacon, so the first request after idle may take
longer. This firmware does not enter CPU light sleep: the bundled ESP-IDF build
has `CONFIG_PM_ENABLE` disabled, and the existing motion timer needs timing
checks before enabling automatic light sleep. See [Espressif's Wi-Fi power
guide](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-guides/wifi.html).
Deep sleep turns Wi-Fi off, so a web request cannot wake this ESP32 without
another always-on device or a different wake source. See [Espressif's sleep
mode guide](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-reference/system/sleep_modes.html).

Measure wall power, request latency, and motion timing on the assembled table
before deploying this as a power saving. The build sets
`SISYPHUS_DISABLE_PRESENCE`, so CSI capture and its ten gateway pings per second
do not run. Presence API status reports unavailable, and calibration returns
503. Remove the build flag to restore presence sensing when needed.

The light is another large load. An earlier full-brightness measurement found
25.4 W at the USB-C input for the
whole table. An optional light-off timeout could save more energy, but it would
also hide the finished sand pattern. The current firmware leaves the light
under the existing On/Off control.

The documented acoustic profile requests theta 700 mA run / 200 mA hold and
RHO 350 mA run / 200 mA hold. On October 7, a read-only check of the running
table's `GET /api/tuning` reported theta 1177 mA run / 200 mA hold and RHO
350 mA run / 350 mA hold. The saved device values therefore differ from the
older documented profile. Check them again before measuring.

Turning off a driver bridge could let a loaded axis shift without position
feedback. Test holding torque and recovery from movement before using that
option. The requested 1100 mA clearing current has not been applied. All these
values are firmware requests rather than measured coil currents.
