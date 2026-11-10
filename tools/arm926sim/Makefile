# Build the freestanding cores for arm926sim.py with the Nspire compiler flags.
# Usage: make -C tools/arm926sim   (needs the Ndless SDK in PATH)
ROOT    := ../..
OUT     := $(ROOT)/build/arm926sim
GCC     := nspire-gcc
LD      := arm-none-eabi-gcc
CFLAGS  := -std=gnu11 -Wall -Wextra -marm -mcpu=arm926ej-s -I$(ROOT)/src -DN84_NSPIRE \
           -O2 -fomit-frame-pointer -ffreestanding -DN84_ARMSIM
OBJ     := $(OUT)/driver.o $(OUT)/z80.o $(OUT)/ez80.o $(OUT)/bench_arm.o
DEPS    := $(wildcard $(ROOT)/src/cpu/*/*.h $(ROOT)/src/cpu/*/*.inc $(ROOT)/src/frontend/bench_progs.h)

$(OUT)/cores.elf: $(OBJ)
	$(LD) -marm -mcpu=arm926ej-s -nostdlib -static -Wl,-Ttext=0x10000 -Wl,-e,_start -o $@ $(OBJ) -lgcc

$(OUT)/driver.o: driver.c $(DEPS)
	@mkdir -p $(OUT)
	$(GCC) $(CFLAGS) -c $< -o $@
$(OUT)/bench_arm.o: $(ROOT)/src/frontend/bench_arm.c $(ROOT)/src/frontend/bench_arm.h
	@mkdir -p $(OUT)
	$(GCC) $(CFLAGS) -DBENCH_ARM_ITERS=2000u -c $< -o $@
$(OUT)/%.o: $(ROOT)/src/cpu/*/%.c $(DEPS)
	@mkdir -p $(OUT)
	$(GCC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT)
