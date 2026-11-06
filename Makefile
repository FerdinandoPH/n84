# N84 - top-level Makefile
#   make host      build host (Linux) tools: test runners, headless emulator
#   make test      run host test suites (ZEXDOC, ti8x machine test)
#   make test-full also run ZEXALL (~40 s)
#   make nspire    build the Ndless .tns files (requires the Ndless SDK in PATH)
#   make clean

HOST_CC     ?= gcc
HOST_CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Isrc
BUILD       := build
HB          := $(BUILD)/host

CORE_Z80  := src/cpu/z80/z80.c
TI8X      := src/machine/ti8x/ti8x.c
PLAT_HOST := src/platform/host/plat_host.c
VIEW_TI8X := src/frontend/ti8x_view.c
HDRS      := $(wildcard src/*/*.h src/*/*/*.h)

.PHONY: all host test test-full nspire clean

all: host

host: $(HB)/zextest $(HB)/bench $(HB)/ti8x_test $(HB)/n84-host

$(HB)/zextest: tests/z80/zextest.c $(CORE_Z80) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/z80/zextest.c $(CORE_Z80)

$(HB)/bench: src/frontend/bench.c $(CORE_Z80) $(PLAT_HOST) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -DN84_HOST -o $@ src/frontend/bench.c $(CORE_Z80) $(PLAT_HOST)

$(HB)/ti8x_test: tests/ti8x/ti8x_test.c $(CORE_Z80) $(TI8X) $(VIEW_TI8X) $(PLAT_HOST) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(filter %.c,$^)

$(HB)/n84-host: src/frontend/host_main.c $(CORE_Z80) $(TI8X) $(VIEW_TI8X) $(PLAT_HOST) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -DTI8X_LOG=printf -o $@ $(filter %.c,$^)

$(BUILD)/ti84p_test.rom: tools/mk_ti8x_testrom.py
	@mkdir -p $(dir $@)
	python3 tools/mk_ti8x_testrom.py $@

test: $(HB)/zextest $(HB)/ti8x_test $(BUILD)/ti84p_test.rom
	$(HB)/zextest tests/z80/zexdoc.com | tee $(BUILD)/zexdoc.log
	@! grep -q "ERROR" $(BUILD)/zexdoc.log
	$(HB)/ti8x_test $(BUILD)/ti84p_test.rom $(BUILD)/ti8x_test.ppm

test-full: test
	$(HB)/zextest tests/z80/zexall.com | tee $(BUILD)/zexall.log
	@! grep -q "ERROR" $(BUILD)/zexall.log

nspire:
	$(MAKE) -f Makefile.nspire

clean:
	rm -rf $(BUILD)
