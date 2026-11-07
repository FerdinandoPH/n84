/*
 * N84 - reference eZ80 build (generic decoder only) for differential tests.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define EZ80_NO_FAST
#define ez80_init  ref_ez80_init
#define ez80_reset ref_ez80_reset
#define ez80_run   ref_ez80_run
#define ez80_end_timeslice ref_ez80_end_timeslice
#define ez80_nmi   ref_ez80_nmi
#define ez80_step  ref_ez80_step
#include "cpu/ez80/ez80.c"
