/*
 * N84 - file browser
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_BROWSER_H
#define N84_BROWSER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Lets the user pick a TI variable file (.8xp, .8xv, ...), starting in the
 * last folder used. Returns true with the full path in out. `backdrop` as in
 * ui_list (NULL for full screen). */
bool browser_pick(const char *title, char *out, size_t size, const uint16_t *backdrop);

#endif
