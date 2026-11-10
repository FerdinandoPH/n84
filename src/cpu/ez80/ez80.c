/*
 * N84 - TI-8x emulator for the TI-Nspire
 * Copyright (C) 2026 N84 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Zilog eZ80 interpreter core. See ez80.h for the design notes.
 */
#include <stddef.h>
#include "ez80.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define ALWAYS_INLINE inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))

#define CF EZ80_CF
#define NF EZ80_NF
#define PF EZ80_PF
#define XF EZ80_XF
#define HF EZ80_HF
#define YF EZ80_YF
#define ZF EZ80_ZF
#define SF EZ80_SF
#define UNDEF(f) ((f) & (XF | YF))     /* eZ80 keeps bits 3/5 of F unchanged */

static uint8_t parity_tab[256];       /* PF if even number of bits */
static bool tables_ready;

static void init_tables(void) {
    for (int i = 0; i < 256; i++) {
        int p = 0;
        for (int b = 0; b < 8; b++) p ^= (i >> b) & 1;
        parity_tab[i] = p ? 0 : PF;
    }
    tables_ready = true;
}

static ALWAYS_INLINE uint8_t sz(uint8_t v) { return (uint8_t)((v & SF) | (v ? 0 : ZF)); }
static ALWAYS_INLINE uint8_t szp(uint8_t v) { return (uint8_t)(sz(v) | parity_tab[v]); }

/* ---- addressing ------------------------------------------------------ */

static inline uint32_t maskm(uint32_t v, unsigned mode) { return v & (mode ? 0xFFFFFFu : 0xFFFFu); }
static inline uint32_t addr_mode(const ez80_t *z, uint32_t a, unsigned mode) {
    return mode ? (a & 0xFFFFFFu) : (((uint32_t)z->mbase << 16) | (a & 0xFFFFu));
}

/* Direct page access is inlined; MMIO/Flash/unmapped go through out-of-line
 * helpers so that the inlined part stays small. */
static NOINLINE uint8_t mem_rd_slow(ez80_t *z, uint32_t a) { return z->read_slow(z->ctx, a); }
static NOINLINE void mem_wr_slow(ez80_t *z, uint32_t a, uint8_t v) { z->write_slow(z->ctx, a, v); }

static ALWAYS_INLINE uint8_t mem_rd(ez80_t *z, uint32_t a) {
    unsigned pg = a >> EZ80_PAGE_SHIFT;
    z->cycles += z->cost[pg];
    const uint8_t *p = z->rd[pg];
    if (likely(p != NULL)) return p[a & EZ80_PAGE_MASK];
    return mem_rd_slow(z, a);
}
static ALWAYS_INLINE void mem_wr(ez80_t *z, uint32_t a, uint8_t v) {
    unsigned pg = a >> EZ80_PAGE_SHIFT;
    z->cycles += z->wcost[pg];
    uint8_t *p = z->wr[pg];
    if (likely(p != NULL)) p[a & EZ80_PAGE_MASK] = v;
    else mem_wr_slow(z, a, v);
}

static inline uint8_t fetch(ez80_t *z) {
    uint8_t v = mem_rd(z, z->pc);
    z->pc = addr_mode(z, z->pc + 1, z->adl);
    return v;
}
static inline int8_t fetch_off(ez80_t *z) { return (int8_t)fetch(z); }
static inline uint32_t fetch_word(ez80_t *z) {
    uint32_t v = fetch(z);
    v |= (uint32_t)fetch(z) << 8;
    if (z->IL) v |= (uint32_t)fetch(z) << 16;
    return v;
}

static inline uint8_t read_byte(ez80_t *z, uint32_t a) { return mem_rd(z, addr_mode(z, a, z->L)); }
static inline void write_byte(ez80_t *z, uint32_t a, uint8_t v) { mem_wr(z, addr_mode(z, a, z->L), v); }
static inline uint32_t read_word(ez80_t *z, uint32_t a) {
    uint32_t v = read_byte(z, a);
    v |= (uint32_t)read_byte(z, a + 1) << 8;
    if (z->L) v |= (uint32_t)read_byte(z, a + 2) << 16;
    return v;
}
static inline void write_word(ez80_t *z, uint32_t a, uint32_t v) {
    write_byte(z, a, (uint8_t)v);
    write_byte(z, a + 1, (uint8_t)(v >> 8));
    if (z->L) write_byte(z, a + 2, (uint8_t)(v >> 16));
}

/* ---- stack ------------------------------------------------------------ */

static inline uint8_t pop_byte_mode(ez80_t *z, unsigned mode) {
    uint32_t *sp = mode ? &z->spl : &z->sps;
    uint32_t a = addr_mode(z, *sp, mode);
    *sp = maskm(*sp + 1, mode);
    return mem_rd(z, a);
}
static inline void push_byte_mode(ez80_t *z, uint8_t v, unsigned mode) {
    uint32_t *sp = mode ? &z->spl : &z->sps;
    *sp = maskm(*sp - 1, mode);
    mem_wr(z, addr_mode(z, *sp, mode), v);
}
static inline void push_word(ez80_t *z, uint32_t v) {
    if (z->L) push_byte_mode(z, (uint8_t)(v >> 16), z->L);
    push_byte_mode(z, (uint8_t)(v >> 8), z->L);
    push_byte_mode(z, (uint8_t)v, z->L);
}
static inline uint32_t pop_word(ez80_t *z) {
    uint32_t v = pop_byte_mode(z, z->L);
    v |= (uint32_t)pop_byte_mode(z, z->L) << 8;
    if (z->L) v |= (uint32_t)pop_byte_mode(z, z->L) << 16;
    return v;
}
static inline uint32_t read_sp(ez80_t *z) { return z->L ? z->spl : z->sps; }
static inline void write_sp(ez80_t *z, uint32_t v) {
    if (z->L) z->spl = v & 0xFFFFFF; else z->sps = v & 0xFFFF;
}

/* ---- registers --------------------------------------------------------- */

