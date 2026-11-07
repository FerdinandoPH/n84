/*
 * N84 - differential test: optimised eZ80 core vs the generic decoder.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Runs random instruction streams (biased towards the opcodes that matter)
 * on both builds from identical random states and compares registers,
 * memory, cycle counts and port traffic after every instruction.
 *
 *   ez80_diff [iterations] [seed]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu/ez80/ez80.h"

void ref_ez80_init(ez80_t *z);
void ref_ez80_step(ez80_t *z);
int32_t ref_ez80_run(ez80_t *z, int32_t cycles);

#define RAM_SIZE 0x20000   /* D00000-D1FFFF */
static uint8_t ram_a[RAM_SIZE], ram_b[RAM_SIZE], ram_b_start[RAM_SIZE];
static ez80_t za, zb;
static uint32_t port_log_a, port_log_b;

static uint32_t rng_state;
static uint32_t run_state = 1;
static uint32_t rnd_run(void) { run_state ^= run_state << 13; run_state ^= run_state >> 17; run_state ^= run_state << 5; return run_state; }
static uint32_t rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static uint8_t rslow(void *c, uint32_t a) { (void)c; return (uint8_t)(a * 7 + (a >> 8)); }
static void wslow_a(void *c, uint32_t a, uint8_t v) { (void)c; port_log_a = port_log_a * 31 + a * 3 + v; }
static void wslow_b(void *c, uint32_t a, uint8_t v) { (void)c; port_log_b = port_log_b * 31 + a * 3 + v; }
static uint8_t pin(void *c, uint16_t p) { (void)c; return (uint8_t)(p ^ (p >> 8) ^ 0x5A); }
static void pout_a(void *c, uint16_t p, uint8_t v) { (void)c; port_log_a = port_log_a * 37 + p + v; }
static void pout_b(void *c, uint16_t p, uint8_t v) { (void)c; port_log_b = port_log_b * 37 + p + v; }

static void map(ez80_t *z, uint8_t *ram, bool a) {
    for (unsigned i = 0; i < EZ80_NUM_PAGES; i++) { z->rd[i] = z->wr[i] = NULL; z->cost[i] = z->wcost[i] = 2; }
    for (unsigned i = 0; i < RAM_SIZE / EZ80_PAGE_SIZE; i++) {
        z->rd[0xD00 + i] = z->wr[0xD00 + i] = ram + i * EZ80_PAGE_SIZE;
        z->cost[0xD00 + i] = (uint8_t)(1 + (i & 3));
        z->wcost[0xD00 + i] = (uint8_t)(1 + ((i + 1) & 3));
    }
    z->ctx = z;
    z->read_slow = rslow;
    z->write_slow = a ? wslow_a : wslow_b;
    z->port_in = pin;
    z->port_out = a ? pout_a : pout_b;
}

/* opcode generator: mostly plain opcodes, some prefixes/suffixes so the
 * hand-over between the two decoders is exercised too */
static uint8_t gen_op(void) {
    uint32_t r = rnd() % 100;
    if (r < 80) return (uint8_t)rnd();
    static const uint8_t pre[] = { 0xDD, 0xFD, 0xED, 0xCB, 0x40, 0x49, 0x52, 0x5B };
    return pre[rnd() % sizeof pre];
}

static void randomise(void) {
    /* fill RAM once; later iterations refresh the code bytes at the new PC
     * (RAM written by earlier sequences keeps adding variety) */
    ez80_init(&za);
    za.bc.w = rnd() & 0xFFFFFF; za.de.w = rnd() & 0xFFFFFF; za.hl.w = rnd() & 0xFFFFFF;
    za.ix.w = rnd() & 0xFFFFFF; za.iy.w = rnd() & 0xFFFFFF;
    za.bc_.w = rnd() & 0xFFFFFF; za.de_.w = rnd() & 0xFFFFFF; za.hl_.w = rnd() & 0xFFFFFF;
    /* point most pairs into RAM so memory ops hit something interesting */
    if (rnd() & 1) za.hl.w = 0xD00000 | (rnd() & 0x1FFFF);
    if (rnd() & 1) za.de.w = 0xD00000 | (rnd() & 0x1FFFF);
    if (rnd() & 1) za.bc.w = 0xD00000 | (rnd() & 0x1FFFF);
    /* small values hit loop ends (LDIR with BC=1, DJNZ with B=1, ...) */
    if (rnd() % 4 == 0) za.bc.w = rnd() % 3;
    if (rnd() % 4 == 0) za.bc.b.h = (uint8_t)(rnd() % 3);
    if (rnd() % 8 == 0) za.de.w = rnd() % 3;
    if (rnd() % 8 == 0) za.hl.w = 0xD00000 | (rnd() % 3);
    za.a = (uint8_t)rnd(); za.f = (uint8_t)rnd(); za.a_ = (uint8_t)rnd(); za.f_ = (uint8_t)rnd();
    za.adl = rnd() & 1; za.madl = (rnd() % 4) == 0;
    za.mbase = 0xD0 + (uint8_t)(rnd() & 1);
    za.spl = 0xD00000 | (rnd() & 0x1FFFF);
    za.sps = rnd() & 0xFFFF;
    za.pc = za.adl ? (0xD00000 | (rnd() & 0x1FFFF)) : (((uint32_t)za.mbase << 16) | (rnd() & 0xFFFF));
    za.i = (uint16_t)rnd(); za.r = (uint8_t)rnd();
    for (unsigned k = 0; k < 256; k++) ram_a[(za.pc + k) & (RAM_SIZE - 1)] = gen_op();
    za.iff1 = za.iff2 = rnd() & 1; za.im = 2;
    memcpy(ram_b, ram_a, RAM_SIZE);
    ez80_t saved = za;
    map(&za, ram_a, true);
    zb = saved;
    map(&zb, ram_b, false);
    port_log_a = port_log_b = 0;
}

