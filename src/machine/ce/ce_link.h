/*
 * N84 - sending variables to the TI-84 Plus CE
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_CE_LINK_H
#define N84_CE_LINK_H
#include "link/tivar.h"

/* Creates the variable on the emulated calculator (runs the machine for a
 * while). Returns NULL on success or an error message. */
const char *ce_send_var(const tivar_t *v, bool *archived);

#endif
