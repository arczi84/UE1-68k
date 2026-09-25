ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_DIR := $(ROOT)/build/minigl-replay
SDK := /mnt/d/dev/Pistorm3D/Pistorm3D_v12
CC := /opt/amiga-gcc16-rc9/bin/m68k-amigaos-gcc
CFLAGS := -B/home/arczi/toolchains/amiga-gcc16-modefix/ -mcrt=nix20 -mcpu=68060 -m68881 -O2 -fomit-frame-pointer -fno-optimize-sibling-calls -fno-fast-math -fno-strict-aliasing -fno-PIC -fno-pic -std=gnu11 -Wall -Wextra
.PHONY: all test
all: $(BUILD_DIR)/MGL-replay
$(BUILD_DIR)/MGL-replay: trace.c Makefile
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(SDK)/include -I/mnt/d/amiga-gcc2/include $< $(SDK)/libminigl_dispatch.a -Wl,--strip-all -o $@

HOST_FLAGS := -std=c11 -g -fsanitize=address,undefined -DTRACE_HOST_TEST
$(BUILD_DIR)/host-tests/capture.o: trace.c tests/test_api.h
	mkdir -p $(BUILD_DIR)/host-tests
	cc $(HOST_FLAGS) -DMGL_CAPTURE -c $< -o $@
$(BUILD_DIR)/host-tests/replay.o: trace.c tests/test_api.h
	mkdir -p $(BUILD_DIR)/host-tests
	cc $(HOST_FLAGS) -Dmain=ReplayMain -c $< -o $@
$(BUILD_DIR)/host-tests/roundtrip: tests/roundtrip.c tests/test_api.h $(BUILD_DIR)/host-tests/capture.o $(BUILD_DIR)/host-tests/replay.o
	cc $(HOST_FLAGS) tests/roundtrip.c $(BUILD_DIR)/host-tests/capture.o $(BUILD_DIR)/host-tests/replay.o -o $@
test: $(BUILD_DIR)/host-tests/roundtrip
	test_run_dir=$$(mktemp -d /tmp/ue-mgl-roundtrip.XXXXXX) && cd "$$test_run_dir" && $(BUILD_DIR)/host-tests/roundtrip
