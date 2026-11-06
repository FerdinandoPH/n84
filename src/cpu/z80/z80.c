/*
 * N84 - TI-8x emulator for the TI-Nspire
 * Copyright (C) 2026 N84 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Zilog Z80 interpreter core. Flag computations follow the well-documented
 * behaviour of the NMOS Z80 (including undocumented X/Y flags and MEMPTR),
 * so the core passes ZEXDOC and ZEXALL.
 */
#include <stddef.h>
#include "z80.h"

#define CF Z80_CF
#define NF Z80_NF
#define PF Z80_PF
#define VF Z80_VF
#define XF Z80_XF
#define HF Z80_HF
#define YF Z80_YF
#define ZF Z80_ZF
#define SF Z80_SF

#define A   (z->af.b.h)
#define F   (z->af.b.l)
#define B   (z->bc.b.h)
#define C   (z->bc.b.l)
#define D   (z->de.b.h)
#define E   (z->de.b.l)
#define H   (z->hl.b.h)
#define L   (z->hl.b.l)
#define AF  (z->af.w)
#define BC  (z->bc.w)
#define DE  (z->de.w)
#define HL  (z->hl.w)
#define SP  (z->sp.w)
#define PC  (z->pc.w)
#define WZ  (z->wz.w)

static uint8_t sz53[256], sz53p[256], parity[256];
static bool tables_ready;

static const uint8_t hc_add[8] = { 0, HF, HF, HF, 0, 0, 0, HF };
static const uint8_t hc_sub[8] = { 0, 0, HF, 0, HF, 0, HF, HF };
static const uint8_t ov_add[8] = { 0, 0, 0, VF, VF, 0, 0, 0 };
static const uint8_t ov_sub[8] = { 0, VF, 0, 0, 0, 0, VF, 0 };

/* Base T-states for unprefixed opcodes (conditional extras added in code). */
static const uint8_t cyc_main[256] = {
     4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,
     8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
     7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,
     7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     5,10,10,10,10,11, 7,11, 5,10,10, 0,10,17, 7,11,
     5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 0, 7,11,
     5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 0, 7,11,
     5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 0, 7,11,
};

/* Register offsets inside z80_t, indexed by the 3-bit register field.
 * Index 6 ((HL)) is never used through these tables. */
#define OFF(f) ((uint8_t)offsetof(z80_t, f))
static const uint8_t roff_hl[8] = { OFF(bc)+1, OFF(bc), OFF(de)+1, OFF(de), OFF(hl)+1, OFF(hl), 0, OFF(af)+1 };
static const uint8_t roff_ix[8] = { OFF(bc)+1, OFF(bc), OFF(de)+1, OFF(de), OFF(ix)+1, OFF(ix), 0, OFF(af)+1 };
static const uint8_t roff_iy[8] = { OFF(bc)+1, OFF(bc), OFF(de)+1, OFF(de), OFF(iy)+1, OFF(iy), 0, OFF(af)+1 };
#define REG8(tab, i) (*((uint8_t *)z + (tab)[i]))

static void init_tables(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t p = 0;
        for (int b = 0; b < 8; b++) p ^= (i >> b) & 1;
        parity[i] = p ? 0 : PF;
        sz53[i] = (uint8_t)(i & (SF | YF | XF));
        if (i == 0) sz53[i] |= ZF;
        sz53p[i] = sz53[i] | parity[i];
    }
    tables_ready = true;
}

/* ---- memory / IO ---------------------------------------------------- */

static inline uint8_t rb(z80_t *z, uint16_t a) {
    return z->rd[a >> Z80_PAGE_SHIFT][a & Z80_PAGE_MASK];
}
static inline void wb(z80_t *z, uint16_t a, uint8_t v) {
    uint8_t *p = z->wr[a >> Z80_PAGE_SHIFT];
    if (p) p[a & Z80_PAGE_MASK] = v;
    else z->write_slow(z->ctx, a, v);
}
static inline uint16_t rw(z80_t *z, uint16_t a) {
    return (uint16_t)(rb(z, a) | (rb(z, (uint16_t)(a + 1)) << 8));
}
static inline void ww(z80_t *z, uint16_t a, uint16_t v) {
    wb(z, a, (uint8_t)v);
    wb(z, (uint16_t)(a + 1), (uint8_t)(v >> 8));
}
static inline uint8_t fetch(z80_t *z) { return rb(z, PC++); }
static inline uint16_t fetch16(z80_t *z) {
    uint16_t v = rw(z, PC);
    PC += 2;
    return v;
}
static inline void push(z80_t *z, uint16_t v) {
    SP -= 2;
    ww(z, SP, v);
}
static inline uint16_t pop(z80_t *z) {
    uint16_t v = rw(z, SP);
    SP += 2;
    return v;
}
static inline uint8_t port_in(z80_t *z, uint16_t p) { return z->port_in(z->ctx, p); }
static inline void port_out(z80_t *z, uint16_t p, uint8_t v) { z->port_out(z->ctx, p, v); }

