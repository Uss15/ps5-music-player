# PS5 Music Player

An early-stage homebrew music-player project for an exploited PS5. The goal is a browsable USB music library with playback controls that can coexist with a game. This repository is not yet a finished player: audio decoding/output, an on-console UI, and game-background behavior still require implementation and real-console testing.

## Implemented in this branch

- Recursive library scan for `.mp3`, `.wav`, `.ogg`, and `.flac` files.
- Scans `/data/music`, `/mnt/usb0`, and `/mnt/usb1`; USB mount paths can vary by firmware and loader, so verify them on the target console.
- Case-insensitive sorted playlist output, bounded path and track counts, and symbolic-link skipping.
- TCP playlist service on port `9023` and a host CLI that reads the complete multi-line response.
- Reserved control commands (`STATUS`, `NEXT`, `PREV`, `PAUSE`, `RESUME`, `STOP`) currently return `audio-backend-not-implemented`. They do not play or control audio yet.

## Requirements

- [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk)
- An ELF loader compatible with the console's homebrew environment
- Python 3.10+ for the optional host controller

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

Run the host-side integration test on Linux without the PS5 SDK:

```sh
make test-host
```

## Target-device checks still needed

1. Confirm the PS5 firmware, loader, and SDK combination can build and load the ELF.
2. Confirm the mounted USB paths and whether supported audio files can be read from them.
3. Identify and test an audio output API and decoder on that target combination.
4. Test pause/resume/skip and output mixing while a game is running. The project does not currently guarantee that homebrew audio can be mixed with game audio or controlled through the PS5 Control Center.
5. Add an on-console library/player UI and test media formats, track duration, buffering, and error handling.

## License

GPL-3.0-or-later. Any decoder added later must retain its own license and attribution.
