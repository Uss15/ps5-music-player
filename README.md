# PS5 Music Player

An early test build with two ways to listen: a local computer player and a PS5 homebrew payload. The computer player plays files through the browser's audio engine. The ELF version scans PS5 storage and sends MP3/WAV PCM through the PS5 BGM AudioOut port; it still requires testing on a physical console before game-audio mixing can be claimed.

## Computer version

Requirements: Python 3.10+ and a current desktop browser. No Python packages are required.

On Windows, double-click `run_desktop.bat`. On Linux or macOS, run `./run_desktop.sh` (or `make run-desktop`). The app opens a local page at `http://127.0.0.1:8765/desktop`. Choose a folder containing music; the browser reads the selected files locally and does not upload them. Use the playlist to play, pause, skip, seek, and change volume. Playback format support depends on the installed browser; MP3 and WAV are broadly supported, while OGG/FLAC support varies.

To use the separate PS5 library/control page, run `make run-web` and open `http://127.0.0.1:8765`. The bridge defaults to loopback. It has no login; do not bind it to a public or untrusted network.

## PS5 ELF version

### Current capability

- Recursively scans `/data/music`, `/mnt/usb0`, and `/mnt/usb1` for MP3 and WAV files, sorting paths and skipping symbolic links.
- Serves the playlist and controls over TCP port `9023`.
- Decodes MP3 with the bundled CC0 minimp3 decoder and PCM WAV (8/16-bit mono/stereo).
- Resamples to 48 kHz stereo, then calls `sceAudioOut` on the PS5 BGM port. Pause, resume, stop, next, previous, status, and volume commands are implemented.
- Audio output, BGM routing, USB mount paths, and especially mixing with a running game are **not verified on real PS5 hardware**. The BGM route is selected in code; whether a given firmware/loader accepts it and mixes as desired must be tested on the target console.
- This is a payload plus a companion browser UI; it does not create a native PS5 TV-screen application or Control Center integration.

### Build

Install the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk), a compatible ELF loader, and LLVM 18. Then:

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
export LLVM_CONFIG=/usr/bin/llvm-config-18
make
```

Output: `ps5-music-player.elf`. Start it with a compatible ELF loader. The network service listens on port `9023` and reports whether `AudioOutOpen` succeeded. SDK and firmware compatibility varies; a successful compile does not prove playback on the console.

To build the ELF and a standalone computer ZIP for a GitHub release, run `make release-assets`. The files are written under `dist/`.

### Controller examples

```sh
python3 host/music_ctl.py --host 192.168.1.50 playlist
python3 host/music_ctl.py --host 192.168.1.50 status
python3 host/music_ctl.py --host 192.168.1.50 play --track 1
python3 host/music_ctl.py --host 192.168.1.50 pause
python3 host/music_ctl.py --host 192.168.1.50 resume
python3 host/music_ctl.py --host 192.168.1.50 next
python3 host/music_ctl.py --host 192.168.1.50 volume --volume 70
python3 host/music_ctl.py --host 192.168.1.50 stop
```

### Host tests

Run `make test-host` on Linux without a PS5 SDK. It exercises recursive scanning, the TCP protocol, the HTTP bridge, and the audio decode/output control path using a generated WAV fixture and a fake AudioOut sink. It does not test the PS5 audio hardware.

## Release status

Build artifacts are prepared only after the host tests pass and the ELF compiles. This is an experimental/pre-release build until it has been exercised on a physical PS5. Do not assume that game audio remains unaffected until tested on your own firmware/loader combination.

## Licenses

Project code: GPL-3.0-or-later. The bundled `third_party/minimp3` decoder is CC0 1.0; its license is included alongside the source.