static inline ez80_pair *idx(ez80_t *z) {
    return z->prefix == 2 ? &z->ix : z->prefix == 3 ? &z->iy : &z->hl;
}
static inline ez80_pair *other_idx(ez80_t *z) { return z->prefix == 2 ? &z->iy : &z->ix; }

static inline uint32_t index_address(ez80_t *z) {
    uint32_t v = idx(z)->w;
    if (z->prefix) v += (uint32_t)(int32_t)fetch_off(z);
    return maskm(v, z->L);
}

static inline uint8_t read_reg_at(ez80_t *z, unsigned i, uint32_t addr) {
    switch (i) {
    case 0: return z->bc.b.h;
    case 1: return z->bc.b.l;
    case 2: return z->de.b.h;
    case 3: return z->de.b.l;
    case 4: return idx(z)->b.h;
    case 5: return idx(z)->b.l;
    case 6: return read_byte(z, addr);
    default: return z->a;
    }
}
static inline void write_reg_at(ez80_t *z, unsigned i, uint32_t addr, uint8_t v) {
    switch (i) {
    case 0: z->bc.b.h = v; break;
    case 1: z->bc.b.l = v; break;
    case 2: z->de.b.h = v; break;
    case 3: z->de.b.l = v; break;
    case 4: idx(z)->b.h = v; break;
    case 5: idx(z)->b.l = v; break;
    case 6: write_byte(z, addr, v); break;
    default: z->a = v; break;
    }
}
static inline uint8_t read_reg(ez80_t *z, unsigned i) {
    return read_reg_at(z, i, i == 6 ? index_address(z) : 0);
}
static inline void write_reg(ez80_t *z, unsigned i, uint8_t v) {
    write_reg_at(z, i, i == 6 ? index_address(z) : 0, v);
}

static inline uint32_t read_rp(ez80_t *z, unsigned p) {
    uint32_t v;
    switch (p) {
    case 0: v = z->bc.w; break;
    case 1: v = z->de.w; break;
    case 2: v = idx(z)->w; break;
    default: v = read_sp(z); break;
    }
    return maskm(v, z->L);
}
static inline void write_rp(ez80_t *z, unsigned p, uint32_t v) {
    v = maskm(v, z->L);
    switch (p) {
    case 0: z->bc.w = v; break;
    case 1: z->de.w = v; break;
    case 2: idx(z)->w = v; break;
    default: write_sp(z, v); break;
    }
}
static inline uint32_t read_rp2(ez80_t *z, unsigned p) {
    return p == 3 ? (uint32_t)((z->a << 8) | z->f) : read_rp(z, p);
}
static inline void write_rp2(ez80_t *z, unsigned p, uint32_t v) {
    if (p == 3) { z->a = (uint8_t)(v >> 8); z->f = (uint8_t)v; }
    else write_rp(z, p, v);
}
static inline uint32_t read_rp3(ez80_t *z, unsigned p) {
    uint32_t v;
    switch (p) {
    case 0: v = z->bc.w; break;
    case 1: v = z->de.w; break;
    case 2: v = z->hl.w; break;
    default: v = idx(z)->w; break;
    }
    return maskm(v, z->L);
}
static inline void write_rp3(ez80_t *z, unsigned p, uint32_t v) {
    v = maskm(v, z->L);
    switch (p) {
    case 0: z->bc.w = v; break;
    case 1: z->de.w = v; break;
    case 2: z->hl.w = v; break;
    default: idx(z)->w = v; break;
    }
}

static inline bool cond(ez80_t *z, unsigned cc) {
    switch (cc) {
    case 0: return !(z->f & ZF);
    case 1: return z->f & ZF;
    case 2: return !(z->f & CF);
    case 3: return z->f & CF;
    case 4: return !(z->f & PF);
    case 5: return z->f & PF;
    case 6: return !(z->f & SF);
    default: return z->f & SF;
    }
}

/* ---- control flow -------------------------------------------------------- */

static inline void jump(ez80_t *z, uint32_t addr, unsigned mode) {
    z->adl = (uint8_t)mode;
    z->pc = addr_mode(z, addr, mode);
}

static void do_rst(ez80_t *z, uint32_t addr, unsigned stack, unsigned mode, unsigned mixed) {
    z->cycles++;
    if (mixed) {
        uint8_t mb = (uint8_t)((z->madl << 1) | z->adl);
        if (z->adl) {
            push_byte_mode(z, (uint8_t)(z->pc >> 16), 1);
            push_byte_mode(z, mb, 1);
        }
        push_byte_mode(z, (uint8_t)(z->pc >> 8), stack);
        push_byte_mode(z, (uint8_t)z->pc, stack);
        if (!z->adl) push_byte_mode(z, mb, 1);
    } else {
        push_word(z, z->pc);
    }
    jump(z, addr, mode);
}

static void do_call(ez80_t *z, uint32_t addr, unsigned mode, unsigned mixed) {
    if (mixed) {
        unsigned stack = z->IL || (z->L && !z->adl);
        uint8_t mb = (uint8_t)((z->madl << 1) | z->adl);
        if (z->adl) {
            push_byte_mode(z, (uint8_t)(z->pc >> 16), 1);
            if (!z->IL) push_byte_mode(z, mb, 1);
        }
        push_byte_mode(z, (uint8_t)(z->pc >> 8), stack);
        push_byte_mode(z, (uint8_t)z->pc, stack);
        if (z->IL || !z->adl) push_byte_mode(z, mb, 1);
    } else {
        push_word(z, z->pc);
    }
    jump(z, addr, mode);
}

static void do_return(ez80_t *z) {
    uint32_t addr;
    unsigned mode = z->adl;
    z->cycles++;
    if (z->suffix) {
        mode = pop_byte_mode(z, 1) & 1;
        addr = pop_byte_mode(z, z->adl);
        addr |= (uint32_t)pop_byte_mode(z, z->adl) << 8;
        if (mode) addr |= maskm((uint32_t)pop_byte_mode(z, 1) << 16, z->adl || z->L);
    } else {
        addr = pop_word(z);
    }
    jump(z, addr, mode);
}

