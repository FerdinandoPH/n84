/*
 * N84 - TI-84 Plus CE machine (eZ80, CEmu-derived peripherals)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Single instance (CEmu's peripherals use global state).
 */
#ifndef N84_CE_H
#define N84_CE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define CE_LCD_W 320
#define CE_LCD_H 240

/* Load a ROM image (4 MB Flash dump) and reset the machine. */
bool ce_init(const uint8_t *rom, size_t size);
void ce_free(void);
void ce_reset(void);

/* Emulate `ticks` sixtieths of a second. */
void ce_run(uint32_t ticks);

/* Keypad: CEmu row/column numbering; row 2 col 0 is the ON key. */
void ce_key(unsigned row, unsigned col, bool down);
void ce_release_all_keys(void);

/* Current frame as RGB565 (320x240). Returns false (and draws black) when
 * the LCD controller is disabled. */
bool ce_lcd_render(uint16_t *out);
/* Same, only rows [y0, y1); `out` is still the top-left of the frame. */
bool ce_lcd_render_rows(uint16_t *out, unsigned y0, unsigned y1);

/* Number of LCD frames started since reset (for frame pacing / skipping). */
uint32_t ce_lcd_frames(void);

/* Flash contents (for writing back to the ROM file) and a change counter. */
const uint8_t *ce_flash(size_t *size);
uint32_t ce_flash_generation(void);

/* Save states (RAM and hardware state; Flash lives in the ROM file). A failed
 * load leaves the machine inconsistent: call ce_reset() then. */
bool ce_save_state(FILE *f);
bool ce_load_state(FILE *f);

/* Debugging */
uint32_t ce_pc(void);
uint64_t ce_total_cycles(void);

#endif