/* ---- ALU ------------------------------------------------------------ */

static inline void op_add(z80_t *z, uint8_t v) {
    unsigned t = A + v;
    unsigned lk = ((A & 0x88) >> 3) | ((v & 0x88) >> 2) | ((t & 0x88) >> 1);
    A = (uint8_t)t;
    F = (uint8_t)(((t & 0x100) ? CF : 0) | hc_add[lk & 7] | ov_add[lk >> 4] | sz53[A]);
}
static inline void op_adc(z80_t *z, uint8_t v) {
    unsigned t = A + v + (F & CF);
    unsigned lk = ((A & 0x88) >> 3) | ((v & 0x88) >> 2) | ((t & 0x88) >> 1);
    A = (uint8_t)t;
    F = (uint8_t)(((t & 0x100) ? CF : 0) | hc_add[lk & 7] | ov_add[lk >> 4] | sz53[A]);
}
static inline void op_sub(z80_t *z, uint8_t v) {
    unsigned t = A - v;
    unsigned lk = ((A & 0x88) >> 3) | ((v & 0x88) >> 2) | ((t & 0x88) >> 1);
    A = (uint8_t)t;
    F = (uint8_t)(((t & 0x100) ? CF : 0) | NF | hc_sub[lk & 7] | ov_sub[lk >> 4] | sz53[A]);
}
static inline void op_sbc(z80_t *z, uint8_t v) {
    unsigned t = A - v - (F & CF);
    unsigned lk = ((A & 0x88) >> 3) | ((v & 0x88) >> 2) | ((t & 0x88) >> 1);
    A = (uint8_t)t;
    F = (uint8_t)(((t & 0x100) ? CF : 0) | NF | hc_sub[lk & 7] | ov_sub[lk >> 4] | sz53[A]);
}
static inline void op_cp(z80_t *z, uint8_t v) {
    unsigned t = A - v;
    unsigned lk = ((A & 0x88) >> 3) | ((v & 0x88) >> 2) | ((t & 0x88) >> 1);
    F = (uint8_t)(((t & 0x100) ? CF : ((t & 0xff) ? 0 : ZF)) | NF | hc_sub[lk & 7] |
                  ov_sub[lk >> 4] | (v & (XF | YF)) | (t & SF));
}
static inline void op_and(z80_t *z, uint8_t v) { A &= v; F = HF | sz53p[A]; }
static inline void op_xor(z80_t *z, uint8_t v) { A ^= v; F = sz53p[A]; }
static inline void op_or(z80_t *z, uint8_t v)  { A |= v; F = sz53p[A]; }

static inline void alu(z80_t *z, unsigned op, uint8_t v) {
    switch (op & 7) {
    case 0: op_add(z, v); break;
    case 1: op_adc(z, v); break;
    case 2: op_sub(z, v); break;
    case 3: op_sbc(z, v); break;
    case 4: op_and(z, v); break;
    case 5: op_xor(z, v); break;
    case 6: op_or(z, v); break;
    default: op_cp(z, v); break;
    }
}

static inline uint8_t op_inc(z80_t *z, uint8_t v) {
    v++;
    F = (uint8_t)((F & CF) | (v == 0x80 ? VF : 0) | ((v & 0x0f) ? 0 : HF) | sz53[v]);
    return v;
}
static inline uint8_t op_dec(z80_t *z, uint8_t v) {
    F = (uint8_t)((F & CF) | ((v & 0x0f) ? 0 : HF) | NF);
    v--;
    F |= (uint8_t)((v == 0x7f ? VF : 0) | sz53[v]);
    return v;
}
static inline uint16_t op_add16(z80_t *z, uint16_t a, uint16_t b) {
    unsigned t = a + b;
    unsigned lk = ((a & 0x0800) >> 11) | ((b & 0x0800) >> 10) | ((t & 0x0800) >> 9);
    WZ = (uint16_t)(a + 1);
    F = (uint8_t)((F & (VF | ZF | SF)) | ((t & 0x10000) ? CF : 0) | ((t >> 8) & (XF | YF)) | hc_add[lk]);
    return (uint16_t)t;
}
static inline void op_adc16(z80_t *z, uint16_t v) {
    unsigned t = HL + v + (F & CF);
    unsigned lk = ((HL & 0x8800) >> 11) | ((v & 0x8800) >> 10) | ((t & 0x8800) >> 9);
    WZ = (uint16_t)(HL + 1);
    HL = (uint16_t)t;
    F = (uint8_t)(((t & 0x10000) ? CF : 0) | ov_add[lk >> 4] | (H & (XF | YF | SF)) |
                  hc_add[lk & 7] | (HL ? 0 : ZF));
}
static inline void op_sbc16(z80_t *z, uint16_t v) {
    unsigned t = HL - v - (F & CF);
    unsigned lk = ((HL & 0x8800) >> 11) | ((v & 0x8800) >> 10) | ((t & 0x8800) >> 9);
    WZ = (uint16_t)(HL + 1);
    HL = (uint16_t)t;
    F = (uint8_t)(((t & 0x10000) ? CF : 0) | NF | ov_sub[lk >> 4] | (H & (XF | YF | SF)) |
                  hc_sub[lk & 7] | (HL ? 0 : ZF));
}

