# N84 - top-level Makefile
#   make host      build host (Linux) tools: test runners, headless emulator
#   make test      run host test suites (ZEXDOC; ZEXALL with `make test-full`)
#   make nspire    build the Ndless .tns (requires the Ndless SDK in PATH)
#   make clean

HOST_CC     ?= gcc
HOST_CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Isrc
BUILD       := build

CORE_Z80  := src/cpu/z80/z80.c
PLAT_HOST := src/platform/host/plat_host.c

.PHONY: all host test test-full nspire clean

all: host

host: $(BUILD)/host/zextest $(BUILD)/host/bench

$(BUILD)/host/zextest: tests/z80/zextest.c $(CORE_Z80) src/cpu/z80/z80.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/z80/zextest.c $(CORE_Z80)

$(BUILD)/host/bench: src/frontend/bench.c $(CORE_Z80) $(PLAT_HOST) src/cpu/z80/z80.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -DN84_HOST -o $@ src/frontend/bench.c $(CORE_Z80) $(PLAT_HOST)

test: $(BUILD)/host/zextest
	$(BUILD)/host/zextest tests/z80/zexdoc.com | tee $(BUILD)/zexdoc.log
	@! grep -q "ERROR" $(BUILD)/zexdoc.log

test-full: test
	$(BUILD)/host/zextest tests/z80/zexall.com | tee $(BUILD)/zexall.log
	@! grep -q "ERROR" $(BUILD)/zexall.log

nspire:
	$(MAKE) -f Makefile.nspire

clean:
	rm -rf $(BUILD)
	$(MAKE) -f Makefile.nspire clean
