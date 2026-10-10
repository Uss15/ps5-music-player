#!/usr/bin/env python3
import argparse
import socket


def main():
    parser = argparse.ArgumentParser(description="PS5 Music Player host controller")
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=9023)
    parser.add_argument("command", choices=["playlist", "next"])
    args = parser.parse_args()
    with socket.create_connection((args.host, args.port), timeout=10) as sock:
        sock.sendall(("PLAYLIST\n" if args.command == "playlist" else "NEXT\n").encode())
        while True:
            data = sock.recv(4096)
            if not data: break
            print(data.decode("utf-8", errors="replace"), end="")
            if b".\n" in data or args.command == "next": break


if __name__ == "__main__":
    main()
