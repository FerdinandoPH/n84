/* N84 - Nspire -> TI-84 key mapping. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef N84_KEYMAP_TI8X_H
#define N84_KEYMAP_TI8X_H
#include <stddef.h>
#include "platform/platform.h"
#include "machine/ti8x/ti8x.h"

/* Optional key file next to n84.tns. Each line maps one Nspire key to one
 * TI-84 key ("ctrl 2ND", "ctrl = 2ND", "a none"); '#' starts a comment.
 * Lines override the default map; keys not listed keep their default. */
#define KEYMAP_FILE "n84.keys.tns"

/* Restores the default map. */
void keymap_reset(void);

/* Applies `path` over the default map. Returns 0 if there is no such file,
 * 1 if it was applied, -1 if some line was wrong (`err` then describes the
 * first one; the valid lines are still applied). */
int keymap_load(const char *path, char *err, size_t err_size);

/* True when the map in use came from the key file. */
bool keymap_custom(void);

/* TI-84 key (TK_*) for an Nspire key, or TK_NONE. */
uint8_t keymap_get(plat_key_t pk);

/* Names as written in the key file and shown in the key guide. */
const char *keymap_pk_name(plat_key_t pk);
const char *keymap_tk_label(uint8_t tk);

/* Writes the map in use in the key file format (same layout as the
 * n84.keys.tns shipped with releases). */
void keymap_write(FILE *f);

/* Reads the latched platform key state into down[TK code] (0x41 entries,
 * TK_ON included). Shared by the TI-84 Plus and TI-84 Plus CE frontends:
 * the CE keypad matrix is the TI-84 one with the rows reversed. */
void ti8x_keymap_scan(bool down[0x41]);

/* Reads the latched platform key state and updates the emulated keypad. */
void ti8x_keymap_apply(ti8x_t *m);

#endif
