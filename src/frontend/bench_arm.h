/*
 * N84 - ARM926 instruction timing micro-benchmarks (n84bench)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_BENCH_ARM_H
#define N84_BENCH_ARM_H
#if defined(__arm__)
#include <stdint.h>
void bench_arm(void (*out)(const char *fmt, ...));
unsigned bench_arm_count(void);
const char *bench_arm_name(unsigned i);
uint32_t bench_arm_run(unsigned i);
#endif
#endif
