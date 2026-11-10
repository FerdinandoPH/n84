/*
 * N84 - ARM926 instruction timing micro-benchmarks (n84bench)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Each test runs a loop of 8 copies of one instruction pattern plus SUBS/BNE
 * (4 cycles) and reports cycles per pattern, assuming the 396 MHz clock that
 * the 36-cycle calibration loop confirms. The figures calibrate the cycle
 * model in tools/arm926sim, which Firebird cannot provide.
 */
#if defined(__arm__)
#include <stdint.h>
#include "platform/platform.h"
#include "frontend/bench_arm.h"

#ifndef BENCH_ARM_ITERS
#define BENCH_ARM_ITERS 500000u
#endif
#define ITERS BENCH_ARM_ITERS
#define CPU_HZ 396000000ull
#define NOINLINE_BENCH __attribute__((noinline))

/* r4 = pointer into a static (bss) buffer, r5 = 0, r6 = pointer into a stack
 * buffer, r7 = pointer to a word holding r4 (for load-to-base chains) */
#define ARM_TEST(name, body)                                                       \
    static uint32_t name(uint8_t *bss, uint8_t *stk, uint32_t *chain) {            \
        uint32_t n = ITERS;                                                        \
        uint32_t t0 = plat_ticks();                                                \
        __asm__ volatile(                                                          \
            "mov r4, %1\n mov r5, #0\n mov r6, %2\n mov r7, %3\n"                   \
            "2:\n"                                                                 \
            ".rept 8\n" body "\n.endr\n"                                           \
            "subs %0, %0, #1\n"                                                    \
            "bne 2b\n"                                                             \
            : "+r"(n) : "r"(bss), "r"(stk), "r"(chain)                             \
            : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "cc", "memory");      \
        return plat_ticks() - t0;                                                  \
    }

ARM_TEST(t_alu,       "add r0, r0, #1")
ARM_TEST(t_ldr_use,   "ldr r0, [r4]\n add r1, r0, #1")
ARM_TEST(t_ldrb_use,  "ldrb r0, [r4]\n add r1, r0, #1")
ARM_TEST(t_ldrb_gap,  "ldrb r0, [r4]\n add r2, r2, #1\n add r1, r0, #1")
ARM_TEST(t_ldr_free,  "ldr r0, [r4]\n add r1, r1, #1")
ARM_TEST(t_ldr_sp,    "ldr r0, [r6]\n add r1, r1, #1")
ARM_TEST(t_ldr_lsl2,  "ldr r0, [r4, r5, lsl #2]\n add r1, r1, #1")
ARM_TEST(t_ldrb_reg,  "ldrb r0, [r4, r5]\n add r1, r1, #1")
ARM_TEST(t_ldr_lsr,   "ldr r0, [r4, r5, lsr #12]\n add r1, r1, #1")
ARM_TEST(t_ldr_base,  "ldr r0, [r7]\n ldr r1, [r0]")
ARM_TEST(t_alu_shop,  "add r0, r0, #1\n add r1, r1, r0, lsl #2")
ARM_TEST(t_alu_shreg, "add r1, r1, r0, lsl r5")
ARM_TEST(t_str,       "str r0, [r4]")
ARM_TEST(t_strb_seq,  "strb r0, [r4, #0]\n strb r0, [r4, #1]\n strb r0, [r4, #2]\n strb r0, [r4, #3]")
ARM_TEST(t_strb_sp,   "strb r0, [r6, #0]\n strb r0, [r6, #1]\n strb r0, [r6, #2]\n strb r0, [r6, #3]")
ARM_TEST(t_b,         "b 1f\n 1:")
ARM_TEST(t_addpc,     "add pc, pc, r5, lsl #2\n nop")
ARM_TEST(t_ldrpc,     "ldr pc, [pc, #-4]\n .word 1f\n 1:")
ARM_TEST(t_ldrh_jmp,  "ldrh r0, [r4]\n add pc, pc, r0, lsl #2\n nop")
ARM_TEST(t_pushpop,   "push {r0-r3}\n pop {r0-r3}")

static uint8_t bss_buf[64] __attribute__((aligned(32)));
static uint32_t chain_word;

/* cycles per pattern x10 */
static unsigned per_pattern(uint32_t ticks) {
    uint64_t cyc = (uint64_t)ticks * CPU_HZ / PLAT_TICKS_PER_SEC;
    uint64_t per_iter_x10 = cyc * 10 / ITERS;
    return per_iter_x10 > 40 ? (unsigned)((per_iter_x10 - 40) / 8) : 0;
}

