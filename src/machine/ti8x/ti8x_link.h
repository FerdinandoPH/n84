/*
 * N84 - sending variables to the TI-84 Plus family
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_TI8X_LINK_H
#define N84_TI8X_LINK_H
#include "machine/ti8x/ti8x.h"
#include "link/tivar.h"

/* Creates the variable on the emulated calculator (runs the machine for a
 * while). Returns NULL on success or an error message. */
const char *ti8x_send_var(ti8x_t *m, const tivar_t *v, bool *archived);

#endif
