/*
 * N84 - key guide: the Nspire -> TI-84 key map in use, in two columns.
 * Keys that map to the TI-84 key of the same name (digits, arrows, sin...)
 * are left out.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <ctype.h>
#include <stdio.h>
#include "frontend/keyguide.h"
#include "frontend/keymap_ti8x.h"
#include "frontend/ui.h"

#define TOP     22
#define ROW     14
#define ROWS    14                     /* per column */
#define COL_W   160
#define PER_PAGE (2 * ROWS)

static bool same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

void keyguide_show(void) {
    plat_key_t keys[PK_COUNT];
    int n = 0;
    keys[n++] = PK_QUESEXCL;                        /* "?! / ctrl+esc  N84 menu" */
    for (int k = 1; k < PK_COUNT; k++) {
        uint8_t tk = keymap_get((plat_key_t)k);
        if (tk != TK_NONE && !same_name(keymap_pk_name((plat_key_t)k), keymap_tk_label(tk))) keys[n++] = (plat_key_t)k;
    }
    int pages = n ? (n + PER_PAGE - 1) / PER_PAGE : 1, page = 0;

    ui_wait_release();
    for (;;) {
        char right[24], help[64];
        snprintf(right, sizeof right, "%d/%d", page + 1, pages);
        ui_clear(UI_BG);
        ui_header(keymap_custom() ? "Keys (n84.keys.tns)" : "Keys (default)", right);
        snprintf(help, sizeof help, "Same-name keys not listed  %s", pages > 1 ? "<>: page" : "");
        ui_footer(help);
        for (int i = 0; i < PER_PAGE && page * PER_PAGE + i < n; i++) {
            plat_key_t k = keys[page * PER_PAGE + i];
            int x = 8 + (i / ROWS) * COL_W, y = TOP + (i % ROWS) * ROW;
            if (k == PK_QUESEXCL) {
                ui_text(x, y, "?!", UI_ACCENT);
                ui_text(x + 92, y, "N84 menu", UI_ACCENT);
                continue;
            }
            ui_text(x, y, keymap_pk_name(k), UI_DIM);
            ui_text(x + 92, y, keymap_tk_label(keymap_get(k)), UI_FG);
        }
        ui_rect(COL_W, TOP, 1, ROWS * ROW, UI_PANEL);
        ui_present();
        switch (ui_poll()) {
        case UI_LEFT: case UI_UP: if (page > 0) page--; break;
        case UI_RIGHT: case UI_DOWN: if (page + 1 < pages) page++; break;
        case UI_OK: case UI_BACK: case UI_MENU: ui_wait_release(); return;
        default: break;
        }
    }
}
