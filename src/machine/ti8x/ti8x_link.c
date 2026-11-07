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
    uint16_t mark_created, mark_found, mark_done, mark_error;
} stub_marks_t;

static stub_marks_t build_stub(uint8_t *s, const tivar_t *v, uint16_t create_size, bool archive) {
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
    B(0x3E); B(archive ? 1 : 0);                        /* ld a,archive */
    B(0xB7);                                            /* or a */
    B(0x28); B(9);                                      /* jr z,+9 */
    B(0x21); uint16_t op1c_pos3 = (uint16_t)n; W(0);    /* ld hl,op1c */
    BCALL(B_MOV9TOOP1);
    BCALL(B_ARC_UNARC);
    B(0x21); uint16_t op1c_pos4 = (uint16_t)n; W(0);    /* ld hl,op1c */
    BCALL(B_MOV9TOOP1);
    BCALL(B_CHKFINDSYM);                                /* HL = VAT entry, B:DE = data */
    mk.mark_found = (uint16_t)(base + n); B(0x18); B(0xFE);
    B(0xCD); W(APP_POP_ERRORH);                         /* call APP_POP_ERRORH */
    mk.mark_done = (uint16_t)(base + n); B(0x18); B(0xFE);
    mk.mark_error = (uint16_t)(base + n); B(0x18); B(0xFE);
    uint16_t op1c = (uint16_t)(base + n);
    B(v->type); for (int i = 0; i < 8; i++) B(v->name[i]);
    s[errh_pos] = (uint8_t)mk.mark_error; s[errh_pos + 1] = (uint8_t)(mk.mark_error >> 8);
    s[op1c_pos] = s[op1c_pos2] = s[op1c_pos3] = s[op1c_pos4] = (uint8_t)op1c;
    s[op1c_pos + 1] = s[op1c_pos2 + 1] = s[op1c_pos3 + 1] = s[op1c_pos4 + 1] = (uint8_t)(op1c >> 8);
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

/*
 * Archived variables bigger than the free RAM: the OS creates and archives an
 * empty variable with the right name and type (so the VAT entry and the
 * archive entry are made by the OS itself), then the data is written straight
 * into the archive, as the OS would when receiving through the link port.
 *
 * Archive layout (user archive from page 08h up to the lowest Flash app;
 * apps grow down from 15h/29h/69h on the 83+/84+/SE): 64 KiB sectors whose
 * first byte is F0h/FCh (in use), FEh (swap) or FFh (empty); then entries
 * FCh (valid) / F0h (deleted), size (2 bytes, counted from T), T, T2,
 * version, address (4000h-7FFFh form) and page of the entry itself, name
 * length, name, data.
 * The OS does not let an entry cross a sector boundary, and a VAT entry of
 * an archived variable points to the entry's FCh byte.
 */
#define SECTOR 0x10000u

static unsigned archive_top_page(const ti8x_t *m) {
    return m->flash_pages >= 128 ? 0x69 : m->flash_pages >= 64 ? 0x29 : 0x15;
}

/* First page used by Flash apps (apps grow down from the top page). */
static unsigned lowest_app_page(const ti8x_t *m) {
    unsigned p = archive_top_page(m);
    for (int guard = 0; guard < 128; guard++) {
        const uint8_t *pg = m->flash + p * 0x4000u;
        if (pg[0] != 0x80 && pg[0] != 0x00) break;         /* not a (deleted) app */
        unsigned pages = 1;
        for (unsigned i = 0; i < 0x80; i++)
            if (pg[i] == 0x80 && (pg[i + 1] & 0xF0) == 0x80 && (pg[i + 1] & 0x0F) == 1) {
                pages = pg[i + 2] ? pg[i + 2] : 1;           /* field 8081h: page count */
                break;
            }
        if (pages > p) break;
        p -= pages;
    }
    return p + 1;
}

static void flash_write(ti8x_t *m, uint32_t a, uint8_t v) {
    m->flash[a] = v;
    m->flash_dirty |= 1u << (a >> 16);
}

/* Moves/grows the empty archived variable at `entry` to hold v's data. */
static const char *archive_fill(ti8x_t *m, const tivar_t *v, uint16_t vat, uint32_t entry) {
    const uint8_t *f = m->flash;
    if (f[entry] != 0xFC) return "archive entry not found";
    unsigned nl = f[entry + 9];
    uint32_t hdr = 10 + nl;                                  /* FC, size, T..name */
    uint32_t total = hdr + v->size;
    uint32_t old_end = entry + 3 + (f[entry + 1] | f[entry + 2] << 8);
    if (total > SECTOR - 1) return "variable too big for the archive";

    uint32_t dest;
    bool room = (entry & ~(SECTOR - 1)) == ((entry + total - 1) & ~(SECTOR - 1));
    for (uint32_t a = old_end; room && a < entry + total; a++) room = f[a] == 0xFF;
    if (room) {
        dest = entry;                                        /* last entry, room after it */
    } else {
        /* a new sector, keeping one empty sector for the OS's garbage collection */
        unsigned first = 0x08 / 4, end = lowest_app_page(m) / 4, empty = 0, pick = 0;
        bool swap = false;
        for (unsigned sc = first; sc < end; sc++) {
            uint8_t st = f[sc * SECTOR];
            if (st == 0xFE) swap = true;
            if (st == 0xFF && (*(const uint32_t *)(f + sc * SECTOR) == 0xFFFFFFFF)) {
                if (!pick) pick = sc;
                empty++;
            }
        }
        if (!pick || (!swap && empty < 2))
            return "the archive is full (try Garbage Collect in the MEM menu)";
        dest = pick * SECTOR + 1;
        flash_write(m, pick * SECTOR, 0xF0);
        for (unsigned i = 0; i < hdr; i++) flash_write(m, dest + i, f[entry + i]);
        flash_write(m, entry, 0xF0);                         /* old one deleted */
    }
    uint16_t sz = (uint16_t)(total - 3);
    flash_write(m, dest + 1, (uint8_t)sz);
    flash_write(m, dest + 2, (uint8_t)(sz >> 8));
    flash_write(m, dest + 6, (uint8_t)dest);
    flash_write(m, dest + 7, (uint8_t)(((dest & 0x3FFF) >> 8) | 0x40));
    flash_write(m, dest + 8, (uint8_t)(dest >> 14));
    for (unsigned k = 0; k < v->size; k++) flash_write(m, dest + hdr + k, v->data[k]);
    /* VAT entry (grows down): T, T2, version, DAL, DAH, page */
    poke(m, (uint16_t)(vat - 3), (uint8_t)(dest & 0xFF));
    poke(m, (uint16_t)(vat - 4), (uint8_t)(((dest & 0x3FFF) >> 8) | 0x40));
    poke(m, (uint16_t)(vat - 5), (uint8_t)(dest >> 14));
    return NULL;
}

/*
 * Flash apps: whole 16 KiB pages that grow down from the top archive page
 * (the app's first page, with the 800Fh header, is the highest). A base page
 * starting with 80h is a valid app, 00h a deleted one (until Garbage
 * Collect); field 8081h holds the page count, 804xh the name. The OS walks
 * this chain when it lists apps, so writing the pages is all it takes (the
 * signature is only checked by the OS's link receiver).
 */
static bool app_name_is(const uint8_t *pg, const uint8_t *name) {
    for (unsigned i = 0; i < 0x80; i++)
        if (pg[i] == 0x80 && (pg[i + 1] & 0xF0) == 0x40) {
            unsigned len = pg[i + 1] & 0x0F;
            for (unsigned k = 0; k < 8; k++) {
                uint8_t a = k < len ? pg[i + 2 + k] : ' ', b = name[k] ? name[k] : ' ';
                if (a != b) return false;
            }
            return true;
        }
    return false;
}

static const char *install_app(ti8x_t *m, const tivar_t *v) {
    unsigned n = v->size / 0x4000;
    /* delete an installed copy */
    unsigned p = archive_top_page(m);
    while (p >= 8 && (m->flash[p * 0x4000u] == 0x80 || m->flash[p * 0x4000u] == 0x00)) {
        const uint8_t *pg = m->flash + p * 0x4000u;
        unsigned pages = 1;
        for (unsigned i = 0; i < 0x80; i++)
            if (pg[i] == 0x80 && pg[i + 1] == 0x81) { pages = pg[i + 2] ? pg[i + 2] : 1; break; }
        if (pg[0] == 0x80 && app_name_is(pg, v->name)) flash_write(m, p * 0x4000u, 0x00);
        if (pages > p) break;
        p -= pages;
    }
    unsigned top = lowest_app_page(m) - 1, bottom = top + 1 - n;
    if (n == 0 || n > top || bottom < 0x08) return "not enough Flash for the app";
    /* the pages must be free, in sectors the archive does not use */
    for (unsigned q = bottom; q <= top; q++) {
        const uint8_t *pg = m->flash + q * 0x4000u;
        for (unsigned i = 0; i < 0x4000; i++)
            if (pg[i] != 0xFF) return "not enough free Flash for the app (try Garbage Collect in the MEM menu)";
    }
    unsigned empty = 0;
    bool swap = false;
    for (unsigned sc = 0x08 / 4; sc < bottom / 4; sc++) {
        if (m->flash[sc * SECTOR] == 0xFE) swap = true;
        if (m->flash[sc * SECTOR] == 0xFF) empty++;
    }
    if (!swap && !empty) return "not enough free Flash for the app (try Garbage Collect in the MEM menu)";
    for (unsigned k = 0; k < n; k++)
        for (unsigned i = 0; i < 0x4000; i++)
            if (v->data[k * 0x4000u + i] != 0xFF) flash_write(m, (top - k) * 0x4000u + i, v->data[k * 0x4000u + i]);
    return NULL;
}

static bool direct_archive_type(uint8_t type) {
    type &= 0x1F;
    return type == 0x05 || type == 0x06 || type == 0x15 || type == 0x17;
}

const char *ti8x_send_var(ti8x_t *m, const tivar_t *v) {
    if (v->type == TIVAR_APP) {
        if (!reach_safe_screen(m)) {
            press(m, TK_2ND, TK_MODE);
            return "the calculator is busy (go to the home screen and try again)";
        }
        const char *e = install_app(m, v);
        press(m, TK_2ND, TK_MODE);
        wait_idle(m);
        return e;
    }
    if (v->size < 2 && tivar_has_size_word(v->type)) return "corrupt variable";
    if (!reach_safe_screen(m)) {
        press(m, TK_2ND, TK_MODE);
        return "the calculator is busy (go to the home screen and try again)";
    }

    uint16_t create_size = tivar_has_size_word(v->type)
        ? (uint16_t)(v->data[0] | v->data[1] << 8) : v->size;

    unsigned free_ram = (unsigned)(peek16(m, RAM_OPS) - peek16(m, RAM_FPS));
    /* too big for the RAM (archived or not): create it empty, archive it,
     * then fill it in (like choosing "Archive" when sending from a computer) */
    bool direct = direct_archive_type(v->type) && tivar_has_size_word(v->type)
                  && v->size + 64u > free_ram;
    if (direct) create_size = 0;
    uint8_t saved_mem[STUB_SIZE], stub[STUB_SIZE];
    for (int i = 0; i < STUB_SIZE; i++) saved_mem[i] = z80_peek(&m->cpu, (uint16_t)(STUB_ADDR + i));
    z80_t saved = m->cpu;

    memset(stub, 0, sizeof stub);
    stub_marks_t mk = build_stub(stub, v, create_size, v->archived || direct);
    for (int i = 0; i < STUB_SIZE; i++) poke(m, (uint16_t)(STUB_ADDR + i), stub[i]);

    m->cpu.pc.w = STUB_ADDR;
    m->cpu.halted = 0;
    const char *err = "timed out";
    bool copied = false;
    const char *fill_err = NULL;
    for (int i = 0; i < 1200; i++) {                     /* up to 20 s (archiving) */
        run_frames(m, 1);
        uint16_t pc = m->cpu.pc.w;
        if (pc == mk.mark_created && !copied) {
            uint16_t de = m->cpu.de.w;
            /* data (with its size word, which _CreateVar has just written) */
            if (!direct)
                for (unsigned k = 0; k < v->size; k++) poke(m, (uint16_t)(de + k), v->data[k]);
            copied = true;
            m->cpu.pc.w = (uint16_t)(mk.mark_created + 2);
        } else if (pc == mk.mark_found) {
            if (direct) {
                uint8_t page = m->cpu.bc.b.h;
                uint16_t de = m->cpu.de.w;
                if (!page || de < 0x4000 || de >= 0x8000 || page >= m->flash_pages)
                    fill_err = "the variable was not archived";
                else
                    fill_err = archive_fill(m, v, m->cpu.hl.w, (uint32_t)page * 0x4000u + (de & 0x3FFF));
            }
            m->cpu.pc.w = (uint16_t)(mk.mark_found + 2);
        } else if (pc == mk.mark_done) {
            err = fill_err;
            break;
        } else if (pc == mk.mark_error) {
            err = tivar_os_error(m->cpu.af.b.h, v->size, free_ram);
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
