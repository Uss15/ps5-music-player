PS5_HOST ?= ps5
PS5_PORT ?= 9021
PS5_PAYLOAD_SDK ?=

NEEDS_SDK := $(filter all test release-assets,$(MAKECMDGOALS))
ifeq ($(strip $(MAKECMDGOALS)),)
NEEDS_SDK := all
endif

ifeq ($(strip $(NEEDS_SDK)),)
else
ifeq ($(strip $(PS5_PAYLOAD_SDK)),)
$(error Set PS5_PAYLOAD_SDK to your installed ps5-payload-sdk directory)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
endif

ELF := ps5-music-player.elf
CFLAGS := -Wall -Wextra -Werror -O2 -g
LDLIBS := -lSceNet -lSceAudioOut -pthread

all: $(ELF)

$(ELF): payload/main.c payload/audio.c payload/audio.h third_party/minimp3/minimp3.h third_party/minimp3/minimp3_ex.h
	$(CC) $(CFLAGS) -o $@ payload/main.c payload/audio.c $(LDLIBS)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

test-host:
	python3 -m unittest discover -s tests -v
	$(CC_HOST) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O2 -pthread -o /tmp/ps5-music-audio-test tests/audio_host_test.c payload/audio.c
	/tmp/ps5-music-audio-test

run-web:
	python3 host/music_web.py

run-desktop:
	python3 host/music_web.py --open-desktop

release-assets: $(ELF)
	rm -rf dist
	mkdir -p dist
	cp $(ELF) dist/ps5-music-player.elf
	zip -rq dist/PS5-Music-Desktop.zip README.md LICENSE run_desktop.bat run_desktop.sh host/music_web.py host/static/desktop.html

CC_HOST ?= cc

.PHONY: all clean test test-host run-web run-desktop release-assets
