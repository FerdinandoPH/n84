/*
 * N84 - replacement for CEmu's emu.h (frontend callbacks).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef EMU_H
#define EMU_H
#include "schedule.h"
#include "bootver.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef CE_LOG
#define gui_console_printf(...)     CE_LOG(__VA_ARGS__)
#define gui_console_err_printf(...) CE_LOG(__VA_ARGS__)
#else
#define gui_console_printf(...)     ((void)0)
#define gui_console_err_printf(...) ((void)0)
#endif
#define gui_console_clear()         ((void)0)

#endif
