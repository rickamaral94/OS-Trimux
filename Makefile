# TriMux build entry point.
#
#   make test            unit tests + Python tests (host, no device needed)
#   make toolchain       build the Docker cross toolchain image
#   make cross           cross-compile trimuxctl and trimux-ui (inside Docker)
#   make retroarch cores build RetroArch and libretro cores (inside Docker)
#   make sdcard          assemble the card tree in build/sdcard
#   make image           build build/out/TriMux-<ver>-brickpro.img.xz + SHA-256
#   make all-docker      everything above, reproducibly, through Docker
VERSION := $(shell cat VERSION)
HOST_CC ?= cc
CFLAGS ?= -O2 -g
WARN := -std=c11 -Wall -Wextra -Wshadow -Wno-unused-parameter -Werror=implicit-function-declaration \
        -Werror=format-security -Wno-format-truncation -D_FILE_OFFSET_BITS=64 -DTRIMUX_VERSION='"$(VERSION)"'
CORE_SRC := $(wildcard src/core/*.c)
UI_SRC := $(wildcard src/ui/*.c)
TOOLCHAIN_IMAGE ?= trimux-toolchain:$(shell sha256sum docker/Dockerfile docker/cmake-aarch64.cmake | sha256sum | cut -c1-12)
DOCKER_RUN = docker run --rm -u $$(id -u):$$(id -g) -v "$(CURDIR)":/src -w /src $(TOOLCHAIN_IMAGE)

NATIVE := build/native
CROSS := build/aarch64

.PHONY: all test test-unit test-py native cross toolchain retroarch cores sdcard image clean all-docker \
        firmware smoke ui-native sources

all: test

# ---------------------------------------------------------------- host build
$(NATIVE)/test_core: tests/unit/test_core.c $(CORE_SRC) src/core/*.h
	@mkdir -p $(@D)
	$(HOST_CC) $(WARN) $(CFLAGS) -fsanitize=address,undefined -o $@ tests/unit/test_core.c $(CORE_SRC)

$(NATIVE)/trimuxctl: src/tools/trimuxctl.c $(CORE_SRC) src/core/*.h
	@mkdir -p $(@D)
	$(HOST_CC) $(WARN) $(CFLAGS) -o $@ src/tools/trimuxctl.c $(CORE_SRC)

# The UI also builds on the host (SDL2 dummy/offscreen drivers) for screenshots/tests.
$(NATIVE)/trimux-ui: $(UI_SRC) $(CORE_SRC) src/core/*.h src/ui/*.h
	@mkdir -p $(@D)
	$(HOST_CC) $(WARN) $(CFLAGS) -o $@ $(UI_SRC) $(CORE_SRC) $$(pkg-config --cflags --libs sdl2) -lm

native: $(NATIVE)/trimuxctl $(NATIVE)/test_core
ui-native: $(NATIVE)/trimux-ui

test-unit: $(NATIVE)/test_core
	$(NATIVE)/test_core

test-py: $(NATIVE)/trimuxctl
	python3 -m pytest -q tests/py

test: test-unit test-py

# ---------------------------------------------------------------- cross build
CROSS_CC ?= aarch64-linux-gnu-gcc
CROSS_FLAGS := -O2 -march=armv8-a+crc -mtune=cortex-a53 -fstack-protector-strong -D_FORTIFY_SOURCE=2

$(CROSS)/trimuxctl: src/tools/trimuxctl.c $(CORE_SRC) src/core/*.h
	@mkdir -p $(@D)
	$(CROSS_CC) $(WARN) $(CROSS_FLAGS) -o $@ src/tools/trimuxctl.c $(CORE_SRC)
	aarch64-linux-gnu-strip $@

$(CROSS)/trimux-ui: $(UI_SRC) $(CORE_SRC) src/core/*.h src/ui/*.h
	@mkdir -p $(@D)
	$(CROSS_CC) $(WARN) $(CROSS_FLAGS) -o $@ $(UI_SRC) $(CORE_SRC) \
	    $$(pkg-config --cflags --libs sdl2) -lm -Wl,-rpath-link,/opt/aarch64/lib
	aarch64-linux-gnu-strip $@

cross: $(CROSS)/trimuxctl $(CROSS)/trimux-ui

# ---------------------------------------------------------------- docker wrappers
docker/downloads/SDL2-2.30.8.tar.gz:
	scripts/fetch.sh https://github.com/libsdl-org/SDL/releases/download/release-2.30.8/SDL2-2.30.8.tar.gz \
	    380c295ea76b9bd72d90075793971c8bcb232ba0a69a9b14da4ae8f603350058 $@

toolchain: docker/downloads/SDL2-2.30.8.tar.gz
	docker image inspect $(TOOLCHAIN_IMAGE) >/dev/null 2>&1 || docker build -t $(TOOLCHAIN_IMAGE) docker/

docker-%: toolchain
	$(DOCKER_RUN) make $*

retroarch:
	scripts/build_retroarch.sh

cores:
	scripts/build_cores.sh

sources:
	scripts/fetch_sources.sh

firmware:
	scripts/fetch_firmware.sh

smoke:
	scripts/smoke_qemu.sh

sdcard:
	scripts/assemble_sdcard.sh

image: sdcard
	python3 scripts/make_image.py --version $(VERSION) --tree build/sdcard --out build/out
	python3 scripts/make_update_zip.py --version $(VERSION) --tree build/sdcard --out build/out
	cd build/out && sha256sum TriMux-$(VERSION)-update.zip TriMux-$(VERSION)-update.tar.gz >> TriMux-$(VERSION)-brickpro.sha256

# Full reproducible pipeline: sources and firmware are fetched on the host
# (pinned commits / SHA-256); everything else runs in the pinned container.
all-docker: toolchain sources firmware
	$(DOCKER_RUN) make test cross retroarch cores
	$(DOCKER_RUN) scripts/smoke_qemu.sh
	$(DOCKER_RUN) make image

clean:
	rm -rf build
