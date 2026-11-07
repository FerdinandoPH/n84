/*
 * N84 - phase 0 diagnostic and benchmark (docs/SPEC.md, phase F0)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Checks the platform layer (timer, LCD, big allocations) and measures raw Z80
 * interpreter throughput on a synthetic workload. Results are shown and saved
 * to n84bench.txt(.tns).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/platform.h"
#include "cpu/z80/z80.h"
#include "cpu/ez80/ez80.h"
#ifdef N84_NSPIRE
#include <os.h>
#include <libndls.h>
#define RESULT_FILE "n84bench.txt.tns"
#define STEP_FILE "n84bench.log.tns"
#else
#define RESULT_FILE "n84bench.txt"
#define STEP_FILE "n84bench.log"
#endif

/* Progress log written (and closed) before each risky step, so that if the
 * calculator hangs or resets we know which step was running. */
static void step(const char *what) {
    FILE *f = plat_fopen(STEP_FILE, "a");
    if (f) { fprintf(f, "%s\n", what); fclose(f); }
}

/* Synthetic workload: table walk + call/ret + LDIR block copy (see tools/) */
static const uint8_t bench_prog[] = {
    0x31, 0xF0, 0xFF,       /* 0000 ld sp,FFF0h      */
    0x06, 0x00,             /* 0003 ld b,0     outer */
    0x21, 0x00, 0x80,       /* 0005 ld hl,8000h loop */
    0x11, 0x00, 0x90,       /* 0008 ld de,9000h      */
    0x7E,                   /* 000B ld a,(hl)        */
    0x80,                   /* 000C add a,b          */
    0x12,                   /* 000D ld (de),a        */
    0x23,                   /* 000E inc hl           */
    0xCD, 0x21, 0x00,       /* 000F call 0021h       */
    0x10, 0xF1,             /* 0012 djnz 0005h       */
    0x21, 0x00, 0x80,       /* 0014 ld hl,8000h      */
    0x11, 0x00, 0xA0,       /* 0017 ld de,A000h      */
    0x01, 0x40, 0x00,       /* 001A ld bc,64         */
    0xED, 0xB0,             /* 001D ldir             */
    0x18, 0xE2,             /* 001F jr 0003h         */
    0xC5,                   /* 0021 push bc          */
    0x4F,                   /* 0022 ld c,a           */
    0xAF,                   /* 0023 xor a            */
    0xCB, 0x11,             /* 0024 rl c             */
    0x89,                   /* 0026 adc a,c          */
    0xC1,                   /* 0027 pop bc           */
    0xC9,                   /* 0028 ret              */
};

static char report[1024];
static size_t rlen;

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void out(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(report + rlen, sizeof report - rlen, fmt, ap);
    va_end(ap);
    if (n > 0) rlen += (size_t)n < sizeof report - rlen ? (size_t)n : sizeof report - rlen - 1;
}

static void dummy_write(void *c, uint16_t a, uint8_t v) { (void)c; (void)a; (void)v; }
static uint8_t dummy_in(void *c, uint16_t p) { (void)c; (void)p; return 0xff; }
static void dummy_out(void *c, uint16_t p, uint8_t v) { (void)c; (void)p; (void)v; }
static uint8_t ez_rslow(void *c, uint32_t a) { (void)c; (void)a; return 0xff; }
static void ez_wslow(void *c, uint32_t a, uint8_t v) { (void)c; (void)a; (void)v; }

