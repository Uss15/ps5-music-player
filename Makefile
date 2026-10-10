PS5_HOST ?= ps5
PS5_PORT ?= 9021
PS5_PAYLOAD_SDK ?=

NEEDS_SDK := $(filter all test,$(MAKECMDGOALS))
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
LDLIBS := -lSceNet

all: $(ELF)

$(ELF): payload/main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

test-host:
	python3 -m unittest discover -s tests -v

.PHONY: all clean test test-host