/* CB-group rotate/shift. op = bits 5..3 of the CB opcode */
static inline uint8_t op_rot(z80_t *z, unsigned op, uint8_t v) {
    uint8_t c;
    switch (op & 7) {
    case 0: c = v >> 7; v = (uint8_t)((v << 1) | c); break;                 /* RLC */
    case 1: c = v & 1;  v = (uint8_t)((v >> 1) | (c << 7)); break;          /* RRC */
    case 2: c = v >> 7; v = (uint8_t)((v << 1) | (F & CF)); break;          /* RL  */
    case 3: c = v & 1;  v = (uint8_t)((v >> 1) | ((F & CF) << 7)); break;   /* RR  */
    case 4: c = v >> 7; v = (uint8_t)(v << 1); break;                       /* SLA */
    case 5: c = v & 1;  v = (uint8_t)((v >> 1) | (v & 0x80)); break;        /* SRA */
    case 6: c = v >> 7; v = (uint8_t)((v << 1) | 1); break;                 /* SLL */
    default: c = v & 1; v = (uint8_t)(v >> 1); break;                       /* SRL */
    }
    F = (uint8_t)(c | sz53p[v]);
    return v;
}

static inline void op_bit(z80_t *z, unsigned bit, uint8_t v, uint8_t xy) {
    F = (uint8_t)((F & CF) | HF | (xy & (XF | YF)));
    if (v & (1u << bit)) {
        if (bit == 7) F |= SF;
    } else {
        F |= ZF | PF;
    }
}

static void op_daa(z80_t *z) {
    uint8_t add = 0, carry = F & CF;
    if ((F & HF) || ((A & 0x0f) > 9)) add = 6;
    if (carry || A > 0x99) add |= 0x60;
    if (A > 0x99) carry = CF;
    if (F & NF) op_sub(z, add);
    else op_add(z, add);
    F = (uint8_t)((F & ~(CF | PF)) | carry | parity[A]);
}

static inline bool cond(z80_t *z, unsigned cc) {
    switch (cc & 7) {
    case 0: return !(F & ZF);
    case 1: return F & ZF;
    case 2: return !(F & CF);
    case 3: return F & CF;
    case 4: return !(F & PF);
    case 5: return F & PF;
    case 6: return !(F & SF);
    default: return F & SF;
    }
}

/* ---- prefixed groups ----------------------------------------------- */

static void exec_main(z80_t *z, uint8_t op);

static void exec_cb(z80_t *z) {
    uint8_t op = fetch(z);
    z->r++;
    unsigned r = op & 7, y = (op >> 3) & 7;
    if (r == 6) {
        uint8_t v = rb(z, HL);
        switch (op >> 6) {
        case 0: wb(z, HL, op_rot(z, y, v)); z->cycles += 15; break;
        case 1: op_bit(z, y, v, z->wz.b.h); z->cycles += 12; break;
        case 2: wb(z, HL, (uint8_t)(v & ~(1u << y))); z->cycles += 15; break;
        default: wb(z, HL, (uint8_t)(v | (1u << y))); z->cycles += 15; break;
        }
        return;
    }
    uint8_t *p = &REG8(roff_hl, r);
    z->cycles += 8;
    switch (op >> 6) {
    case 0: *p = op_rot(z, y, *p); break;
    case 1: op_bit(z, y, *p, *p); break;
    case 2: *p &= (uint8_t)~(1u << y); break;
    default: *p |= (uint8_t)(1u << y); break;
    }
}

