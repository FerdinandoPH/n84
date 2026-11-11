/*
 * N84 - TI-8x emulator for the TI-Nspire
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TI-83 Plus / TI-83 Plus SE / TI-84 Plus / TI-84 Plus SE machine (Z80 family).
 *
 * Hardware behaviour is based on WikiTI documentation and on TilEm
 * (LGPL-2.1+, Benjamin Moody et al.), which was used as a reference.
 *
 * Time base: the machine counts time in "ticks" of a 30 MHz reference clock,
 * so both CPU speeds (6 MHz = 5 ticks/cycle, 15 MHz = 2 ticks/cycle) and the
 * microsecond-based hardware timers (30 ticks/us) are exact integers.
 */
#ifndef N84_TI8X_H
#define N84_TI8X_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "cpu/z80/z80.h"

#define TI8X_TICKS_PER_SEC 30000000u
#define TI8X_TICKS_PER_US  30u

#define TI8X_LCD_W 96
#define TI8X_LCD_H 64
#define TI8X_LCD_STRIDE 15      /* T6A04 has 120 columns of RAM */

typedef enum {
    TI8X_MODEL_NONE = 0,
    TI8X_MODEL_83PSE,
    TI8X_MODEL_84P,
    TI8X_MODEL_84PSE,
    TI8X_MODEL_83P,             /* after the others: saved states store the number */
    TI8X_MODEL_COUNT
} ti8x_model_t;

/* TI key codes: (group << 3) | bit, matching port 01 scanning */
#define TI_KEY(g, b) (((g) << 3) | (b))
enum {
    TK_DOWN = TI_KEY(0,0), TK_LEFT = TI_KEY(0,1), TK_RIGHT = TI_KEY(0,2), TK_UP = TI_KEY(0,3),
    TK_ENTER = TI_KEY(1,0), TK_ADD = TI_KEY(1,1), TK_SUB = TI_KEY(1,2), TK_MUL = TI_KEY(1,3),
    TK_DIV = TI_KEY(1,4), TK_POWER = TI_KEY(1,5), TK_CLEAR = TI_KEY(1,6),
    TK_CHS = TI_KEY(2,0), TK_3 = TI_KEY(2,1), TK_6 = TI_KEY(2,2), TK_9 = TI_KEY(2,3),
    TK_RPAREN = TI_KEY(2,4), TK_TAN = TI_KEY(2,5), TK_VARS = TI_KEY(2,6),
    TK_DOT = TI_KEY(3,0), TK_2 = TI_KEY(3,1), TK_5 = TI_KEY(3,2), TK_8 = TI_KEY(3,3),
    TK_LPAREN = TI_KEY(3,4), TK_COS = TI_KEY(3,5), TK_PRGM = TI_KEY(3,6), TK_STAT = TI_KEY(3,7),
    TK_0 = TI_KEY(4,0), TK_1 = TI_KEY(4,1), TK_4 = TI_KEY(4,2), TK_7 = TI_KEY(4,3),
    TK_COMMA = TI_KEY(4,4), TK_SIN = TI_KEY(4,5), TK_APPS = TI_KEY(4,6), TK_XTTN = TI_KEY(4,7),
    TK_STO = TI_KEY(5,1), TK_LN = TI_KEY(5,2), TK_LOG = TI_KEY(5,3), TK_SQUARE = TI_KEY(5,4),
    TK_RECIP = TI_KEY(5,5), TK_MATH = TI_KEY(5,6), TK_ALPHA = TI_KEY(5,7),
    TK_GRAPH = TI_KEY(6,0), TK_TRACE = TI_KEY(6,1), TK_ZOOM = TI_KEY(6,2), TK_WINDOW = TI_KEY(6,3),
    TK_YEQU = TI_KEY(6,4), TK_2ND = TI_KEY(6,5), TK_MODE = TI_KEY(6,6), TK_DEL = TI_KEY(6,7),
    TK_ON = 0x40,
    TK_NONE = 0xFF
};

enum {
    TI8X_EV_TIMER1, TI8X_EV_TIMER2A, TI8X_EV_TIMER2B,
    TI8X_EV_CRYS0, TI8X_EV_CRYS1, TI8X_EV_CRYS2,
    TI8X_EV_LCD_SAMPLE,
    TI8X_EV_COUNT
};
#define TI8X_EV_OFF UINT64_MAX

