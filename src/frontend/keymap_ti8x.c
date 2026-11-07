/*
 * N84 - default TI-Nspire CX (II) -> TI-84 Plus key mapping
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keys with the same legend map directly. Letter keys map to the TI-84 key
 * whose ALPHA function is that letter, so [shift]+letter types it.
 */
#include <string.h>
#include "frontend/keymap_ti8x.h"

typedef struct { uint8_t pk; uint8_t tk; } keymap_entry_t;

static const keymap_entry_t default_map[] = {
    { PK_UP, TK_UP }, { PK_DOWN, TK_DOWN }, { PK_LEFT, TK_LEFT }, { PK_RIGHT, TK_RIGHT },
    { PK_ENTER, TK_ENTER }, { PK_RET, TK_ENTER }, { PK_CLICK, TK_ENTER },
    { PK_ESC, TK_CLEAR }, { PK_DEL, TK_DEL },
    { PK_CTRL, TK_2ND }, { PK_SHIFT, TK_ALPHA }, { PK_HOME, TK_ON },
    /* function row */
    { PK_SCRATCHPAD, TK_YEQU }, { PK_TAB, TK_WINDOW }, { PK_DOC, TK_ZOOM },
    { PK_MENU, TK_TRACE }, { PK_CAT, TK_GRAPH },
    /* other named keys */
    { PK_VAR, TK_VARS }, { PK_FRAC, TK_MODE }, { PK_TRIG, TK_STAT },
    { PK_EQU, TK_STO }, { PK_EXP, TK_POWER }, { PK_SQU, TK_SQUARE },
    { PK_TENX, TK_LOG }, { PK_EEXP, TK_LN }, { PK_EE, TK_RECIP }, { PK_FLAG, TK_PRGM },
    { PK_SIN, TK_SIN }, { PK_COS, TK_COS }, { PK_TAN, TK_TAN },
    { PK_LP, TK_LPAREN }, { PK_RP, TK_RPAREN }, { PK_COMMA, TK_COMMA },
    { PK_PLUS, TK_ADD }, { PK_MINUS, TK_SUB }, { PK_MULTIPLY, TK_MUL }, { PK_DIVIDE, TK_DIV },
    { PK_NEGATIVE, TK_CHS }, { PK_PERIOD, TK_DOT }, { PK_SPACE, TK_0 },
    { PK_0, TK_0 }, { PK_1, TK_1 }, { PK_2, TK_2 }, { PK_3, TK_3 }, { PK_4, TK_4 },
    { PK_5, TK_5 }, { PK_6, TK_6 }, { PK_7, TK_7 }, { PK_8, TK_8 }, { PK_9, TK_9 },
    /* letters: TI-84 key whose ALPHA legend is the letter */
    { PK_A, TK_MATH }, { PK_B, TK_APPS }, { PK_C, TK_PRGM }, { PK_D, TK_RECIP },
    { PK_E, TK_SIN }, { PK_F, TK_COS }, { PK_G, TK_TAN }, { PK_H, TK_POWER },
    { PK_I, TK_SQUARE }, { PK_J, TK_COMMA }, { PK_K, TK_LPAREN }, { PK_L, TK_RPAREN },
    { PK_M, TK_DIV }, { PK_N, TK_LOG }, { PK_O, TK_7 }, { PK_P, TK_8 }, { PK_Q, TK_9 },
    { PK_R, TK_MUL }, { PK_S, TK_LN }, { PK_T, TK_4 }, { PK_U, TK_5 }, { PK_V, TK_6 },
    { PK_W, TK_SUB }, { PK_X, TK_XTTN }, { PK_Y, TK_1 }, { PK_Z, TK_2 },
};

void ti8x_keymap_scan(bool down[0x41]) {
    memset(down, 0, 0x41 * sizeof *down);
    for (size_t i = 0; i < sizeof default_map / sizeof *default_map; i++)
        if (plat_key_down((plat_key_t)default_map[i].pk)) down[default_map[i].tk] = true;
    if (plat_on_down()) down[TK_ON] = true;
}

void ti8x_keymap_apply(ti8x_t *m) {
    bool down[0x41];
    ti8x_keymap_scan(down);
    for (uint8_t k = 0; k < 0x41; k++)
        ti8x_set_key(m, k, down[k]);
}
