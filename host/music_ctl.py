#!/usr/bin/env python3
"""Small TCP client for the PS5 Music Player payload."""
import argparse
import socket
import sys

COMMANDS = {
    "playlist": "PLAYLIST",
    "status": "STATUS",
    "next": "NEXT",
    "previous": "PREV",
    "pause": "PAUSE",
    "resume": "RESUME",
    "stop": "STOP",
}


def main() -> int:
    parser = argparse.ArgumentParser(description="PS5 Music Player host controller")
    parser.add_argument("--host", required=True, help="PS5 IP address")
    parser.add_argument("--port", type=int, default=9023)
    parser.add_argument("command", choices=COMMANDS)
    args = parser.parse_args()

    try:
        with socket.create_connection((args.host, args.port), timeout=5) as sock:
            sock.settimeout(5)
            sock.sendall((COMMANDS[args.command] + "\n").encode("ascii"))
            stream = sock.makefile("rb")
            while True:
                line = stream.readline()
                if not line:
                    break
                text = line.decode("utf-8", errors="replace")
                print(text, end="")
                if args.command != "playlist" or text.rstrip("\r\n") == ".":
                    break
    except (OSError, TimeoutError) as exc:
        print(f"Could not contact PS5 at {args.host}:{args.port}: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
