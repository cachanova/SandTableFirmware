import importlib.util
import json
from pathlib import Path
from http.server import BaseHTTPRequestHandler, HTTPServer
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('refresh_previews', Path(__file__).parents[1] / 'scripts/refresh_previews.py')
refresh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(refresh)


class UploadFramingTests(unittest.TestCase):
    def test_busy_retry_replays_complete_length_delimited_multipart(self):
        received = []

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers['Content-Length']))
                received.append((self.path, dict(self.headers), body))
                status = 503 if len(received) == 1 else 200
                result = json.dumps({'success': status == 200}).encode()
                self.send_response(status)
                self.send_header('Content-Length', str(len(result)))
                self.end_headers()
                self.wfile.write(result)

            def log_message(self, *args):
                pass

        server = HTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        data = bytes(range(256)) * 100
        try:
            with patch.object(refresh.time, 'sleep'):
                refresh.Table(f'http://127.0.0.1:{server.server_port}').upload('test.png', data, True)
        finally:
            server.shutdown()
            thread.join()
            server.server_close()
        self.assertEqual(len(received), 2)
        self.assertEqual(received[0], received[1])
        path, headers, body = received[0]
        self.assertEqual(path, '/api/files/upload?thumbnail=1')
        self.assertNotIn('Transfer-Encoding', headers)
        self.assertEqual(int(headers['Content-Length']), len(body))
        self.assertEqual(body.split(b'\r\n\r\n', 1)[1], data + b'\r\n--PolarPreviewRefresh2026--\r\n')