/* DDCB/FDCB: displacement already fetched; addr = IX/IY + d */
static void exec_xycb(z80_t *z, uint16_t addr) {
    uint8_t op = fetch(z);
    unsigned r = op & 7, y = (op >> 3) & 7;
    uint8_t v = rb(z, addr), res;
    WZ = addr;
    switch (op >> 6) {
    case 0: res = op_rot(z, y, v); break;
    case 1: op_bit(z, y, v, (uint8_t)(addr >> 8)); z->cycles += 20; return;
    case 2: res = (uint8_t)(v & ~(1u << y)); break;
    default: res = (uint8_t)(v | (1u << y)); break;
    }
    wb(z, addr, res);
    if (r != 6) REG8(roff_hl, r) = res;
    z->cycles += 23;
}

static void exec_xy(z80_t *z, z80_pair *xy, const uint8_t *roff) {
    uint8_t op = fetch(z);
    z->r++;
    uint16_t addr;
    switch (op) {
    case 0x09: xy->w = op_add16(z, xy->w, BC); z->cycles += 15; return;
    case 0x19: xy->w = op_add16(z, xy->w, DE); z->cycles += 15; return;
    case 0x29: xy->w = op_add16(z, xy->w, xy->w); z->cycles += 15; return;
    case 0x39: xy->w = op_add16(z, xy->w, SP); z->cycles += 15; return;
    case 0x21: xy->w = fetch16(z); z->cycles += 14; return;
    case 0x22: addr = fetch16(z); ww(z, addr, xy->w); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x2A: addr = fetch16(z); xy->w = rw(z, addr); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x23: xy->w++; z->cycles += 10; return;
    case 0x2B: xy->w--; z->cycles += 10; return;
    case 0x24: xy->b.h = op_inc(z, xy->b.h); z->cycles += 8; return;
    case 0x25: xy->b.h = op_dec(z, xy->b.h); z->cycles += 8; return;
    case 0x26: xy->b.h = fetch(z); z->cycles += 11; return;
    case 0x2C: xy->b.l = op_inc(z, xy->b.l); z->cycles += 8; return;
    case 0x2D: xy->b.l = op_dec(z, xy->b.l); z->cycles += 8; return;
    case 0x2E: xy->b.l = fetch(z); z->cycles += 11; return;
    case 0x34:
        addr = (uint16_t)(xy->w + (int8_t)fetch(z)); WZ = addr;
        wb(z, addr, op_inc(z, rb(z, addr))); z->cycles += 23; return;
    case 0x35:
        addr = (uint16_t)(xy->w + (int8_t)fetch(z)); WZ = addr;
        wb(z, addr, op_dec(z, rb(z, addr))); z->cycles += 23; return;
    case 0x36:
        addr = (uint16_t)(xy->w + (int8_t)fetch(z)); WZ = addr;
        wb(z, addr, fetch(z)); z->cycles += 19; return;
    case 0x76:
        z->halted = 1; z->cycles += 8; return;
    case 0x40 ... 0x75: case 0x77 ... 0x7F: {
        unsigned d = (op >> 3) & 7, s = op & 7;
        if (s == 6) {
            addr = (uint16_t)(xy->w + (int8_t)fetch(z)); WZ = addr;
            REG8(roff_hl, d) = rb(z, addr); z->cycles += 19;
        } else if (d == 6) {
            addr = (uint16_t)(xy->w + (int8_t)fetch(z)); WZ = addr;
            wb(z, addr, REG8(roff_hl, s)); z->cycles += 19;
        } else {
            REG8(roff, d) = REG8(roff, s); z->cycles += 8;
        }
        return;
    }
    case 0x80 ... 0xBF: {
        unsigned s = op & 7;
        if (s == 6) {
            addr = (uint16_t)(xy->w + (int8_t)fetch(z)); WZ = addr;
            alu(z, op >> 3, rb(z, addr)); z->cycles += 19;
        } else {
            alu(z, op >> 3, REG8(roff, s)); z->cycles += 8;
        }
        return;
    }
    case 0xCB:
        addr = (uint16_t)(xy->w + (int8_t)fetch(z));
        exec_xycb(z, addr);
        return;
    case 0xE1: xy->w = pop(z); z->cycles += 14; return;
    case 0xE5: push(z, xy->w); z->cycles += 15; return;
    case 0xE3: {
        uint16_t t = rw(z, SP);
        ww(z, SP, xy->w); xy->w = t; WZ = t; z->cycles += 23; return;
    }
    case 0xE9: PC = xy->w; z->cycles += 8; return;
    case 0xF9: SP = xy->w; z->cycles += 10; return;
    default:
        /* Prefix has no effect: behaves as a 4 T-state NOP prefix */
        z->cycles += 4;
        exec_main(z, op);
        return;
    }
}

