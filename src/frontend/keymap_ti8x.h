/* N84 - Nspire -> TI-84 key mapping. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef N84_KEYMAP_TI8X_H
#define N84_KEYMAP_TI8X_H
#include "platform/platform.h"
#include "machine/ti8x/ti8x.h"

/* Reads the latched platform key state into down[TK code] (0x41 entries,
 * TK_ON included). Shared by the TI-84 Plus and TI-84 Plus CE frontends:
 * the CE keypad matrix is the TI-84 one with the rows reversed. */
void ti8x_keymap_scan(bool down[0x41]);

/* Reads the latched platform key state and updates the emulated keypad. */
void ti8x_keymap_apply(ti8x_t *m);

#endif
