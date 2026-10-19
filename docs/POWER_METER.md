# Power meter

`scripts/tc66_monitor.py` logs voltage, current, and power from an RDTech
TC66C USB-C meter over Bluetooth LE. Put the meter between the supply and
the table's USB-C input to measure the whole table.

```bash
uv run scripts/tc66_monitor.py                        # live readout, 1 Hz
uv run scripts/tc66_monitor.py --csv power.csv -q     # log quietly
uv run scripts/tc66_monitor.py --duration 3600 --interval 0.5 --csv idle.csv
```

`uv` installs the script's dependencies (`bleak`, `cryptography`) from its
inline metadata. The script reconnects after a dropped link and does not
integrate energy across the gap. On exit it prints the sample count, average,
minimum, and maximum power, and the energy in Wh.

CSV columns: `time, volts, amps, watts, ohms, mah, mwh, temp_c, dplus_v,
dminus_v`. `mah` and `mwh` are the meter's own group-0 accumulators, which
keep counting between sessions; use the script's energy total, or difference
two rows, for one run.

## Meter

- Bluetooth must be on in the meter's settings menu (item AE in the manual).
- The BLE module advertises as `BT24-M`, not `TC66C`. The script connects to
  the first `BT24-M` it finds; pass `--address` if more than one is nearby.
  The meter on hand is `48:87:2D:C2:89:62`.
- Linux pairs nothing; BlueZ connects directly. The phone app must be closed,
  since the module accepts one connection.
- At −85 to −91 dBm the first connect often times out and a retry succeeds.
  Keep the meter within a few metres of the computer for long logs.

## Protocol

The module exposes a UART service `FFE0`. Each poll writes `bgetva\r\n` to
`FFE2`; the meter notifies a 192-byte frame on `FFE1`. The frame is AES-256
ECB with the fixed key from sigrok's `rdtech-tc` driver. Decrypted, it is
three 64-byte blocks tagged `pac1`, `pac2`, `pac3`, each ending in a
little-endian CRC16/Modbus of its first 60 bytes. The script drops frames
with a bad tag or CRC.

All values are little-endian `uint32` at offsets into the decrypted frame:

| Offset | Field | Scale |
|-------:|-------|-------|
| 48 | voltage | 0.1 mV |
| 52 | current | 10 µA |
| 56 | power | 0.1 mW |
| 68 | load resistance | 0.1 Ω |
| 72 / 76 | group 0 mAh / mWh | 1 |
| 88 / 92 | temperature sign / magnitude | 1 °C |
| 96 / 100 | D+ / D− | 10 mV |

`pac1` also carries the product name (`TC66`) at offset 4 and firmware
version (`1.18` on this unit) at offset 8.

First check, October 18, 2026: a power bank charging at 19.38 V, 3.232 A,
62.63 W. V × A matched the reported power and V / A matched the reported
resistance, which confirms the offsets.
