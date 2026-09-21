#!/usr/bin/env python3
import json
import time
import urllib.request
import urllib.parse
from urllib.error import URLError, HTTPError

BASE = 'http://100.76.149.200'


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=5) as resp:
        return resp.read().decode()


def post_form(path, data):
    body = urllib.parse.urlencode(data).encode('utf-8')
    req = urllib.request.Request(BASE + path, data=body, method='POST')
    req.add_header('Content-Type', 'application/x-www-form-urlencoded')
    with urllib.request.urlopen(req, timeout=5) as resp:
        return resp.read().decode()

def get_with_status(path):
    with urllib.request.urlopen(BASE + path, timeout=5) as resp:
        body = resp.read()
        return resp.status, resp.getheader('Content-Length'), len(body)


def safe(label, func):
    try:
        return func()
    except (HTTPError, URLError, OSError, ValueError) as exc:
        print(f"{label} error {exc}")
        return None


def main():
    status = safe('status', lambda: get('/api/status'))
    if status:
        print('status', status)

    bright_set = safe('brightness_set', lambda: post_form('/api/led/brightness', {'brightness': '0'}))
    if bright_set:
        print('brightness_set', bright_set)

    bright_get = safe('brightness_get', lambda: get('/api/led/brightness'))
    if bright_get:
        print('brightness_get', bright_get)

    speed_set = safe('speed_set', lambda: post_form('/api/speed', {'speed': '6'}))
    if speed_set:
        print('speed_set', speed_set)

    files_info = safe('files_info', lambda: get_with_status('/files'))
    if files_info is not None:
        print('files_status', files_info[0], 'files_header_len', files_info[1], 'files_body_len', files_info[2])

    tuning_info = safe('tuning_info', lambda: get_with_status('/tuning'))
    if tuning_info is not None:
        print('tuning_status', tuning_info[0], 'tuning_header_len', tuning_info[1], 'tuning_body_len', tuning_info[2])

    start = safe('start', lambda: post_form('/api/pattern/start', {'file': 'shell.thr', 'clearing': '0'}))
    if start:
        print('start', start)

    last = None
    for i in range(5):
        pos = safe('position', lambda: get('/api/position'))
        if not pos:
            break
        try:
            data = json.loads(pos)
        except json.JSONDecodeError:
            print('position_parse_error')
            break
        cur = data.get('current')
        if not cur:
            print('position none')
        else:
            key = (round(cur['x'], 4), round(cur['y'], 4))
            moved = last is not None and key != last
            print('position', key, 'moved' if moved else 'same')
            last = key
        time.sleep(0.5)

    status2 = safe('status2', lambda: get('/api/status'))
    if status2:
        print('status2', status2)


if __name__ == '__main__':
    main()