static void do_interrupt(ez80_t *z, uint32_t addr) {
    do_rst(z, addr, z->adl, z->adl | z->madl, z->madl);
}

static void trap(ez80_t *z, unsigned rewind) {
    z->cycles++;
    z->pc = maskm(z->pc - rewind, z->adl);
    z->L = z->IL = z->adl;
    z->prefix = z->suffix = 0;
    do_interrupt(z, 0x00);
}

/* ---- ALU ----------------------------------------------------------------- */

static ALWAYS_INLINE uint8_t hc_add(uint8_t a, uint8_t b, unsigned c) { return (((a & 0xF) + (b & 0xF) + c) & 0x10) ? HF : 0; }
static ALWAYS_INLINE uint8_t hc_sub(uint8_t a, uint8_t b, unsigned c) { return (((a & 0xF) - (b & 0xF) - c) & 0x10) ? HF : 0; }
static ALWAYS_INLINE uint8_t ov_add(uint8_t a, uint8_t b, uint8_t r) { return ((a ^ r) & (b ^ r) & 0x80) ? PF : 0; }
static ALWAYS_INLINE uint8_t ov_sub(uint8_t a, uint8_t b, uint8_t r) { return ((a ^ b) & (a ^ r) & 0x80) ? PF : 0; }

static ALWAYS_INLINE void alu(ez80_t *z, unsigned op, uint8_t v) {
    uint8_t a = z->a, r;
    unsigned c = z->f & CF, t;
    switch (op) {
    case 0: t = a + v;     r = (uint8_t)t; z->a = r;
        z->f = (uint8_t)(sz(r) | UNDEF(z->f) | ov_add(a, v, r) | ((t & 0x100) ? CF : 0) | hc_add(a, v, 0)); break;
    case 1: t = a + v + c; r = (uint8_t)t; z->a = r;
        z->f = (uint8_t)(sz(r) | UNDEF(z->f) | ov_add(a, v, r) | ((t & 0x100) ? CF : 0) | hc_add(a, v, c)); break;
    case 2: t = a - v;     r = (uint8_t)t; z->a = r;
        z->f = (uint8_t)(sz(r) | UNDEF(z->f) | ov_sub(a, v, r) | NF | ((t & 0x100) ? CF : 0) | hc_sub(a, v, 0)); break;
    case 3: t = a - v - c; r = (uint8_t)t; z->a = r;
        z->f = (uint8_t)(sz(r) | UNDEF(z->f) | ov_sub(a, v, r) | NF | ((t & 0x100) ? CF : 0) | hc_sub(a, v, c)); break;
    case 4: r = a & v; z->a = r; z->f = (uint8_t)(szp(r) | UNDEF(z->f) | HF); break;
    case 5: r = a ^ v; z->a = r; z->f = (uint8_t)(szp(r) | UNDEF(z->f)); break;
    case 6: r = a | v; z->a = r; z->f = (uint8_t)(szp(r) | UNDEF(z->f)); break;
    default: t = a - v; r = (uint8_t)t;
        z->f = (uint8_t)(sz(r) | UNDEF(z->f) | NF | ((t & 0x100) ? CF : 0) | ov_sub(a, v, r) | hc_sub(a, v, 0)); break;
    }
}

static void daa(ez80_t *z) {
    uint8_t old = z->a, v = 0;
    if ((z->a & 0xF) > 9 || (z->f & HF)) v += 6;
    if ((((z->a + v) & 0x1FF) >> 4) > 9 || ((z->a + v) & 0x100) || (z->f & CF)) v += 0x60;
    if (z->f & NF) {
        z->a = (uint8_t)(z->a - v);
        z->f = (uint8_t)(szp(z->a) | UNDEF(z->f) | NF | (v >= 0x60 ? CF : 0) | hc_sub(old, v, 0));
    } else {
        z->a = (uint8_t)(z->a + v);
        z->f = (uint8_t)(szp(z->a) | UNDEF(z->f) | (v >= 0x60 ? CF : 0) | hc_add(old, v, 0));
    }
}

static void rot_acc(ez80_t *z, unsigned y) {
    uint8_t a = z->a, keep = z->f & (SF | ZF | PF | XF | YF);
    switch (y) {
    case 0: z->a = (uint8_t)((a << 1) | (a >> 7)); z->f = (uint8_t)(keep | (a >> 7)); break;
    case 1: z->a = (uint8_t)((a >> 1) | (a << 7)); z->f = (uint8_t)(keep | (a & 1)); break;
    case 2: z->a = (uint8_t)((a << 1) | (z->f & CF)); z->f = (uint8_t)(keep | (a >> 7)); break;
    case 3: z->a = (uint8_t)((a >> 1) | ((z->f & CF) << 7)); z->f = (uint8_t)(keep | (a & 1)); break;
    case 4: daa(z); break;
    case 5: z->a = (uint8_t)~a; z->f |= NF | HF; break;
    case 6: z->f = (uint8_t)((z->f & ~(NF | HF)) | CF); break;
    default: z->f = (uint8_t)((z->f & ~(NF | HF | CF)) | ((z->f & CF) ? HF : CF)); break;
    }
}

static uint8_t rot(ez80_t *z, unsigned y, uint8_t v) {
    uint8_t c7 = v >> 7, c0 = v & 1, oc = z->f & CF, nc;
    switch (y) {
    case 0: v = (uint8_t)((v << 1) | c7); nc = c7; break;
    case 1: v = (uint8_t)((v >> 1) | (c0 << 7)); nc = c0; break;
    case 2: v = (uint8_t)((v << 1) | oc); nc = c7; break;
    case 3: v = (uint8_t)((v >> 1) | (oc << 7)); nc = c0; break;
    case 4: v = (uint8_t)(v << 1); nc = c7; break;
    case 5: v = (uint8_t)((v >> 1) | (c7 << 7)); nc = c0; break;
    default: v = (uint8_t)(v >> 1); nc = c0; break;   /* 7: SRL (6 traps) */
    }
    z->f = (uint8_t)(nc | szp(v) | UNDEF(z->f));
    return v;
}

