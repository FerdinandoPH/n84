/*
 * N84 - eZ80 fast path (included from ez80.c)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Inline helpers for the fast path: memory access, the code page cache, and
 * the CB, ED and DD/FD forms it handles. The unprefixed dispatcher is in
 * ez80_run.inc. Every helper takes the CPU mode as a compile-time constant
 * (M = 1: ADL, M = 0: Z80), so address width, stack pointer and operand
 * size fold away. With no suffix, L = IL = ADL.
 *
 * The prefix helpers return false before modifying any state when the
 * instruction must go to the generic decoder in ez80.c.
 */

#define FM(v)        ((M) ? ((v) & 0xFFFFFFu) : ((v) & 0xFFFFu))
#define FADDR(a)     ((M) ? ((a) & 0xFFFFFFu) : (((uint32_t)z->mbase << 16) | ((a) & 0xFFFFu)))

/* PC and cycle counter live in this local context (registers) while the
 * fast path runs; they are written back to the ez80_t before any call that
 * can observe them (MMIO, ports, the generic decoder). R is kept here too
 * (only the generic decoder reads it).
 *
 * tgt is the cycle limit the run loop checks after each instruction. Setting
 * it to FC_BRK makes the loop stop and re-check interrupt state (EI, HALT,
 * or any external call, which may also move cycle_target) with a single
 * compare per instruction. */
typedef struct {
    uint32_t pc; int32_t cyc; int32_t tgt; uint32_t r;
    /* Code page cache: the page holding the last fetched/peeked opcode byte.
     * fbase + addr is the host address of any byte of page fpg, fcost its
     * read cost. Fetches from that page skip the rd[]/cost[] lookups.
     * Invalidated (fpg = FC_NOPAGE) by every call that may change the
     * memory map: MMIO, ports and the generic decoder. */
    uintptr_t fbase; uint32_t fpg; int32_t fcost;
} fc_t;
#define FC_NOPAGE 0xFFFFFFFFu
#define FC_BRK    INT32_MIN

static NOINLINE uint8_t f_mrd_slow(ez80_t *z, fc_t *c, uint32_t a) {
    z->pc = c->pc; z->cycles = c->cyc;
    uint8_t v = z->read_slow(z->ctx, a);
    c->cyc = z->cycles; c->tgt = FC_BRK; c->fpg = FC_NOPAGE;
    return v;
}
static NOINLINE void f_mwr_slow(ez80_t *z, fc_t *c, uint32_t a, uint8_t v) {
    z->pc = c->pc; z->cycles = c->cyc;
    z->write_slow(z->ctx, a, v);
    c->cyc = z->cycles; c->tgt = FC_BRK; c->fpg = FC_NOPAGE;
}
static NOINLINE uint8_t f_prd(ez80_t *z, fc_t *c, uint16_t port) {
    z->pc = c->pc; z->cycles = c->cyc;
    uint8_t v = z->port_in(z->ctx, port);
    c->cyc = z->cycles; c->tgt = FC_BRK; c->fpg = FC_NOPAGE;
    return v;
}
static NOINLINE void f_pwr(ez80_t *z, fc_t *c, uint16_t port, uint8_t v) {
    z->pc = c->pc; z->cycles = c->cyc;
    z->port_out(z->ctx, port, v);
    c->cyc = z->cycles; c->tgt = FC_BRK; c->fpg = FC_NOPAGE;
}
static ALWAYS_INLINE uint8_t f_mrd(ez80_t *z, fc_t *c, uint32_t a) {
    unsigned pg = a >> EZ80_PAGE_SHIFT;
    c->cyc += z->cost[pg];
    const uint8_t *p = z->rd[pg];
    if (likely(p != NULL)) return p[a & EZ80_PAGE_MASK];
    return f_mrd_slow(z, c, a);
}
static ALWAYS_INLINE void f_mwr(ez80_t *z, fc_t *c, uint32_t a, uint8_t v) {
    unsigned pg = a >> EZ80_PAGE_SHIFT;
    c->cyc += z->wcost[pg];
    uint8_t *p = z->wr[pg];
    if (likely(p != NULL)) p[a & EZ80_PAGE_MASK] = v;
    else f_mwr_slow(z, c, a, v);
}

