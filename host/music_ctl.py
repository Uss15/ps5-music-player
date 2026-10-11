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
    "play": "PLAY",
    "volume": "VOLUME",
}


def main() -> int:
    parser = argparse.ArgumentParser(description="PS5 Music Player host controller")
    parser.add_argument("--host", required=True, help="PS5 IP address")
    parser.add_argument("--port", type=int, default=9023)
    parser.add_argument("--track", type=int, help="Track number for the play command")
    parser.add_argument("--volume", type=int, help="Volume 0-100 for the volume command")
    parser.add_argument("command", choices=COMMANDS)
    args = parser.parse_args()

    if args.command == "play" and (args.track is None or args.track < 1):
        parser.error("play requires --track with a positive playlist number")
    if args.command == "volume" and (args.volume is None or not 0 <= args.volume <= 100):
        parser.error("volume requires --volume between 0 and 100")
    command = COMMANDS[args.command]
    if args.command == "play":
        command += f" {args.track}"
    elif args.command == "volume":
        command += f" {args.volume}"

    try:
        with socket.create_connection((args.host, args.port), timeout=5) as sock:
            sock.settimeout(5)
            sock.sendall((command + "\n").encode("ascii"))
            stream = sock.makefile("rb")
            first = stream.readline()
            if not first:
                return 1
            text = first.decode("utf-8", errors="replace")
            print(text, end="")
            if args.command == "playlist" and text.rstrip("\r\n") == "OK":
                while True:
                    line = stream.readline()
                    if not line:
                        break
                    text = line.decode("utf-8", errors="replace")
                    print(text, end="")
                    if text.rstrip("\r\n") == ".":
                        break
            elif text.startswith("OK"):
                line = stream.readline()
                if line:
                    print(line.decode("utf-8", errors="replace"), end="")
    except (OSError, TimeoutError) as exc:
        print(f"Could not contact PS5 at {args.host}:{args.port}: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
