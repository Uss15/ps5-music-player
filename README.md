# PS5 Music Player

An early-stage homebrew music-player project for an exploited PS5. The goal is a browsable USB music library with playback controls that can coexist with a game. This repository is not yet a finished player: audio decoding/output, an on-console UI, and game-background behavior still require implementation and real-console testing.

## Implemented in this branch

- Recursive library scan for `.mp3`, `.wav`, `.ogg`, and `.flac` files.
- Scans `/data/music`, `/mnt/usb0`, and `/mnt/usb1`; USB mount paths can vary by firmware and loader, so verify them on the target console.
- Case-insensitive sorted playlist output, bounded path and track counts, and symbolic-link skipping.
- TCP playlist service on port `9023` and a host CLI that reads the complete multi-line response.
- Responsive English browser UI with PS5 connection fields, search, source labels, and a playlist view.
- Reserved control commands (`STATUS`, `NEXT`, `PREV`, `PAUSE`, `RESUME`, `STOP`) currently return `audio-backend-not-implemented`. They do not play or control audio yet.
- The browser UI is hosted by the computer/phone running the Python server. It is not rendered on the PS5 TV screen; a native console UI is still a separate task.

## Requirements

- [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk)
- An ELF loader compatible with the console's homebrew environment
- Python 3.10+ for the host controller and web UI

Firmware support depends on the SDK, loader, and libraries available in the target environment. This project has not been tested on a physical PS5 in this branch.

## Build

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
export LLVM_CONFIG=/usr/bin/llvm-config-18
make
```

The result is `ps5-music-player.elf`. Start it with a compatible ELF loader. The payload listens on TCP port `9023`.

```sh
python3 host/music_ctl.py --host 192.168.1.50 playlist
python3 host/music_ctl.py --host 192.168.1.50 status
```

Run the English web interface on the same computer as the browser:

```sh
make run-web
```

Open `http://127.0.0.1:8765`, enter the PS5's local IP address, and choose **Connect & scan**. To open the page from a phone on the same trusted home network, run `python3 host/music_web.py --listen 0.0.0.0` and browse to `http://<computer-ip>:8765`. The web bridge has no login yet; do not expose it to the public internet or an untrusted network.

The interface currently scans and displays the playlist. Play/pause/skip and volume controls are visibly disabled until the PS5 audio backend exists. This browser page runs on a computer or phone, not on the PS5 TV screen.

Run the host-side integration test on Linux without the PS5 SDK:

```sh
make test-host
```

## Target-device checks still needed

1. Confirm the PS5 firmware, loader, and SDK combination can build and load the ELF.
2. Confirm the mounted USB paths and whether supported audio files can be read from them.
3. Identify and test an audio output API and decoder on that target combination.
4. Implement pause/resume/skip and test output mixing while a game is running. The project does not currently guarantee that homebrew audio can be mixed with game audio or controlled through the PS5 Control Center.
5. Add a native on-console library/player UI if the target homebrew environment supports it.

## License

GPL-3.0-or-later. Any decoder added later must retain its own license and attribution.