/* ---- block instructions (one iteration; returns true to repeat) --------- */

static uint32_t dec_bc_partial(ez80_t *z) {
    uint32_t v = maskm(z->bc.w - 1, z->L);
    if (z->L) z->bc.w = v; else z->bc.s = (uint16_t)v;
    return v;
}

static inline uint8_t port_rd(ez80_t *z, uint16_t p) { return z->port_in(z->ctx, p); }
static inline void port_wr(ez80_t *z, uint16_t p, uint8_t v) { z->port_out(z->ctx, p, v); }

/* returns: 0 done, 1 repeat, -1 trapped */
static int block(ez80_t *z, uint8_t op) {
    unsigned x = op >> 6, p = (op >> 4) & 3, q = (op >> 3) & 1, zz = op & 7;
    unsigned xp = (x << 2) | p;
    int delta = q ? -1 : 1;
    bool repeat = ((x | p) & 1) != 0;
    uint8_t old, nw = 0;
    switch (zz) {
    case 0:
        if (xp != 0xA && xp != 0xB) { trap(z, 1); return -1; }
        write_byte(z, z->de.w, read_byte(z, z->hl.w));
        z->de.w = maskm(z->de.w + (uint32_t)delta, z->L);
        z->f &= (uint8_t)~(HF | NF | PF);
        if (dec_bc_partial(z)) z->f |= PF;
        repeat = repeat && (z->f & PF);
        break;
    case 1:
        if (xp != 0xA && xp != 0xB) { trap(z, 1); return -1; }
        old = read_byte(z, z->hl.w);
        nw = (uint8_t)(z->a - old);
        z->f = (uint8_t)(sz(nw) | hc_sub(z->a, old, 0) | (dec_bc_partial(z) ? PF : 0) | NF |
                         (z->f & CF) | UNDEF(z->f));
        repeat = repeat && !(z->f & ZF) && (z->f & PF);
        if (xp == 0xB) z->cycles++;
        break;
    case 2:
        switch (xp) {
        case 0x8: /* INIM, INDM */
            write_byte(z, z->hl.w, nw = port_rd(z, z->bc.b.l));
            z->bc.b.l = (uint8_t)(z->bc.b.l + delta);
            old = z->bc.b.h--;
            z->f = (uint8_t)(sz(z->bc.b.h) | hc_sub(old, 0, 1) | ((nw & 0x80) ? NF : 0) | UNDEF(z->f));
            break;
        case 0x9: /* INIMR, INDMR */
            write_byte(z, z->hl.w, nw = port_rd(z, z->bc.b.l));
            z->bc.b.l = (uint8_t)(z->bc.b.l + delta);
            z->bc.b.h--;
            z->f = (uint8_t)((z->f & ~(ZF | NF)) | (z->bc.b.h ? 0 : ZF) | ((nw & 0x80) ? NF : 0));
            break;
        case 0xA: case 0xB: /* INI, IND, INIR, INDR */
            write_byte(z, z->hl.w, nw = port_rd(z, z->bc.s));
            z->bc.b.h--;
            z->f = (uint8_t)((z->f & ~(ZF | NF)) | (z->bc.b.h ? 0 : ZF) | ((nw & 0x80) ? NF : 0));
            break;
        case 0xC: /* INIRX, INDRX */
            write_byte(z, z->hl.w, nw = port_rd(z, z->de.s));
            z->f = (uint8_t)((z->f & ~(ZF | NF)) | (dec_bc_partial(z) ? 0 : ZF) | ((nw & 0x80) ? NF : 0));
            break;
        default: trap(z, 1); return -1;
        }
        repeat = repeat && !(z->f & ZF);
        break;
    case 3:
        switch (xp) {
        case 0x8: case 0x9: /* OTIM, OTDM, OTIMR, OTDMR */
            port_wr(z, z->bc.b.l, nw = read_byte(z, z->hl.w));
            z->bc.b.l = (uint8_t)(z->bc.b.l + delta);
            old = z->bc.b.h--;
            z->f = (uint8_t)(sz(z->bc.b.h) | hc_sub(old, 0, 1) | ((nw & 0x80) ? NF : 0) | UNDEF(z->f));
            break;
        case 0xA: case 0xB: /* OUTI, OUTD, OTIR, OTDR */
            port_wr(z, z->bc.s, nw = read_byte(z, z->hl.w));
            z->bc.b.h--;
            z->f = (uint8_t)((z->f & ~(ZF | NF)) | (z->bc.b.h ? 0 : ZF) | ((nw & 0x80) ? NF : 0));
            break;
        case 0xC: /* OTIRX, OTDRX */
            port_wr(z, z->de.s, nw = read_byte(z, z->hl.w));
            z->f = (uint8_t)((z->f & ~(ZF | NF)) | (dec_bc_partial(z) ? 0 : ZF) | ((nw & 0x80) ? NF : 0));
            break;
        default: trap(z, 1); return -1;
        }
        repeat = repeat && !(z->f & ZF);
        break;
    case 4:
        if (xp & 1) {
            if (xp & 2) port_wr(z, z->de.s, nw = read_byte(z, z->hl.w));    /* OTI2R, OTD2R */
            else write_byte(z, z->hl.w, nw = port_rd(z, z->de.s));          /* INI2R, IND2R */
            z->de.w = maskm(z->de.w + (uint32_t)delta, z->L);
            z->f = (uint8_t)((z->f & ~ZF) | (dec_bc_partial(z) ? 0 : ZF));
            repeat = repeat && !(z->f & ZF);
        } else {
            if (xp & 2) port_wr(z, z->bc.s, nw = read_byte(z, z->hl.w));    /* OUTI2, OUTD2 */
            else write_byte(z, z->hl.w, nw = port_rd(z, z->bc.s));          /* INI2, IND2 */
            z->bc.b.l = (uint8_t)(z->bc.b.l + delta);
            z->bc.b.h--;
            z->f = (uint8_t)((z->f & ~ZF) | (z->bc.b.h ? 0 : ZF));
        }
        z->f = (uint8_t)((z->f & ~NF) | ((nw & 0x80) ? NF : 0));
        break;
    default:
        trap(z, 1);
        return -1;
    }
    z->hl.w = maskm(z->hl.w + (uint32_t)delta, z->L);
    z->cycles++;
    return repeat ? 1 : 0;
}