#define CMP(field) if (za.field != zb.field) { printf("  " #field ": fast=%lX ref=%lX\n", (unsigned long)za.field, (unsigned long)zb.field); bad = 1; }

static int compare(bool with_ram) {
    int bad = 0;
    CMP(a) CMP(f) CMP(a_) CMP(f_) CMP(bc.w) CMP(de.w) CMP(hl.w) CMP(ix.w) CMP(iy.w)
    CMP(bc_.w) CMP(de_.w) CMP(hl_.w) CMP(sps) CMP(spl) CMP(pc) CMP(i) CMP(r) CMP(mbase)
    CMP(adl) CMP(madl) CMP(iff1) CMP(iff2) CMP(im) CMP(halted) CMP(ei_delay) CMP(cycles) CMP(total_cycles)
    if (port_log_a != port_log_b) { printf("  port/MMIO traffic differs\n"); bad = 1; }
    if (with_ram && memcmp(ram_a, ram_b, RAM_SIZE)) {
        for (unsigned i = 0; i < RAM_SIZE; i++)
            if (ram_a[i] != ram_b[i]) { printf("  ram[%06X]: fast=%02X ref=%02X\n", 0xD00000 + i, ram_a[i], ram_b[i]); break; }
        bad = 1;
    }
    return bad;
}

int main(int argc, char **argv) {
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    rng_state = argc > 2 ? (uint32_t)atol(argv[2]) : 12345;
    ref_ez80_init(&zb);
    long insns = 0;
    for (unsigned i = 0; i < RAM_SIZE; i++) ram_a[i] = gen_op();
    for (long it = 0; it < iters; it++) {
        bool use_run = it & 1;
        uint32_t seed = rng_state;
        /* pass 0: compare RAM once at the end; pass 1 (only after a RAM
         * mismatch): replay the same sequence comparing RAM at every step */
        for (int pass = 0; pass < 2; pass++) {
            rng_state = seed;
            run_state = seed ^ 0x9E3779B9u;
            if (pass) memcpy(ram_a, ram_b_start, RAM_SIZE);
            else memcpy(ram_b_start, ram_a, RAM_SIZE);
            randomise();
            int failed = 0;
            for (int n = 0; n < 64 && !failed; n++) {
                ez80_t before = zb;
                uint32_t pc = zb.pc;
                uint8_t code[8];
                for (int k = 0; k < 8; k++) code[k] = ez80_peek(&zb, pc + (uint32_t)k);
                za.cycles = zb.cycles = 0;
                if (use_run) {
                    /* exercise ez80_run's inner loops: random budgets and an
                     * interrupt line that comes and goes */
                    int32_t budget = (int32_t)(1 + rnd_run() % 60);
                    uint8_t irq = (rnd_run() % 8) == 0;
                    za.irq_line = zb.irq_line = irq;
                    ez80_run(&za, budget);
                    ref_ez80_run(&zb, budget);
                } else {
                    ez80_step(&za);
                    ref_ez80_step(&zb);
                }
                if (!pass) insns++;
                bool last = n == 63 || zb.halted;
                if (compare(pass == 1 || last)) {
                    if (!pass && !compare(false)) { failed = 1; break; }  /* RAM only: replay */
                    printf("MISMATCH (iteration %ld, step %d): adl=%u mbase=%02X pc=%06X code:", it, n, before.adl, before.mbase, pc);
                    for (int k = 0; k < 8; k++) printf(" %02X", code[k]);
                    printf("\n  before: a=%02X f=%02X bc=%06X de=%06X hl=%06X ix=%06X iy=%06X sps=%04X spl=%06X\n",
                           before.a, before.f, before.bc.w, before.de.w, before.hl.w, before.ix.w, before.iy.w, before.sps, before.spl);
                    return 1;
                }
                if (zb.halted) break;
            }
            if (!failed) break;
        }
    }
    printf("ez80_diff: %ld instructions, fast path == reference. OK\n", insns);
    return 0;
}
