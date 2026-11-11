/*
 * N84 - TI-Nspire CX (II) -> TI-84 Plus key mapping
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Default map: keys with the same legend map directly. Letter keys map to
 * the TI-84 key whose ALPHA function is that letter, so [shift]+letter types
 * it. The optional key file (KEYMAP_FILE) overrides single keys.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "frontend/keymap_ti8x.h"
#include "frontend/keynames.h"

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

/* Nspire key names (plat_key_t order): the first is written and shown, the
 * second is also accepted when reading the key file */
static const char *const pk_names[PK_COUNT][2] = {
    { "", "" },
    { "esc", "" }, { "scratchpad", "pad" }, { "tab", "" }, { "home", "on" }, { "doc", "" }, { "menu", "" },
    { "ctrl", "" }, { "shift", "" }, { "var", "" }, { "del", "" }, { "cat", "book" }, { "templates", "frac" },
    { "up", "" }, { "down", "" }, { "left", "" }, { "right", "" }, { "click", "" },
    { "=", "equ" }, { "trig", "" }, { "^", "exp" }, { "x2", "squ" }, { "10x", "tenx" }, { "ex", "eexp" },
    { "ee", "" }, { "flag", "" },
    { "(", "lp" }, { ")", "rp" }, { ",", "comma" }, { "?!", "quesexcl" }, { "pi", "" },
    { "sin", "" }, { "cos", "" }, { "tan", "" },
    { "+", "plus" }, { "-", "minus" }, { "*", "multiply" }, { "/", "divide" }, { "(-)", "negative" },
    { ".", "period" },
    { "enter", "" }, { "return", "ret" }, { "space", "" },
    { "0", "" }, { "1", "" }, { "2", "" }, { "3", "" }, { "4", "" },
    { "5", "" }, { "6", "" }, { "7", "" }, { "8", "" }, { "9", "" },
    { "a", "" }, { "b", "" }, { "c", "" }, { "d", "" }, { "e", "" }, { "f", "" }, { "g", "" },
    { "h", "" }, { "i", "" }, { "j", "" }, { "k", "" }, { "l", "" }, { "m", "" }, { "n", "" },
    { "o", "" }, { "p", "" }, { "q", "" }, { "r", "" }, { "s", "" }, { "t", "" }, { "u", "" },
    { "v", "" }, { "w", "" }, { "x", "" }, { "y", "" }, { "z", "" },
};

static uint8_t map[PK_COUNT];
static bool custom, ready;

void keymap_reset(void) {
    ready = true;
    memset(map, TK_NONE, sizeof map);
    for (size_t i = 0; i < sizeof default_map / sizeof *default_map; i++)
        map[default_map[i].pk] = default_map[i].tk;
    custom = false;
}

uint8_t keymap_get(plat_key_t pk) {
    if (!ready) keymap_reset();
    return pk > PK_NONE && pk < PK_COUNT ? map[pk] : TK_NONE;
}

bool keymap_custom(void) { return custom; }

const char *keymap_pk_name(plat_key_t pk) { return pk > PK_NONE && pk < PK_COUNT ? pk_names[pk][0] : "?"; }

const char *keymap_tk_label(uint8_t tk) {
    for (size_t i = 0; i < sizeof keynames / sizeof *keynames; i++)
        if (keynames[i].key == tk) return keynames[i].label;
    return "none";
}

static bool same(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return *a == *b;
}

static int find_pk(const char *s) {
    for (int k = 1; k < PK_COUNT; k++)
        if (same(s, pk_names[k][0]) || (pk_names[k][1][0] && same(s, pk_names[k][1]))) return k;
    return -1;
}

/* TK code, TK_NONE for "none", -1 if unknown */
static int find_tk(const char *s) {
    if (same(s, "none")) return TK_NONE;
    for (size_t i = 0; i < sizeof keynames / sizeof *keynames; i++)
        if (same(s, keynames[i].name) || same(s, keynames[i].label)) return keynames[i].key;
    return -1;
}

int keymap_load(const char *path, char *err, size_t err_size) {
    keymap_reset();
    FILE *f = plat_fopen(path, "r");
    if (!f) return 0;
    char line[128];
    int n = 0, result = 1;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *tok[4];
        int nt = 0;
        for (char *t = strtok(line, " \t\r\n"); t && nt < 4; t = strtok(NULL, " \t\r\n")) tok[nt++] = t;
        if (nt == 3 && !strcmp(tok[1], "=")) { tok[1] = tok[2]; nt = 2; }
        if (nt == 0) continue;
        const char *problem = NULL;
        int pk = -1, tk = -1;
        if (nt != 2) problem = "expected: nspire_key ti84_key";
        else if ((pk = find_pk(tok[0])) < 0) problem = "unknown Nspire key";
        else if (pk == PK_QUESEXCL) problem = "?! is reserved for the N84 menu";
        else if ((tk = find_tk(tok[1])) < 0) problem = "unknown TI-84 key";
        if (problem) {
            if (result > 0 && err) snprintf(err, err_size, "%s, line %d: %s.", KEYMAP_FILE, n, problem);
            result = -1;
            continue;
        }
        map[pk] = (uint8_t)tk;
    }
    fclose(f);
    custom = true;
    return result;
}

void keymap_write(FILE *f) {
    fprintf(f, "# N84 key map: one \"nspire_key  ti84_key\" pair per line ('#' starts a comment).\n"
               "# Keys not listed keep their default; \"none\" disables a key. ?! (and ctrl+esc)\n"
               "# always open the N84 menu. TI-84 keys: their names as printed on the keypad\n"
               "# (2ND, ALPHA, Y=, STO>, x^2, x^-1, X,T,n, (-)...) or YEQU, STO, SQUARE, RECIP,\n"
               "# XTTN, CHS, ADD, SUB, MUL, DIV, POWER, LPAREN, RPAREN, COMMA, DOT.\n\n");
    for (int k = 1; k < PK_COUNT; k++)
        if (map[k] != TK_NONE) fprintf(f, "%-12s %s\n", pk_names[k][0], keymap_tk_label(map[k]));
}

void ti8x_keymap_scan(bool down[0x41]) {
    memset(down, 0, 0x41 * sizeof *down);
    for (int k = 1; k < PK_COUNT; k++) {
        uint8_t tk = keymap_get((plat_key_t)k);
        if (tk != TK_NONE && plat_key_down((plat_key_t)k)) down[tk] = true;
    }
    if (plat_on_down()) down[TK_ON] = true;
}

void ti8x_keymap_apply(ti8x_t *m) {
    bool down[0x41];
    ti8x_keymap_scan(down);
    for (uint8_t k = 0; k < 0x41; k++)
        ti8x_set_key(m, k, down[k]);
}