/* ---- ED prefix --------------------------------------------------------- */

static void exec_ed(ez80_t *z, uint32_t start_pc) {
    uint8_t op = fetch(z);
    z->r++;
    unsigned x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;
    uint8_t nw, old;
    uint32_t ow, opw, nwd;
    z->prefix = 0;
    switch (x) {
    case 0:
        switch (zz) {
        case 0: /* IN0 r,(n) */
            nw = port_rd(z, fetch(z));
            if (y != 6) write_reg(z, y, nw);
            z->f = (uint8_t)(szp(nw) | UNDEF(z->f) | (z->f & CF));
            return;
        case 1:
            if (y == 6) z->iy.w = read_word(z, z->hl.w);          /* LD IY,(HL) */
            else { uint8_t n = fetch(z); port_wr(z, n, read_reg(z, y)); }  /* OUT0 (n),r */
            return;
        case 2: case 3: /* LEA rp3,IX/IY+d */
            if (q) { trap(z, 1); return; }
            z->prefix = (uint8_t)zz;
            write_rp3(z, p, index_address(z));
            return;
        case 4: /* TST A,r */
            nw = z->a & read_reg(z, y);
            z->f = (uint8_t)(szp(nw) | UNDEF(z->f) | HF);
            return;
        case 6:
            if (y == 7) { write_word(z, z->hl.w, z->iy.w); return; }   /* LD (HL),IY */
            trap(z, 1);
            return;
        case 5:
            trap(z, 1);
            return;
        default: /* 7 */
            z->prefix = 2;
            if (q) write_word(z, z->hl.w, read_rp3(z, p));          /* LD (HL),rp3 */
            else write_rp3(z, p, read_word(z, z->hl.w));            /* LD rp3,(HL) */
            return;
        }
    case 1:
        switch (zz) {
        case 0: /* IN r,(BC) */
            nw = port_rd(z, z->bc.s);
            if (y != 6) write_reg(z, y, nw);
            z->f = (uint8_t)(szp(nw) | UNDEF(z->f) | (z->f & CF));
            return;
        case 1: /* OUT (BC),r */
            if (y == 6) { trap(z, 1); return; }
            port_wr(z, z->bc.s, read_reg(z, y));
            return;
        case 2: {
            unsigned c = z->f & CF;
            uint32_t topbit = z->L ? 0x800000u : 0x8000u;
            ow = maskm(z->hl.w, z->L);
            opw = maskm(read_rp(z, p), z->L);
            if (!q) { /* SBC HL,rp */
                nwd = ow - opw - c;
                z->hl.w = maskm(nwd, z->L);
                z->f = (uint8_t)(((z->hl.w & topbit) ? SF : 0) | (z->hl.w ? 0 : ZF) | UNDEF(z->f) |
                                 (((ow ^ opw) & (ow ^ z->hl.w) & topbit) ? PF : 0) | NF |
                                 ((nwd & (topbit << 1)) ? CF : 0) |
                                 ((((ow & 0xFFF) - (opw & 0xFFF) - c) & 0x1000) ? HF : 0));
            } else {  /* ADC HL,rp */
                nwd = ow + opw + c;
                z->hl.w = maskm(nwd, z->L);
                z->f = (uint8_t)(((z->hl.w & topbit) ? SF : 0) | (z->hl.w ? 0 : ZF) | UNDEF(z->f) |
                                 (((ow ^ z->hl.w) & (opw ^ z->hl.w) & topbit) ? PF : 0) |
                                 ((nwd & (topbit << 1)) ? CF : 0) |
                                 ((((ow & 0xFFF) + (opw & 0xFFF) + c) & 0x1000) ? HF : 0));
            }
            return;
        }
        case 3:
            if (!q) { uint32_t a = fetch_word(z); write_word(z, a, read_rp(z, p)); }
            else write_rp(z, p, read_word(z, fetch_word(z)));
            return;
        case 4:
            if (!q) {
                switch (p) {
                case 0: /* NEG */
                    old = z->a;
                    z->a = (uint8_t)-old;
                    z->f = (uint8_t)(sz(z->a) | UNDEF(z->f) | (old == 0x80 ? PF : 0) | NF |
                                     (old ? CF : 0) | hc_sub(0, old, 0));
                    return;
                case 1: /* LEA IX,IY+d */
                    z->prefix = 3;
                    z->ix.w = index_address(z);
                    return;
                case 2: /* TST A,n */
                    nw = z->a & fetch(z);
                    z->f = (uint8_t)(szp(nw) | UNDEF(z->f) | HF);
                    return;
                default: /* TSTIO n */
                    nw = port_rd(z, z->bc.b.l);
                    nw &= fetch(z);
                    z->f = (uint8_t)(szp(nw) | UNDEF(z->f) | HF);
                    return;
                }
            }
            /* MLT rp */
            z->cycles += 4;
            ow = read_rp(z, p);
            write_rp(z, p, (ow & 0xFF) * ((ow >> 8) & 0xFF));
            return;
        case 5:
            switch (y) {
            case 0: case 1: /* RETN, RETI */
                z->iff1 = z->iff2;
                do_return(z);
                return;
            case 2: /* LEA IY,IX+d */
                z->prefix = 2;
                z->iy.w = index_address(z);
                return;
            case 4: /* PEA IX+d */
                push_word(z, z->ix.w + (uint32_t)(int32_t)fetch_off(z));
                return;
            case 5: /* LD MB,A */
                if (z->adl) z->mbase = z->a;
                return;
            case 7: /* STMIX */
                z->madl = 1;
                return;
            default:
                trap(z, 1);
                return;
            }
        case 6:
            switch (y) {
            case 0: case 2: case 3: z->im = (uint8_t)y; return;
            case 4: push_word(z, z->iy.w + (uint32_t)(int32_t)fetch_off(z)); return;  /* PEA IY+d */
            case 5: z->a = z->mbase; return;                                          /* LD A,MB */
            case 6: z->cycles++; z->halted = 1; return;                               /* SLP */
            case 7: z->madl = 0; return;                                              /* RSMIX */
            default: trap(z, 1); return;
            }
        default: /* 7 */
            switch (y) {
            case 0: z->i = (uint16_t)((z->i & 0xFF00) | z->a); return;   /* LD I,A */
            case 1: z->r = z->a; return;                                   /* LD R,A */
            case 2: /* LD A,I */
                z->a = (uint8_t)z->i;
                z->f = (uint8_t)(sz(z->a) | UNDEF(z->f) | (z->iff1 ? PF : 0) | (z->f & CF));
                return;
            case 3: /* LD A,R */
                z->a = z->r;
                z->f = (uint8_t)(sz(z->a) | UNDEF(z->f) | (z->iff1 ? PF : 0) | (z->f & CF));
                return;
            case 4: /* RRD */
                old = z->a;
                nw = read_byte(z, z->hl.w);
                z->cycles++;
                z->a = (uint8_t)((z->a & 0xF0) | (nw & 0x0F));
                write_byte(z, z->hl.w, (uint8_t)((nw >> 4) | (old << 4)));
                z->f = (uint8_t)((z->f & CF) | szp(z->a) | UNDEF(z->f));
                return;
            case 5: /* RLD */
                old = z->a;
                nw = read_byte(z, z->hl.w);
                z->cycles++;
                z->a = (uint8_t)((z->a & 0xF0) | (nw >> 4));
                write_byte(z, z->hl.w, (uint8_t)((nw << 4) | (old & 0x0F)));
                z->f = (uint8_t)((z->f & CF) | szp(z->a) | UNDEF(z->f));
                return;
            default:
                trap(z, 1);
                return;
            }
        }
    case 2:
        if (block(z, op) == 1) z->pc = start_pc;
        return;
    default:
        switch (op) {
        case 0xC2: case 0xC3: case 0xCA: case 0xCB: /* INIRX OTIRX INDRX OTDRX */
            if (block(z, op) == 1) z->pc = start_pc;
            return;
        case 0xC7: z->i = (uint16_t)z->hl.w; return;                         /* LD I,HL */
        case 0xD7:                                                            /* LD HL,I */
            z->hl.w = maskm(z->i | ((uint32_t)z->mbase << 16), z->L);
            z->f = UNDEF(z->f);
            return;
        default:
            trap(z, 1);
            return;
        }
    }
}

