/*
 * N84 - eZ80 core test: runs CP/M ZEX programs in Z80 mode (ADL=0, MBASE=0)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu/ez80/ez80.h"

static uint8_t mem[0x10000];
static int done;

static uint8_t rslow(void *c, uint32_t a) { (void)c; (void)a; return 0xFF; }
static void wslow(void *c, uint32_t a, uint8_t v) { (void)c; (void)a; (void)v; }
static uint8_t pin(void *c, uint16_t p) { (void)c; (void)p; return 0xFF; }
static void pout(void *ctx, uint16_t p, uint8_t v) {
    ez80_t *z = ctx;
    (void)v;
    if ((p & 0xFF) == 1) { done = 1; ez80_end_timeslice(z); return; }
    switch (z->bc.b.l) {
    case 2: putchar(z->de.b.l); break;
    case 9: for (uint16_t a = z->de.s; mem[a] != '$'; a++) putchar(mem[a]); break;
    }
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s file.com [skip-index...]\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fread(mem + 0x100, 1, 0x10000 - 0x100, f) == 0) return 2;
    fclose(f);
    /* Optionally drop entries from ZEXDOC/ZEXALL's test table (at 013Ah):
     * the eZ80 traps on DD/FD-prefixed opcodes that the Z80 ignores. */
    for (int a = argc - 1; a >= 2; a--) {
        int k = atoi(argv[a]);
        uint8_t *t = mem + 0x13A;
        int n = 0;
        while (t[2 * n] | t[2 * n + 1]) n++;
        if (k >= 0 && k < n) memmove(t + 2 * k, t + 2 * k + 2, (size_t)(n - k) * 2);
    }
    mem[0x0000] = 0xD3; mem[0x0001] = 0x01;
    mem[0x0005] = 0xC3; mem[0x0006] = 0x00; mem[0x0007] = 0xFE;
    mem[0xFE00] = 0xD3; mem[0xFE01] = 0x00; mem[0xFE02] = 0xC9;

    static ez80_t z;
    ez80_init(&z);
    for (unsigned i = 0; i < EZ80_NUM_PAGES; i++) { z.rd[i] = z.wr[i] = NULL; z.cost[i] = 1; }
    for (unsigned i = 0; i < 16; i++) z.rd[i] = z.wr[i] = mem + i * EZ80_PAGE_SIZE;
    z.ctx = &z; z.read_slow = rslow; z.write_slow = wslow; z.port_in = pin; z.port_out = pout;
    z.pc = 0x100; z.sps = 0xF000;
    long guard = 0;
    while (!done && guard++ < 200000) ez80_run(&z, 1000000);
    printf("\n[%llu cycles]\n", (unsigned long long)z.total_cycles);
    return 0;
}
