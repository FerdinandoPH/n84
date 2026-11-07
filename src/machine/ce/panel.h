/*
 * N84 - LCD panel (ST7789-like) stub for the CEmu-derived TI-84 Plus CE machine.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * N84 draws frames straight from the LCD controller's frame buffer (CEmu's
 * non-DMA path), so the SPI-attached panel is not emulated: commands sent to
 * it are accepted and ignored, and reads return zero.
 */
#ifndef N84_CE_PANEL_H
#define N84_CE_PANEL_H
#include <stdint.h>
#include <stdbool.h>
typedef struct panel_state { bool skipFrame, gammaDirty, accurateGamma; } panel_state_t;
extern panel_state_t panel;
static inline void panel_hw_reset(void) {}
static inline void panel_vsync(void) {}
static inline void panel_reset(void) {}
static inline void init_panel(void) {}
void panel_spi_select(bool low);
uint8_t panel_spi_peek(uint32_t *rxData);
uint8_t panel_spi_transfer(uint32_t txData, uint32_t *rxData);
#endif