/* ---- CB prefix ----------------------------------------------------------- */

static void exec_cb(ez80_t *z) {
    uint32_t w = index_address(z);
    uint8_t op = fetch(z);
    if (!z->prefix) z->r++;
    unsigned x = op >> 6, y = (op >> 3) & 7, zz = op & 7;
    if (z->prefix && zz != 6) { trap(z, 2); return; }
    if (x == 0 && y == 6) { trap(z, 1u + (z->prefix != 0)); return; }
    uint8_t v = read_reg_at(z, zz, w);
    switch (x) {
    case 0:
        z->cycles += zz == 6;
        write_reg_at(z, zz, w, rot(z, y, v));
        break;
    case 1:
        v &= (uint8_t)(1u << y);
        z->f = (uint8_t)(szp(v) | UNDEF(z->f) | (z->f & CF) | HF);
        break;
    case 2:
        z->cycles += zz == 6;
        write_reg_at(z, zz, w, (uint8_t)(v & ~(1u << y)));
        break;
    default:
        z->cycles += zz == 6;
        write_reg_at(z, zz, w, (uint8_t)(v | (1u << y)));
        break;
    }
}

/* ---- one instruction ------------------------------------------------------ */

static void exec(ez80_t *z) {
    uint32_t start_pc = z->pc;
    z->L = z->IL = z->adl;
    z->prefix = z->suffix = 0;
    for (;;) {
        uint8_t op = fetch(z);
        z->r++;
        unsigned x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;
        int8_t s;
        uint8_t old, nw;
        uint32_t w, ow, opw, nwd;
        switch (x) {
        case 0:
            switch (zz) {
            case 0:
                if (z->prefix) { trap(z, 1); return; }
                switch (y) {
                case 0: return;                                     /* NOP */
                case 1: {                                           /* EX AF,AF' */
                    uint8_t t = z->a; z->a = z->a_; z->a_ = t;
                    t = z->f; z->f = z->f_; z->f_ = t;
                    return;
                }
                case 2:                                             /* DJNZ */
                    s = fetch_off(z);
                    if (--z->bc.b.h) { z->cycles++; z->pc = addr_mode(z, maskm(z->pc + (uint32_t)(int32_t)s, z->L), z->adl); }
                    return;
                case 3:                                             /* JR */
                    s = fetch_off(z);
                    z->pc = addr_mode(z, maskm(z->pc + (uint32_t)(int32_t)s, z->L), z->adl);
                    return;
                default:                                            /* JR cc */
                    s = fetch_off(z);
                    if (cond(z, y - 4)) { z->cycles++; z->pc = addr_mode(z, maskm(z->pc + (uint32_t)(int32_t)s, z->L), z->adl); }
                    return;
                }
            case 1:
                if (!q) {
                    if (z->prefix && p != 2) {
                        if (y == 6) { w = index_address(z); other_idx(z)->w = read_word(z, w); }  /* LD IY/IX,(IX/IY+d) */
                        else trap(z, 1);
                        return;
                    }
                    write_rp(z, p, fetch_word(z));                  /* LD rr,Mmn */
                } else {                                            /* ADD HL,rr */
                    ow = maskm(idx(z)->w, z->L);
                    opw = maskm(read_rp(z, p), z->L);
                    nwd = ow + opw;
                    idx(z)->w = maskm(nwd, z->L);
                    z->f = (uint8_t)((z->f & (SF | ZF | PF | XF | YF)) |
                                     ((nwd & (z->L ? 0x1000000u : 0x10000u)) ? CF : 0) |
                                     ((((ow & 0xFFF) + (opw & 0xFFF)) & 0x1000) ? HF : 0));
                }
                return;
            case 2:
                if (z->prefix && p != 2) { trap(z, 1); return; }
                if (!q) {
                    switch (p) {
                    case 0: write_byte(z, z->bc.w, z->a); break;
                    case 1: write_byte(z, z->de.w, z->a); break;
                    case 2: w = fetch_word(z); write_word(z, w, idx(z)->w); break;
                    default: w = fetch_word(z); write_byte(z, w, z->a); break;
                    }
                } else {
                    switch (p) {
                    case 0: z->a = read_byte(z, z->bc.w); break;
                    case 1: z->a = read_byte(z, z->de.w); break;
                    case 2: idx(z)->w = read_word(z, fetch_word(z)); break;
                    default: z->a = read_byte(z, fetch_word(z)); break;
                    }
                }
                return;
            case 3:
                if (z->prefix && p != 2) { trap(z, 1); return; }
                write_rp(z, p, read_rp(z, p) + (q ? (uint32_t)-1 : 1u));  /* INC/DEC rp */
                return;
            case 4: case 5:                                         /* INC/DEC r */
                if (z->prefix && (y < 4 || y == 7)) { trap(z, 1); return; }
                w = y == 6 ? index_address(z) : 0;
                old = read_reg_at(z, y, w);
                z->cycles += y == 6;
                if (zz == 4) {
                    nw = (uint8_t)(old + 1);
                    write_reg_at(z, y, w, nw);
                    z->f = (uint8_t)((z->f & CF) | sz(nw) | hc_add(old, 0, 1) | (nw == 0x80 ? PF : 0) | UNDEF(z->f));
                } else {
                    nw = (uint8_t)(old - 1);
                    write_reg_at(z, y, w, nw);
                    z->f = (uint8_t)((z->f & CF) | sz(nw) | hc_sub(old, 0, 1) | (old == 0x80 ? PF : 0) | NF | UNDEF(z->f));
                }
                return;
            case 6:                                                 /* LD r,n */
                if (z->prefix) {
                    if (y < 4) { trap(z, 1); return; }
                    if (y == 7) { w = index_address(z); write_word(z, w, other_idx(z)->w); return; }  /* LD (IX+d),IY */
                }
                w = y == 6 ? index_address(z) : 0;
                write_reg_at(z, y, w, fetch(z));
                return;
            default:                                                /* 7 */
                if (z->prefix) {
                    w = index_address(z);
                    if (q) write_word(z, w, read_rp3(z, p));      /* LD (IX+d),rp3 */
                    else write_rp3(z, p, read_word(z, w));        /* LD rp3,(IX+d) */
                } else {
                    rot_acc(z, y);
                }
                return;
            }
        case 1:
            if (zz == y) {
                if (z->prefix && zz != 4 && zz != 5) { trap(z, 1); return; }
                if (zz < 4) {                                       /* .SIS .LIS .SIL .LIL */
                    z->L = op & 1;
                    z->IL = (op >> 1) & 1;
                    z->suffix = 1;
                    continue;
                }
                if (zz == 6) { z->cycles++; z->halted = 1; }        /* HALT */
                return;
            }
            if (z->prefix && (zz < 4 || zz == 7) && (y < 4 || y == 7)) { trap(z, 1); return; }
            {   /* LD r[y], r[z] -- (IX+d) never combines with IXH/IXL */
                uint8_t pf = z->prefix;
                z->prefix = y != 6 ? pf : 0;
                uint8_t v = read_reg(z, zz);
                z->prefix = zz != 6 ? pf : 0;
                write_reg(z, y, v);
                z->prefix = pf;
            }
            return;
        case 2:                                                     /* ALU r */
            if (z->prefix && (zz < 4 || zz == 7)) { trap(z, 1); return; }
            alu(z, y, read_reg(z, zz));
            return;
        default:                                                    /* x = 3 */
            if (z->prefix && !(op & 1)) { trap(z, 1); return; }
            switch (zz) {
            case 0:                                                 /* RET cc */
                z->cycles++;
                if (cond(z, y)) do_return(z);
                return;
            case 1:
                if (!q) {                                           /* POP rp2 */
                    if (z->prefix && p != 2) { trap(z, 1); return; }
                    write_rp2(z, p, pop_word(z));
                    return;
                }
                if (z->prefix && p < 2) { trap(z, 1); return; }
                switch (p) {
                case 0: do_return(z); return;                       /* RET */
                case 1: {                                           /* EXX */
                    ez80_pair t;
                    t = z->bc; z->bc = z->bc_; z->bc_ = t;
                    t = z->de; z->de = z->de_; z->de_ = t;
                    t = z->hl; z->hl = z->hl_; z->hl_ = t;
                    return;
                }
                case 2: jump(z, idx(z)->w, z->L); return;           /* JP (HL) */
                default: write_sp(z, idx(z)->w); return;            /* LD SP,HL */
                }
            case 2:                                                 /* JP cc,nn */
                w = fetch_word(z);
                if (cond(z, y)) { z->cycles++; jump(z, w, z->L); }
                return;
            case 3:
                if (z->prefix && y != 1 && y != 4) { trap(z, 1); return; }
                switch (y) {
                case 0: z->cycles++; jump(z, fetch_word(z), z->L); return;   /* JP nn */
                case 1: exec_cb(z); return;
                case 2: nw = fetch(z); port_wr(z, (uint16_t)((z->a << 8) | nw), z->a); return;  /* OUT (n),A */
                case 3: nw = fetch(z); z->a = port_rd(z, (uint16_t)((z->a << 8) | nw)); return; /* IN A,(n) */
                case 4: {                                           /* EX (SP),HL */
                    uint32_t sp = maskm(read_sp(z), z->L);
                    ow = read_word(z, sp);
                    nwd = idx(z)->w;
                    ow = maskm(ow, z->L);
                    if (z->L) idx(z)->w = ow; else idx(z)->s = (uint16_t)ow;
                    write_word(z, sp, nwd);
                    return;
                }
                case 5: {                                           /* EX DE,HL */
                    uint32_t t = maskm(z->de.w, z->L);
                    z->de.w = maskm(z->hl.w, z->L);
                    z->hl.w = t;
                    return;
                }
                case 6: z->iff1 = z->iff2 = 0; z->ei_delay = 0; return;      /* DI */
                default: z->iff1 = z->iff2 = 1; z->ei_delay = 1; return;     /* EI */
                }
            case 4:                                                 /* CALL cc,nn */
                w = fetch_word(z);
                if (cond(z, y)) {
                    z->cycles += !z->suffix && !z->adl;
                    do_call(z, w, z->IL, z->suffix);
                }
                return;
            case 5:
                if (z->prefix && y != 4) { trap(z, 1); return; }
                if (!q) { push_word(z, read_rp2(z, p)); return; }   /* PUSH rp2 */
                switch (p) {
                case 0: w = fetch_word(z); do_call(z, w, z->IL, z->suffix); return;  /* CALL nn */
                case 1:                                             /* DD */
                    if (z->prefix) { trap(z, 1); return; }
                    z->prefix = 2;
                    continue;
                case 2:                                             /* ED */
                    exec_ed(z, start_pc);
                    return;
                default:                                            /* FD */
                    if (z->prefix) { trap(z, 1); return; }
                    z->prefix = 3;
                    continue;
                }
            case 6: alu(z, y, fetch(z)); return;                    /* ALU n */
            default:                                                /* RST */
                if (z->prefix) { trap(z, 1); return; }
                do_rst(z, (uint32_t)y << 3, z->L, z->L, z->suffix);
                return;
            }
        }
    }
}