/* Load page pg into the code cache; false if it has no direct pointer. */
static ALWAYS_INLINE bool f_crefill(ez80_t *z, fc_t *c, uint32_t pg) {
    const uint8_t *p = z->rd[pg];
    if (unlikely(p == NULL)) return false;
    c->fbase = (uintptr_t)p - ((uintptr_t)pg << EZ80_PAGE_SHIFT);
    c->fpg = pg;
    c->fcost = z->cost[pg];
    return true;
}
static NOINLINE uint8_t f_fetch_miss(ez80_t *z, fc_t *c, uint32_t pc) {
    if (f_crefill(z, c, pc >> EZ80_PAGE_SHIFT)) {
        c->cyc += c->fcost;
        return *(const uint8_t *)(c->fbase + pc);
    }
    return f_mrd(z, c, pc);
}
static ALWAYS_INLINE uint8_t f_fetch(ez80_t *z, fc_t *c, const unsigned M) {
    uint32_t pc = c->pc;
    uint8_t v;
    if (likely((pc >> EZ80_PAGE_SHIFT) == c->fpg)) {
        v = *(const uint8_t *)(c->fbase + pc);
        c->cyc += c->fcost;
    } else {
        v = f_fetch_miss(z, c, pc);
    }
    c->pc = FADDR(pc + 1);
    return v;
}
static ALWAYS_INLINE uint32_t f_fetch_word(ez80_t *z, fc_t *c, const unsigned M) {
    uint32_t pc = c->pc;
    /* whole operand inside the cached page: one check, one base */
    if (likely((pc >> EZ80_PAGE_SHIFT) == c->fpg &&
               (pc & EZ80_PAGE_MASK) <= EZ80_PAGE_SIZE - (M ? 3 : 2))) {
        const uint8_t *p = (const uint8_t *)(c->fbase + pc);
        uint32_t v = p[0] | (uint32_t)p[1] << 8;
        if (M) v |= (uint32_t)p[2] << 16;
        c->cyc += (M ? 3 : 2) * c->fcost;
        c->pc = FADDR(pc + (M ? 3 : 2));
        return v;
    }
    uint32_t v = f_fetch(z, c, M);
    v |= (uint32_t)f_fetch(z, c, M) << 8;
    if (M) v |= (uint32_t)f_fetch(z, c, M) << 16;
    return v;
}
static ALWAYS_INLINE uint8_t f_rd(ez80_t *z, fc_t *c, uint32_t a, const unsigned M) {
    return f_mrd(z, c, FADDR(a));
}
static ALWAYS_INLINE void f_wr(ez80_t *z, fc_t *c, uint32_t a, uint8_t v, const unsigned M) {
    f_mwr(z, c, FADDR(a), v);
}
static ALWAYS_INLINE uint32_t f_rdw(ez80_t *z, fc_t *c, uint32_t a, const unsigned M) {
    uint32_t v = f_rd(z, c, a, M);
    v |= (uint32_t)f_rd(z, c, a + 1, M) << 8;
    if (M) v |= (uint32_t)f_rd(z, c, a + 2, M) << 16;
    return v;
}
static ALWAYS_INLINE void f_wrw(ez80_t *z, fc_t *c, uint32_t a, uint32_t v, const unsigned M) {
    f_wr(z, c, a, (uint8_t)v, M);
    f_wr(z, c, a + 1, (uint8_t)(v >> 8), M);
    if (M) f_wr(z, c, a + 2, (uint8_t)(v >> 16), M);
}
static ALWAYS_INLINE void f_push(ez80_t *z, fc_t *c, uint32_t v, const unsigned M) {
    uint32_t sp = M ? z->spl : z->sps;
    if (M) { sp = FM(sp - 1); f_mwr(z, c, sp, (uint8_t)(v >> 16)); }
    sp = FM(sp - 1); f_mwr(z, c, FADDR(sp), (uint8_t)(v >> 8));
    sp = FM(sp - 1); f_mwr(z, c, FADDR(sp), (uint8_t)v);
    if (M) z->spl = sp; else z->sps = sp;
}
static ALWAYS_INLINE uint32_t f_pop(ez80_t *z, fc_t *c, const unsigned M) {
    uint32_t sp = M ? z->spl : z->sps;
    uint32_t v = f_mrd(z, c, FADDR(sp)); sp = FM(sp + 1);
    v |= (uint32_t)f_mrd(z, c, FADDR(sp)) << 8; sp = FM(sp + 1);
    if (M) { v |= (uint32_t)f_mrd(z, c, sp) << 16; sp = FM(sp + 1); }
    if (M) z->spl = sp; else z->sps = sp;
    return v;
}
/* jump within the current mode (no suffix: the mode does not change) */
#define F_JUMP(a)    (c->pc = FADDR(a))

