/*
 * N84 - sending variables to the TI-84 Plus family (high-level emulation)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Instead of emulating the link protocol, a small routine is placed in
 * appBackUpScreen and run by the emulated CPU while the OS is idle on the
 * MODE screen (see reach_safe_screen). It uses
 * the OS's own routines (bcalls): delete any variable with the same name,
 * _CreateVar (A = type, HL = data size without the size word; it writes the
 * size word and returns DE pointing at it, as _CreateProg = ld a,5 + this),
 * then (if requested) _Arc_Unarc. The host copies the data in
 * when the routine stops at its first marker. Each marker is "jr $"; the
 * routine also installs an OS error handler so that errors (e.g. not enough
 * memory) end at a marker instead of on the error screen.
 */
#include <stdio.h>
#include <string.h>
#include "machine/ti8x/ti8x.h"
#include "machine/ti8x/ti8x_link.h"

/* TI-83 Plus / TI-84 Plus system equates (ti83plus.inc) */
#define B_MOV9TOOP1    0x417A
#define B_CHKFINDSYM   0x42F1
#define B_CREATEVAR    0x4E70
#define B_DELVARARC    0x4FC6
#define B_ARC_UNARC    0x4FD8

#define APP_PUSH_ERRORH 0x0059
#define APP_POP_ERRORH  0x005C
#define STUB_ADDR      0x9872   /* appBackUpScreen (768 bytes) */
#define STUB_SIZE      96

static void poke(ti8x_t *m, uint16_t a, uint8_t v) {
    uint8_t *p = m->cpu.wr[a >> 14];
    if (p) p[a & 0x3FFF] = v;
    else m->cpu.write_slow(m->cpu.ctx, a, v);
}

static void run_frames(ti8x_t *m, int n) {
    while (n--) ti8x_run(m, TI8X_TICKS_PER_SEC / 60);
}

/* Key press as a person does it (2nd is pressed and released first). */
static void press(ti8x_t *m, uint8_t k1, uint8_t k2) {
    ti8x_set_key(m, k1, true);
    run_frames(m, 6);
    ti8x_set_key(m, k1, false);
    run_frames(m, 10);
    if (k2) {
        ti8x_set_key(m, k2, true);
        run_frames(m, 6);
        ti8x_set_key(m, k2, false);
        run_frames(m, 10);
    }
}

static uint16_t peek16(ti8x_t *m, uint16_t a) {
    return (uint16_t)(z80_peek(&m->cpu, a) | z80_peek(&m->cpu, (uint16_t)(a + 1)) << 8);
}

/* Waits until the OS sits in HALT with interrupts enabled (key wait). */
static bool wait_idle(ti8x_t *m) {
    for (int i = 0; i < 600; i++) {               /* up to 10 s */
        if (!ti8x_lcd_on(m) && i % 60 == 0) {      /* calculator off: press ON */
            ti8x_set_key(m, TK_ON, true);
            run_frames(m, 6);
            ti8x_set_key(m, TK_ON, false);
        }
        if (m->cpu.halted && m->cpu.iff1 && ti8x_lcd_on(m)) {
            run_frames(m, 2);                      /* let the key-wait settle */
            if (m->cpu.halted && m->cpu.iff1) return true;
        }
        run_frames(m, 1);
    }
    return false;
}

typedef struct {
    uint16_t mark_created, mark_done, mark_error;
} stub_marks_t;

static stub_marks_t build_stub(uint8_t *s, const tivar_t *v, uint16_t create_size) {
    uint16_t base = STUB_ADDR;
    int n = 0;
    stub_marks_t mk;
    uint16_t op1c_pos, errh_pos;
#define B(x) (s[n++] = (uint8_t)(x))
#define W(x) (B((x) & 0xFF), B((x) >> 8))
#define BCALL(a) (B(0xEF), W(a))
    B(0x21); errh_pos = (uint16_t)n; W(0);              /* ld hl,errh */
    B(0xCD); W(APP_PUSH_ERRORH);                        /* call APP_PUSH_ERRORH */
    B(0x21); op1c_pos = (uint16_t)n; W(0);              /* ld hl,op1c */
    BCALL(B_MOV9TOOP1);
    BCALL(B_CHKFINDSYM);
    B(0x38); B(3);                                      /* jr c,+3 (not found) */
    BCALL(B_DELVARARC);
    B(0x21); uint16_t op1c_pos2 = (uint16_t)n; W(0);    /* ld hl,op1c */
    BCALL(B_MOV9TOOP1);
    B(0x21); W(create_size);                            /* ld hl,size */
    B(0x3E); B(v->type & 0x1F);                         /* ld a,type (_CreateVar takes it in A) */
    BCALL(B_CREATEVAR);
    mk.mark_created = (uint16_t)(base + n); B(0x18); B(0xFE);   /* jr $ */
    B(0x3E); B(v->archived ? 1 : 0);                    /* ld a,archive */
    B(0xB7);                                            /* or a */
    B(0x28); B(9);                                      /* jr z,+9 */
    B(0x21); uint16_t op1c_pos3 = (uint16_t)n; W(0);    /* ld hl,op1c */
    BCALL(B_MOV9TOOP1);
    BCALL(B_ARC_UNARC);
    B(0xCD); W(APP_POP_ERRORH);                         /* call APP_POP_ERRORH */
    mk.mark_done = (uint16_t)(base + n); B(0x18); B(0xFE);
    mk.mark_error = (uint16_t)(base + n); B(0x18); B(0xFE);
    uint16_t op1c = (uint16_t)(base + n);
    B(v->type); for (int i = 0; i < 8; i++) B(v->name[i]);
    s[errh_pos] = (uint8_t)mk.mark_error; s[errh_pos + 1] = (uint8_t)(mk.mark_error >> 8);
    s[op1c_pos] = s[op1c_pos2] = s[op1c_pos3] = (uint8_t)op1c;
    s[op1c_pos + 1] = s[op1c_pos2 + 1] = s[op1c_pos3 + 1] = (uint8_t)(op1c >> 8);
#undef B
#undef W
#undef BCALL
    return mk;
}