static void exec_ed(z80_t *z) {
    uint8_t op = fetch(z);
    z->r++;
    uint16_t addr;
    uint8_t v;
    switch (op) {
    case 0x40: case 0x48: case 0x50: case 0x58: case 0x60: case 0x68: case 0x70: case 0x78:
        v = port_in(z, BC); WZ = (uint16_t)(BC + 1);
        F = (uint8_t)((F & CF) | sz53p[v]);
        if (op != 0x70) REG8(roff_hl, (op >> 3) & 7) = v;
        z->cycles += 12; return;
    case 0x41: case 0x49: case 0x51: case 0x59: case 0x61: case 0x69: case 0x71: case 0x79:
        port_out(z, BC, op == 0x71 ? 0 : REG8(roff_hl, (op >> 3) & 7));
        WZ = (uint16_t)(BC + 1);
        z->cycles += 12; return;
    case 0x42: op_sbc16(z, BC); z->cycles += 15; return;
    case 0x52: op_sbc16(z, DE); z->cycles += 15; return;
    case 0x62: op_sbc16(z, HL); z->cycles += 15; return;
    case 0x72: op_sbc16(z, SP); z->cycles += 15; return;
    case 0x4A: op_adc16(z, BC); z->cycles += 15; return;
    case 0x5A: op_adc16(z, DE); z->cycles += 15; return;
    case 0x6A: op_adc16(z, HL); z->cycles += 15; return;
    case 0x7A: op_adc16(z, SP); z->cycles += 15; return;
    case 0x43: addr = fetch16(z); ww(z, addr, BC); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x53: addr = fetch16(z); ww(z, addr, DE); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x63: addr = fetch16(z); ww(z, addr, HL); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x73: addr = fetch16(z); ww(z, addr, SP); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x4B: addr = fetch16(z); BC = rw(z, addr); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x5B: addr = fetch16(z); DE = rw(z, addr); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x6B: addr = fetch16(z); HL = rw(z, addr); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x7B: addr = fetch16(z); SP = rw(z, addr); WZ = (uint16_t)(addr + 1); z->cycles += 20; return;
    case 0x44: case 0x4C: case 0x54: case 0x5C: case 0x64: case 0x6C: case 0x74: case 0x7C:
        v = A; A = 0; op_sub(z, v); z->cycles += 8; return;
    case 0x45: case 0x4D: case 0x55: case 0x5D: case 0x65: case 0x6D: case 0x75: case 0x7D:
        z->iff1 = z->iff2; PC = pop(z); WZ = PC; z->cycles += 14; return;
    case 0x46: case 0x4E: case 0x66: case 0x6E: z->im = 0; z->cycles += 8; return;
    case 0x56: case 0x76: z->im = 1; z->cycles += 8; return;
    case 0x5E: case 0x7E: z->im = 2; z->cycles += 8; return;
    case 0x47: z->i = A; z->cycles += 9; return;
    case 0x4F: z->r = A; z->r7 = A & 0x80; z->cycles += 9; return;
    case 0x57: A = z->i; F = (uint8_t)((F & CF) | sz53[A] | (z->iff2 ? PF : 0)); z->cycles += 9; return;
    case 0x5F: A = (uint8_t)((z->r & 0x7f) | z->r7); F = (uint8_t)((F & CF) | sz53[A] | (z->iff2 ? PF : 0)); z->cycles += 9; return;
    case 0x67: /* RRD */
        v = rb(z, HL);
        wb(z, HL, (uint8_t)((A << 4) | (v >> 4)));
        A = (uint8_t)((A & 0xf0) | (v & 0x0f));
        F = (uint8_t)((F & CF) | sz53p[A]); WZ = (uint16_t)(HL + 1);
        z->cycles += 18; return;
    case 0x6F: /* RLD */
        v = rb(z, HL);
        wb(z, HL, (uint8_t)((v << 4) | (A & 0x0f)));
        A = (uint8_t)((A & 0xf0) | (v >> 4));
        F = (uint8_t)((F & CF) | sz53p[A]); WZ = (uint16_t)(HL + 1);
        z->cycles += 18; return;

    case 0xA0: case 0xA8: case 0xB0: case 0xB8: { /* LDI LDD LDIR LDDR */
        int dir = (op & 0x08) ? -1 : 1;
        v = rb(z, HL);
        BC--;
        wb(z, DE, v);
        DE = (uint16_t)(DE + dir); HL = (uint16_t)(HL + dir);
        v = (uint8_t)(v + A);
        F = (uint8_t)((F & (CF | ZF | SF)) | (BC ? VF : 0) | (v & XF) | ((v & 0x02) ? YF : 0));
        z->cycles += 16;
        if ((op & 0x10) && BC) { PC -= 2; WZ = (uint16_t)(PC + 1); z->cycles += 5; }
        return;
    }
    case 0xA1: case 0xA9: case 0xB1: case 0xB9: { /* CPI CPD CPIR CPDR */
        int dir = (op & 0x08) ? -1 : 1;
        v = rb(z, HL);
        uint8_t t = (uint8_t)(A - v);
        unsigned lk = ((A & 0x08) >> 3) | ((v & 0x08) >> 2) | ((t & 0x08) >> 1);
        HL = (uint16_t)(HL + dir); BC--;
        F = (uint8_t)((F & CF) | (BC ? (VF | NF) : NF) | hc_sub[lk] | (t ? 0 : ZF) | (t & SF));
        if (F & HF) t--;
        F |= (uint8_t)((t & XF) | ((t & 0x02) ? YF : 0));
        z->cycles += 16;
        if ((op & 0x10) && (F & (VF | ZF)) == VF) {
            PC -= 2; WZ = (uint16_t)(PC + 1); z->cycles += 5;
        } else {
            WZ = (uint16_t)(WZ + dir);
        }
        return;
    }
    case 0xA2: case 0xAA: case 0xB2: case 0xBA: { /* INI IND INIR INDR */
        int dir = (op & 0x08) ? -1 : 1;
        uint8_t t = port_in(z, BC);
        WZ = (uint16_t)(BC + dir);
        wb(z, HL, t);
        B--; HL = (uint16_t)(HL + dir);
        uint8_t t2 = (uint8_t)(t + C + dir);
        F = (uint8_t)(((t & 0x80) ? NF : 0) | ((t2 < t) ? (HF | CF) : 0) |
                      parity[(t2 & 7) ^ B] | sz53[B]);
        z->cycles += 16;
        if ((op & 0x10) && B) { PC -= 2; z->cycles += 5; }
        return;
    }
    case 0xA3: case 0xAB: case 0xB3: case 0xBB: { /* OUTI OUTD OTIR OTDR */
        int dir = (op & 0x08) ? -1 : 1;
        uint8_t t = rb(z, HL);
        B--;
        WZ = (uint16_t)(BC + dir);
        port_out(z, BC, t);
        HL = (uint16_t)(HL + dir);
        uint8_t t2 = (uint8_t)(t + L);
        F = (uint8_t)(((t & 0x80) ? NF : 0) | ((t2 < t) ? (HF | CF) : 0) |
                      parity[(t2 & 7) ^ B] | sz53[B]);
        z->cycles += 16;
        if ((op & 0x10) && B) { PC -= 2; z->cycles += 5; }
        return;
    }
    default:
        z->cycles += 8; /* undefined: NOP */
        return;
    }
}