/* ---- fast path ----------------------------------------------------------------- */

#ifdef EZ80_NO_FAST   /* reference build for tests/ez80/ez80_diff.c */
static void exec_adl(ez80_t *z) { exec(z); }
static void exec_z80(ez80_t *z) { exec(z); }
static void run_adl(ez80_t *z) { exec(z); }
static void run_z80(ez80_t *z) { exec(z); }
#else
#include "ez80_fast.inc"

#define M 1
#define RUN_FN run_adl_fast
#include "ez80_run.inc"
#undef RUN_FN
#undef M
#define M 0
#define RUN_FN run_z80_fast
#include "ez80_run.inc"
#undef RUN_FN
#undef M
#undef F_JUMP
#undef FM
#undef FADDR

static void exec_adl(ez80_t *z) { run_adl_fast(z, true); }
static void exec_z80(ez80_t *z) { run_z80_fast(z, true); }
/* Run instructions in one mode until something needs the outer loop: the
 * cycle budget is spent, an interrupt may be taken, EI/HALT executed, or the
 * generic decoder changed the mode. */
static void run_adl(ez80_t *z) { run_adl_fast(z, false); }
static void run_z80(ez80_t *z) { run_z80_fast(z, false); }
#endif

/* ---- public API -------------------------------------------------------------- */

