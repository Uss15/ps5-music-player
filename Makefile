PS5_HOST ?= ps5
PS5_PORT ?= 9021
PS5_PAYLOAD_SDK ?=

ifeq ($(strip $(PS5_PAYLOAD_SDK)),)
$(error Set PS5_PAYLOAD_SDK to your installed ps5-payload-sdk directory)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

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

.PHONY: all clean test
