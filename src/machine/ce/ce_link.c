/*
 * N84 - sending variables to the TI-84 Plus CE (high-level emulation)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Same method as ti8x_link.c: with the OS idle on the MODE screen (where the
 * home screen's edit buffer, which owns all free RAM, is closed), a small
 * ADL-mode routine in pixelShadow2 calls the OS jump table: delete any
 * variable with the same name, _CreateVar (A = type, HL = size), copy the
 * data in from the host, optionally _Arc_Unarc. 2nd+QUIT then returns to the
 * home screen. OS errors end at a marker through an OS error handler.
 * Addresses: ti84pce.inc (as shipped in CEmu's tests).
 */
#include <stdio.h>
#include <string.h>
#include "ce.h"
#include "ce_link.h"
#include "cpu.h"
#include "mem.h"

#define J_MOV9TOOP1        0x020320
#define J_CHKFINDSYM       0x02050C
#define J_PUSHERRORHANDLER 0x020798
#define J_POPERRORHANDLER  0x02079C
#define J_CREATEVAR        0x021338
#define J_DELVARARC        0x021434
#define J_ARC_UNARC        0x021448
#define RAM_FPS            0xD0258D
#define RAM_OPS            0xD02593
#define STUB_ADDR          0xD052C6   /* pixelShadow2 (8400 bytes) */
#define STUB_SIZE          128

/* TI-84 key matrix positions (group, bit) of the keys used; CE row = 7 - group */
#define KEY_2ND   6, 5
#define KEY_MODE  6, 6

static void frames(int n) { while (n--) ce_run(1); }

static void press_key(unsigned group, unsigned bit) {
    ce_key(7 - group, bit, true);
    frames(6);
    ce_key(7 - group, bit, false);
    frames(10);
}

static uint32_t peek24(uint32_t a) {
    return mem_peek_byte(a) | (uint32_t)mem_peek_byte(a + 1) << 8 | (uint32_t)mem_peek_byte(a + 2) << 16;
}

static bool wait_idle(void) {
    for (int i = 0; i < 600; i++) {
        if (cpu.z.halted && cpu.z.iff1) {
            frames(2);
            if (cpu.z.halted && cpu.z.iff1) return true;
        }
        frames(1);
    }
    return false;
}

static bool reach_safe_screen(void) {
    if (!wait_idle()) return false;
    press_key(KEY_2ND); press_key(KEY_MODE);      /* 2nd+QUIT: home screen */
    if (!wait_idle()) return false;
    press_key(KEY_MODE);                          /* MODE: home put away */
    if (!wait_idle()) return false;
    return peek24(RAM_FPS) <= peek24(RAM_OPS);
}

typedef struct { uint32_t created, done, error; } marks_t;

static marks_t build_stub(uint8_t *s, const tivar_t *v, uint32_t size) {
    uint32_t base = STUB_ADDR;
    int n = 0;
    marks_t mk;
    int errh_pos, op1_pos[3];
#define B(x) (s[n++] = (uint8_t)(x))
#define L(x) (B((x) & 0xFF), B((x) >> 8 & 0xFF), B((x) >> 16 & 0xFF))
#define CALL(a) (B(0xCD), L(a))
    B(0x21); errh_pos = n; L(0);                          /* ld hl,errh */
    CALL(J_PUSHERRORHANDLER);
    B(0x21); op1_pos[0] = n; L(0);                        /* ld hl,op1c */
    CALL(J_MOV9TOOP1);
    CALL(J_CHKFINDSYM);
    B(0x38); B(4);                                        /* jr c,+4 (not found) */
    CALL(J_DELVARARC);
    B(0x21); op1_pos[1] = n; L(0);
    CALL(J_MOV9TOOP1);
    B(0x21); L(size);                                     /* ld hl,size */
    B(0x3E); B(v->type & 0x3F);                           /* ld a,type */
    CALL(J_CREATEVAR);
    mk.created = base + (uint32_t)n; B(0x18); B(0xFE);    /* jr $ */
    B(0x3E); B(v->archived ? 1 : 0);                      /* ld a,archive */
    B(0xB7);                                              /* or a */
    B(0x28); B(12);                                       /* jr z,+12 */
    B(0x21); op1_pos[2] = n; L(0);
    CALL(J_MOV9TOOP1);
    CALL(J_ARC_UNARC);
    CALL(J_POPERRORHANDLER);
    mk.done = base + (uint32_t)n; B(0x18); B(0xFE);
    mk.error = base + (uint32_t)n; B(0x18); B(0xFE);
    uint32_t op1c = base + (uint32_t)n;
    B(v->type); for (int i = 0; i < 8; i++) B(v->name[i]);
    s[errh_pos] = (uint8_t)mk.error; s[errh_pos + 1] = (uint8_t)(mk.error >> 8); s[errh_pos + 2] = (uint8_t)(mk.error >> 16);
    for (int k = 0; k < 3; k++) {
        s[op1_pos[k]] = (uint8_t)op1c; s[op1_pos[k] + 1] = (uint8_t)(op1c >> 8); s[op1_pos[k] + 2] = (uint8_t)(op1c >> 16);
    }
#undef B
#undef L
#undef CALL
    return mk;
}

