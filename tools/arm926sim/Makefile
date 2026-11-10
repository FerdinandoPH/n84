# Build the freestanding cores for arm926sim.py with the Nspire compiler flags.
# Usage: make -C tools/arm926sim   (needs the Ndless SDK in PATH)
ROOT    := ../..
OUT     := $(ROOT)/build/arm926sim
GCC     := nspire-gcc
LD      := arm-none-eabi-gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -marm -mcpu=arm926ej-s -I$(ROOT)/src -DN84_NSPIRE \
           -O2 -fomit-frame-pointer -ffreestanding -DN84_ARMSIM
OBJ     := $(OUT)/driver.o $(OUT)/z80.o $(OUT)/ez80.o $(OUT)/bench_arm.o
# every object depends on every header (as in the main Makefiles): a stale
# object with an old struct layout corrupts the machine silently
DEPS    := $(wildcard $(ROOT)/src/*/*.h $(ROOT)/src/*/*/*.h $(ROOT)/src/*/*/*/*.h $(ROOT)/src/cpu/*/*.inc)

.DEFAULT_GOAL := all

$(OUT)/cores.elf: $(OBJ)
	$(LD) -marm -mcpu=arm926ej-s -nostdlib -static -Wl,-Ttext=0x10000 -Wl,-e,_start -o $@ $(OBJ) -lgcc

$(OUT)/driver.o: driver.c $(DEPS)
	@mkdir -p $(OUT)
	$(GCC) $(CFLAGS) -c $< -o $@
$(OUT)/bench_arm.o: $(ROOT)/src/frontend/bench_arm.c $(DEPS)
	@mkdir -p $(OUT)
	$(GCC) $(CFLAGS) -DBENCH_ARM_ITERS=2000u -c $< -o $@
$(OUT)/%.o: $(ROOT)/src/cpu/*/%.c $(DEPS)
	@mkdir -p $(OUT)
	$(GCC) $(CFLAGS) -c $< -o $@

# The whole CE machine, linked with newlib (nosys) for sim_ce_* (arm926sim.py --ce)
CE_SRCS := $(wildcard $(ROOT)/src/machine/ce/*.c)
CE_OBJ  := $(OUT)/driver_ce.o $(OUT)/ez80.o $(OUT)/tivar.o $(patsubst $(ROOT)/src/machine/ce/%.c,$(OUT)/ce/%.o,$(CE_SRCS))

$(OUT)/ce.elf: $(CE_OBJ)
	$(LD) -marm -mcpu=arm926ej-s -static --specs=nosys.specs -nostartfiles -Wl,-Ttext=0x10000 -Wl,-e,_start -o $@ $(CE_OBJ) -lc -lm -lgcc

$(OUT)/driver_ce.o: driver_ce.c $(DEPS) $(ROOT)/src/frontend/keynames.h
	@mkdir -p $(OUT)
	$(GCC) $(CFLAGS) -c $< -o $@
$(OUT)/tivar.o: $(ROOT)/src/link/tivar.c $(DEPS)
	@mkdir -p $(OUT)
	$(GCC) $(CFLAGS) -c $< -o $@
$(OUT)/ce/%.o: $(ROOT)/src/machine/ce/%.c $(DEPS)
	@mkdir -p $(OUT)/ce
	$(GCC) $(CFLAGS) -c $< -o $@

all: $(OUT)/cores.elf $(OUT)/ce.elf
.PHONY: all clean

clean:
	rm -rf $(OUT)
