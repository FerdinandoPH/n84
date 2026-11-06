/*
 * N84 - eZ80-specific behaviour tests (ADL mode, suffixes, MBASE, mixed calls)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Expected values follow the eZ80 CPU user manual (UM0077) semantics.
 */
#include <stdio.h>
#include <string.h>
#include "cpu/ez80/ez80.h"

static uint8_t ram[0x20000];          /* mapped at D00000-D1FFFF */
static ez80_t z;
static int failures;

#define CHECK(cond, ...) do { if (cond) printf("  ok   "); else { printf("  FAIL "); failures++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

static uint8_t rslow(void *c, uint32_t a) { (void)c; (void)a; return 0xFF; }
static void wslow(void *c, uint32_t a, uint8_t v) { (void)c; (void)a; (void)v; }
static uint8_t pin(void *c, uint16_t p) { (void)c; (void)p; return 0; }
static void pout(void *c, uint16_t p, uint8_t v) { (void)c; (void)p; (void)v; }

static void setup(const uint8_t *code, size_t n, bool adl) {
    memset(ram, 0, sizeof ram);
    memcpy(ram, code, n);
    ez80_init(&z);
    for (unsigned i = 0; i < EZ80_NUM_PAGES; i++) { z.rd[i] = z.wr[i] = NULL; z.cost[i] = 1; }
    for (unsigned i = 0; i < sizeof ram / EZ80_PAGE_SIZE; i++)
        z.rd[0xD00 + i] = z.wr[0xD00 + i] = ram + i * EZ80_PAGE_SIZE;
    z.ctx = &z; z.read_slow = rslow; z.write_slow = wslow; z.port_in = pin; z.port_out = pout;
    z.adl = adl;
    z.mbase = 0xD0;
    z.pc = 0xD00000;
    z.spl = 0xD10000;
    z.sps = 0x8000;            /* D08000 in Z80 mode */
}
static void steps(int n) { while (n--) ez80_step(&z); }
static uint8_t *at(uint32_t a) { return ram + (a - 0xD00000); }

