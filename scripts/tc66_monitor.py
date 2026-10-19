#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["bleak>=0.22", "cryptography>=42"]
# ///
"""Log power from an RDTech TC66C USB-C meter over Bluetooth LE.

The meter's BLE module advertises as "BT24-M" with a UART service on FFE0.
Each poll writes "bgetva\\r\\n" to FFE2 and the meter notifies a 192-byte
AES-ECB frame on FFE1: three 64-byte blocks (pac1, pac2, pac3), each ending
in a CRC16/Modbus. Layout follows sigrok's rdtech-tc driver.

    uv run scripts/tc66_monitor.py --csv power.csv
"""
import argparse
import asyncio
import csv
import struct
import sys
import time
from datetime import datetime

from bleak import BleakClient, BleakScanner
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

DEVICE_NAME = 'BT24-M'
NOTIFY_UUID = '0000ffe1-0000-1000-8000-00805f9b34fb'
WRITE_UUID = '0000ffe2-0000-1000-8000-00805f9b34fb'
POLL_CMD = b'bgetva\r\n'
FRAME_LEN = 192
AES_KEY = bytes([
    0x58, 0x21, 0xfa, 0x56, 0x01, 0xb2, 0xf0, 0x26,
    0x87, 0xff, 0x12, 0x04, 0x62, 0x2a, 0x4f, 0xb0,
    0x86, 0xf4, 0x02, 0x60, 0x81, 0x6f, 0x9a, 0x0b,
    0xa7, 0xf1, 0x06, 0x61, 0x9a, 0xb8, 0x72, 0x88,
])
FIELDS = ['time', 'volts', 'amps', 'watts', 'ohms', 'mah', 'mwh',
          'temp_c', 'dplus_v', 'dminus_v']


def crc16_modbus(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def decode(frame):
    """Decrypt and parse one frame; raise ValueError if it is corrupt."""
    dec = Cipher(algorithms.AES(AES_KEY), modes.ECB()).decryptor()
    p = dec.update(frame) + dec.finalize()
    for i, magic in enumerate((b'pac1', b'pac2', b'pac3')):
        block = p[i * 64:(i + 1) * 64]
        if block[:4] != magic:
            raise ValueError(f'bad block {i} magic {block[:4]!r}')
        if crc16_modbus(block[:60]) != struct.unpack_from('<I', block, 60)[0]:
            raise ValueError(f'bad block {i} crc')

    def u(off):
        return struct.unpack_from('<I', p, off)[0]

    temp = u(92) * (-1 if u(88) else 1)
    return {
        'volts': round(u(48) * 1e-4, 4),
        'amps': round(u(52) * 1e-5, 5),
        'watts': round(u(56) * 1e-4, 4),
        'ohms': round(u(68) * 1e-1, 1),
        'mah': u(72),
        'mwh': u(76),
        'temp_c': temp,
        'dplus_v': round(u(96) * 1e-2, 2),
        'dminus_v': round(u(100) * 1e-2, 2),
    }


class Stats:
    def __init__(self):
        self.n = 0
        self.wh = 0.0
        self.sum_w = 0.0
        self.min_w = float('inf')
        self.max_w = 0.0
        self.last_t = None

    def add(self, t, watts):
        if self.last_t is not None:
            self.wh += watts * (t - self.last_t) / 3600
        self.last_t = t
        self.n += 1
        self.sum_w += watts
        self.min_w = min(self.min_w, watts)
        self.max_w = max(self.max_w, watts)

    def summary(self):
        if not self.n:
            return 'no samples'
        return (f'{self.n} samples  avg {self.sum_w / self.n:.3f} W  '
                f'min {self.min_w:.3f} W  max {self.max_w:.3f} W  '
                f'energy {self.wh:.4f} Wh')


async def find_meter(address, timeout):
    if address:
        return await BleakScanner.find_device_by_address(address, timeout=timeout)
    return await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=timeout)


async def session(device, args, writer, stats, deadline):
    """Poll one connection until it drops or the deadline passes."""
    buf = bytearray()
    got_frame = asyncio.Event()

    def on_notify(_, data):
        buf.extend(data)
        if len(buf) >= FRAME_LEN:
            got_frame.set()

    async with BleakClient(device, timeout=30) as client:
        print(f'connected to {device.address}', file=sys.stderr)
        await client.start_notify(NOTIFY_UUID, on_notify)
        misses = 0
        while client.is_connected and (deadline is None or time.monotonic() < deadline):
            started = time.monotonic()
            buf.clear()
            got_frame.clear()
            await client.write_gatt_char(WRITE_UUID, POLL_CMD, response=False)
            try:
                await asyncio.wait_for(got_frame.wait(), timeout=3)
                row = decode(bytes(buf[:FRAME_LEN]))
                misses = 0
            except (asyncio.TimeoutError, ValueError) as exc:
                misses += 1
                print(f'poll failed: {exc or "timeout"}', file=sys.stderr)
                if misses >= 5:
                    return
                continue
            stats.add(time.monotonic(), row['watts'])
            row['time'] = datetime.now().isoformat(timespec='milliseconds')
            if writer:
                writer.writerow(row)
                args.csv_file.flush()
            if not args.quiet:
                print(f"{row['time']}  {row['volts']:7.4f} V  {row['amps']:7.5f} A  "
                      f"{row['watts']:8.4f} W  {row['temp_c']:3d} C", flush=True)
            await asyncio.sleep(max(0, args.interval - (time.monotonic() - started)))


async def run(args):
    writer = None
    if args.csv_file:
        writer = csv.DictWriter(args.csv_file, fieldnames=FIELDS)
        if args.csv_file.tell() == 0:
            writer.writeheader()
    stats = Stats()
    deadline = time.monotonic() + args.duration if args.duration else None
    try:
        while deadline is None or time.monotonic() < deadline:
            device = await find_meter(args.address, timeout=30)
            if device is None:
                print('meter not found; is it powered with Bluetooth on?', file=sys.stderr)
                continue
            try:
                await session(device, args, writer, stats, deadline)
            except Exception as exc:  # BlueZ raises several types on link loss
                print(f'connection lost: {exc!r}', file=sys.stderr)
            stats.last_t = None  # do not integrate energy across a gap
            if deadline is None or time.monotonic() < deadline:
                print('reconnecting...', file=sys.stderr)
    finally:
        print(stats.summary(), file=sys.stderr)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--address', help=f'meter BLE address (default: first "{DEVICE_NAME}" found)')
    ap.add_argument('--interval', type=float, default=1.0, help='seconds between polls')
    ap.add_argument('--duration', type=float, help='stop after this many seconds')
    ap.add_argument('--csv', dest='csv_file', type=argparse.FileType('a', encoding='utf-8'),
                    help='append samples to this CSV file')
    ap.add_argument('-q', '--quiet', action='store_true', help='do not print each sample')
    args = ap.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
