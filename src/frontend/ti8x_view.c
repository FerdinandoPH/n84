/*
 * N84 - renders the TI-8x 96x64 LCD into the 320x240 Nspire framebuffer
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "frontend/ti8x_view.h"

#define SCALE 3
#define OFS_X ((PLAT_SCREEN_W - TI8X_LCD_W * SCALE) / 2)
#define OFS_Y ((PLAT_SCREEN_H - TI8X_LCD_H * SCALE) / 2)

static inline uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* Reflective LCD look: pale grey-green background, dark blue-grey pixels.
 * The T6A04 contrast (0..63) scales how dark "on" pixels get. */
static void build_palette(uint16_t pal[256], uint8_t contrast, bool on) {
    const int off_r = 0xC4, off_g = 0xCF, off_b = 0xB4;
    const int on_r = 0x18, on_g = 0x20, on_b = 0x30;
    int k = on ? (int)contrast * 256 / 48 : 0;   /* ~1.0 at contrast 48 */
    if (k > 256) k = 256;
    if (k < 32 && on) k = 32;
    for (int lv = 0; lv < 256; lv++) {
        int f = lv * k / 256;                     /* 0..256 */
        pal[lv] = rgb565((unsigned)(off_r + (on_r - off_r) * f / 256),
                         (unsigned)(off_g + (on_g - off_g) * f / 256),
                         (unsigned)(off_b + (on_b - off_b) * f / 256));
    }
}

void ti8x_view_init(uint16_t *fb) {
    const uint16_t border = rgb565(0x30, 0x34, 0x3C);
    for (int i = 0; i < PLAT_SCREEN_W * PLAT_SCREEN_H; i++) fb[i] = border;
}

void ti8x_view_render(const uint8_t *levels, uint8_t contrast, bool lcd_on, uint16_t *fb) {
    uint16_t pal[256];
    build_palette(pal, contrast, lcd_on);
    for (int y = 0; y < TI8X_LCD_H; y++) {
        uint16_t *d0 = fb + (OFS_Y + y * SCALE) * PLAT_SCREEN_W + OFS_X;
        const uint8_t *src = levels + y * TI8X_LCD_W;
        uint16_t *d = d0;
        for (int x = 0; x < TI8X_LCD_W; x++) {
            uint16_t c = pal[lcd_on ? src[x] : 0];
            d[0] = c; d[1] = c; d[2] = c;
            d += SCALE;
        }
        for (int r = 1; r < SCALE; r++) {
            uint16_t *dr = d0 + r * PLAT_SCREEN_W;
            for (int i = 0; i < TI8X_LCD_W * SCALE; i++) dr[i] = d0[i];
        }
    }
}