int main(void) {
    printf("ADL basics\n");
    {   static const uint8_t c[] = { 0x21, 0x56, 0x34, 0x12 };            /* ld hl,123456h */
        setup(c, sizeof c, true); steps(1);
        CHECK(z.hl.w == 0x123456 && z.pc == 0xD00004, "LD HL,Mmn (24-bit) hl=%06X pc=%06X", z.hl.w, z.pc); }
    {   static const uint8_t c[] = { 0x01, 0xEF, 0xCD, 0xAB, 0xC5, 0xD1 }; /* ld bc / push bc / pop de */
        setup(c, sizeof c, true); steps(2);
        CHECK(z.spl == 0xD0FFFD && *at(0xD0FFFD) == 0xEF && *at(0xD0FFFF) == 0xAB, "PUSH pushes 3 bytes (spl=%06X)", z.spl);
        steps(1);
        CHECK(z.de.w == 0xABCDEF && z.spl == 0xD10000, "POP DE = %06X", z.de.w); }
    {   static const uint8_t c[] = { 0x40, 0x21, 0x34, 0x12 };            /* ld.sis hl,1234h */
        setup(c, sizeof c, true); z.hl.w = 0xFFFFFF; steps(1);
        CHECK(z.hl.w == 0x001234 && z.pc == 0xD00004, ".SIS LD HL,mn clears HLU (hl=%06X pc=%06X)", z.hl.w, z.pc); }
    {   static const uint8_t c[] = { 0x21, 0xFF, 0xFF, 0xFF, 0x11, 0x01, 0x00, 0x00, 0x19 };
        setup(c, sizeof c, true); steps(3);
        CHECK(z.hl.w == 0 && (z.f & EZ80_CF), "ADD HL,DE carries out of bit 23 (hl=%06X f=%02X)", z.hl.w, z.f); }
    {   static const uint8_t c[] = { 0xB7, 0xED, 0x52 };                   /* or a / sbc hl,de */
        setup(c, sizeof c, true); z.hl.w = 0; z.de.w = 1; steps(2);
        CHECK(z.hl.w == 0xFFFFFF && (z.f & (EZ80_SF | EZ80_CF | EZ80_NF)) == (EZ80_SF | EZ80_CF | EZ80_NF),
              "SBC HL,DE 24-bit borrow (hl=%06X f=%02X)", z.hl.w, z.f); }
    {   static const uint8_t c[] = { 0xED, 0x4C };                         /* mlt bc */
        setup(c, sizeof c, true); z.bc.w = 0xFF0304; steps(1);
        CHECK(z.bc.w == 0x00000C, "MLT BC = %06X", z.bc.w); }
    {   static const uint8_t c[] = { 0xED, 0x54, 0xFB };                   /* lea ix,iy-5 */
        setup(c, sizeof c, true); z.iy.w = 0x100000; steps(1);
        CHECK(z.ix.w == 0x0FFFFB, "LEA IX,IY-5 = %06X", z.ix.w); }
    {   static const uint8_t c[] = { 0xDD, 0x27, 0x02 };                   /* ld hl,(ix+2) */
        setup(c, sizeof c, true); z.ix.w = 0xD00100; *at(0xD00102) = 0x11; *at(0xD00103) = 0x22; *at(0xD00104) = 0x33;
        steps(1);
        CHECK(z.hl.w == 0x332211, "LD HL,(IX+d) = %06X", z.hl.w); }
    {   static const uint8_t c[] = { 0xED, 0x0F };                         /* ld (hl),bc */
        setup(c, sizeof c, true); z.hl.w = 0xD00200; z.bc.w = 0x445566; steps(1);
        CHECK(*at(0xD00200) == 0x66 && *at(0xD00202) == 0x44, "LD (HL),BC stores 3 bytes"); }
    {   static const uint8_t c[] = { 0xE3 };                               /* ex (sp),hl */
        setup(c, sizeof c, true); z.hl.w = 0xA1B2C3; z.spl = 0xD00300;
        *at(0xD00300) = 1; *at(0xD00301) = 2; *at(0xD00302) = 3; steps(1);
        CHECK(z.hl.w == 0x030201 && *at(0xD00302) == 0xA1, "EX (SP),HL 24-bit (hl=%06X)", z.hl.w); }
    {   static const uint8_t c[] = { 0xCD, 0x10, 0x00, 0xD0 };             /* call D00010 ; at 10: ret */
        setup(c, sizeof c, true); ram[0x10] = 0xC9; steps(1);
        CHECK(z.pc == 0xD00010 && z.spl == 0xD0FFFD && *at(0xD0FFFF) == 0xD0, "CALL pushes 24-bit return address");
        steps(1);
        CHECK(z.pc == 0xD00004 && z.spl == 0xD10000, "RET returns (pc=%06X)", z.pc); }

    printf("Z80 mode / MBASE\n");
    {   static const uint8_t c[] = { 0x3A, 0x00, 0x01 };                   /* ld a,(0100h) */
        setup(c, sizeof c, false); ram[0x100] = 0x5A; steps(1);
        CHECK(z.a == 0x5A && z.pc == 0xD00003, "LD A,(mn) uses MBASE (a=%02X pc=%06X)", z.a, z.pc); }
    {   static const uint8_t c[] = { 0x23 };                               /* inc hl */
        setup(c, sizeof c, false); z.hl.w = 0x12FFFF; steps(1);
        CHECK(z.hl.w == 0x000000, "INC HL in Z80 mode wraps at 16 bits and clears HLU (%06X)", z.hl.w); }
    {   static const uint8_t c[] = { 0xC5 };                               /* push bc (uses SPS) */
        setup(c, sizeof c, false); z.bc.w = 0x123456; steps(1);
        CHECK(z.sps == 0x7FFE && *at(0xD07FFE) == 0x56 && *at(0xD07FFF) == 0x34, "PUSH in Z80 mode: 2 bytes on SPS"); }
    {   static const uint8_t c[] = { 0x18, 0xFE };                         /* jr $ at FFFE wraps */
        setup(c, sizeof c, false); steps(1);
        CHECK(z.pc == 0xD00000, "JR stays inside the MBASE page (pc=%06X)", z.pc); }
    {   static const uint8_t c[] = { 0x5B, 0x21, 0x56, 0x34, 0x12 };       /* ld.lil hl,123456h in Z80 mode */
        setup(c, sizeof c, false); steps(1);
        CHECK(z.hl.w == 0x123456 && z.pc == 0xD00005, ".LIL in Z80 mode loads 24 bits (hl=%06X)", z.hl.w); }

    printf("Mixed-mode calls\n");
    {   /* ADL: call.is 0200h -> Z80 mode at D00200; there: ret.l -> back to ADL */
        static const uint8_t c[] = { 0x40, 0xCD, 0x00, 0x02 };
        setup(c, sizeof c, true); ram[0x200] = 0x49; ram[0x201] = 0xC9;
        steps(1);
        CHECK(!z.adl && z.pc == 0xD00200, "CALL.IS enters Z80 mode at MBASE:0200 (adl=%d pc=%06X)", z.adl, z.pc);
        CHECK(z.spl == 0xD0FFFE && *at(0xD0FFFE) == 0x01 && *at(0xD0FFFF) == 0xD0, "  SPL holds PCU and ADL flag");
        CHECK(z.sps == 0x7FFE && *at(0xD07FFE) == 0x04 && *at(0xD07FFF) == 0x00, "  SPS holds PC low 16 bits");
        steps(1);
        CHECK(z.adl && z.pc == 0xD00004 && z.spl == 0xD10000 && z.sps == 0x8000,
              "RET.L returns to ADL (adl=%d pc=%06X)", z.adl, z.pc); }
    {   /* Z80 mode: call.il D00300 (ADL routine), which returns with ret.l */
        static const uint8_t c[] = { 0x52, 0xCD, 0x00, 0x03, 0xD0 };
        setup(c, sizeof c, false); ram[0x300] = 0x49; ram[0x301] = 0xC9;
        steps(1);
        CHECK(z.adl && z.pc == 0xD00300, "CALL.IL enters ADL mode (pc=%06X)", z.pc);
        steps(1);
        CHECK(!z.adl && z.pc == 0xD00005 && z.spl == 0xD10000, "RET.L returns to Z80 mode (pc=%06X spl=%06X)", z.pc, z.spl); }

    printf("Interrupts and traps\n");
    {   static const uint8_t c[] = { 0xFB, 0x00, 0x00 };                   /* ei / nop */
        setup(c, sizeof c, true); z.irq_line = 1; z.im = 2;
        steps(1);
        CHECK(z.pc == 0xD00001, "no interrupt right after EI");
        steps(1);
        CHECK(z.pc == 0xD00002, "the instruction after EI still executes");
        steps(1);
        CHECK(z.pc == 0x000038 && z.spl == 0xD0FFFD && !z.iff1, "IM 1 interrupt -> 38h (pc=%06X)", z.pc); }
    {   static const uint8_t c[] = { 0xDD, 0x00 };                         /* invalid: dd nop */
        setup(c, sizeof c, true); steps(1);
        CHECK(z.pc == 0x000000 && *at(0xD0FFFD) == 0x01, "DD 00 traps to RST 0 (pc=%06X)", z.pc); }
    {   static const uint8_t c[] = { 0xED, 0x6D, 0xED, 0x6E };             /* ld mb,a / ld a,mb */
        setup(c, sizeof c, true); z.a = 0x42; steps(1); z.a = 0; steps(1);
        CHECK(z.mbase == 0x42 && z.a == 0x42, "LD MB,A / LD A,MB"); }
    {   static const uint8_t c[] = { 0x01, 0x03, 0x00, 0x00, 0xED, 0xB0 }; /* ldir 3 bytes */
        setup(c, sizeof c, true); z.hl.w = 0xD00100; z.de.w = 0xD00180;
        ram[0x100] = 7; ram[0x101] = 8; ram[0x102] = 9;
        steps(1 + 3);
        CHECK(z.bc.w == 0 && ram[0x182] == 9 && z.pc == 0xD00006 && z.de.w == 0xD00183, "LDIR (bc=%06X pc=%06X)", z.bc.w, z.pc); }

    printf(failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
    return failures != 0;
}
