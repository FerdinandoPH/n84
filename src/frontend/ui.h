/*
 * N84 - minimal UI toolkit drawn into the 320x240 RGB565 frame buffer
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_UI_H
#define N84_UI_H

#include <stdint.h>
#include <stdbool.h>

#define UI_W 320
#define UI_H 240
#define UI_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* palette */
#define UI_BG      UI_RGB(0x16, 0x1A, 0x22)
#define UI_PANEL   UI_RGB(0x24, 0x2A, 0x36)
#define UI_BAR     UI_RGB(0x2E, 0x5C, 0x9E)
#define UI_SEL     UI_RGB(0x3A, 0x74, 0xC8)
#define UI_FG      UI_RGB(0xF0, 0xF0, 0xF0)
#define UI_DIM     UI_RGB(0x96, 0x9C, 0xA8)
#define UI_ACCENT  UI_RGB(0xF2, 0xB8, 0x3A)
#define UI_RED     UI_RGB(0xE0, 0x60, 0x60)

typedef enum {
    UI_NONE, UI_UP, UI_DOWN, UI_LEFT, UI_RIGHT, UI_OK, UI_BACK, UI_MENU
} ui_action_t;

/* drawing (into plat_video_buffer()) */
void ui_clear(uint16_t color);
void ui_rect(int x, int y, int w, int h, uint16_t color);
void ui_frame(int x, int y, int w, int h, uint16_t color);
int  ui_text(int x, int y, const char *s, uint16_t color);       /* returns end x */
int  ui_text_width(const char *s);
int  ui_line_height(void);
void ui_dim(uint16_t *fb);                 /* darken a frame (menu overlay) */
void ui_header(const char *title, const char *right);
void ui_footer(const char *help);
void ui_present(void);

/* input: waits for the next frame (~60 Hz), with auto-repeat on arrows */
ui_action_t ui_poll(void);
void ui_wait_release(void);

/* Widgets. Items are plain strings; an optional per-item right-hand text
 * (`detail`, may be NULL) is drawn right-aligned in a dimmer colour.
 * `backdrop` (may be NULL) is a 320x240 frame drawn dimmed behind a centred
 * panel (in-game menu); otherwise the list fills the screen.
 * Returns the chosen index, or -1 if the user backs out. If `menu_out` is
 * not NULL, the MENU action returns -2 and stores the highlighted index. */
typedef struct {
    const char *title;
    const char *const *items;
    const char *const *detail;
    int count;
    int selected;
    const char *footer;
    const char *empty;                 /* text shown when count == 0 */
    const uint16_t *backdrop;
} ui_list_t;
int  ui_list(const ui_list_t *list, int *menu_out);

/* Text dialogs (wrapped). ui_message waits for OK/BACK; ui_confirm returns
 * true on OK. */
void ui_message(const char *title, const char *text, const uint16_t *backdrop);
bool ui_confirm(const char *title, const char *text, const uint16_t *backdrop);

#endif
