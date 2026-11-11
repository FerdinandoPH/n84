/*
 * N84 - diagnostic and benchmark (n84bench.tns)
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
#include "frontend/bench_progs.h"
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

static char report[4096];
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

#if defined(__arm__)
    step("arm clock");
    /* 1b. ARM clock: 32 independent ALU ops + SUBS + taken BNE = 36 cycles per
     * iteration on the ARM926 (1 each, 3 for the branch). Confirms the CPU
     * clock and the timer, so the emulator figures below can be read as ARM
     * cycles per emulated instruction. */
    {
        uint32_t c0 = plat_ticks();
        uint32_t n = 4000000;
        __asm__ volatile(
            "1:\n"
            ".rept 8\n"
            "add r4, r4, #1\n add r5, r5, #1\n add r6, r6, #1\n add r7, r7, #1\n"
            ".endr\n"
            "subs %0, %0, #1\n"
            "bne 1b\n"
            : "+r"(n) : : "r4", "r5", "r6", "r7", "cc");
        uint32_t c1 = plat_ticks();
        if (c1 != c0)
            out("ARM: %lu MHz (36-cycle loop)\n",
                (unsigned long)(4000000ull * 36 * PLAT_TICKS_PER_SEC / (c1 - c0) / 1000000u));
    }
#endif

    step("z80");
    /* 4. Z80 throughput */
    static uint8_t mem[0x10000];
    static z80_t z;
    bench_setup_z80(&z, mem);

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
    /* 5. eZ80 throughput: same loop in ADL mode (see bench_progs.h), once per
     * memory placement */
    {
        static uint8_t mem_arena[0x10000 + BENCH_ARENA_ALIGN] __attribute__((aligned(BENCH_ARENA_ALIGN)));
        static uint8_t cpu_arena[sizeof(ez80_t) + BENCH_ARENA_ALIGN] __attribute__((aligned(BENCH_ARENA_ALIGN)));
        unsigned long best = 0, worst = ~0ul, sum = 0;
        out("eZ80 %% per placement:");
        for (unsigned pl = 0; pl < BENCH_PLACEMENTS; pl++) {
            ez80_t *e = (ez80_t *)(cpu_arena + bench_placements[pl][1]);
            bench_setup_ez80(e, mem_arena + bench_placements[pl][0]);
            uint32_t s0 = plat_ticks(), s1;
            uint64_t ecyc = 0;
            do {
                ecyc += (uint64_t)ez80_run(e, 100000);
                s1 = plat_ticks();
            } while (s1 - s0 < PLAT_TICKS_PER_SEC / 2);
            unsigned long khz = (unsigned long)(ecyc * PLAT_TICKS_PER_SEC / (s1 - s0) / 1000u);
            out(" %lu", khz / 480);
            if (khz > best) best = khz;
            if (khz < worst) worst = khz;
            sum += khz;
        }
        unsigned long avg = sum / BENCH_PLACEMENTS;
        out("\neZ80: %lu.%02lu MHz equiv (%lu%% of 48MHz; %lu-%lu%%)\n", avg / 1000, (avg % 1000) / 10,
            avg / 480, worst / 480, best / 480);
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
