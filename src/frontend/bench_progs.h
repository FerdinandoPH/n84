/*
 * N84 - benchmark workloads, shared by n84bench and tools/arm926sim
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Synthetic loop: table walk + call/ret + LDIR block copy. The Z80 version
 * runs from a flat 64 KiB RAM; the eZ80 one is the same loop in ADL mode
 * (24-bit pointers) running from RAM at D00000h, as CE programs do. There
 * every memory access costs 3 cycles (CEmu: RAM ~2-4, cached flash 2-3), so
 * the result is comparable with the real 48 MHz CPU.
 */
#ifndef N84_BENCH_PROGS_H
#define N84_BENCH_PROGS_H

#include <string.h>
#include "cpu/z80/z80.h"
#include "cpu/ez80/ez80.h"

static const uint8_t bench_prog_z80[] = {
    0x31, 0xF0, 0xFF,       /* 0000 ld sp,FFF0h      */
    0x06, 0x00,             /* 0003 ld b,0     outer */
    0x21, 0x00, 0x80,       /* 0005 ld hl,8000h loop */
    0x11, 0x00, 0x90,       /* 0008 ld de,9000h      */
    0x7E,                   /* 000B ld a,(hl)        */
    0x80,                   /* 000C add a,b          */
    0x12,                   /* 000D ld (de),a        */
    0x23,                   /* 000E inc hl           */
    0xCD, 0x21, 0x00,       /* 000F call 0021h       */
    0x10, 0xF1,             /* 0012 djnz 0005h       */
    0x21, 0x00, 0x80,       /* 0014 ld hl,8000h      */
    0x11, 0x00, 0xA0,       /* 0017 ld de,A000h      */
    0x01, 0x40, 0x00,       /* 001A ld bc,64         */
    0xED, 0xB0,             /* 001D ldir             */
    0x18, 0xE2,             /* 001F jr 0003h         */
    0xC5,                   /* 0021 push bc          */
    0x4F,                   /* 0022 ld c,a           */
    0xAF,                   /* 0023 xor a            */
    0xCB, 0x11,             /* 0024 rl c             */
    0x89,                   /* 0026 adc a,c          */
    0xC1,                   /* 0027 pop bc           */
    0xC9,                   /* 0028 ret              */
};

static const uint8_t bench_prog_ez80[] = {
    0x31, 0x00, 0xF0, 0xD0,   /* 00 ld sp,D0F000h       */
    0x06, 0x00,               /* 04 ld b,0        outer */
    0x21, 0x00, 0x80, 0xD0,   /* 06 ld hl,D08000h loop  */
    0x11, 0x00, 0x90, 0xD0,   /* 0A ld de,D09000h       */
    0x7E,                     /* 0E ld a,(hl)           */
    0x80,                     /* 0F add a,b             */
    0x12,                     /* 10 ld (de),a           */
    0x23,                     /* 11 inc hl              */
    0xCD, 0x2A, 0x00, 0xD0,   /* 12 call D0002Ah        */
    0x10, 0xEE,               /* 16 djnz 06h            */
    0x21, 0x00, 0x80, 0xD0,   /* 18 ld hl,D08000h       */
    0x11, 0x00, 0xA0, 0xD0,   /* 1C ld de,D0A000h       */
    0x01, 0x40, 0x00, 0x00,   /* 20 ld bc,64            */
    0xED, 0xB0,               /* 24 ldir                */
    0x18, 0xDC,               /* 26 jr 04h              */
    0x00, 0x00,               /* 28 (pad)               */
    0xC5,                     /* 2A push bc             */
    0x4F,                     /* 2B ld c,a              */
    0xAF,                     /* 2C xor a               */
    0xCB, 0x11,               /* 2D rl c                */
    0x89,                     /* 2F adc a,c             */
    0xC1,                     /* 30 pop bc              */
    0xC9,                     /* 31 ret                 */
};

static void bench_dummy_write(void *c, uint16_t a, uint8_t v) { (void)c; (void)a; (void)v; }
static uint8_t bench_dummy_in(void *c, uint16_t p) { (void)c; (void)p; return 0xff; }
static void bench_dummy_out(void *c, uint16_t p, uint8_t v) { (void)c; (void)p; (void)v; }
static uint8_t bench_ez_rslow(void *c, uint32_t a) { (void)c; (void)a; return 0xff; }
static void bench_ez_wslow(void *c, uint32_t a, uint8_t v) { (void)c; (void)a; (void)v; }

/* mem: 64 KiB of host memory */
static inline void bench_setup_z80(z80_t *z, uint8_t *mem) {
    memset(mem, 0, 0x10000);
    memcpy(mem, bench_prog_z80, sizeof bench_prog_z80);
    z80_init(z);
    for (int i = 0; i < 4; i++) { z->rd[i] = mem + i * Z80_PAGE_SIZE; z->wr[i] = mem + i * Z80_PAGE_SIZE; }
    z->ctx = z; z->write_slow = bench_dummy_write; z->port_in = bench_dummy_in; z->port_out = bench_dummy_out;
}

static inline void bench_setup_ez80(ez80_t *e, uint8_t *mem) {
    ez80_init(e);
    for (unsigned i = 0; i < EZ80_NUM_PAGES; i++) { e->rd[i] = e->wr[i] = NULL; e->cost[i] = e->wcost[i] = 3; }
    for (unsigned i = 0; i < 16; i++) e->rd[0xD00 + i] = e->wr[0xD00 + i] = mem + i * EZ80_PAGE_SIZE;
    memset(mem, 0, 0x10000);
    memcpy(mem, bench_prog_ez80, sizeof bench_prog_ez80);
    e->ctx = e; e->read_slow = bench_ez_rslow; e->write_slow = bench_ez_wslow;
    e->port_in = bench_dummy_in; e->port_out = bench_dummy_out;
    e->adl = 1; e->pc = 0xD00000;
}

/* Placements for the eZ80 run: byte offsets of the 64 KiB memory and of the
 * ez80_t inside arenas aligned to the D-cache way size (2 KiB on the CX II).
 * The loop's buffers sit at multiples of 4 KiB of each other, so whether
 * they collide in the 4-way D-cache with the CPU state depends on where
 * things land; measuring several placements shows that spread. */
#define BENCH_ARENA_ALIGN 2048
static const uint16_t bench_placements[][2] = {    /* { mem offset, ez80_t offset } */
    { 0, 0 }, { 0x2A0, 0x620 }, { 0x540, 0x0E0 }, { 0x7C0, 0x380 },
};
#define BENCH_PLACEMENTS (sizeof bench_placements / sizeof bench_placements[0])

#endif
