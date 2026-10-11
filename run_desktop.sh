#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
exec python3 host/music_web.py --open-desktop
