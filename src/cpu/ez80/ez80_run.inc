/*
 * N84 - eZ80 fast-path dispatcher (included from ez80.c once per CPU mode)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Before including: M (1 = ADL, 0 = Z80) and RUN_FN (function name).
 *
 * Unprefixed opcodes jump through a 256-entry table of label addresses
 * (goto *ops[op]), so dispatch is one load into PC with no range check; each
 * handler ends with NEXT, a single compare against c->tgt before the next
 * dispatch. Suffixes, DD/FD/ED/CB forms the fast helpers do not cover, and
 * code on slow pages go to the generic decoder in ez80.c. Semantics must
 * match exec() exactly; tests/ez80 compare both.
 *
 * GCC never inlines a function with a computed goto, which is why this is a
 * file included per mode rather than an inline function taking M.
 *
 * With `single` set the function returns after one instruction (ez80_step).
 */

static NOINLINE void RUN_FN(ez80_t *z, bool single) {
    static const void *const ops[256] = {
        &&op_00, &&op_01, &&op_02, &&op_03, &&op_04, &&op_05, &&op_06, &&op_07,
        &&op_08, &&op_09, &&op_0A, &&op_0B, &&op_0C, &&op_0D, &&op_0E, &&op_0F,
        &&op_10, &&op_11, &&op_12, &&op_13, &&op_14, &&op_15, &&op_16, &&op_17,
        &&op_18, &&op_19, &&op_1A, &&op_1B, &&op_1C, &&op_1D, &&op_1E, &&op_1F,
        &&op_20, &&op_21, &&op_22, &&op_23, &&op_24, &&op_25, &&op_26, &&op_27,
        &&op_28, &&op_29, &&op_2A, &&op_2B, &&op_2C, &&op_2D, &&op_2E, &&op_2F,
        &&op_30, &&op_31, &&op_32, &&op_33, &&op_34, &&op_35, &&op_36, &&op_37,
        &&op_38, &&op_39, &&op_3A, &&op_3B, &&op_3C, &&op_3D, &&op_3E, &&op_3F,
        &&op_40, &&op_41, &&op_42, &&op_43, &&op_44, &&op_45, &&op_46, &&op_47,
        &&op_48, &&op_49, &&op_4A, &&op_4B, &&op_4C, &&op_4D, &&op_4E, &&op_4F,
        &&op_50, &&op_51, &&op_52, &&op_53, &&op_54, &&op_55, &&op_56, &&op_57,
        &&op_58, &&op_59, &&op_5A, &&op_5B, &&op_5C, &&op_5D, &&op_5E, &&op_5F,
        &&op_60, &&op_61, &&op_62, &&op_63, &&op_64, &&op_65, &&op_66, &&op_67,
        &&op_68, &&op_69, &&op_6A, &&op_6B, &&op_6C, &&op_6D, &&op_6E, &&op_6F,
        &&op_70, &&op_71, &&op_72, &&op_73, &&op_74, &&op_75, &&op_76, &&op_77,
        &&op_78, &&op_79, &&op_7A, &&op_7B, &&op_7C, &&op_7D, &&op_7E, &&op_7F,
        &&op_80, &&op_81, &&op_82, &&op_83, &&op_84, &&op_85, &&op_86, &&op_87,
        &&op_88, &&op_89, &&op_8A, &&op_8B, &&op_8C, &&op_8D, &&op_8E, &&op_8F,
        &&op_90, &&op_91, &&op_92, &&op_93, &&op_94, &&op_95, &&op_96, &&op_97,
        &&op_98, &&op_99, &&op_9A, &&op_9B, &&op_9C, &&op_9D, &&op_9E, &&op_9F,
        &&op_A0, &&op_A1, &&op_A2, &&op_A3, &&op_A4, &&op_A5, &&op_A6, &&op_A7,
        &&op_A8, &&op_A9, &&op_AA, &&op_AB, &&op_AC, &&op_AD, &&op_AE, &&op_AF,
        &&op_B0, &&op_B1, &&op_B2, &&op_B3, &&op_B4, &&op_B5, &&op_B6, &&op_B7,
        &&op_B8, &&op_B9, &&op_BA, &&op_BB, &&op_BC, &&op_BD, &&op_BE, &&op_BF,
        &&op_C0, &&op_C1, &&op_C2, &&op_C3, &&op_C4, &&op_C5, &&op_C6, &&op_C7,
        &&op_C8, &&op_C9, &&op_CA, &&op_CB, &&op_CC, &&op_CD, &&op_CE, &&op_CF,
        &&op_D0, &&op_D1, &&op_D2, &&op_D3, &&op_D4, &&op_D5, &&op_D6, &&op_D7,
        &&op_D8, &&op_D9, &&op_DA, &&op_DB, &&op_DC, &&op_DD, &&op_DE, &&op_DF,
        &&op_E0, &&op_E1, &&op_E2, &&op_E3, &&op_E4, &&op_E5, &&op_E6, &&op_E7,
        &&op_E8, &&op_E9, &&op_EA, &&op_EB, &&op_EC, &&op_ED, &&op_EE, &&op_EF,
        &&op_F0, &&op_F1, &&op_F2, &&op_F3, &&op_F4, &&op_F5, &&op_F6, &&op_F7,
        &&op_F8, &&op_F9, &&op_FA, &&op_FB, &&op_FC, &&op_FD, &&op_FE, &&op_FF,
    };
    /* tgt starts as FC_BRK: the first instruction may be the one right after
     * EI, and the interrupt check must happen once it has executed */
    fc_t cc = { z->pc, z->cycles, FC_BRK, z->r, 0, FC_NOPAGE, 0 };
    fc_t *const c = &cc;
    uint32_t pc0, w, ow, opw, nwd;
    int op;
    int8_t s;

#define R_B  z->bc.b.h
#define R_C  z->bc.b.l
#define R_D  z->de.b.h
#define R_E  z->de.b.l
#define R_H  z->hl.b.h
#define R_L  z->hl.b.l
#define R_A  z->a
#define HLADDR FADDR(z->hl.w)
/* Replicated dispatch: every handler ends with its own budget check, fetch
 * and table jump, so the common path takes no extra branches. */
#define NEXT do {                                                           \
        if (unlikely(c->cyc >= c->tgt)) goto slow_next;                     \
        pc0 = c->pc;                                                        \
        op = f_peek(z, c, pc0);                                             \
        if (unlikely(op < 0)) goto generic;                                 \
        c->cyc += c->fcost;                                                 \
        c->pc = FADDR(pc0 + 1);                                             \
        c->r++;                                                             \
        goto *ops[op];                                                      \
    } while (0)
