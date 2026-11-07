/*
 * N84 - minimal UI toolkit
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <string.h>
#include "frontend/ui.h"
#include "frontend/font8x13.h"
#include "platform/platform.h"

#define ROW_H    16
#define HEADER_H 18
#define FOOTER_H 16

static uint16_t *fb(void) { return plat_video_buffer(); }

void ui_clear(uint16_t color) {
    uint16_t *p = fb();
    for (int i = 0; i < UI_W * UI_H; i++) p[i] = color;
}

void ui_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > UI_W) w = UI_W - x;
    if (y + h > UI_H) h = UI_H - y;
    if (w <= 0 || h <= 0) return;
    uint16_t *p = fb() + y * UI_W + x;
    for (int j = 0; j < h; j++, p += UI_W)
        for (int i = 0; i < w; i++) p[i] = color;
}

void ui_frame(int x, int y, int w, int h, uint16_t color) {
    ui_rect(x, y, w, 1, color);
    ui_rect(x, y + h - 1, w, 1, color);
    ui_rect(x, y, 1, h, color);
    ui_rect(x + w - 1, y, 1, h, color);
}

int ui_text(int x, int y, const char *s, uint16_t color) {
    uint16_t *base = fb();
    for (; *s; s++, x += FONT_W) {
        unsigned c = (unsigned char)*s;
        if (c < FONT_FIRST || c > FONT_LAST) c = '?';
        if (x + FONT_W > UI_W) break;
        if (x < 0) continue;
        const uint8_t *g = font_data[c - FONT_FIRST];
        for (int r = 0; r < FONT_H; r++) {
            int yy = y + r;
            if (yy < 0 || yy >= UI_H) continue;
            uint8_t bits = g[r];
            uint16_t *p = base + yy * UI_W + x;
            for (int b = 0; b < 8; b++)
                if (bits & (0x80 >> b)) p[b] = color;
        }
    }
    return x;
}

int ui_text_width(const char *s) { return (int)strlen(s) * FONT_W; }
int ui_line_height(void) { return FONT_H; }

void ui_dim(uint16_t *p) {
    /* halve each channel: (c >> 1) with the top bit of each field cleared */
    for (int i = 0; i < UI_W * UI_H; i++) p[i] = (uint16_t)((p[i] >> 1) & 0x7BEF);
}

void ui_header(const char *title, const char *right) {
    ui_rect(0, 0, UI_W, HEADER_H, UI_BAR);
    ui_text(6, 3, title, UI_FG);
    if (right) ui_text(UI_W - 6 - ui_text_width(right), 3, right, UI_FG);
}

void ui_footer(const char *help) {
    ui_rect(0, UI_H - FOOTER_H, UI_W, FOOTER_H, UI_PANEL);
    if (help) ui_text(6, UI_H - FOOTER_H + 2, help, UI_DIM);
}

void ui_present(void) { plat_video_present(); }

/* ---- input ---------------------------------------------------------- */

static uint32_t next_tick;
static bool prev[8];
static int held[8];

ui_action_t ui_poll(void) {
    uint32_t now = plat_ticks();
    if ((int32_t)(next_tick - now) > 0) plat_wait_until(next_tick);
    else next_tick = now;
    next_tick += PLAT_TICKS_PER_SEC / 60;
    plat_input_poll();

    bool cur[8];
    cur[UI_UP] = plat_key_down(PK_UP) || plat_key_down(PK_8);
    cur[UI_DOWN] = plat_key_down(PK_DOWN) || plat_key_down(PK_2);
    cur[UI_LEFT] = plat_key_down(PK_LEFT) || plat_key_down(PK_4);
    cur[UI_RIGHT] = plat_key_down(PK_RIGHT) || plat_key_down(PK_6);
    cur[UI_OK] = plat_key_down(PK_ENTER) || plat_key_down(PK_RET) || plat_key_down(PK_CLICK);
    cur[UI_BACK] = plat_key_down(PK_ESC);
    cur[UI_MENU] = plat_key_down(PK_QUESEXCL) || plat_key_down(PK_MENU);
    cur[UI_NONE] = false;

    ui_action_t result = UI_NONE;
    for (int a = UI_UP; a <= UI_MENU; a++) {
        if (cur[a] && !prev[a]) {
            if (result == UI_NONE) result = (ui_action_t)a;
            held[a] = 0;
        } else if (cur[a] && a <= UI_RIGHT) {
            /* auto-repeat for arrows: after 300 ms, every ~66 ms */
            if (++held[a] >= 18 && (held[a] - 18) % 4 == 0 && result == UI_NONE) result = (ui_action_t)a;
        }
        prev[a] = cur[a];
    }
    return result;
}

void ui_wait_release(void) {
    for (;;) {
        plat_input_poll();
        bool any = false;
        for (int k = 1; k < PK_COUNT && !any; k++) any = plat_key_down((plat_key_t)k);
        if (!any && !plat_on_down()) break;
        plat_wait_until(plat_ticks() + PLAT_TICKS_PER_SEC / 60);
    }
    memset(prev, 0, sizeof prev);
}

/* ---- widgets -------------------------------------------------------- */

static void draw_backdrop(const uint16_t *backdrop) {
    uint16_t *p = fb();
    memcpy(p, backdrop, UI_W * UI_H * sizeof *p);
    ui_dim(p);
}

