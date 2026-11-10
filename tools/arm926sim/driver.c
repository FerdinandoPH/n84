/*
 * N84 - freestanding driver for tools/arm926sim
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Built with the same compiler and flags as the Nspire binaries, linked
 * without libc, and run inside Unicorn by arm926sim.py. The simulator calls
 * these entry points directly and stops when they return.
 */
#include "frontend/bench_progs.h"
#include "frontend/bench_arm.h"

/* the micro-benchmarks time themselves with plat_ticks; the simulator counts
 * cycles instead */
uint32_t plat_ticks(void) { return 0; }

void *memset(void *d, int c, size_t n) {
    uint8_t *p = d;
    while (n--) *p++ = (uint8_t)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *p = d;
    const uint8_t *q = s;
    while (n--) *p++ = *q++;
    return d;
}

static uint8_t mem_arena[0x10000 + BENCH_ARENA_ALIGN] __attribute__((aligned(BENCH_ARENA_ALIGN)));
static uint8_t cpu_arena[sizeof(ez80_t) + BENCH_ARENA_ALIGN] __attribute__((aligned(BENCH_ARENA_ALIGN)));
static z80_t z;
static ez80_t *e;

void sim_setup_z80(void) { bench_setup_z80(&z, mem_arena); }
/* placement: index into bench_placements */
void sim_setup_ez80(unsigned pl) {
    e = (ez80_t *)(cpu_arena + bench_placements[pl][1]);
    bench_setup_ez80(e, mem_arena + bench_placements[pl][0]);
}
unsigned sim_placements(void) { return BENCH_PLACEMENTS; }
int32_t sim_run_z80(int32_t cycles) { return z80_run(&z, cycles); }
int32_t sim_run_ez80(int32_t cycles) { return ez80_run(e, cycles); }

uint32_t sim_arm_test(unsigned i) { return bench_arm_run(i); }
unsigned sim_arm_count(void) { return bench_arm_count(); }

/* entry point is unused (the simulator calls the functions above) */
void _start(void) { for (;;) {} }
