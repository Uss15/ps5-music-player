# PS5 Music Player

A homebrew music-player foundation for an exploited PS5. The first milestone scans `/data/music`, builds a playlist, and exposes a small control protocol. The audio-output backend is deliberately isolated so it can be implemented and tested against the exact SDK/firmware environment available on the device.

## Current status

- recursive playlist scan for `.mp3`, `.wav`, `.ogg`, and `.flac`
- deterministic alphabetical ordering
- `PLAYLIST` and `NEXT` commands over a local TCP control socket
- safe path filtering and clear logging
- audio decoding/output backend is the next milestone; this repository does not pretend that an unsupported SCE audio API is already implemented

## Requirements

- [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk)
- an ELF loader compatible with your PS5 homebrew environment
- Python 3.10+ for the optional host controller

Firmware support depends on the SDK, loader, and libraries available in your environment. Test on a device before relying on any API.

## Build

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
export LLVM_CONFIG=/usr/bin/llvm-config-18
make
```

The result is `ps5-music-player.elf`. Put music files under `/data/music` and run the payload. It listens on TCP port `9023` for control commands.

```sh
python3 host/music_ctl.py --host 192.168.1.50 playlist
python3 host/music_ctl.py --host 192.168.1.50 next
```

## Roadmap

1. PCM output adapter for the audio API confirmed on the target environment.
2. MP3/OGG decoding through a separately licensed decoder.
3. queue, pause, seek, and controller input.
4. optional local web UI.

## License

GPL-3.0-or-later. Decoder libraries, if added later, must retain their own licenses.