static const struct {
    const char *name;
    uint32_t (*fn)(uint8_t *, uint8_t *, uint32_t *);
} tests[] = {
    { "alu", t_alu }, { "ldr+use", t_ldr_use }, { "ldrb+use", t_ldrb_use },
    { "ldrb+1+use", t_ldrb_gap }, { "ldr", t_ldr_free }, { "ldr sp", t_ldr_sp },
    { "ldr[r,r,lsl2]", t_ldr_lsl2 }, { "ldrb[r,r]", t_ldrb_reg }, { "ldr[r,r,lsr]", t_ldr_lsr },
    { "ldr>base", t_ldr_base }, { "alu>shift", t_alu_shop }, { "shift reg", t_alu_shreg },
    { "str", t_str }, { "4strb", t_strb_seq }, { "4strb sp", t_strb_sp },
    { "b", t_b }, { "add pc", t_addpc }, { "ldr pc", t_ldrpc }, { "ldrh+add pc", t_ldrh_jmp },
    { "push/pop4", t_pushpop },
};

unsigned bench_arm_count(void) { return sizeof tests / sizeof tests[0]; }
const char *bench_arm_name(unsigned i) { return tests[i].name; }

/* run test i (ITERS iterations); returns elapsed timer ticks */
uint32_t bench_arm_run(unsigned i) {
    uint8_t stk_buf[64] __attribute__((aligned(32)));
    for (unsigned k = 0; k < 64; k++) bss_buf[k] = stk_buf[k] = 0;
    chain_word = (uint32_t)(uintptr_t)bss_buf;
    return tests[i].fn(bss_buf, stk_buf, &chain_word);
}

#if defined(N84_NSPIRE) && !defined(N84_ARMSIM)
#include <stdlib.h>
/* Strided loads over a buffer: same code for every case, so the differences
 * give the cost of a D-cache miss (32-byte stride over 64 KiB, 16 pages) and
 * of a D-cache + TLB miss (4 KiB + 32 stride over 1 MiB, 256 pages). */
static NOINLINE_BENCH uint32_t stride_loads(const uint8_t *buf, uint32_t stride, uint32_t mask, uint32_t n) {
    uint32_t sum = 0, off = 0;
    for (uint32_t i = 0; i < n; i++) {
        sum += *(const volatile uint32_t *)(buf + off);
        off = (off + stride) & mask;
    }
    return sum;
}

static void bench_mem(void (*out)(const char *fmt, ...)) {
    uint8_t *buf = malloc((1u << 20) + 4096);
    if (!buf) return;
    for (uint32_t i = 0; i < (1u << 20); i += 32) buf[i] = (uint8_t)i;
    static const struct { const char *name; uint32_t stride, mask; } c[] = {
        { "hit", 0, 0 }, { "dmiss", 32, 0xFFFF }, { "dmiss+tlb", 4096 + 32, 0xFFFFF },
    };
    const uint32_t n = 400000;
    unsigned base = 0;
    out("load cost (cycles):");
    for (unsigned i = 0; i < 3; i++) {
        stride_loads(buf, c[i].stride, c[i].mask, n / 8);         /* warm up */
        uint32_t t0 = plat_ticks();
        stride_loads(buf, c[i].stride, c[i].mask, n);
        uint32_t t = plat_ticks() - t0;
        unsigned v = (unsigned)((uint64_t)t * CPU_HZ / PLAT_TICKS_PER_SEC * 10 / n);
        if (i == 0) base = v;
        out(" %s %u.%u", c[i].name, (v - (i ? base : 0)) / 10, (v - (i ? base : 0)) % 10);
        if (i == 0) out(" (loop) |");
    }
    out("\n");
    free(buf);
}
#endif

void bench_arm(void (*out)(const char *fmt, ...)) {
    out("ARM cycles per pattern:\n");
    unsigned n = bench_arm_count();
    for (unsigned i = 0; i < n; i++) {
        unsigned v = per_pattern(bench_arm_run(i));
        out("%s %u.%u%s", tests[i].name, v / 10, v % 10, (i + 1) % 3 && i + 1 < n ? " | " : "\n");
    }
#if defined(N84_NSPIRE) && !defined(N84_ARMSIM)
    bench_mem(out);
#endif
}
#endif
