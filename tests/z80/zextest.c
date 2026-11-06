/*
 * N84 - Z80 core test harness: runs CP/M .COM test programs (ZEXDOC/ZEXALL)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Minimal CP/M environment: BDOS call 5 is implemented through OUT (0),A at
 * 0xFE00 and warm boot (jump to 0) through OUT (1),A.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cpu/z80/z80.h"

static uint8_t mem[0x10000];
static int done;

static void wslow(void *ctx, uint16_t a, uint8_t v) { (void)ctx; mem[a] = v; }
static uint8_t pin(void *ctx, uint16_t p) { (void)ctx; (void)p; return 0xff; }
static void pout(void *ctx, uint16_t p, uint8_t v) {
    z80_t *z = ctx;
    (void)v;
    if ((p & 0xff) == 1) { done = 1; z80_end_timeslice(z); return; }
    switch (z->bc.b.l) {
    case 2: putchar(z->de.b.l); break;
    case 9:
        for (uint16_t a = z->de.w; mem[a] != '$'; a++) putchar(mem[a]);
        break;
    }
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s file.com\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(mem + 0x100, 1, 0x10000 - 0x100, f);
    fclose(f);
    (void)n;

    mem[0x0000] = 0xD3; mem[0x0001] = 0x01;                     /* OUT (1),A : exit */
    mem[0x0005] = 0xC3; mem[0x0006] = 0x00; mem[0x0007] = 0xFE; /* JP 0FE00h */
    mem[0xFE00] = 0xD3; mem[0xFE01] = 0x00; mem[0xFE02] = 0xC9; /* OUT (0),A ; RET */

    static z80_t z;
    z80_init(&z);
    for (int i = 0; i < 4; i++) { z.rd[i] = mem + i * Z80_PAGE_SIZE; z.wr[i] = mem + i * Z80_PAGE_SIZE; }
    z.ctx = &z;
    z.write_slow = wslow;
    z.port_in = pin;
    z.port_out = pout;
    z.pc.w = 0x100;
    z.sp.w = 0xF000;

    clock_t t0 = clock();
    while (!done) z80_run(&z, 1000000);
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("\n[%llu T-states in %.2fs = %.1f MHz equivalent]\n",
           (unsigned long long)z.total_cycles, s, z.total_cycles / s / 1e6);
    return 0;
}
