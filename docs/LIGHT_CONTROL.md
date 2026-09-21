# On/off lighting

As of September 19, 2026, the light has a single On/Off switch. On drives
GPIO4 steadily high for full light output; Off drives it low. There is no
LEDC PWM, frequency selection, brightness interpolation, or fade task.

The output latch is set low before the pin becomes an output, and the GPIO
internal pull-down is enabled. Initialization occurs before Wi-Fi startup and
the light stays off through it, so the radio and the SD card are not brought
up against the lighting load. `setup()` then switches the light on as its
last step, alongside the default speed: every boot ends with the light on,
and a failed write there is logged without blocking startup. Failed GPIO
writes do not change the reported state or the user's selected state.

The existing `/api/led/brightness` API remains compatible with binary values:
POST `brightness=0` for Off or `brightness=100` for On. Intermediate values
return HTTP 400, including requests carrying the old `immediate=1` parameter.
GET reports `on`, `ready`, `pin`, and binary `brightness`/`targetBrightness`.
The `/api/led/frequency` and `/api/led/signal` routes have been removed.
Status responses retain `ledBrightness` and `ledTargetBrightness` as 0 or 100.

The UI uses a keyboard-accessible switch with On/Off labels. Rapid changes
are serialized with only the latest selection queued. Stale status replies
cannot undo a selection; failed or uncertain commands display an error and
are never automatically replayed after a connection failure.

The existing optional presence restoration respects the selected On/Off
state and restores immediately. Selecting Off keeps the light off. The saved
action's numeric value is preserved; its API name is now `restore_light`,
with `fade_light_on` accepted only as an alias for older clients. Presence
sensing and motor behavior are otherwise unchanged.

The `esp32dev_lights_only_ota` environment keeps motor and SD initialization
disabled for the connected lights-only bench, with normal CPU/radio settings
and OTA retained. `esp32dev` uses the same on/off lighting in the full build.

The dated PWM/acoustics reports are historical measurements. Their old
diagnostic APIs, sweep scripts, fading code, and preset controls are removed.

## Verification and deployment

Production and lights-only firmware builds passed. Host checks cover startup
low, pull-down configuration, binary output, write failures, and respecting
manual Off during presence events. Native tests, the presence allocation
stress test, and 15 UI tests passed.

The lights-only image was flashed and checked on the device: On/Off commands
and status agree, intermediate requests (including the former diagnostic
bypass) return 400, removed diagnostic routes return 404, and the served page
contains the switch. The device was left Off. Logs and deployed readbacks are
in `.pio/light-toggle/`.
