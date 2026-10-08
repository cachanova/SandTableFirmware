# Idle electronics power

Production firmware keeps the ESP32 at its 240 MHz boot clock and Wi-Fi modem
sleep off. The motor task retains its original 1 ms poll. The production build
sets `SISYPHUS_DISABLE_PRESENCE`, so CSI capture and its ten gateway pings per
second do not run. Presence API status reports unavailable, and calibration
returns 503. Remove the build flag to restore sensing when needed.

An initial 80 MHz and modem-sleep build was flashed after the Spiral7 pattern
completed on October 7. The first request after idle took 7.3 seconds; later
requests timed out and ping lost most packets. A second build kept Wi-Fi awake
but still lowered the CPU to 80 MHz at idle; it also suffered HTTP timeouts and
task-watchdog resets. These observations do not isolate the cause, so both
clock switching and modem sleep are removed from production pending a separate
instrumented investigation. The bundled ESP-IDF build has `CONFIG_PM_ENABLE`
disabled, and the existing motion timer needs timing checks before enabling
automatic light sleep. See [Espressif's Wi-Fi power
guide](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-guides/wifi.html).
Deep sleep turns Wi-Fi off, so a web request cannot wake this ESP32 without
another always-on device or a different wake source. See [Espressif's sleep
mode guide](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-reference/system/sleep_modes.html).

Measure wall power with sensing enabled and disabled before quantifying the
remaining saving. Any future CPU or Wi-Fi sleep trial also needs idle request
latency, packet loss, reset reason, and motion timing checks on the table.

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
