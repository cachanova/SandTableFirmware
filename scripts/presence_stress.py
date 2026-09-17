#!/usr/bin/env python3
"""Bounded, GET-only stationary device load test. Never flashes or starts motion.

Prints a JSON report to stdout; progress goes to stderr. Calibration and firmware
installation are deliberately separate, operator-controlled steps.
"""
import argparse
import collections
import hashlib
import json
import statistics
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request


def check_memory(info):
    """A previous low-water failure cannot become a pass on the same boot."""
    assert info.get('heap8Bit', info['heap']) >= 8192, 'heap reserve guard'
    assert info.get('largestFree8BitBlock', info['largestFreeBlock']) >= 4096, \
        'fragmentation guard'
    if 'minimumFree8BitHeap' in info:
        assert info['minimumFree8BitHeap'] >= 8192, \
            'transient byte-heap low below 8 KiB; reboot before retesting'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default='http://100.76.149.200')
    parser.add_argument('--duration', type=int, default=120)
    parser.add_argument('--presence', action='store_true', help='Require live CSI samples')
    args = parser.parse_args()
    if not 10 <= args.duration <= 600:
        parser.error('duration must be 10–600 seconds')
    stop = threading.Event()
    rows, samples, failures = [], [], []
    images, events = {}, [0, 0]
    lock = threading.Lock()
    start = time.monotonic()

    def fail(message):
        with lock:
            failures.append(str(message))
        stop.set()

    def request(path):
        began = time.monotonic()
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            response = opener.open(args.base.rstrip('/') + path, timeout=12)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            body = response.read(5 * 1024 * 1024 + 1)
            assert len(body) <= 5 * 1024 * 1024, 'response size bound exceeded'
            length = response.headers.get('Content-Length')
            assert length is None or len(body) == int(length), 'truncated response: ' + path
            code = response.status
        with lock:
            rows.append({'path': path, 'code': code, 'bytes': len(body),
                         'ms': round((time.monotonic() - began) * 1000, 2)})
        assert code in (200, 503), (path, code, body[:200])
        return None if code == 503 else body

    def get_json(path):
        body = request(path)
        return json.loads(body) if body is not None else None

    def monitor_json(path):
        # Busy is normal admission control, not a transport failure. Allow
        # three short retries, but stop rather than load the device blind.
        for attempt in range(4):
            value = get_json(path)
            if value is not None:
                return value
            if stop.wait(.25 * (attempt + 1)):
                return None
        raise AssertionError('monitor busy after bounded retries: ' + path)

    initial = get_json('/api/status')
    assert initial and initial['state'] == 'IDLE', 'refusing load: table is not idle'
    initial_info = get_json('/api/system/info')
    assert initial_info, 'system info busy; retry later'
    check_memory(initial_info)
    initial_errors = get_json('/api/errors')
    assert initial_errors is not None, 'error log busy; retry later'
    if args.presence:
        assert initial['presence']['available'] and initial['presence']['receiving'], \
            'presence is not receiving samples'
    library = get_json('/api/files')
    assert library and not library.get('loading'), 'library not ready'
    image_names = [f['name'] for f in library['files'] if f.get('hasImage')][:4]
    assert image_names, 'no existing images to test'

    def monitor():
        previous_uptime = initial['uptime']
        stale = 0
        while not stop.is_set():
            status = monitor_json('/api/status')
            info = monitor_json('/api/system/info')
            if status and info:
                # Preserve the failing sample too, so a safety-stop report
                # contains the exact heap/state values that triggered it.
                with lock:
                    samples.append({'at': round(time.monotonic() - start, 2),
                                    'status': status, 'memory': info})
                assert status['uptime'] >= previous_uptime, 'device rebooted'
                previous_uptime = status['uptime']
                assert status['state'] == 'IDLE', 'table state changed; stopping load'
                # Allow the designed 16 KiB presence pause / 24 KiB resume
                # hysteresis to operate, while keeping an 8 KiB hard floor.
                check_memory(info)
                if args.presence:
                    presence = status['presence']
                    assert presence['available'], 'CSI unavailable'
                    stale = stale + 1 if (not presence['receiving'] and
                                          not presence['memoryLimited']) else 0
                    assert stale < 5, 'CSI stale over five monitoring samples'
            stop.wait(1)

    def pages():
        paths = ['/', '/settings' if args.presence else '/tuning', '/api/files',
                 '/api/presence' if args.presence else '/api/tuning']
        index = 0
        while not stop.is_set():
            request(paths[index % len(paths)])
            index += 1
            stop.wait(.5)

    def downloads():
        index = 0
        while not stop.is_set():
            name = image_names[index % len(image_names)]
            body = request('/api/pattern/image?file=' + urllib.parse.quote(name))
            if body is not None:
                assert body.startswith(b'\x89PNG\r\n\x1a\n'), 'invalid PNG'
                digest = hashlib.sha256(body).hexdigest()
                assert images.setdefault(name, digest) == digest, 'image bytes changed'
                index += 1
            stop.wait(.5)

    def stream(index):
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        request = urllib.request.Request(args.base.rstrip('/') + '/api/stream',
                                         headers={'Accept': 'text/event-stream'})
        with opener.open(request, timeout=5) as response:
            while not stop.is_set():
                line = response.readline(4096)
                assert line, 'SSE closed unexpectedly'
                if line.startswith(b'data:'):
                    value = json.loads(line[5:])
                    assert all(key in value for key in ('x', 'y', 'r', 't'))
                    events[index] += 1

    def guarded(function, *values):
        try:
            function(*values)
        except Exception as error:
            if not stop.is_set():
                fail(function.__name__ + ': ' + repr(error))

    jobs = [(monitor, ()), (pages, ()), (downloads, ()),
            (stream, (0,)), (stream, (1,))]
    threads = [threading.Thread(target=guarded, args=(fn, *values)) for fn, values in jobs]
    for thread in threads:
        thread.start()
    while not stop.wait(min(15, max(0, args.duration - (time.monotonic() - start)))):
        print(f'{time.monotonic() - start:.0f}s: {len(rows)} requests, '
              f'{len(samples)} memory samples, SSE {events}', file=sys.stderr, flush=True)
        if time.monotonic() - start >= args.duration:
            break
    stop.set()
    for thread in threads:
        thread.join()
    # Give disconnected TCP/image workers time to release resources.
    time.sleep(5)
    final = final_info = errors = logs = None
    try:
        final = get_json('/api/status')
        errors = get_json('/api/errors')
        body = request('/api/logs/text')
        logs = body.decode() if body is not None else None
        final_info = get_json('/api/system/info')
        assert final and final_info, 'recovery endpoints busy'
        check_memory(final_info)
        assert final['state'] == 'IDLE' and final['uptime'] >= initial['uptime']
        if args.presence:
            assert final['presence']['receiving'] and not final['presence']['suppressed'], \
                'presence did not resume after load'
        assert errors is not None and errors['total'] == initial_errors['total'], \
            'device logged new errors'
        assert all(events), 'both SSE clients must receive events'
        assert images, 'no image completed'
    except Exception as error:
        failures.append('recovery: ' + repr(error))
    groups = collections.defaultdict(list)
    for row in rows:
        groups[row['path'].split('?')[0]].append(row['ms'])
    summary = {path: {'count': len(values), 'p50_ms': statistics.median(values),
                     'p95_ms': sorted(values)[int((len(values) - 1) * .95)],
                     'max_ms': max(values)} for path, values in groups.items()}
    print(json.dumps({'passed': not failures, 'duration_s': time.monotonic() - start,
                      'presence_required': args.presence, 'initial': initial,
                      'initial_errors': initial_errors,
                      'initial_memory': initial_info, 'final': final,
                      'final_memory': final_info, 'failures': failures,
                      'busy_responses': sum(row['code'] == 503 for row in rows),
                      'latencies': summary, 'samples': samples, 'requests': rows,
                      'image_sha256': images, 'sse_events': events,
                      'errors': errors, 'logs': logs}, indent=2))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
