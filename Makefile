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
CORE_EZ80 := src/cpu/ez80/ez80.c
TI8X      := src/machine/ti8x/ti8x.c
PLAT_HOST := src/platform/host/plat_host.c
VIEW_TI8X := src/frontend/ti8x_view.c
HDRS      := $(wildcard src/*/*.h src/*/*/*.h src/*/*/*.inc)

.PHONY: all host test test-full rom-smoke nspire clean

all: host

host: $(HB)/zextest $(HB)/ezextest $(HB)/ez80_adl_test $(HB)/ez80_diff $(HB)/bench $(HB)/ti8x_test $(HB)/n84-host

$(HB)/zextest: tests/z80/zextest.c $(CORE_Z80) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/z80/zextest.c $(CORE_Z80)

$(HB)/ezextest: tests/ez80/ezextest.c $(CORE_EZ80) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/ez80/ezextest.c $(CORE_EZ80)

$(HB)/ez80_adl_test: tests/ez80/ez80_adl_test.c $(CORE_EZ80) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/ez80/ez80_adl_test.c $(CORE_EZ80)

$(HB)/ez80_diff: tests/ez80/ez80_diff.c tests/ez80/ez80_ref.c $(CORE_EZ80) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/ez80/ez80_diff.c tests/ez80/ez80_ref.c $(CORE_EZ80)

$(HB)/bench: src/frontend/bench.c $(CORE_Z80) $(CORE_EZ80) $(PLAT_HOST) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -DN84_HOST -o $@ $(filter %.c,$^)

$(HB)/ti8x_test: tests/ti8x/ti8x_test.c $(CORE_Z80) $(TI8X) $(VIEW_TI8X) $(PLAT_HOST) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $(filter %.c,$^)

$(HB)/n84-host: src/frontend/host_main.c $(CORE_Z80) $(TI8X) $(VIEW_TI8X) $(PLAT_HOST) $(HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -DTI8X_LOG=printf -o $@ $(filter %.c,$^)

$(BUILD)/ti84p_test.rom: tools/mk_ti8x_testrom.py
	@mkdir -p $(dir $@)
	python3 tools/mk_ti8x_testrom.py $@

# ZEXDOC on the eZ80 skips tests 50, 59, 60: they use DD-prefixed non-index
# opcodes and SLL, which trap on a real eZ80.
test: $(HB)/zextest $(HB)/ezextest $(HB)/ez80_adl_test $(HB)/ez80_diff $(HB)/ti8x_test $(BUILD)/ti84p_test.rom
	$(HB)/zextest tests/z80/zexdoc.com | tee $(BUILD)/zexdoc.log
	@! grep -q "ERROR" $(BUILD)/zexdoc.log
	$(HB)/ezextest tests/z80/zexdoc.com 50 59 60 | tee $(BUILD)/ezexdoc.log
	@! grep -q "ERROR" $(BUILD)/ezexdoc.log
	@grep -q "Tests complete" $(BUILD)/ezexdoc.log
	$(HB)/ez80_adl_test
	$(HB)/ez80_diff 20000
	$(HB)/ti8x_test $(BUILD)/ti84p_test.rom $(BUILD)/ti8x_test.ppm

test-full: test
	$(HB)/zextest tests/z80/zexall.com | tee $(BUILD)/zexall.log
	@! grep -q "ERROR" $(BUILD)/zexall.log

rom-smoke: $(HB)/n84-host
	tools/rom_smoke.sh $(ROM)

nspire:
	$(MAKE) -f Makefile.nspire

clean:
	rm -rf $(BUILD)
