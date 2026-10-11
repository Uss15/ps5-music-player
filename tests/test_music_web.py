#!/usr/bin/env python3
"""HTTP and TCP integration tests for the local music web UI."""
from __future__ import annotations

import json
import socket
import threading
import unittest
from http.server import ThreadingHTTPServer
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from host.music_web import BridgeError, MusicWebHandler, validate_target


class FakePayload:
    def __enter__(self):
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen()
        self.port = self.sock.getsockname()[1]
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()
        return self

    def _serve(self):
        self.sock.settimeout(0.2)
        while not self.stop_event.is_set():
            try:
                conn, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            with conn:
                command = b""
                while not command.endswith(b"\n"):
                    part = conn.recv(1)
                    if not part:
                        break
                    command += part
                if command.strip() == b"PLAYLIST":
                    conn.sendall(b"OK\n1\t/mnt/usb0/Music/Sample.MP3\n.\n")
                elif command.strip() == b"STATUS":
                    conn.sendall(b"OK\nSTOPPED\t0\t0\t0\t80\t1\t\n")
                elif command.strip() == b"PLAY 1":
                    conn.sendall(b"OK\nQUEUED\n")
                elif command.strip() == b"VOLUME 70":
                    conn.sendall(b"OK\nVOLUME_SET\n")
                else:
                    conn.sendall(b"ERR no-tracks\n")

    def __exit__(self, *_args):
        self.stop_event.set()
        self.sock.close()
        self.thread.join(timeout=1)


class MusicWebTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.http = ThreadingHTTPServer(("127.0.0.1", 0), MusicWebHandler)
        cls.http_thread = threading.Thread(target=cls.http.serve_forever, daemon=True)
        cls.http_thread.start()
        cls.base = f"http://127.0.0.1:{cls.http.server_address[1]}"

    @classmethod
    def tearDownClass(cls):
        cls.http.shutdown()
        cls.http.server_close()
        cls.http_thread.join(timeout=2)

    def test_validates_private_target_only(self):
        self.assertEqual(validate_target("192.168.1.50", "9023"), ("192.168.1.50", 9023))
        with self.assertRaises(BridgeError):
            validate_target("8.8.8.8", "9023")

    def test_serves_desktop_player_page(self):
        with urlopen(self.base + "/desktop") as response:
            page = response.read().decode("utf-8")
        self.assertEqual(response.status, 200)
        self.assertIn('id="audio"', page)
        self.assertIn("webkitdirectory", page)
        self.assertIn("audio.play()", page)
        self.assertIn("Files stay in your browser", page)

    def test_serves_english_ui_and_bridges_playlist(self):
        with FakePayload() as payload:
            with urlopen(self.base + "/") as response:
                page = response.read().decode("utf-8")
                self.assertEqual(response.status, 200)
                self.assertIn('<html lang="en">', page)
                self.assertIn("Connect &amp; scan", page)
                self.assertIn('/ps5-controls.js', page)
            with urlopen(self.base + "/ps5-controls.js") as response:
                script = response.read().decode("utf-8")
                self.assertEqual(response.status, 200)
                self.assertIn("BGM output", script)
            query = urlencode({"host": "127.0.0.1", "port": payload.port})
            with urlopen(self.base + "/api/playlist?" + query) as response:
                data = json.loads(response.read())
            self.assertEqual(data["count"], 1)
            self.assertEqual(data["tracks"][0]["title"], "Sample.MP3")
            self.assertEqual(data["tracks"][0]["path"], "/mnt/usb0/Music/Sample.MP3")

    def test_reads_playback_status(self):
        with FakePayload() as payload:
            query = urlencode({"host": "127.0.0.1", "port": payload.port})
            with urlopen(self.base + "/api/status?" + query) as response:
                data = json.loads(response.read())
        self.assertTrue(data["available"])
        self.assertEqual(data["state"], "STOPPED")
        self.assertEqual(data["volume"], 80)

    def test_bridges_play_and_volume_commands(self):
        with FakePayload() as payload:
            for payload_json, expected in [
                ({"host": "127.0.0.1", "port": payload.port, "command": "play", "track": 1}, "QUEUED"),
                ({"host": "127.0.0.1", "port": payload.port, "command": "volume", "volume": 70}, "VOLUME_SET"),
            ]:
                request = Request(self.base + "/api/control", data=json.dumps(payload_json).encode(),
                                  headers={"Content-Type": "application/json"}, method="POST")
                with urlopen(request) as response:
                    self.assertEqual(json.loads(response.read())["response"], expected)
            bad = {"host": "127.0.0.1", "port": payload.port, "command": "play", "track": 0}
            request = Request(self.base + "/api/control", data=json.dumps(bad).encode(),
                              headers={"Content-Type": "application/json"}, method="POST")
            with self.assertRaises(HTTPError) as caught:
                urlopen(request)
            self.assertEqual(caught.exception.code, 409)

    def test_rejects_public_target_and_reports_control_errors(self):
        query = urlencode({"host": "8.8.8.8", "port": 9023})
        with self.assertRaises(HTTPError) as caught:
            urlopen(self.base + "/api/playlist?" + query)
        self.assertEqual(caught.exception.code, 502)
        self.assertIn("private-network", caught.exception.read().decode())

        with FakePayload() as payload:
            body = json.dumps({"host": "127.0.0.1", "port": payload.port, "command": "next"}).encode()
            request = Request(self.base + "/api/control", data=body,
                              headers={"Content-Type": "application/json"}, method="POST")
            with self.assertRaises(HTTPError) as control_error:
                urlopen(request)
            self.assertEqual(control_error.exception.code, 409)
            self.assertIn("no-tracks", control_error.exception.read().decode())


if __name__ == "__main__":
    unittest.main()