const char *ce_send_var(const tivar_t *v) {
    if (v->type == TIVAR_APP) return "TI-84 Plus apps (.8xk) do not run on the CE";
    if (v->size < 2 && tivar_has_size_word(v->type)) return "corrupt variable";
    if (!reach_safe_screen()) {
        press_key(KEY_2ND); press_key(KEY_MODE);
        return "the calculator is busy (go to the home screen and try again)";
    }
    uint32_t size = tivar_has_size_word(v->type) ? (uint32_t)(v->data[0] | v->data[1] << 8) : v->size;

    unsigned long free_ram = peek24(RAM_OPS) - peek24(RAM_FPS);
    uint8_t saved_mem[STUB_SIZE], stub[STUB_SIZE];
    for (int i = 0; i < STUB_SIZE; i++) saved_mem[i] = mem_peek_byte(STUB_ADDR + (uint32_t)i);
    ez80_t saved = cpu.z;
    memset(stub, 0, sizeof stub);
    marks_t mk = build_stub(stub, v, size);
    for (int i = 0; i < STUB_SIZE; i++) mem_poke_byte(STUB_ADDR + (uint32_t)i, stub[i]);

    cpu.z.pc = STUB_ADDR;
    cpu.z.adl = 1;
    cpu.z.halted = 0;
    const char *err = "timed out";
    bool copied = false;
    for (int i = 0; i < 1200; i++) {
        frames(1);
        uint32_t pc = cpu.z.pc;
        if (pc == mk.created && !copied) {
            uint32_t de = cpu.z.de.w;
            for (unsigned k = 0; k < v->size; k++) mem_poke_byte(de + k, v->data[k]);
            copied = true;
            cpu.z.pc = mk.created + 2;
        } else if (pc == mk.done) {
            err = NULL;
            break;
        } else if (pc == mk.error) {
            err = tivar_os_error(cpu.z.a, v->size, free_ram);
            break;
        }
    }

    /* registers back to the MODE screen's key wait (page tables etc. untouched) */
    cpu.z.a = saved.a; cpu.z.f = saved.f; cpu.z.a_ = saved.a_; cpu.z.f_ = saved.f_;
    cpu.z.bc = saved.bc; cpu.z.de = saved.de; cpu.z.hl = saved.hl; cpu.z.ix = saved.ix; cpu.z.iy = saved.iy;
    cpu.z.bc_ = saved.bc_; cpu.z.de_ = saved.de_; cpu.z.hl_ = saved.hl_;
    cpu.z.sps = saved.sps; cpu.z.spl = saved.spl; cpu.z.pc = saved.pc; cpu.z.mbase = saved.mbase;
    cpu.z.adl = saved.adl; cpu.z.madl = saved.madl; cpu.z.iff1 = saved.iff1; cpu.z.iff2 = saved.iff2;
    cpu.z.im = saved.im; cpu.z.halted = saved.halted; cpu.z.ei_delay = 0;
    for (int i = 0; i < STUB_SIZE; i++) mem_poke_byte(STUB_ADDR + (uint32_t)i, saved_mem[i]);
    press_key(KEY_2ND); press_key(KEY_MODE);      /* back to the home screen */
    wait_idle();
    return err;
}
