/*
 * N84 - TI-8x emulator for the TI-Nspire
 * Copyright (C) 2026 N84 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Zilog Z80 interpreter core.
 *
 * Memory is accessed through four 16 KiB page pointers (rd/wr), which matches
 * the banking granularity of every TI Z80 calculator. A NULL pointer routes
 * the access through read_slow()/write_slow() (Flash command emulation,
 * read-only areas, sub-page remapping).
 *
 * Host requirement: little-endian (x86, ARM as used by Ndless).
 */
#ifndef N84_Z80_H
#define N84_Z80_H

#include <stdint.h>
#include <stdbool.h>

#define Z80_PAGE_SHIFT 14
#define Z80_PAGE_SIZE  (1u << Z80_PAGE_SHIFT)
#define Z80_PAGE_MASK  (Z80_PAGE_SIZE - 1)

enum {
    Z80_CF = 0x01, Z80_NF = 0x02, Z80_PF = 0x04, Z80_VF = Z80_PF,
    Z80_XF = 0x08, Z80_HF = 0x10, Z80_YF = 0x20, Z80_ZF = 0x40, Z80_SF = 0x80
};

typedef union {
    uint16_t w;
    struct { uint8_t l, h; } b;
} z80_pair;

typedef struct z80 z80_t;

struct z80 {
    /* Registers. For AF: b.l = F, b.h = A. */
    z80_pair af, bc, de, hl, ix, iy, sp, pc, wz;
    z80_pair af_, bc_, de_, hl_;
    uint8_t i;
    uint8_t r;        /* low 7 bits are counted; bit 7 kept in r7 */
    uint8_t r7;
    uint8_t iff1, iff2, im;
    uint8_t halted;
    uint8_t ei_delay; /* set by EI: interrupts not accepted before next insn */

    /* Interrupt inputs (driven by the machine) */
    uint8_t irq_line;   /* level-triggered /INT */
    uint8_t nmi_pending;
    uint8_t irq_data;   /* value on the data bus during INT ack (IM 0/2) */

    /* Cycle accounting. `cycles` counts up inside z80_run(). */
    int32_t cycles;
    int32_t cycle_target;
    uint64_t total_cycles;

    /* Memory map: 4 x 16 KiB pages */
    uint8_t *rd[4];
    uint8_t *wr[4];

    /* Machine callbacks */
    void *ctx;
    uint8_t (*read_slow)(void *ctx, uint16_t addr);
    void    (*write_slow)(void *ctx, uint16_t addr, uint8_t val);
    uint8_t (*port_in)(void *ctx, uint16_t port);
    void    (*port_out)(void *ctx, uint16_t port, uint8_t val);
};

void     z80_init(z80_t *z);
void     z80_reset(z80_t *z);
/* Run for at least `cycles` T-states (may overshoot by one instruction).
 * Returns the number of T-states actually executed. The machine may call
 * z80_end_timeslice() from a callback to return early. */
int32_t  z80_run(z80_t *z, int32_t cycles);
void     z80_end_timeslice(z80_t *z);
void     z80_set_irq(z80_t *z, bool level);
void     z80_nmi(z80_t *z);

static inline uint8_t z80_peek(const z80_t *z, uint16_t a) {
    const uint8_t *p = z->rd[a >> Z80_PAGE_SHIFT];
    return p ? p[a & Z80_PAGE_MASK] : z->read_slow(z->ctx, a);
}

#endif
