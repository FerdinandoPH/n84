/* N84 - TI-8x LCD renderer. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef N84_TI8X_VIEW_H
#define N84_TI8X_VIEW_H
#include <stdbool.h>
#include <stdint.h>
#include "platform/platform.h"
#include "machine/ti8x/ti8x.h"

void ti8x_view_init(uint16_t *fb);   /* paints the border */
void ti8x_view_render(const uint8_t *levels, uint8_t contrast, bool lcd_on, uint16_t *fb);

#endif
