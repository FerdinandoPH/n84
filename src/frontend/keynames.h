/*
 * N84 - TI-84 key names for the host test runners (TK_* codes, ti8x.h).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_KEYNAMES_H
#define N84_KEYNAMES_H
#include <stdint.h>
#include "machine/ti8x/ti8x.h"

static const struct { const char *name; uint8_t key; } keynames[] = {
    {"DOWN",TK_DOWN},{"LEFT",TK_LEFT},{"RIGHT",TK_RIGHT},{"UP",TK_UP},{"ENTER",TK_ENTER},
    {"ADD",TK_ADD},{"SUB",TK_SUB},{"MUL",TK_MUL},{"DIV",TK_DIV},{"POWER",TK_POWER},
    {"CLEAR",TK_CLEAR},{"CHS",TK_CHS},{"3",TK_3},{"6",TK_6},{"9",TK_9},{"RPAREN",TK_RPAREN},
    {"TAN",TK_TAN},{"VARS",TK_VARS},{"DOT",TK_DOT},{"2",TK_2},{"5",TK_5},{"8",TK_8},
    {"LPAREN",TK_LPAREN},{"COS",TK_COS},{"PRGM",TK_PRGM},{"STAT",TK_STAT},{"0",TK_0},
    {"1",TK_1},{"4",TK_4},{"7",TK_7},{"COMMA",TK_COMMA},{"SIN",TK_SIN},{"APPS",TK_APPS},
    {"XTTN",TK_XTTN},{"STO",TK_STO},{"LN",TK_LN},{"LOG",TK_LOG},{"SQUARE",TK_SQUARE},
    {"RECIP",TK_RECIP},{"MATH",TK_MATH},{"ALPHA",TK_ALPHA},{"GRAPH",TK_GRAPH},
    {"TRACE",TK_TRACE},{"ZOOM",TK_ZOOM},{"WINDOW",TK_WINDOW},{"YEQU",TK_YEQU},
    {"2ND",TK_2ND},{"MODE",TK_MODE},{"DEL",TK_DEL},{"ON",TK_ON},
};

#endif