int main(int argc, char **argv) {
    if (!plat_init(argc, argv)) return 1;
    { FILE *f = plat_fopen(STEP_FILE, "w"); if (f) fclose(f); }
    step("start");

#ifdef N84_NSPIRE
    out("hwtype=%u subtype=%u lcd_type=%d\n", hwtype(), nl_hwsubtype(), (int)lcd_type());
#endif

    step("timer");
    /* 1. Timer sanity */
    uint32_t t0 = plat_ticks();
    volatile uint32_t spin = 0;
    for (uint32_t i = 0; i < 2000000; i++) spin += i;
    uint32_t t1 = plat_ticks();
    out("timer: %lu ticks over busy loop %s\n", (unsigned long)(t1 - t0),
        t1 != t0 ? "OK" : "FAIL (not counting)");

    /* 2. Large allocations */
    size_t sizes[] = { 4u << 20, 8u << 20, 16u << 20 };
    for (unsigned i = 0; i < 3; i++) {
        step(i == 0 ? "malloc 4MB" : i == 1 ? "malloc 8MB" : "malloc 16MB");
        void *p = malloc(sizes[i]);
        out("malloc %2uMB: %s\n", (unsigned)(sizes[i] >> 20), p ? "OK" : "FAIL");
        if (p) { memset(p, 0xA5, sizes[i]); free(p); }
    }

    /* 3. Video: colour bars */
    step("lcd_init");
    bool vid = plat_video_init();
    out("lcd_init: %s\n", vid ? "OK" : "FAIL");
    if (vid) {
        uint16_t *fb = plat_video_buffer();
        static const uint16_t bars[8] = { 0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000 };
        for (int y = 0; y < PLAT_SCREEN_H; y++)
            for (int x = 0; x < PLAT_SCREEN_W; x++)
                fb[y * PLAT_SCREEN_W + x] = bars[x / (PLAT_SCREEN_W / 8)];
        step("blit");
        uint32_t b0 = plat_ticks();
        for (int i = 0; i < 60; i++) plat_video_present();
        uint32_t b1 = plat_ticks();
        if (b1 != b0)
            out("blit: %lu fps\n", (unsigned long)(60ull * PLAT_TICKS_PER_SEC / (b1 - b0)));
    }

    step("z80");
    /* 4. Z80 throughput */
    static uint8_t mem[0x10000];
    static z80_t z;
    memcpy(mem, bench_prog, sizeof bench_prog);
    z80_init(&z);
    for (int i = 0; i < 4; i++) { z.rd[i] = mem + i * Z80_PAGE_SIZE; z.wr[i] = mem + i * Z80_PAGE_SIZE; }
    z.ctx = &z; z.write_slow = dummy_write; z.port_in = dummy_in; z.port_out = dummy_out;

    uint32_t start = plat_ticks(), now;
    uint64_t cyc = 0;
    do {
        cyc += (uint64_t)z80_run(&z, 100000);
        now = plat_ticks();
    } while (now - start < 2 * PLAT_TICKS_PER_SEC && cyc < 20000000000ull);
    uint32_t el = now - start;
    if (el) {
        unsigned long khz = (unsigned long)(cyc * PLAT_TICKS_PER_SEC / el / 1000u);
        out("Z80: %lu.%02lu MHz equiv (%lu%% of 15MHz)\n", khz / 1000, (khz % 1000) / 10,
            khz / 150);
    }

    step("ez80");
    /* 5. eZ80 throughput: same loop in ADL mode (24-bit pointers) running from
     * RAM at D00000, as CE programs do. Every memory access costs 3 cycles
     * (CEmu: RAM ~2-4, cached flash 2-3), so the result is comparable with
     * the real 48 MHz CPU. */
    {
        static ez80_t e;
        static const uint8_t prog[] = {
            0x31, 0x00, 0xF0, 0xD0,   /* 00 ld sp,D0F000h       */
            0x06, 0x00,               /* 04 ld b,0        outer */
            0x21, 0x00, 0x80, 0xD0,   /* 06 ld hl,D08000h loop  */
            0x11, 0x00, 0x90, 0xD0,   /* 0A ld de,D09000h       */
            0x7E,                     /* 0E ld a,(hl)           */
            0x80,                     /* 0F add a,b             */
            0x12,                     /* 10 ld (de),a           */
            0x23,                     /* 11 inc hl              */
            0xCD, 0x2A, 0x00, 0xD0,   /* 12 call D0002Ah        */
            0x10, 0xEE,               /* 16 djnz 06h            */
            0x21, 0x00, 0x80, 0xD0,   /* 18 ld hl,D08000h       */
            0x11, 0x00, 0xA0, 0xD0,   /* 1C ld de,D0A000h       */
            0x01, 0x40, 0x00, 0x00,   /* 20 ld bc,64            */
            0xED, 0xB0,               /* 24 ldir                */
            0x18, 0xDC,               /* 26 jr 04h              */
            0x00, 0x00,               /* 28 (pad)               */
            0xC5,                     /* 2A push bc             */
            0x4F,                     /* 2B ld c,a              */
            0xAF,                     /* 2C xor a               */
            0xCB, 0x11,               /* 2D rl c                */
            0x89,                     /* 2F adc a,c             */
            0xC1,                     /* 30 pop bc              */
            0xC9,                     /* 31 ret                 */
        };
        ez80_init(&e);
        for (unsigned i = 0; i < EZ80_NUM_PAGES; i++) { e.rd[i] = e.wr[i] = NULL; e.cost[i] = e.wcost[i] = 3; }
        for (unsigned i = 0; i < 16; i++) e.rd[0xD00 + i] = e.wr[0xD00 + i] = mem + i * EZ80_PAGE_SIZE;
        memset(mem, 0, sizeof mem);
        memcpy(mem, prog, sizeof prog);
        e.ctx = &e; e.read_slow = ez_rslow; e.write_slow = ez_wslow; e.port_in = dummy_in; e.port_out = dummy_out;
        e.adl = 1; e.pc = 0xD00000;
        uint32_t s0 = plat_ticks(), s1;
        uint64_t ecyc = 0;
        do {
            ecyc += (uint64_t)ez80_run(&e, 100000);
            s1 = plat_ticks();
        } while (s1 - s0 < 2 * PLAT_TICKS_PER_SEC && ecyc < 20000000000ull);
        if (s1 != s0) {
            unsigned long khz = (unsigned long)(ecyc * PLAT_TICKS_PER_SEC / (s1 - s0) / 1000u);
            out("eZ80: %lu.%02lu MHz equiv (%lu%% of 48MHz)\n", khz / 1000, (khz % 1000) / 10, khz / 480);
        }
    }

    step("lcd shutdown");
    plat_video_shutdown();
    step("write results");

    FILE *f = plat_fopen(RESULT_FILE, "w");
    if (f) { fputs(report, f); fclose(f); }
    plat_log("%s", report);
    plat_msgbox("N84 bench", report);
    step("done");
    plat_shutdown();
    return 0;
}