static ALWAYS_INLINE bool f_cond(const ez80_t *z, unsigned cc) {
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

static ALWAYS_INLINE void f_inc(ez80_t *z, uint8_t *r) {
    uint8_t old = *r, nw = (uint8_t)(old + 1);
    *r = nw;
    z->f = (uint8_t)((z->f & CF) | sz(nw) | hc_add(old, 0, 1) | (nw == 0x80 ? PF : 0) | UNDEF(z->f));
}
static ALWAYS_INLINE void f_dec(ez80_t *z, uint8_t *r) {
    uint8_t old = *r, nw = (uint8_t)(old - 1);
    *r = nw;
    z->f = (uint8_t)((z->f & CF) | sz(nw) | hc_sub(old, 0, 1) | (old == 0x80 ? PF : 0) | NF | UNDEF(z->f));
}


/* Peek a code byte without side effects on the emulated machine (it may
 * load the code cache); -1 if the page needs the slow path. */
static NOINLINE int f_peek_miss(ez80_t *z, fc_t *c, uint32_t a) {
    if (!f_crefill(z, c, a >> EZ80_PAGE_SHIFT)) return -1;
    return *(const uint8_t *)(c->fbase + a);
}
static ALWAYS_INLINE int f_peek(ez80_t *z, fc_t *c, uint32_t a) {
    if (likely((a >> EZ80_PAGE_SHIFT) == c->fpg)) return *(const uint8_t *)(c->fbase + a);
    return f_peek_miss(z, c, a);
}

/* consume `n` already-peeked bytes starting at pc0 */
static ALWAYS_INLINE void f_skip(ez80_t *z, fc_t *c, uint32_t pc0, unsigned n, const unsigned M) {
    uint32_t last = FADDR(pc0 + n - 1);
    if (likely((pc0 >> EZ80_PAGE_SHIFT) == c->fpg && (last >> EZ80_PAGE_SHIFT) == c->fpg)) {
        c->cyc += (int32_t)n * c->fcost;
        c->pc = FADDR(last + 1);
        return;
    }
    uint32_t pc = pc0;
    for (unsigned i = 0; i < n; i++) {
        c->cyc += z->cost[pc >> EZ80_PAGE_SHIFT];
        pc = FADDR(pc + 1);
    }
    c->pc = pc;
}

static ALWAYS_INLINE uint8_t f_cb_op(ez80_t *z, uint8_t op, uint8_t v) {
    unsigned x = op >> 6, y = (op >> 3) & 7;
    switch (x) {
    case 0: return rot(z, y, v);
    case 1: z->f = (uint8_t)(szp(v & (uint8_t)(1u << y)) | UNDEF(z->f) | (z->f & CF) | HF); return v;
    case 2: return (uint8_t)(v & ~(1u << y));
    default: return (uint8_t)(v | (1u << y));
    }
}

/* CB xx (no index prefix) */
static ALWAYS_INLINE bool fast_cb(ez80_t *z, fc_t *c, uint32_t pc0, const unsigned M) {
    int op = f_peek(z, c, FADDR(pc0 + 1));
    if (op < 0 || (op & 0xF8) == 0x30) return false;          /* slow page / SLL traps */
    f_skip(z, c, pc0, 2, M);
    c->r += 2;
    unsigned r = op & 7;
    uint8_t *reg;
    switch (r) {
    case 0: reg = &z->bc.b.h; break;
    case 1: reg = &z->bc.b.l; break;
    case 2: reg = &z->de.b.h; break;
    case 3: reg = &z->de.b.l; break;
    case 4: reg = &z->hl.b.h; break;
    case 5: reg = &z->hl.b.l; break;
    case 7: reg = &z->a; break;
    default: {
        uint32_t a = FADDR(z->hl.w);
        uint8_t v = f_mrd(z, c, a);
        uint8_t nv = f_cb_op(z, (uint8_t)op, v);
        if ((op & 0xC0) != 0x40) { c->cyc++; f_mwr(z, c, a, nv); }
        return true;
    }
    }
    *reg = f_cb_op(z, (uint8_t)op, *reg);
    return true;
}

/* ED xx: the frequent ones; everything else goes to the generic decoder */
static ALWAYS_INLINE bool fast_ed(ez80_t *z, fc_t *c, uint32_t pc0, const unsigned M) {
    int op = f_peek(z, c, FADDR(pc0 + 1));
    uint32_t ow, opw, nwd;
    switch (op) {
    case 0xA0: case 0xA8: case 0xB0: case 0xB8: {               /* LDI LDD LDIR LDDR */
        f_skip(z, c, pc0, 2, M);
        c->r += 2;
        int delta = (op & 8) ? -1 : 1;
        f_mwr(z, c, FADDR(z->de.w), f_mrd(z, c, FADDR(z->hl.w)));
        z->de.w = FM(z->de.w + (uint32_t)delta);
        z->hl.w = FM(z->hl.w + (uint32_t)delta);
        uint32_t bc = FM(z->bc.w - 1);
        if (M) z->bc.w = bc; else z->bc.s = (uint16_t)bc;
        z->f = (uint8_t)((z->f & ~(HF | NF | PF)) | (bc ? PF : 0));
        c->cyc++;
        if ((op & 0x10) && bc) c->pc = pc0;                     /* repeat */
        return true;
    }
    case 0x4C: case 0x5C: case 0x6C:                            /* MLT BC/DE/HL */
        f_skip(z, c, pc0, 2, M);
        c->r += 2;
        c->cyc += 4;
        {
            ez80_pair *rp = op == 0x4C ? &z->bc : op == 0x5C ? &z->de : &z->hl;
            rp->w = (uint32_t)rp->b.l * rp->b.h;
        }
        return true;
    case 0x42: case 0x52: case 0x62: case 0x72:                 /* SBC HL,rr */
    case 0x4A: case 0x5A: case 0x6A: case 0x7A: {               /* ADC HL,rr */
        f_skip(z, c, pc0, 2, M);
        c->r += 2;
        unsigned c = z->f & CF;
        const uint32_t topbit = M ? 0x800000u : 0x8000u;
        switch ((op >> 4) & 3) {
        case 0: opw = z->bc.w; break;
        case 1: opw = z->de.w; break;
        case 2: opw = z->hl.w; break;
        default: opw = M ? z->spl : z->sps; break;
        }
        ow = FM(z->hl.w);
        opw = FM(opw);
        if (!(op & 8)) {
            nwd = ow - opw - c;
            z->hl.w = FM(nwd);
            z->f = (uint8_t)(((z->hl.w & topbit) ? SF : 0) | (z->hl.w ? 0 : ZF) | UNDEF(z->f) |
                             (((ow ^ opw) & (ow ^ z->hl.w) & topbit) ? PF : 0) | NF |
                             ((nwd & (topbit << 1)) ? CF : 0) |
                             ((((ow & 0xFFF) - (opw & 0xFFF) - c) & 0x1000) ? HF : 0));
        } else {
            nwd = ow + opw + c;
            z->hl.w = FM(nwd);
            z->f = (uint8_t)(((z->hl.w & topbit) ? SF : 0) | (z->hl.w ? 0 : ZF) | UNDEF(z->f) |
                             (((ow ^ z->hl.w) & (opw ^ z->hl.w) & topbit) ? PF : 0) |
                             ((nwd & (topbit << 1)) ? CF : 0) |
                             ((((ow & 0xFFF) + (opw & 0xFFF) + c) & 0x1000) ? HF : 0));
        }
        return true;
    }
    case 0x02: case 0x12: case 0x22: case 0x32:                 /* LEA rp3,IX+d */
    case 0x03: case 0x13: case 0x23: case 0x33: {               /* LEA rp3,IY+d */
        int d = f_peek(z, c, FADDR(pc0 + 2));
        if (d < 0) return false;
        f_skip(z, c, pc0, 3, M);
        c->r += 2;
        uint32_t v = FM(((op & 1) ? z->iy.w : z->ix.w) + (uint32_t)(int32_t)(int8_t)d);
        switch (op >> 4) {
        case 0: z->bc.w = v; break;
        case 1: z->de.w = v; break;
        case 2: z->hl.w = v; break;
        default: if (op & 1) z->iy.w = v; else z->ix.w = v; break;
        }
        return true;
    }
    case 0x07: case 0x17: case 0x27: case 0x37:                 /* LD rp3,(HL) (37: IX) */
    case 0x0F: case 0x1F: case 0x2F: case 0x3F: {               /* LD (HL),rp3 (3F: IX) */
        f_skip(z, c, pc0, 2, M);
        c->r += 2;
        ez80_pair *rp = (op >> 4) == 0 ? &z->bc : (op >> 4) == 1 ? &z->de : (op >> 4) == 2 ? &z->hl : &z->ix;
        if (op & 8) f_wrw(z, c, z->hl.w, FM(rp->w), M);
        else rp->w = FM(f_rdw(z, c, z->hl.w, M));
        return true;
    }
    default:
        return false;
    }
}

/* DD/FD xx: IX/IY forms; invalid combinations (traps) and rare ones go to the
 * generic decoder. `xy` is the selected index register, `oxy` the other one. */
static ALWAYS_INLINE bool fast_xy(ez80_t *z, fc_t *c, uint32_t pc0, ez80_pair *xy, ez80_pair *oxy, const unsigned M) {
    int op = f_peek(z, c, FADDR(pc0 + 1));
    int d;
    uint32_t a, ow, opw, nwd;
    if (op < 0) return false;

#define NEED_D() do { d = f_peek(z, c, FADDR(pc0 + 2)); if (d < 0) return false; } while (0)
#define XYADDR   FADDR(xy->w + (uint32_t)(int32_t)(int8_t)d)
#define TAKE(n)  do { f_skip(z, c, pc0, (n), M); c->r += 2; } while (0)

    switch (op) {
    case 0x21: TAKE(2); xy->w = FM(f_fetch_word(z, c, M)); return true;           /* LD IX,nn */
    case 0x22: TAKE(2); a = f_fetch_word(z, c, M); f_wrw(z, c, a, xy->w, M); return true;
    case 0x2A: TAKE(2); a = f_fetch_word(z, c, M); xy->w = f_rdw(z, c, a, M); return true;
    case 0x23: TAKE(2); xy->w = FM(xy->w + 1); return true;
    case 0x2B: TAKE(2); xy->w = FM(xy->w - 1); return true;
    case 0x09: case 0x19: case 0x29: case 0x39:                                 /* ADD IX,rr */
        TAKE(2);
        ow = FM(xy->w);
        switch (op >> 4) {
        case 0: opw = z->bc.w; break;
        case 1: opw = z->de.w; break;
        case 2: opw = xy->w; break;
        default: opw = M ? z->spl : z->sps; break;
        }
        opw = FM(opw);
        nwd = ow + opw;
        xy->w = FM(nwd);
        z->f = (uint8_t)((z->f & (SF | ZF | PF | XF | YF)) |
                         ((nwd & (M ? 0x1000000u : 0x10000u)) ? CF : 0) |
                         ((((ow & 0xFFF) + (opw & 0xFFF)) & 0x1000) ? HF : 0));
        return true;
    case 0x07: case 0x17: case 0x27: case 0x37: case 0x31:                     /* LD rp,(IX+d) */
        NEED_D(); TAKE(3);
        a = f_rdw(z, c, xy->w + (uint32_t)(int32_t)(int8_t)d, M);
        switch (op) {
        case 0x07: z->bc.w = FM(a); break;
        case 0x17: z->de.w = FM(a); break;
        case 0x27: z->hl.w = FM(a); break;
        case 0x37: xy->w = FM(a); break;
        default: oxy->w = a; break;          /* generic: other_idx = read_word (already masked) */
        }
        return true;
    case 0x0F: case 0x1F: case 0x2F: case 0x3F: case 0x3E:                     /* LD (IX+d),rp */
        NEED_D(); TAKE(3);
        switch (op) {
        case 0x0F: ow = z->bc.w; break;
        case 0x1F: ow = z->de.w; break;
        case 0x2F: ow = z->hl.w; break;
        case 0x3F: ow = xy->w; break;
        default: ow = oxy->w; break;
        }
        f_wrw(z, c, xy->w + (uint32_t)(int32_t)(int8_t)d, op == 0x3E ? ow : FM(ow), M);
        return true;
    case 0x34: case 0x35: {                                                     /* INC/DEC (IX+d) */
        NEED_D(); TAKE(3);
        a = XYADDR;
        uint8_t v = f_mrd(z, c, a);
        c->cyc++;
        if (op == 0x34) f_inc(z, &v); else f_dec(z, &v);
        f_mwr(z, c, a, v);
        return true;
    }
    case 0x24: TAKE(2); f_inc(z, &xy->b.h); return true;
    case 0x25: TAKE(2); f_dec(z, &xy->b.h); return true;
    case 0x2C: TAKE(2); f_inc(z, &xy->b.l); return true;
    case 0x2D: TAKE(2); f_dec(z, &xy->b.l); return true;
    case 0x26: TAKE(2); xy->b.h = f_fetch(z, c, M); return true;
    case 0x2E: TAKE(2); xy->b.l = f_fetch(z, c, M); return true;
    case 0x36: NEED_D(); TAKE(3); a = XYADDR; f_mwr(z, c, a, f_fetch(z, c, M)); return true;  /* LD (IX+d),n */
    /* LD r,(IX+d) */
    case 0x46: NEED_D(); TAKE(3); z->bc.b.h = f_mrd(z, c, XYADDR); return true;
    case 0x4E: NEED_D(); TAKE(3); z->bc.b.l = f_mrd(z, c, XYADDR); return true;
    case 0x56: NEED_D(); TAKE(3); z->de.b.h = f_mrd(z, c, XYADDR); return true;
    case 0x5E: NEED_D(); TAKE(3); z->de.b.l = f_mrd(z, c, XYADDR); return true;
    case 0x66: NEED_D(); TAKE(3); z->hl.b.h = f_mrd(z, c, XYADDR); return true;
    case 0x6E: NEED_D(); TAKE(3); z->hl.b.l = f_mrd(z, c, XYADDR); return true;
    case 0x7E: NEED_D(); TAKE(3); z->a = f_mrd(z, c, XYADDR); return true;
    /* LD (IX+d),r */
    case 0x70: NEED_D(); TAKE(3); f_mwr(z, c, XYADDR, z->bc.b.h); return true;
    case 0x71: NEED_D(); TAKE(3); f_mwr(z, c, XYADDR, z->bc.b.l); return true;
    case 0x72: NEED_D(); TAKE(3); f_mwr(z, c, XYADDR, z->de.b.h); return true;
    case 0x73: NEED_D(); TAKE(3); f_mwr(z, c, XYADDR, z->de.b.l); return true;
    case 0x74: NEED_D(); TAKE(3); f_mwr(z, c, XYADDR, z->hl.b.h); return true;
    case 0x75: NEED_D(); TAKE(3); f_mwr(z, c, XYADDR, z->hl.b.l); return true;
    case 0x77: NEED_D(); TAKE(3); f_mwr(z, c, XYADDR, z->a); return true;
    /* ALU A,(IX+d) / IXH / IXL */
    case 0x86: case 0x8E: case 0x96: case 0x9E: case 0xA6: case 0xAE: case 0xB6: case 0xBE:
        NEED_D(); TAKE(3); alu(z, ((unsigned)op >> 3) & 7, f_mrd(z, c, XYADDR)); return true;
    case 0x84: case 0x8C: case 0x94: case 0x9C: case 0xA4: case 0xAC: case 0xB4: case 0xBC:
        TAKE(2); alu(z, ((unsigned)op >> 3) & 7, xy->b.h); return true;
    case 0x85: case 0x8D: case 0x95: case 0x9D: case 0xA5: case 0xAD: case 0xB5: case 0xBD:
        TAKE(2); alu(z, ((unsigned)op >> 3) & 7, xy->b.l); return true;
    case 0xE1: TAKE(2); xy->w = FM(f_pop(z, c, M)); return true;                  /* POP IX */
    case 0xE5: TAKE(2); f_push(z, c, FM(xy->w), M); return true;                  /* PUSH IX */
    case 0xE9: TAKE(2); F_JUMP(xy->w); return true;                            /* JP (IX) */
    case 0xF9: TAKE(2); if (M) z->spl = xy->w & 0xFFFFFF; else z->sps = xy->w & 0xFFFF; return true;
    case 0xE3: {                                                                /* EX (SP),IX */
        TAKE(2);
        uint32_t sp = M ? z->spl : z->sps;
        ow = FM(f_rdw(z, c, sp, M));
        nwd = xy->w;
        if (M) xy->w = ow; else xy->s = (uint16_t)ow;
        f_wrw(z, c, sp, nwd, M);
        return true;
    }
    case 0xCB: {                                                                /* DD CB d op */
        NEED_D();
        int cop = f_peek(z, c, FADDR(pc0 + 3));
        if (cop < 0 || (cop & 7) != 6 || (cop & 0xF8) == 0x30) return false;
        TAKE(4);
        a = XYADDR;
        uint8_t v = f_mrd(z, c, a);
        uint8_t nv = f_cb_op(z, (uint8_t)cop, v);
        if ((cop & 0xC0) != 0x40) { c->cyc++; f_mwr(z, c, a, nv); }
        return true;
    }
    default:
        return false;
    }
#undef NEED_D
#undef XYADDR
#undef TAKE
}
