#!/usr/bin/env python3
"""Local English web UI and bridge to the PS5 Music Player payload."""
from __future__ import annotations

import argparse
import ipaddress
import json
import socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

STATIC_DIR = Path(__file__).resolve().parent / "static"
CONTROL_COMMANDS = {"next": "NEXT", "previous": "PREV", "pause": "PAUSE", "resume": "RESUME", "stop": "STOP"}
MAX_TRACKS = 2048
MAX_LINE = 4096


class BridgeError(Exception):
    """Expected validation or device communication error."""


def validate_target(host: str, port_value: str | int) -> tuple[str, int]:
    try:
        address = ipaddress.ip_address(host)
        port = int(port_value)
    except (ValueError, TypeError) as exc:
        raise BridgeError("Enter a valid private-network PS5 IP address and port.") from exc
    if not (address.is_private or address.is_loopback) or address.is_multicast or address.is_unspecified:
        raise BridgeError("For safety, the bridge only connects to private-network or loopback IP addresses.")
    if not 1 <= port <= 65535:
        raise BridgeError("Port must be between 1 and 65535.")
    return str(address), port


def exchange(host: str, port: int, command: str) -> list[str]:
    try:
        with socket.create_connection((host, port), timeout=3) as sock:
            sock.settimeout(3)
            sock.sendall((command + "\n").encode("ascii"))
            reader = sock.makefile("rb")
            first = reader.readline(MAX_LINE + 1)
            if len(first) > MAX_LINE:
                raise BridgeError("The PS5 sent an oversized response.")
            first_text = first.decode("utf-8", errors="replace").strip()
            if first_text.startswith("ERR"):
                raise BridgeError(first_text)
            if first_text != "OK":
                raise BridgeError("Unexpected response from the PS5 payload.")
            if command != "PLAYLIST":
                response = reader.readline(MAX_LINE + 1).decode("utf-8", errors="replace").strip()
                return [response] if response else []

            lines: list[str] = []
            while len(lines) < MAX_TRACKS:
                raw = reader.readline(MAX_LINE + 1)
                if not raw:
                    raise BridgeError("The playlist response ended unexpectedly.")
                if len(raw) > MAX_LINE:
                    raise BridgeError("The PS5 sent an oversized playlist entry.")
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if line == ".":
                    return lines
                lines.append(line)
            raise BridgeError(f"Playlist is too large (limit: {MAX_TRACKS} tracks).")
    except BridgeError:
        raise
    except (OSError, TimeoutError) as exc:
        raise BridgeError(f"Could not reach {host}:{port}: {exc}") from exc


class MusicWebHandler(BaseHTTPRequestHandler):
    server_version = "PS5MusicUI/0.1"

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[{self.log_date_time_string()}] {fmt % args}")

    def send_json(self, status: int, data: dict[str, object]) -> None:
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        parsed = urlsplit(self.path)
        if parsed.path == "/" or parsed.path == "/index.html":
            body = (STATIC_DIR / "index.html").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(body)
            return
        if parsed.path != "/api/playlist":
            self.send_json(404, {"error": "Not found."})
            return
        query = parse_qs(parsed.query)
        try:
            host, port = validate_target(query.get("host", [""])[0], query.get("port", ["9023"])[0])
            raw_tracks = exchange(host, port, "PLAYLIST")
            tracks = []
            for row in raw_tracks:
                number, separator, path = row.partition("\t")
                if not separator:
                    continue
                tracks.append({"number": number, "path": path, "title": Path(path).name})
            self.send_json(200, {"tracks": tracks, "count": len(tracks)})
        except BridgeError as exc:
            self.send_json(502, {"error": str(exc)})

    def do_POST(self) -> None:
        if urlsplit(self.path).path != "/api/control":
            self.send_json(404, {"error": "Not found."})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 1 <= length <= 2048:
                raise BridgeError("Invalid request size.")
            payload = json.loads(self.rfile.read(length))
            if not isinstance(payload, dict):
                raise BridgeError("Expected a JSON object.")
            host, port = validate_target(payload.get("host", ""), payload.get("port", 9023))
            requested = payload.get("command", "")
            command = CONTROL_COMMANDS.get(requested)
            if not command:
                raise BridgeError("Unknown control command.")
            response = exchange(host, port, command)
            self.send_json(200, {"response": response[0] if response else ""})
        except (BridgeError, json.JSONDecodeError, UnicodeDecodeError) as exc:
            self.send_json(409 if isinstance(exc, BridgeError) else 400, {"error": str(exc)})
        except (TypeError, ValueError) as exc:
            self.send_json(400, {"error": str(exc)})


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve the PS5 Music Player web UI")
    parser.add_argument("--listen", default="127.0.0.1", help="Bind address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8765, help="Web UI port")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    server = ThreadingHTTPServer((args.listen, args.port), MusicWebHandler)
    print(f"PS5 Music Player UI: http://{args.listen}:{args.port}")
    print("Keep this process on a trusted network; the UI bridge has no login yet.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping web UI.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
