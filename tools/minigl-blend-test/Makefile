ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_DIR := $(ROOT)/build/minigl-blend-test
SDK := /mnt/d/dev/Pistorm3D/Pistorm3D_v12
CC := /opt/amiga-gcc16-rc9/bin/m68k-amigaos-gcc
CFLAGS := -B/home/arczi/toolchains/amiga-gcc16-modefix/ -mcrt=nix20 -mcpu=68060 -m68881 -O2 -fomit-frame-pointer -fno-optimize-sibling-calls -fno-fast-math -fno-strict-aliasing -fno-PIC -fno-pic -fno-short-enums -std=gnu11 -Wall -Wextra

.PHONY: all
all: $(BUILD_DIR)/MGL-blend-test

$(BUILD_DIR)/main.o: $(ROOT)/tools/minigl-blend-test/main.c Makefile
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(SDK)/include -I/mnt/d/amiga-gcc2/include -c $< -o $@

$(BUILD_DIR)/MGL-blend-test: $(BUILD_DIR)/main.o
	$(CC) $(CFLAGS) -Wl,--strip-all -Wl,-Map=$(BUILD_DIR)/MGL-blend-test.map $< $(SDK)/libminigl_dispatch.a -lm -o $@
