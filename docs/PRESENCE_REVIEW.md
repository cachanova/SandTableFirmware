# Presence sensing review — 2026-09-16

Scope: commits `a8c20fd` and `8763cf4` on `feat/presence-sensing`, including
CSI capture, calibration/classification, light automation, HTTP/settings UI,
tests, and the ESP-IDF 5.4 migration assessment. Main was synced before reading
project documentation. Changes stay in the presence worktree; no firmware was
flashed and no framework upgrade was performed.

## Findings fixed

### High: stale detections could trigger the light again

The classifier retained motion across missing packets. A later packet marked
the sensor fresh before its data was validated, allowing the old motion state
to become a new automation edge. Samples and classifier status are now read
under one mutex. Only usable frames refresh telemetry; gaps over three seconds
clear runtime detection state. Resuming seeds a fresh channel reference.
Automation cancels while sensing is invalid, suppressed, or calibrating.

### High: incompatible CSI frames could be compared

Capture truncated arbitrary payload lengths and included DC/guard carriers,
without checking bandwidth or receive errors. The station now requests HT20;
capture rejects errors, non-128-byte LLTF, and incompatible packet layouts.
Feature extraction retains 51 active carriers, excluding the possibly invalid
first four bytes. Queue frames older than 250 ms are discarded, as are frames
from before calibration or a suppression transition. Source BSSID and primary
channel must both match.

The interpretation follows Espressif's
[4.4.7 CSI layout and HT20/40 documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-guides/wifi.html#wi-fi-channel-state-information).
Actual router metadata and throughput still need bench testing.

### High: calibration mixed unrelated environments

Motor suppression reset motion but retained partially collected calibration.
AP refresh retained the previous BSSID on disconnect, and neither channel
changes nor gateway changes invalidated calibration. Partial calibration now
aborts on motor movement or a sampling gap. AP/channel/gateway changes and
observed disconnects invalidate it, flush capture state, and retarget the ping
session. Duplicate calibration requests are rejected; freshness and suppression
checks happen inside the calibration lock.

### Medium: a static channel change could latch motion indefinitely

Baseline adaptation previously stopped during motion. An enduring change in
reflections could therefore keep the detector above threshold forever. A slow
reference update now runs during all ready-state samples. The occupancy hold is
refreshed throughout hysteresis-active motion; it cannot expire while motion
still reads true. Suppressed or stale readings are unknown, not room-clear
evidence. This remains an experimental heuristic requiring field qualification.

### Medium: fade timing and manual/settings writes could conflict

New motion edges restarted an existing fade, extending it beyond two seconds.
They no longer do. Manual overrides consume the active event. Action saves and
automation updates share the web-state lock, so disabling the action cancels
the fade before the save response. Brightness reads are atomic across the web
and asynchronous HTTP tasks. Invalid stored action values fail closed.

### Medium: settings failures were hidden or misleading

Initial settings load failures were swallowed, leaving an enabled selector
that could overwrite the saved choice. Polling overwrote save errors/successes,
could overlap on slow connections, and waited for unrelated commissioning
settings before starting. Controls now wait for a successful load, requests
have timeouts, polling is sequential, and command feedback is separate from
sensor status. Duplicate calibration submissions are prevented. Stale scores
are hidden, and the page explicitly explains immediate calibration start,
full-brightness fade, manual override, no auto-off, and stationary-occupancy
limitations. `TuningUI.h` is now `SettingsUI.h`; the old URL still redirects.
Presence API routes use exact matching.

### Low: initialization cleanup and misleading freshness edge cases

Failed initialization releases its queue/mutex. Repeated successful `begin()`
calls are harmless. A zero millisecond timestamp is no longer a missing-sample
sentinel, and sample age is saturated instead of overflowing signed telemetry.
Settling deadlines are no longer rechecked after settling finishes.

## Validation and remaining limits

- Native harness: passes existing motion tests and presence regressions for
  invalid carrier data, calibration interruptions, missing samples, static
  changes, occupancy expiry, clock wrap, fade duration, disabling, and manual
  overrides.
- The native harness also passes with AddressSanitizer and UndefinedBehaviorSanitizer.
- Embedded settings JavaScript: six mocked DOM/network tests pass, including
  failed load/save, calibration request races, stale displays, timeouts,
  sequential polling, and independent page initialization.
- All six ESP32 environments compile on the pinned Arduino 2.0.17 / IDF 4.4.7
  stack, including commissioning and OTA variants. Production uses 1,368,541
  of 1,572,864 flash bytes (87.0%) and 84,664 of 327,680 static RAM bytes (25.8%).
- `git diff --check` passes.

These are host/compile checks, not evidence of real-room detection accuracy or
FreeRTOS/radio timing under load. Queue/SDK lifecycle paths were code-reviewed
and firmware-compiled, not exercised with a simulated radio. Hardware acceptance
remains: usable CSI on the installed AP, empty-room drift, walk-up repeatability,
stationary people, motor/settling isolation, reconnect/channel changes, NVS
reboot persistence, manual overrides, and concurrent OTA/web traffic.

The [5.4 assessment](ESP_IDF_5_4_UPGRADE.md) remains applicable: the pinned stack
already supports this prototype, and the earlier 5.4 migration probe exceeded
the current OTA slot. That probe's exact size is a historical measurement,
not a size estimate for this revised firmware. Field results should determine
whether the packaged sensing algorithm warrants the separate migration and
hardware requalification work.