typedef struct {
    uint8_t  freq, loopvalue;
    uint16_t status;          /* bit0 loop, bit1 int, bit2 overflow, 0x100 finished, 0x200 no-halt-int */
    uint64_t period;          /* ticks between expirations after the first; 0 = one-shot */
} ti8x_crystal_t;

typedef struct {
    uint8_t active, mode, inc, x, y, rowshift, contrast, nextbyte;
    uint8_t ram[TI8X_LCD_STRIDE * TI8X_LCD_H];
} t6a04_t;

typedef struct ti8x {
    ti8x_model_t model;
    z80_t cpu;

    /* memory: flash pages, then RAM pages, contiguous */
    uint8_t *mem;
    uint8_t *flash, *ram;
    unsigned flash_pages, ram_pages;
    uint8_t bank_page[4];               /* physical page currently in each bank */

    /* flash */
    uint8_t flash_unlocked, flash_state, flash_override;
    uint32_t flash_dirty;               /* bitmap of modified 64K sectors */

    /* ports */
    uint8_t port3, port4, port5, port6, port7, port8, porte, portf;
    uint8_t port20, port21, port22, port23, port25, port26, port27, port28;
    uint8_t port29_2f[7];
    uint8_t keygroup;
    uint8_t linkout;
    uint8_t clock_mode;
    uint32_t clock_input, clock_base;   /* RTC: seconds = clock_base + elapsed */
    uint64_t clock_start;
    uint32_t md5[6];
    uint8_t md5_shift, md5_mode;

    /* interrupts */
    uint8_t int_pending;                /* port 04 layout: 1=ON 2=T1 4=T2 0x20..0x80 crystal */

    /* input */
    uint8_t keys[8];                    /* pressed bits per group */
    bool on_down;

    /* timing */
    uint64_t now;                       /* ticks */
    uint64_t ev[TI8X_EV_COUNT];
    unsigned cpu_mult;                  /* ticks per CPU cycle: 5 or 2 */
    ti8x_crystal_t crys[3];

    /* display */
    t6a04_t lcd;
    uint8_t lcd_acc[TI8X_LCD_W * TI8X_LCD_H];  /* "on" samples per pixel */
    uint8_t lcd_samples;
    uint8_t lcd_samples_per_frame;      /* 1 = no grayscale blending */
} ti8x_t;

ti8x_model_t ti8x_detect_model(const uint8_t *rom, size_t size);
/* Same, from the image size and its last (boot) 16 KiB page only. */
ti8x_model_t ti8x_detect_model_boot(size_t size, const uint8_t *boot_page);
const char  *ti8x_model_name(ti8x_model_t m);
/* Offset of the boot page (the last 16 KiB of Flash) in an image of `size`. */
size_t       ti8x_boot_page_offset(size_t size);

/* Takes ownership of nothing: the ROM is copied. Returns false on bad size. */
bool  ti8x_init(ti8x_t *m, ti8x_model_t model, const uint8_t *rom, size_t size);
void  ti8x_free(ti8x_t *m);
void  ti8x_reset(ti8x_t *m);

/* Run emulation for `ticks` of the 30 MHz reference clock. */
void  ti8x_run(ti8x_t *m, uint32_t ticks);

void  ti8x_set_key(ti8x_t *m, uint8_t key, bool down);
void  ti8x_release_all_keys(ti8x_t *m);

/* Produce the current frame as 96x64 intensity levels 0 (off)..255 (fully
 * dark), averaged over the samples taken since the previous call. */
void  ti8x_lcd_frame(ti8x_t *m, uint8_t *out96x64);
bool  ti8x_lcd_on(const ti8x_t *m);

/* Persistence. The state file holds CPU, hardware and RAM; Flash changes are
 * written back to the ROM image with ti8x_flush_flash(), which only rewrites
 * the 64 KiB sectors modified since the last flush. */
bool  ti8x_save_state(const ti8x_t *m, FILE *f);
bool  ti8x_load_state(ti8x_t *m, FILE *f);
bool  ti8x_flush_flash(ti8x_t *m, FILE *rom);

#endif