/* ---- main opcode table --------------------------------------------- */

static void exec_main(z80_t *z, uint8_t op) {
    uint16_t t16;
    uint8_t v;
    z->cycles += cyc_main[op];
    switch (op) {
    case 0x00: return;
    case 0x01: BC = fetch16(z); return;
    case 0x11: DE = fetch16(z); return;
    case 0x21: HL = fetch16(z); return;
    case 0x31: SP = fetch16(z); return;
    case 0x02: wb(z, BC, A); WZ = (uint16_t)(((BC + 1) & 0xff) | (A << 8)); return;
    case 0x12: wb(z, DE, A); WZ = (uint16_t)(((DE + 1) & 0xff) | (A << 8)); return;
    case 0x0A: A = rb(z, BC); WZ = (uint16_t)(BC + 1); return;
    case 0x1A: A = rb(z, DE); WZ = (uint16_t)(DE + 1); return;
    case 0x22: t16 = fetch16(z); ww(z, t16, HL); WZ = (uint16_t)(t16 + 1); return;
    case 0x2A: t16 = fetch16(z); HL = rw(z, t16); WZ = (uint16_t)(t16 + 1); return;
    case 0x32: t16 = fetch16(z); wb(z, t16, A); WZ = (uint16_t)(((t16 + 1) & 0xff) | (A << 8)); return;
    case 0x3A: t16 = fetch16(z); A = rb(z, t16); WZ = (uint16_t)(t16 + 1); return;
    case 0x03: BC++; return;
    case 0x13: DE++; return;
    case 0x23: HL++; return;
    case 0x33: SP++; return;
    case 0x0B: BC--; return;
    case 0x1B: DE--; return;
    case 0x2B: HL--; return;
    case 0x3B: SP--; return;
    case 0x09: HL = op_add16(z, HL, BC); return;
    case 0x19: HL = op_add16(z, HL, DE); return;
    case 0x29: HL = op_add16(z, HL, HL); return;
    case 0x39: HL = op_add16(z, HL, SP); return;
    case 0x04: B = op_inc(z, B); return;
    case 0x0C: C = op_inc(z, C); return;
    case 0x14: D = op_inc(z, D); return;
    case 0x1C: E = op_inc(z, E); return;
    case 0x24: H = op_inc(z, H); return;
    case 0x2C: L = op_inc(z, L); return;
    case 0x3C: A = op_inc(z, A); return;
    case 0x34: wb(z, HL, op_inc(z, rb(z, HL))); return;
    case 0x05: B = op_dec(z, B); return;
    case 0x0D: C = op_dec(z, C); return;
    case 0x15: D = op_dec(z, D); return;
    case 0x1D: E = op_dec(z, E); return;
    case 0x25: H = op_dec(z, H); return;
    case 0x2D: L = op_dec(z, L); return;
    case 0x3D: A = op_dec(z, A); return;
    case 0x35: wb(z, HL, op_dec(z, rb(z, HL))); return;
    case 0x06: B = fetch(z); return;
    case 0x0E: C = fetch(z); return;
    case 0x16: D = fetch(z); return;
    case 0x1E: E = fetch(z); return;
    case 0x26: H = fetch(z); return;
    case 0x2E: L = fetch(z); return;
    case 0x3E: A = fetch(z); return;
    case 0x36: v = fetch(z); wb(z, HL, v); return;
    case 0x07:
        A = (uint8_t)((A << 1) | (A >> 7));
        F = (uint8_t)((F & (PF | ZF | SF)) | (A & (CF | XF | YF)));
        return;
    case 0x0F:
        F = (uint8_t)((F & (PF | ZF | SF)) | (A & CF));
        A = (uint8_t)((A >> 1) | (A << 7));
        F |= (uint8_t)(A & (XF | YF));
        return;
    case 0x17:
        v = A;
        A = (uint8_t)((A << 1) | (F & CF));
        F = (uint8_t)((F & (PF | ZF | SF)) | (A & (XF | YF)) | (v >> 7));
        return;
    case 0x1F:
        v = A;
        A = (uint8_t)((A >> 1) | (F << 7));
        F = (uint8_t)((F & (PF | ZF | SF)) | (A & (XF | YF)) | (v & CF));
        return;
    case 0x08: {
        uint16_t t = AF; AF = z->af_.w; z->af_.w = t;
        return;
    }
    case 0x10:
        v = fetch(z);
        if (--B) { PC = (uint16_t)(PC + (int8_t)v); WZ = PC; z->cycles += 5; }
        return;
    case 0x18:
        v = fetch(z);
        PC = (uint16_t)(PC + (int8_t)v); WZ = PC;
        return;
    case 0x20: case 0x28: case 0x30: case 0x38:
        v = fetch(z);
        if (cond(z, (op >> 3) & 3)) { PC = (uint16_t)(PC + (int8_t)v); WZ = PC; z->cycles += 5; }
        return;
    case 0x27: op_daa(z); return;
    case 0x2F:
        A ^= 0xff;
        F = (uint8_t)((F & (CF | PF | ZF | SF)) | (A & (XF | YF)) | NF | HF);
        return;
    case 0x37:
        F = (uint8_t)((F & (PF | ZF | SF)) | (A & (XF | YF)) | CF);
        return;
    case 0x3F:
        F = (uint8_t)((F & (PF | ZF | SF)) | ((F & CF) ? HF : CF) | (A & (XF | YF)));
        return;
    case 0x76:
        z->halted = 1;
        return;
    case 0x40 ... 0x75: case 0x77 ... 0x7F: {
        unsigned d = (op >> 3) & 7, s = op & 7;
        if (s == 6) REG8(roff_hl, d) = rb(z, HL);
        else if (d == 6) wb(z, HL, REG8(roff_hl, s));
        else REG8(roff_hl, d) = REG8(roff_hl, s);
        return;
    }
    case 0x80 ... 0xBF: {
        unsigned s = op & 7;
        alu(z, op >> 3, s == 6 ? rb(z, HL) : REG8(roff_hl, s));
        return;
    }
    case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
        alu(z, op >> 3, fetch(z));
        return;
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        if (cond(z, (op >> 3) & 7)) { PC = pop(z); WZ = PC; z->cycles += 6; }
        return;
    case 0xC9: PC = pop(z); WZ = PC; return;
    case 0xC1: BC = pop(z); return;
    case 0xD1: DE = pop(z); return;
    case 0xE1: HL = pop(z); return;
    case 0xF1: AF = pop(z); return;
    case 0xC5: push(z, BC); return;
    case 0xD5: push(z, DE); return;
    case 0xE5: push(z, HL); return;
    case 0xF5: push(z, AF); return;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA:
        t16 = fetch16(z); WZ = t16;
        if (cond(z, (op >> 3) & 7)) PC = t16;
        return;
    case 0xC3: t16 = fetch16(z); PC = t16; WZ = t16; return;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC:
        t16 = fetch16(z); WZ = t16;
        if (cond(z, (op >> 3) & 7)) { push(z, PC); PC = t16; z->cycles += 7; }
        return;
    case 0xCD: t16 = fetch16(z); push(z, PC); PC = t16; WZ = t16; return;
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        push(z, PC); PC = op & 0x38; WZ = PC;
        return;
    case 0xCB: exec_cb(z); return;
    case 0xDD: exec_xy(z, &z->ix, roff_ix); return;
    case 0xFD: exec_xy(z, &z->iy, roff_iy); return;
    case 0xED: exec_ed(z); return;
    case 0xD3:
        v = fetch(z);
        port_out(z, (uint16_t)(v | (A << 8)), A);
        WZ = (uint16_t)(((v + 1) & 0xff) | (A << 8));
        return;
    case 0xDB:
        v = fetch(z);
        t16 = (uint16_t)(v | (A << 8));
        A = port_in(z, t16);
        WZ = (uint16_t)(t16 + 1);
        return;
    case 0xD9: {
        uint16_t t;
        t = BC; BC = z->bc_.w; z->bc_.w = t;
        t = DE; DE = z->de_.w; z->de_.w = t;
        t = HL; HL = z->hl_.w; z->hl_.w = t;
        return;
    }
    case 0xE3: t16 = rw(z, SP); ww(z, SP, HL); HL = t16; WZ = t16; return;
    case 0xE9: PC = HL; return;
    case 0xEB: t16 = DE; DE = HL; HL = t16; return;
    case 0xF3: z->iff1 = z->iff2 = 0; return;
    case 0xFB: z->iff1 = z->iff2 = 1; z->ei_delay = 1; return;
    case 0xF9: SP = HL; return;
    }
}

