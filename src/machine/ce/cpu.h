/*
 * N84 - CPU interface seen by the CEmu-derived peripherals.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Replaces CEmu's cpu.h. The peripherals only need the cycle counter, the
 * next-event mechanism and a few hooks; the CPU itself is N84's eZ80 core.
 * cpu.z.cycles is CEmu's cpu.cycles (CPU cycles since the start of the
 * current emulated second) and cpu.z.cycle_target plays the role of
 * cpu.next.
 */
#ifndef CPU_H
#define CPU_H
#include "defines.h"
#include "cpu/ez80/ez80.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define cpu_mask_mode(address, mode) ((uint32_t)((address) & ((mode) ? 0xFFFFFF : 0xFFFF)))

typedef enum eZ80signal {
    CPU_SIGNAL_NONE = 0,
    CPU_SIGNAL_EXIT = 1 << 0,
    CPU_SIGNAL_RESET = 1 << 1,
    CPU_SIGNAL_ON_KEY = 1 << 2,
    CPU_SIGNAL_ANY_KEY = 1 << 3
} eZ80signal_t;

typedef struct ce_cpu {
    ez80_t z;
    uint32_t seconds, eiDelay;
    uint64_t baseCycles, haltCycles, dmaCycles;
    uint32_t flashTotalAccesses, flashCacheMisses;
    int64_t flashDelayCycles;
    uint8_t signals;
} ce_cpu_t;

extern ce_cpu_t cpu;

void cpu_restore_next(void);
void cpu_nmi(void);
void cpu_crash(const char *msg);
void cpu_set_signal(uint8_t signal);
uint8_t cpu_clear_signals(void);
uint32_t cpu_address_mode(uint32_t address, bool mode);

#endif