/*
 * On the home screen the OS keeps the entry line's edit buffer open, and that
 * buffer temporarily owns all free RAM (FPS > OPS), so nothing can be created.
 * Switching to another screen makes the OS put the home screen away and close
 * the buffer; returning with 2nd+QUIT reopens it. So: QUIT, MODE, create the
 * variable on the MODE screen, then QUIT again.
 */
#define RAM_FPS 0x9824
#define RAM_OPS 0x9828

static bool reach_safe_screen(ti8x_t *m) {
    if (!wait_idle(m)) return false;
    press(m, TK_2ND, TK_MODE);              /* 2nd+QUIT: home screen */
    if (!wait_idle(m)) return false;
    press(m, TK_MODE, 0);                   /* MODE screen: home put away */
    if (!wait_idle(m)) return false;
    return peek16(m, RAM_FPS) <= peek16(m, RAM_OPS);
}

const char *ti8x_send_var(ti8x_t *m, const tivar_t *v) {
    if (v->size < 2 && tivar_has_size_word(v->type)) return "corrupt variable";
    if (!reach_safe_screen(m)) {
        press(m, TK_2ND, TK_MODE);
        return "the calculator is busy (go to the home screen and try again)";
    }

    uint16_t create_size = tivar_has_size_word(v->type)
        ? (uint16_t)(v->data[0] | v->data[1] << 8) : v->size;

    uint8_t saved_mem[STUB_SIZE], stub[STUB_SIZE];
    for (int i = 0; i < STUB_SIZE; i++) saved_mem[i] = z80_peek(&m->cpu, (uint16_t)(STUB_ADDR + i));
    z80_t saved = m->cpu;

    memset(stub, 0, sizeof stub);
    stub_marks_t mk = build_stub(stub, v, create_size);
    for (int i = 0; i < STUB_SIZE; i++) poke(m, (uint16_t)(STUB_ADDR + i), stub[i]);

    m->cpu.pc.w = STUB_ADDR;
    m->cpu.halted = 0;
    const char *err = "timed out";
    bool copied = false;
    for (int i = 0; i < 1200; i++) {                     /* up to 20 s (archiving) */
        run_frames(m, 1);
        uint16_t pc = m->cpu.pc.w;
        if (pc == mk.mark_created && !copied) {
            uint16_t de = m->cpu.de.w;
            /* data (with its size word, which _CreateVar has just written) */
            for (unsigned k = 0; k < v->size; k++) poke(m, (uint16_t)(de + k), v->data[k]);
            copied = true;
            m->cpu.pc.w = (uint16_t)(mk.mark_created + 2);
        } else if (pc == mk.mark_done) {
            err = NULL;
            break;
        } else if (pc == mk.mark_error) {
            static char msg[48];
            snprintf(msg, sizeof msg, "the calculator OS reported error %u", m->cpu.af.b.h & 0x7F);
            err = msg;
            break;
        }
    }

    /* back to the interrupted key wait of the MODE screen */
    m->cpu.af = saved.af; m->cpu.bc = saved.bc; m->cpu.de = saved.de; m->cpu.hl = saved.hl;
    m->cpu.ix = saved.ix; m->cpu.iy = saved.iy; m->cpu.sp = saved.sp; m->cpu.pc = saved.pc;
    m->cpu.wz = saved.wz; m->cpu.af_ = saved.af_; m->cpu.bc_ = saved.bc_;
    m->cpu.de_ = saved.de_; m->cpu.hl_ = saved.hl_;
    m->cpu.i = saved.i; m->cpu.iff1 = saved.iff1; m->cpu.iff2 = saved.iff2; m->cpu.im = saved.im;
    m->cpu.halted = saved.halted; m->cpu.ei_delay = 0;
    for (int i = 0; i < STUB_SIZE; i++) poke(m, (uint16_t)(STUB_ADDR + i), saved_mem[i]);
    press(m, TK_2ND, TK_MODE);              /* back to the home screen */
    wait_idle(m);
    return err;
}