/* ---- public API ----------------------------------------------------- */

void z80_init(z80_t *z) {
    if (!tables_ready) init_tables();
    z80_reset(z);
}

void z80_reset(z80_t *z) {
    AF = 0xffff; SP = 0xffff;
    PC = 0; WZ = 0;
    z->i = 0; z->r = 0; z->r7 = 0;
    z->iff1 = z->iff2 = 0; z->im = 0;
    z->halted = 0; z->ei_delay = 0;
    z->nmi_pending = 0;
    z->irq_data = 0xff;
}

void z80_set_irq(z80_t *z, bool level) { z->irq_line = level; }
void z80_nmi(z80_t *z) { z->nmi_pending = 1; }
void z80_end_timeslice(z80_t *z) { z->cycle_target = z->cycles; }

static void take_nmi(z80_t *z) {
    z->nmi_pending = 0;
    z->halted = 0;
    z->iff1 = 0;
    z->r++;
    push(z, PC);
    PC = 0x0066; WZ = PC;
    z->cycles += 11;
}

static void take_irq(z80_t *z) {
    z->halted = 0;
    z->iff1 = z->iff2 = 0;
    z->r++;
    switch (z->im) {
    case 2:
        push(z, PC);
        PC = rw(z, (uint16_t)((z->i << 8) | z->irq_data));
        z->cycles += 19;
        break;
    default: /* IM 0 with RST 38h on the bus (TI hardware) and IM 1 */
        push(z, PC);
        PC = 0x0038;
        z->cycles += 13;
        break;
    }
    WZ = PC;
}

int32_t z80_run(z80_t *z, int32_t n) {
    z->cycles = 0;
    z->cycle_target = n;
    while (z->cycles < z->cycle_target) {
        if (z->nmi_pending) {
            take_nmi(z);
        } else if (z->irq_line && z->iff1 && !z->ei_delay) {
            take_irq(z);
        }
        z->ei_delay = 0;
        if (z->halted) {
            /* Nothing can wake us inside this timeslice: skip ahead. */
            int32_t left = z->cycle_target - z->cycles;
            z->r = (uint8_t)(z->r + (left + 3) / 4);
            z->cycles = z->cycle_target;
            break;
        }
        uint8_t op = fetch(z);
        z->r++;
        exec_main(z, op);
    }
    z->total_cycles += (uint64_t)z->cycles;
    return z->cycles;
}