void ez80_init(ez80_t *z) {
    if (!tables_ready) init_tables();
    for (unsigned i = 0; i < EZ80_NUM_PAGES; i++) z->cost[i] = z->wcost[i] = 1;
    z->xpage = EZ80_NO_XPAGE;
    ez80_reset(z);
}

void ez80_reset(ez80_t *z) {
    z->a = z->f = z->a_ = z->f_ = 0;
    z->bc.w = z->de.w = z->hl.w = z->ix.w = z->iy.w = 0;
    z->bc_.w = z->de_.w = z->hl_.w = 0;
    z->sps = z->spl = 0;
    z->pc = 0;
    z->i = 0; z->r = 0; z->mbase = 0;
    z->adl = z->madl = 0;
    z->iff1 = z->iff2 = 0; z->im = 0;
    z->halted = z->ei_delay = 0;
    z->nmi_pending = 0;
    z->L = z->IL = z->prefix = z->suffix = 0;
}

void ez80_end_timeslice(ez80_t *z) { z->cycle_target = z->cycles; }
void ez80_nmi(ez80_t *z) { z->nmi_pending = 1; }

static void take_interrupt(ez80_t *z) {
    z->cycles += z->halted ? 2 : 1;
    z->L = z->IL = z->adl || z->madl;
    z->prefix = z->suffix = 0;
    z->iff1 = 0;
    z->halted = 0;
    if (z->nmi_pending) {
        z->nmi_pending = 0;
        do_interrupt(z, 0x66);
    } else {
        z->iff2 = 0;
        /* IM 0/1/2 all end up at 38h on the TI-84 Plus CE */
        do_interrupt(z, 0x38);
    }
}

void ez80_step(ez80_t *z) {
    if (z->nmi_pending || (z->irq_line && z->iff1 && !z->ei_delay)) {
        take_interrupt(z);
        return;
    }
    z->ei_delay = 0;
    if (z->halted) { z->cycles++; return; }
    if (z->adl) exec_adl(z); else exec_z80(z);
}

void ez80_execute(ez80_t *z) {
    while (z->cycles < z->cycle_target) {
        if (z->nmi_pending || (z->irq_line && z->iff1 && !z->ei_delay)) take_interrupt(z);
        z->ei_delay = 0;
        if (z->halted) {
            z->cycles = z->cycle_target;
            break;
        }
        if (z->adl) run_adl(z); else run_z80(z);
    }
}

int32_t ez80_run(ez80_t *z, int32_t n) {
    z->cycles = 0;
    z->cycle_target = n;
    ez80_execute(z);
    z->total_cycles += (uint64_t)z->cycles;
    return z->cycles;
}
