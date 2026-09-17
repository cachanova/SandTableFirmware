#!/usr/bin/env python3
"""Measure LED command latency without changing brightness or starting motion.

Run while no one else is using the brightness slider. Stops if its target changes.
--load adds concurrent status and file-list reads; no files/settings are written.
"""
import argparse
import concurrent.futures
import json
import math
import threading
import time
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default='http://100.76.149.200')
    parser.add_argument('--samples', type=int, default=30)
    parser.add_argument('--load', action='store_true')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if not 1 <= args.samples <= 300:
        parser.error('samples must be 1–300')
    rows, errors = [], []
    stop = threading.Event()

    def request(path, data=None):
        body = None if data is None else urllib.parse.urlencode(data).encode()
        start = time.monotonic()
        req = urllib.request.Request(args.base.rstrip('/') + path, data=body)
        with urllib.request.urlopen(req, timeout=4) as response:
            result = json.load(response)
        rows.append({'path': path, 'method': 'GET' if data is None else 'POST',
                     'ms': round((time.monotonic() - start) * 1000, 3)})
        return result

    def load():
        while not stop.is_set():
            try:
                request('/api/status')
                request('/api/files')
            except Exception as error:
                errors.append(str(error))
                stop.set()
            stop.wait(.5)

    before = request('/api/status')
    target = before['ledTargetBrightness']
    samples = []
    after = None
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        if args.load:
            pool.submit(load)
        try:
            for _ in range(args.samples):
                if stop.is_set():
                    break
                current = request('/api/led/brightness')
                if current['targetBrightness'] != target:
                    raise RuntimeError('Brightness changed externally; stopping probe')
                reply = request('/api/led/brightness', {'brightness': target})
                if reply.get('success') is not True:
                    raise RuntimeError(f'LED command rejected: {reply}')
                current = request('/api/led/brightness')
                if current['targetBrightness'] != target or current['brightness'] != target:
                    raise RuntimeError(f'Brightness readback mismatch: {current}')
                if 'pwmDuty' in current:
                    expected = 256 if target == 100 else target * 255 // 100
                    if not current['pwmReady'] or current['pwmDuty'] != expected:
                        raise RuntimeError(f'PWM readback mismatch: {current}')
                samples.append(current)
                stop.wait(.25)
            after = request('/api/status')
            if after['uptime'] < before['uptime']:
                raise RuntimeError('Device restarted during probe')
        except Exception as error:
            errors.append(str(error))
        finally:
            stop.set()
    durations = sorted(row['ms'] for row in rows if row['method'] == 'POST')
    def percentile(fraction):
        return durations[max(0, math.ceil(len(durations) * fraction) - 1)] if durations else None
    report = {'load': args.load, 'target': target, 'before': before, 'after': after,
              'errors': errors, 'completed': len(samples), 'requested': args.samples,
              'latencyMs': {'p50': percentile(.5), 'p95': percentile(.95),
                            'max': max(durations) if durations else None},
              'samples': samples, 'requests': rows}
    with open(args.output, 'w') as output:
        json.dump(report, output, indent=2)
    print(json.dumps({k: report[k] for k in ('completed', 'requested', 'latencyMs', 'errors')}))
    return 1 if errors or len(samples) != args.samples else 0


if __name__ == '__main__':
    raise SystemExit(main())
