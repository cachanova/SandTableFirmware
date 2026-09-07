#!/usr/bin/env python3
import argparse
import codecs
import sys
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser(description="Read serial output for a fixed duration.")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port path")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--duration", type=float, default=10.0, help="Seconds to capture")
    parser.add_argument("--chunk", type=int, default=4096, help="Read chunk size in bytes")
    parser.add_argument("--reset", action="store_true", help="Toggle RTS/DTR to reset before reading")
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    try:
        # PySerial asserts DTR when opening by default. Release both modem-control
        # lines immediately so simply observing logs cannot influence boot pins.
        ser.dtr = False
        ser.rts = False
        if args.reset:
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
            ser.dtr = True
            time.sleep(0.1)
            # Release GPIO0 after the reset sequence. Leaving DTR asserted can
            # force a later brownout/watchdog reset into the ROM bootloader.
            ser.dtr = False
        decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        start = time.time()
        while time.time() - start < args.duration:
            data = ser.read(args.chunk)
            if data:
                sys.stdout.write(decoder.decode(data))
                sys.stdout.flush()
        sys.stdout.write(decoder.decode(b"", final=True))
        sys.stdout.flush()
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