int ui_list(const ui_list_t *l, int *menu_out) {
    int sel = l->selected < 0 ? 0 : l->selected;
    if (sel >= l->count) sel = l->count ? l->count - 1 : 0;
    int top = 0;

    /* geometry */
    int x, y, w, rows;
    if (l->backdrop) {
        int maxrows = (UI_H - 40 - HEADER_H) / ROW_H;
        rows = l->count < maxrows ? (l->count ? l->count : 1) : maxrows;
        w = 248;
        int h = HEADER_H + rows * ROW_H + 8;
        x = (UI_W - w) / 2;
        y = (UI_H - h) / 2 + HEADER_H;
    } else {
        rows = (UI_H - HEADER_H - FOOTER_H - 4) / ROW_H;
        x = 0; y = HEADER_H + 2; w = UI_W;
    }

    ui_wait_release();
    for (;;) {
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;

        if (l->backdrop) {
            draw_backdrop(l->backdrop);
            ui_rect(x - 2, y - HEADER_H - 2, w + 4, HEADER_H + rows * ROW_H + 12, UI_PANEL);
            ui_frame(x - 2, y - HEADER_H - 2, w + 4, HEADER_H + rows * ROW_H + 12, UI_BAR);
            ui_rect(x - 1, y - HEADER_H - 1, w + 2, HEADER_H - 1, UI_BAR);
            ui_text(x + 6, y - HEADER_H + 2, l->title, UI_FG);
        } else {
            ui_clear(UI_BG);
            ui_header(l->title, "N84");
            ui_footer(l->footer);
        }
        if (!l->count && l->empty) ui_text(x + 8, y + 4, l->empty, UI_DIM);
        for (int r = 0; r < rows && top + r < l->count; r++) {
            int i = top + r, ry = y + r * ROW_H + 2;
            if (i == sel) ui_rect(x + 2, ry, w - 4, ROW_H, UI_SEL);
            ui_text(x + 8, ry + 2, l->items[i], UI_FG);
            if (l->detail && l->detail[i]) {
                const char *d = l->detail[i];
                ui_text(x + w - 8 - ui_text_width(d), ry + 2, d, i == sel ? UI_FG : UI_DIM);
            }
        }
        /* scroll indicators */
        if (top > 0) ui_text(x + w - 16, y - 2, "^", UI_ACCENT);
        if (top + rows < l->count) ui_text(x + w - 16, y + rows * ROW_H - 8, "v", UI_ACCENT);
        ui_present();

        switch (ui_poll()) {
        case UI_UP:   if (l->count) sel = sel > 0 ? sel - 1 : l->count - 1; break;
        case UI_DOWN: if (l->count) sel = sel + 1 < l->count ? sel + 1 : 0; break;
        case UI_LEFT: sel = sel - rows > 0 ? sel - rows : 0; break;
        case UI_RIGHT: if (l->count) sel = sel + rows < l->count ? sel + rows : l->count - 1; break;
        case UI_OK:   if (l->count) { ui_wait_release(); return sel; } break;
        case UI_BACK: ui_wait_release(); return -1;
        case UI_MENU:
            if (menu_out) { *menu_out = sel; ui_wait_release(); return -2; }
            if (l->backdrop) { ui_wait_release(); return -1; }  /* ?!> again closes */
            break;
        default: break;
        }
    }
}

/* word-wrap `text` into lines of at most `cols` characters */
static int wrap(const char *text, int cols, char lines[][48], int maxlines) {
    int n = 0;
    const char *p = text;
    while (*p && n < maxlines) {
        int len = 0, cut = -1;
        while (p[len] && p[len] != '\n' && len < cols) {
            if (p[len] == ' ') cut = len;
            len++;
        }
        if (p[len] && p[len] != '\n' && cut > 0) len = cut;
        memcpy(lines[n], p, (size_t)len);
        lines[n][len] = 0;
        n++;
        p += len;
        if (*p == '\n' || *p == ' ') p++;
    }
    return n;
}

static bool dialog(const char *title, const char *text, const uint16_t *backdrop, bool confirm) {
    char lines[12][48];
    int n = wrap(text, 34, lines, 12);
    int w = 300, h = HEADER_H + n * (FONT_H + 2) + 34;
    int x = (UI_W - w) / 2, y = (UI_H - h) / 2;
    static uint16_t saved[UI_W * UI_H];
    if (backdrop) memcpy(saved, backdrop, sizeof saved);
    else memcpy(saved, fb(), sizeof saved);

    ui_wait_release();
    for (;;) {
        draw_backdrop(saved);
        ui_rect(x, y, w, h, UI_PANEL);
        ui_frame(x, y, w, h, UI_BAR);
        ui_rect(x + 1, y + 1, w - 2, HEADER_H - 1, UI_BAR);
        ui_text(x + 6, y + 3, title, UI_FG);
        for (int i = 0; i < n; i++) ui_text(x + 10, y + HEADER_H + 6 + i * (FONT_H + 2), lines[i], UI_FG);
        const char *help = confirm ? "enter: yes   esc: no" : "enter: OK";
        ui_text(x + w - 10 - ui_text_width(help), y + h - 18, help, UI_ACCENT);
        ui_present();
        ui_action_t a = ui_poll();
        if (a == UI_OK) { ui_wait_release(); return true; }
        if (a == UI_BACK || (a == UI_MENU && !confirm)) { ui_wait_release(); return false; }
    }
}

void ui_message(const char *title, const char *text, const uint16_t *backdrop) {
    (void)dialog(title, text, backdrop, false);
}

bool ui_confirm(const char *title, const char *text, const uint16_t *backdrop) {
    return dialog(title, text, backdrop, true);
}
