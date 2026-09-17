#!/usr/bin/env python3
"""GET-only image cancellation/recovery check; requires an idle, homed table.

Run after presence_stress.py on the same boot to check cumulative reserve.
Never uploads files, changes settings, or commands motion.
"""
import argparse
import hashlib
import http.client
import json
import time
import urllib.parse

from presence_stress import check_memory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default='http://100.76.149.200')
    parser.add_argument('--cycles', type=int, default=20)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 100:
        parser.error('cycles must be 1–100')
    base = urllib.parse.urlsplit(args.base)
    if base.scheme != 'http' or not base.hostname:
        parser.error('base must be an http:// device address')
    report = {'passed': False, 'cycles': [], 'failures': []}
    started = time.monotonic()

    def get(path, cancel=False):
        for attempt in range(20):
            connection = http.client.HTTPConnection(base.hostname, base.port, timeout=12)
            response = None
            try:
                connection.request('GET', path, headers={'Connection': 'close'})
                response = connection.getresponse()
                if response.status == 503:
                    response.read()
                else:
                    assert response.status == 200, (path, response.status)
                    length = int(response.getheader('Content-Length'))
                    if cancel:
                        assert length > 128, 'fixture too small for cancellation'
                        return response.read(128)
                    body = response.read()
                    assert len(body) == length, 'truncated response'
                    return body
            finally:
                if response is not None:
                    response.close()
                connection.close()
            time.sleep(.25)
        raise AssertionError('busy after bounded retries: ' + path)

    def get_json(path):
        return json.loads(get('/api/' + path))

    try:
        initial = get_json('status')
        assert initial['state'] == 'IDLE', 'table must be idle'
        initial_errors = get_json('errors')
        report['initial_memory'] = get_json('system/info')
        check_memory(report['initial_memory'])
        library = get_json('files')
        names = [entry['name'] for entry in library['files'] if entry.get('hasImage')][:4]
        assert names, 'no existing image fixtures'
        hashes = {}
        for index in range(args.cycles):
            name = names[index % len(names)]
            path = '/api/pattern/image?file=' + urllib.parse.quote(name)
            began = time.monotonic()
            assert get(path, cancel=True).startswith(b'\x89PNG\r\n\x1a\n')
            image = get(path)
            assert image.startswith(b'\x89PNG\r\n\x1a\n')
            digest = hashlib.sha256(image).hexdigest()
            assert hashes.setdefault(name, digest) == digest, 'image bytes changed'
            memory = get_json('system/info')
            check_memory(memory)
            status = get_json('status')
            assert status['state'] == 'IDLE' and status['uptime'] >= initial['uptime']
            report['cycles'].append({'file': name, 'memory': memory,
                                     'seconds': time.monotonic() - began})
        time.sleep(5)
        report['errors'] = get_json('errors')
        report['final_memory'] = get_json('system/info')
        check_memory(report['final_memory'])
        assert report['errors']['total'] == initial_errors['total'], 'new device errors'
        report['image_sha256'] = hashes
        report['passed'] = True
    except Exception as error:
        report['failures'].append(repr(error))
    report['duration_s'] = time.monotonic() - started
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