#define UNDO_OPCODE() do { c->cyc -= c->fcost; c->pc = pc0; c->r--; } while (0)

dispatch:
    pc0 = c->pc;
    op = f_peek(z, c, pc0);
    if (unlikely(op < 0)) goto generic;         /* code on a slow page */
    /* consume the opcode (prefix/suffix cases undo this) */
    c->cyc += c->fcost;                          /* f_peek cached pc0's page */
    c->pc = FADDR(pc0 + 1);
    c->r++;
    goto *ops[op];

    op_40: UNDO_OPCODE(); goto generic;       /* suffixes: generic decoder */
    op_49: UNDO_OPCODE(); goto generic;
    op_52: UNDO_OPCODE(); goto generic;
    op_5B: UNDO_OPCODE(); goto generic;
    op_CB: UNDO_OPCODE(); if (fast_cb(z, c, pc0, M)) NEXT; goto generic;
    op_ED: UNDO_OPCODE(); if (fast_ed(z, c, pc0, M)) NEXT; goto generic;
    op_DD: UNDO_OPCODE(); if (fast_xy(z, c, pc0, &z->ix, &z->iy, M)) NEXT; goto generic;
    op_FD: UNDO_OPCODE(); if (fast_xy(z, c, pc0, &z->iy, &z->ix, M)) NEXT; goto generic;
    op_00: NEXT;                                                         /* NOP */
    op_08: { uint8_t t = z->a; z->a = z->a_; z->a_ = t; t = z->f; z->f = z->f_; z->f_ = t; NEXT; }
    op_10:
        s = (int8_t)f_fetch(z, c, M);                                  /* DJNZ */
        if (--z->bc.b.h) { c->cyc++; F_JUMP(c->pc + (uint32_t)(int32_t)s); }
        NEXT;
    op_18: s = (int8_t)f_fetch(z, c, M); F_JUMP(c->pc + (uint32_t)(int32_t)s); NEXT;
    op_20: op_28: op_30: op_38:
        s = (int8_t)f_fetch(z, c, M);                                  /* JR cc */
        if (f_cond(z, (op >> 3) - 4)) { c->cyc++; F_JUMP(c->pc + (uint32_t)(int32_t)s); }
        NEXT;

    op_01: z->bc.w = FM(f_fetch_word(z, c, M)); NEXT;                    /* LD rr,Mmn */
    op_11: z->de.w = FM(f_fetch_word(z, c, M)); NEXT;
    op_21: z->hl.w = FM(f_fetch_word(z, c, M)); NEXT;
    op_31: w = FM(f_fetch_word(z, c, M)); if (M) z->spl = w; else z->sps = w; NEXT;

    op_09: op_19: op_29: op_39:
        ow = FM(z->hl.w);                                              /* ADD HL,rr */
        switch (op >> 4) {
        case 0: opw = z->bc.w; break;
        case 1: opw = z->de.w; break;
        case 2: opw = z->hl.w; break;
        default: opw = M ? z->spl : z->sps; break;
        }
        opw = FM(opw);
        nwd = ow + opw;
        z->hl.w = FM(nwd);
        z->f = (uint8_t)((z->f & (SF | ZF | PF | XF | YF)) |
                         ((nwd & (M ? 0x1000000u : 0x10000u)) ? CF : 0) |
                         ((((ow & 0xFFF) + (opw & 0xFFF)) & 0x1000) ? HF : 0));
        NEXT;

    op_02: f_wr(z, c, z->bc.w, z->a, M); NEXT;                           /* LD (BC),A */
    op_12: f_wr(z, c, z->de.w, z->a, M); NEXT;
    op_22: w = f_fetch_word(z, c, M); f_wrw(z, c, w, z->hl.w, M); NEXT;     /* LD (Mmn),HL */
    op_32: w = f_fetch_word(z, c, M); f_wr(z, c, w, z->a, M); NEXT;
    op_0A: z->a = f_rd(z, c, z->bc.w, M); NEXT;
    op_1A: z->a = f_rd(z, c, z->de.w, M); NEXT;
    op_2A: w = f_fetch_word(z, c, M); z->hl.w = f_rdw(z, c, w, M); NEXT;
    op_3A: w = f_fetch_word(z, c, M); z->a = f_rd(z, c, w, M); NEXT;

    op_03: z->bc.w = FM(z->bc.w + 1); NEXT;                              /* INC/DEC rr */
    op_13: z->de.w = FM(z->de.w + 1); NEXT;
    op_23: z->hl.w = FM(z->hl.w + 1); NEXT;
    op_33: if (M) z->spl = FM(z->spl + 1); else z->sps = FM(z->sps + 1); NEXT;
    op_0B: z->bc.w = FM(z->bc.w - 1); NEXT;
    op_1B: z->de.w = FM(z->de.w - 1); NEXT;
    op_2B: z->hl.w = FM(z->hl.w - 1); NEXT;
    op_3B: if (M) z->spl = FM(z->spl - 1); else z->sps = FM(z->sps - 1); NEXT;

    op_04: f_inc(z, &R_B); NEXT;                                       /* INC/DEC r */
    op_05: f_dec(z, &R_B); NEXT;
    op_0C: f_inc(z, &R_C); NEXT;
    op_0D: f_dec(z, &R_C); NEXT;
    op_14: f_inc(z, &R_D); NEXT;
    op_15: f_dec(z, &R_D); NEXT;
    op_1C: f_inc(z, &R_E); NEXT;
    op_1D: f_dec(z, &R_E); NEXT;
    op_24: f_inc(z, &R_H); NEXT;
    op_25: f_dec(z, &R_H); NEXT;
    op_2C: f_inc(z, &R_L); NEXT;
    op_2D: f_dec(z, &R_L); NEXT;
    op_3C: f_inc(z, &R_A); NEXT;
    op_3D: f_dec(z, &R_A); NEXT;
    op_34: op_35:
        {                                                              /* INC/DEC (HL) */
            uint8_t v;
            w = HLADDR;
            v = f_mrd(z, c, w);
            c->cyc++;
            if (op == 0x34) f_inc(z, &v); else f_dec(z, &v);
            f_mwr(z, c, w, v);
            NEXT;
        }

    op_06: R_B = f_fetch(z, c, M); NEXT;                                 /* LD r,n */
    op_0E: R_C = f_fetch(z, c, M); NEXT;
    op_16: R_D = f_fetch(z, c, M); NEXT;
    op_1E: R_E = f_fetch(z, c, M); NEXT;
    op_26: R_H = f_fetch(z, c, M); NEXT;
    op_2E: R_L = f_fetch(z, c, M); NEXT;
    op_36: w = HLADDR; f_mwr(z, c, w, f_fetch(z, c, M)); NEXT;
    op_3E: R_A = f_fetch(z, c, M); NEXT;

    op_07: op_0F: op_17: op_1F: op_27: op_2F: op_37: op_3F:
        rot_acc(z, op >> 3);
        NEXT;

    /* LD r,r' (40, 49, 52, 5B are suffixes, handled above; 76 is HALT) */
    op_41: R_B = R_C; NEXT;
    op_42: R_B = R_D; NEXT;
    op_43: R_B = R_E; NEXT;
    op_44: R_B = R_H; NEXT;
    op_45: R_B = R_L; NEXT;
    op_46: R_B = f_mrd(z, c, HLADDR); NEXT;
    op_47: R_B = R_A; NEXT;
    op_48: R_C = R_B; NEXT;
    op_4A: R_C = R_D; NEXT;
    op_4B: R_C = R_E; NEXT;
    op_4C: R_C = R_H; NEXT;
    op_4D: R_C = R_L; NEXT;
    op_4E: R_C = f_mrd(z, c, HLADDR); NEXT;
    op_4F: R_C = R_A; NEXT;
    op_50: R_D = R_B; NEXT;
    op_51: R_D = R_C; NEXT;
    op_53: R_D = R_E; NEXT;
    op_54: R_D = R_H; NEXT;
    op_55: R_D = R_L; NEXT;
    op_56: R_D = f_mrd(z, c, HLADDR); NEXT;
    op_57: R_D = R_A; NEXT;
    op_58: R_E = R_B; NEXT;
    op_59: R_E = R_C; NEXT;
    op_5A: R_E = R_D; NEXT;
    op_5C: R_E = R_H; NEXT;
    op_5D: R_E = R_L; NEXT;
    op_5E: R_E = f_mrd(z, c, HLADDR); NEXT;
    op_5F: R_E = R_A; NEXT;
    op_60: R_H = R_B; NEXT;
    op_61: R_H = R_C; NEXT;
    op_62: R_H = R_D; NEXT;
    op_63: R_H = R_E; NEXT;
    op_64: R_H = R_H; NEXT;
    op_65: R_H = R_L; NEXT;
    op_66: R_H = f_mrd(z, c, HLADDR); NEXT;
    op_67: R_H = R_A; NEXT;
    op_68: R_L = R_B; NEXT;
    op_69: R_L = R_C; NEXT;
    op_6A: R_L = R_D; NEXT;
    op_6B: R_L = R_E; NEXT;
    op_6C: R_L = R_H; NEXT;
    op_6D: R_L = R_L; NEXT;
    op_6E: R_L = f_mrd(z, c, HLADDR); NEXT;
    op_6F: R_L = R_A; NEXT;
    op_78: R_A = R_B; NEXT;
    op_79: R_A = R_C; NEXT;
    op_7A: R_A = R_D; NEXT;
    op_7B: R_A = R_E; NEXT;
    op_7C: R_A = R_H; NEXT;
    op_7D: R_A = R_L; NEXT;
    op_7E: R_A = f_mrd(z, c, HLADDR); NEXT;
    op_7F: R_A = R_A; NEXT;
    op_70: f_mwr(z, c, HLADDR, R_B); NEXT;
    op_71: f_mwr(z, c, HLADDR, R_C); NEXT;
    op_72: f_mwr(z, c, HLADDR, R_D); NEXT;
    op_73: f_mwr(z, c, HLADDR, R_E); NEXT;
    op_74: f_mwr(z, c, HLADDR, R_H); NEXT;
    op_75: f_mwr(z, c, HLADDR, R_L); NEXT;
    op_76: c->cyc++; z->halted = 1; c->tgt = FC_BRK; NEXT;                /* HALT */
    op_77: f_mwr(z, c, HLADDR, R_A); NEXT;

    /* ALU A,r */
    op_80: alu(z, 0, R_B); NEXT;
    op_81: alu(z, 0, R_C); NEXT;
    op_82: alu(z, 0, R_D); NEXT;
    op_83: alu(z, 0, R_E); NEXT;
    op_84: alu(z, 0, R_H); NEXT;
    op_85: alu(z, 0, R_L); NEXT;
    op_86: alu(z, 0, f_mrd(z, c, HLADDR)); NEXT;
    op_87: alu(z, 0, R_A); NEXT;
    op_88: alu(z, 1, R_B); NEXT;
    op_89: alu(z, 1, R_C); NEXT;
    op_8A: alu(z, 1, R_D); NEXT;
    op_8B: alu(z, 1, R_E); NEXT;
    op_8C: alu(z, 1, R_H); NEXT;
    op_8D: alu(z, 1, R_L); NEXT;
    op_8E: alu(z, 1, f_mrd(z, c, HLADDR)); NEXT;
    op_8F: alu(z, 1, R_A); NEXT;
    op_90: alu(z, 2, R_B); NEXT;
    op_91: alu(z, 2, R_C); NEXT;
    op_92: alu(z, 2, R_D); NEXT;
    op_93: alu(z, 2, R_E); NEXT;
    op_94: alu(z, 2, R_H); NEXT;
    op_95: alu(z, 2, R_L); NEXT;
    op_96: alu(z, 2, f_mrd(z, c, HLADDR)); NEXT;
    op_97: alu(z, 2, R_A); NEXT;
    op_98: alu(z, 3, R_B); NEXT;
    op_99: alu(z, 3, R_C); NEXT;
    op_9A: alu(z, 3, R_D); NEXT;
    op_9B: alu(z, 3, R_E); NEXT;
    op_9C: alu(z, 3, R_H); NEXT;
    op_9D: alu(z, 3, R_L); NEXT;
    op_9E: alu(z, 3, f_mrd(z, c, HLADDR)); NEXT;
    op_9F: alu(z, 3, R_A); NEXT;
    op_A0: alu(z, 4, R_B); NEXT;
    op_A1: alu(z, 4, R_C); NEXT;
    op_A2: alu(z, 4, R_D); NEXT;
    op_A3: alu(z, 4, R_E); NEXT;
    op_A4: alu(z, 4, R_H); NEXT;
    op_A5: alu(z, 4, R_L); NEXT;
    op_A6: alu(z, 4, f_mrd(z, c, HLADDR)); NEXT;
    op_A7: alu(z, 4, R_A); NEXT;
    op_A8: alu(z, 5, R_B); NEXT;
    op_A9: alu(z, 5, R_C); NEXT;
    op_AA: alu(z, 5, R_D); NEXT;
    op_AB: alu(z, 5, R_E); NEXT;
    op_AC: alu(z, 5, R_H); NEXT;
    op_AD: alu(z, 5, R_L); NEXT;
    op_AE: alu(z, 5, f_mrd(z, c, HLADDR)); NEXT;
    op_AF: alu(z, 5, R_A); NEXT;
    op_B0: alu(z, 6, R_B); NEXT;
    op_B1: alu(z, 6, R_C); NEXT;
    op_B2: alu(z, 6, R_D); NEXT;
    op_B3: alu(z, 6, R_E); NEXT;
    op_B4: alu(z, 6, R_H); NEXT;
    op_B5: alu(z, 6, R_L); NEXT;
    op_B6: alu(z, 6, f_mrd(z, c, HLADDR)); NEXT;
    op_B7: alu(z, 6, R_A); NEXT;
    op_B8: alu(z, 7, R_B); NEXT;
    op_B9: alu(z, 7, R_C); NEXT;
    op_BA: alu(z, 7, R_D); NEXT;
    op_BB: alu(z, 7, R_E); NEXT;
    op_BC: alu(z, 7, R_H); NEXT;
    op_BD: alu(z, 7, R_L); NEXT;
    op_BE: alu(z, 7, f_mrd(z, c, HLADDR)); NEXT;
    op_BF: alu(z, 7, R_A); NEXT;

    op_C0: op_C8: op_D0: op_D8: op_E0: op_E8: op_F0: op_F8:
        c->cyc++;                                                      /* RET cc */
        if (f_cond(z, (op >> 3) & 7)) { c->cyc++; w = f_pop(z, c, M); F_JUMP(w); }
        NEXT;
    op_C9: c->cyc++; w = f_pop(z, c, M); F_JUMP(w); NEXT;                /* RET */

    op_C1: z->bc.w = FM(f_pop(z, c, M)); NEXT;                           /* POP */
    op_D1: z->de.w = FM(f_pop(z, c, M)); NEXT;
    op_E1: z->hl.w = FM(f_pop(z, c, M)); NEXT;
    op_F1: w = f_pop(z, c, M); z->a = (uint8_t)(w >> 8); z->f = (uint8_t)w; NEXT;
    op_C5: f_push(z, c, FM(z->bc.w), M); NEXT;                           /* PUSH */
    op_D5: f_push(z, c, FM(z->de.w), M); NEXT;
    op_E5: f_push(z, c, FM(z->hl.w), M); NEXT;
    op_F5: f_push(z, c, (uint32_t)((z->a << 8) | z->f), M); NEXT;

    op_D9:
        {                                                              /* EXX */
            ez80_pair t;
            t = z->bc; z->bc = z->bc_; z->bc_ = t;
            t = z->de; z->de = z->de_; z->de_ = t;
            t = z->hl; z->hl = z->hl_; z->hl_ = t;
            NEXT;
        }
    op_E9: F_JUMP(z->hl.w); NEXT;                                        /* JP (HL) */
    op_F9: if (M) z->spl = z->hl.w & 0xFFFFFF; else z->sps = z->hl.w & 0xFFFF; NEXT;

    op_C2: op_CA: op_D2: op_DA: op_E2: op_EA: op_F2: op_FA:
        w = f_fetch_word(z, c, M);                                     /* JP cc */
        if (f_cond(z, (op >> 3) & 7)) { c->cyc++; F_JUMP(w); }
        NEXT;
    op_C3: c->cyc++; w = f_fetch_word(z, c, M); F_JUMP(w); NEXT;

    op_D3: { uint8_t n = f_fetch(z, c, M); f_pwr(z, c, (uint16_t)((z->a << 8) | n), z->a); NEXT; }
    op_DB: { uint8_t n = f_fetch(z, c, M); z->a = f_prd(z, c, (uint16_t)((z->a << 8) | n)); NEXT; }
    op_E3:
        {                                                              /* EX (SP),HL */
            uint32_t sp = M ? z->spl : z->sps;
            ow = FM(f_rdw(z, c, sp, M));
            nwd = z->hl.w;
            if (M) z->hl.w = ow; else z->hl.s = (uint16_t)ow;
            f_wrw(z, c, sp, nwd, M);
            NEXT;
        }
    op_EB: { uint32_t t = FM(z->de.w); z->de.w = FM(z->hl.w); z->hl.w = t; NEXT; }
    op_F3: z->iff1 = z->iff2 = 0; z->ei_delay = 0; NEXT;                 /* DI */
    op_FB: z->iff1 = z->iff2 = 1; z->ei_delay = 1; c->tgt = FC_BRK; NEXT; /* EI */

    op_C4: op_CC: op_D4: op_DC: op_E4: op_EC: op_F4: op_FC:
        w = f_fetch_word(z, c, M);                                     /* CALL cc */
        if (f_cond(z, (op >> 3) & 7)) {
            c->cyc += !M;
            f_push(z, c, c->pc, M);
            F_JUMP(w);
        }
        NEXT;
    op_CD: w = f_fetch_word(z, c, M); f_push(z, c, c->pc, M); F_JUMP(w); NEXT;

    op_C6: op_CE: op_D6: op_DE: op_E6: op_EE: op_F6: op_FE:
        alu(z, (op >> 3) & 7, f_fetch(z, c, M));                       /* ALU n */
        NEXT;
    op_C7: op_CF: op_D7: op_DF: op_E7: op_EF: op_F7: op_FF:
        c->cyc++;                                                      /* RST */
        f_push(z, c, c->pc, M);
        F_JUMP((uint32_t)op & 0x38);
        NEXT;

slow_next:
    /* budget spent, or a break request */
    if (single || c->cyc >= z->cycle_target) goto out;
    c->tgt = z->cycle_target;
    if (z->nmi_pending | (z->irq_line & z->iff1) | z->ei_delay | z->halted) goto out;
    goto dispatch;

generic:
    z->pc = c->pc; z->cycles = c->cyc; z->r = (uint8_t)c->r;
    exec(z);
    c->pc = z->pc; c->cyc = z->cycles; c->r = z->r;
    c->fpg = FC_NOPAGE;
    if (z->adl != M) goto out;
    c->tgt = FC_BRK;
    NEXT;

out:
    z->pc = c->pc; z->cycles = c->cyc; z->r = (uint8_t)c->r;

#undef R_B
#undef R_C
#undef R_D
#undef R_E
#undef R_H
#undef R_L
#undef R_A
#undef HLADDR
#undef NEXT
#undef UNDO_OPCODE
}
