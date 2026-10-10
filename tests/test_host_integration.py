#!/usr/bin/env python3
"""Linux integration test for playlist scanning and the TCP control protocol."""
from __future__ import annotations

import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


class PlaylistIntegrationTest(unittest.TestCase):
    def test_recursive_sorted_scan_and_explicit_audio_status(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ps5-music-test-") as tmp:
            root = Path(tmp)
            library = root / "library"
            usb0 = root / "usb0"
            usb1 = root / "usb1"
            (library / "nested").mkdir(parents=True)
            usb0.mkdir()
            usb1.mkdir()
            (library / "zeta.mp3").write_bytes(b"test")
            (library / "nested" / "Alpha.FLAC").write_bytes(b"test")
            (usb0 / "beta.ogg").write_bytes(b"test")
            (usb1 / "ignore.txt").write_text("not audio", encoding="utf-8")
            try:
                (usb1 / "linked.wav").symlink_to(library / "zeta.mp3")
            except OSError:
                pass

            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                port = probe.getsockname()[1]

            binary = root / "music-server"
            compile_cmd = [
                "gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2",
                f"-DPORT={port}", f'-DMUSIC_ROOT="{library}"',
                f'-DUSB0_ROOT="{usb0}"', f'-DUSB1_ROOT="{usb1}"',
                str(REPO / "payload" / "main.c"), "-o", str(binary),
            ]
            subprocess.run(compile_cmd, check=True, capture_output=True, text=True)
            server = subprocess.Popen([str(binary)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if server.poll() is not None:
                        self.fail("server exited before accepting connections")
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                            break
                    except OSError:
                        time.sleep(0.05)
                else:
                    self.fail("server did not start")

                result = subprocess.run(
                    ["python3", str(REPO / "host" / "music_ctl.py"), "--host", "127.0.0.1",
                     "--port", str(port), "playlist"],
                    check=True, capture_output=True, text=True, timeout=5,
                )
                lines = result.stdout.splitlines()
                self.assertEqual(lines[0], "OK")
                self.assertEqual(lines[-1], ".")
                listed = [line.split("\t", 1)[1] for line in lines[1:-1]]
                self.assertEqual(listed, sorted([
                    str(library / "nested" / "Alpha.FLAC"),
                    str(library / "zeta.mp3"),
                    str(usb0 / "beta.ogg"),
                ], key=str.casefold))

                status = subprocess.run(
                    ["python3", str(REPO / "host" / "music_ctl.py"), "--host", "127.0.0.1",
                     "--port", str(port), "status"],
                    check=True, capture_output=True, text=True, timeout=5,
                )
                self.assertIn("ERR audio-backend-not-implemented", status.stdout)
            finally:
                server.terminate()
                try:
                    server.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait(timeout=2)


if __name__ == "__main__":
    unittest.main()
