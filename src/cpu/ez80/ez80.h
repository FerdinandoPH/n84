/*
 * N84 - TI-8x emulator for the TI-Nspire
 * Copyright (C) 2026 N84 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Zilog eZ80 interpreter core (as used in the TI-84 Plus CE).
 *
 * Instruction semantics (ADL/Z80 modes, suffixes, MBASE, mixed-mode calls,
 * traps, eZ80 flag behaviour) follow CEmu's core/cpu.c (MIT-licensed part of
 * CEmu, derived from z80e), re-implemented for speed on the ARM926.
 *
 * Memory: 24-bit address space split in 4 KiB pages. rd/wr pointers give
 * direct access; NULL routes to read_slow/write_slow (MMIO, Flash commands,
 * unmapped areas). cost[] holds the cycles charged per access to each page.
 *
 * Host requirement: little-endian.
 */
#ifndef N84_EZ80_H
#define N84_EZ80_H

#include <stdint.h>
#include <stdbool.h>

#define EZ80_PAGE_SHIFT 12
#define EZ80_PAGE_SIZE  (1u << EZ80_PAGE_SHIFT)
#define EZ80_PAGE_MASK  (EZ80_PAGE_SIZE - 1)
#define EZ80_NUM_PAGES  (1u << (24 - EZ80_PAGE_SHIFT))

enum {
    EZ80_CF = 0x01, EZ80_NF = 0x02, EZ80_PF = 0x04, EZ80_XF = 0x08,
    EZ80_HF = 0x10, EZ80_YF = 0x20, EZ80_ZF = 0x40, EZ80_SF = 0x80
};

typedef union {
    uint32_t w;             /* 24-bit value (bits 24..31 always 0) */
    uint16_t s;             /* low 16 bits */
    struct { uint8_t l, h, u, pad; } b;
} ez80_pair;

typedef struct ez80 ez80_t;

struct ez80 {
    /* registers */
    uint8_t a, f, a_, f_;
    ez80_pair bc, de, hl, ix, iy;
    ez80_pair bc_, de_, hl_;
    uint32_t sps, spl;      /* SPS uses the low 16 bits */
    uint32_t pc;            /* full 24-bit address (includes MBASE in Z80 mode) */
    uint16_t i;
    uint8_t r, mbase;
    uint8_t adl, madl, iff1, iff2, im;   /* im: 0, 2 (= Z80 IM 1), 3 (= Z80 IM 2) */
    uint8_t halted, ei_delay;

    /* per-instruction decode context */
    uint8_t L, IL, prefix, suffix;   /* prefix: 0 = HL, 2 = IX, 3 = IY */

    /* interrupt inputs */
    uint8_t irq_line, nmi_pending;

    /* cycles */
    int32_t cycles, cycle_target;
    uint64_t total_cycles;

    /* memory map (cost first: keeps it within ARM immediate-offset range) */
    uint8_t cost[EZ80_NUM_PAGES];
    uint8_t *rd[EZ80_NUM_PAGES];
    uint8_t *wr[EZ80_NUM_PAGES];

    void *ctx;
    uint8_t (*read_slow)(void *ctx, uint32_t addr);
    void    (*write_slow)(void *ctx, uint32_t addr, uint8_t val);
    uint8_t (*port_in)(void *ctx, uint16_t port);
    void    (*port_out)(void *ctx, uint16_t port, uint8_t val);
};

void    ez80_init(ez80_t *z);
void    ez80_reset(ez80_t *z);
int32_t ez80_run(ez80_t *z, int32_t cycles);
void    ez80_end_timeslice(ez80_t *z);
void    ez80_nmi(ez80_t *z);
/* Execute exactly one instruction (including prefixes); for tests/tracing. */
void    ez80_step(ez80_t *z);

static inline uint8_t ez80_peek(const ez80_t *z, uint32_t a) {
    a &= 0xFFFFFF;
    const uint8_t *p = z->rd[a >> EZ80_PAGE_SHIFT];
    return p ? p[a & EZ80_PAGE_MASK] : z->read_slow(z->ctx, a);
}

#endif
