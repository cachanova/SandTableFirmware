#!/usr/bin/env python3
"""Back up installed THRs and regenerate/verify PNGs with ThrGenCLI.

Run download, render, then upload. Upload sends only PNGs; source THRs and the
playlist are never changed. Keep the output directory as a rollback archive.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request


def digest(data):
    return hashlib.sha256(data).hexdigest()


class Table:
    def __init__(self, base):
        self.base = base.rstrip('/') + '/api/'

    def get(self, path):
        for attempt in range(8):
            try:
                with urllib.request.urlopen(self.base + path, timeout=30) as response:
                    data = response.read()
                    length = response.headers.get('Content-Length')
                    if length is not None and len(data) != int(length):
                        raise RuntimeError('Incomplete download: ' + path)
                    return data
            except urllib.error.HTTPError as error:
                if error.code != 503 or attempt == 7:
                    raise
                time.sleep(1)
        raise RuntimeError('Controller remains busy')

    def json(self, path):
        return json.loads(self.get(path))

    def image(self, name, thumbnail=False):
        return self.get('pattern/image?' + urllib.parse.urlencode(
            {'file': name, 'thumbnail': int(thumbnail)}))

    def upload(self, name, data, thumbnail=False):
        boundary = 'PolarPreviewRefresh2026'
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; '
                f'filename="{name}"\r\nContent-Type: image/png\r\n\r\n').encode()
        body += data + f'\r\n--{boundary}--\r\n'.encode()
        request = urllib.request.Request(
            self.base + 'files/upload?thumbnail=' + str(int(thumbnail)), data=body,
            headers={'Content-Type': 'multipart/form-data; boundary=' + boundary})
        with urllib.request.urlopen(request, timeout=60) as response:
            result = json.load(response)
            if not result.get('success'):
                raise RuntimeError(str(result))


def safe_name(name):
    if Path(name).name != name or not name.endswith('.thr') or any(c in name for c in '\\\r\n"'):
        raise ValueError('Unsafe pattern name: ' + name)
    return name[:-4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=['download', 'render', 'upload'])
    parser.add_argument('--base', default='http://100.76.149.200')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', type=Path)
    parser.add_argument('--only', action='append', help='Pattern filename; repeat to select several')
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    table = Table(args.base)
    snapshot = out / 'snapshot.json'
    if args.stage == 'download':
        if snapshot.exists():
            raise RuntimeError('Backup already exists; use a new output directory')
        inventory = table.json('files')
        if inventory.get('loading'):
            raise RuntimeError('Wait for the file index to finish')
        snapshot.write_text(json.dumps(inventory, indent=2))
    else:
        inventory = json.loads(snapshot.read_text())
    entries = inventory['files']
    if args.only:
        entries = [entry for entry in entries if entry['name'] in args.only]
        if len(entries) != len(set(args.only)):
            raise RuntimeError('Selected pattern is absent from the snapshot')
    results = []
    manifest = out / (args.stage + '-results.json')
    try:
        for entry in entries:
            name = entry['name']
            base = safe_name(name)
            folder = out / base
            folder.mkdir(exist_ok=True)
            source = folder / name
            result = {'name': name}
            if args.stage == 'download':
                data = table.get('pattern/download?' + urllib.parse.urlencode({'file': name}))
                if len(data) != entry['size']:
                    raise RuntimeError('THR changed during backup: ' + name)
                source.write_bytes(data)
                result['thr_sha256'] = digest(data)
                for thumb in (False, True):
                    if not entry.get('hasThumbnail' if thumb else 'hasImage'):
                        continue
                    data = table.image(name, thumb)
                    path = folder / ('before.thumb.png' if thumb else 'before.png')
                    path.write_bytes(data)
                    result[path.name + '_sha256'] = digest(data)
            elif args.stage == 'render':
                if not args.renderer:
                    raise ValueError('--renderer is required')
                command = [str(args.renderer.resolve()), 'vis', str(source),
                           '--png', str(folder / 'after.png'),
                           '--thumb', str(folder / 'after.thumb.png')]
                rendered = subprocess.run(command, capture_output=True, text=True, timeout=600)
                result['returncode'] = rendered.returncode
                result['output'] = rendered.stdout + rendered.stderr
                if rendered.returncode:
                    result['skipped'] = 'Renderer rejected source; original assets retained'
                else:
                    for file, size, limit in [('after.png', 800, 2*1024*1024),
                                               ('after.thumb.png', 128, 128*1024)]:
                        data = (folder / file).read_bytes()
                        assert data[:8] == b'\x89PNG\r\n\x1a\n'
                        assert struct.unpack('>II', data[16:24]) == (size, size)
                        assert len(data) <= limit
                        result[file + '_sha256'] = digest(data)
            else:
                rendered = json.loads((out / 'render-results.json').read_text())
                record = next(r for r in rendered if r['name'] == name)
                if record.get('returncode') != 0:
                    result['skipped'] = record.get('skipped', 'Render failed')
                else:
                    # Prevent installing a preview for a changed source file.
                    current = next(f for f in table.json('files')['files'] if f['name'] == name)
                    if (current['size'], current['time']) != (entry['size'], entry['time']):
                        raise RuntimeError('Source metadata changed: ' + name)
                    for thumb in (False, True):
                        file = 'after.thumb.png' if thumb else 'after.png'
                        data = (folder / file).read_bytes()
                        if digest(data) != record[file + '_sha256']:
                            raise RuntimeError('Rendered artifact changed: ' + file)
                        table.upload(base + '.png', data, thumb)
                        for attempt in range(30):
                            time.sleep(.3)
                            try:
                                if table.image(name, thumb) == data:
                                    break
                            except urllib.error.HTTPError as error:
                                if error.code not in (404, 503):
                                    raise
                        else:
                            raise RuntimeError('Readback did not match upload: ' + name + ' ' + file)
                        result[file + '_sha256'] = digest(data)
            results.append(result)
            manifest.write_text(json.dumps(results, indent=2))
            print(json.dumps(result), flush=True)
    finally:
        manifest.write_text(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
