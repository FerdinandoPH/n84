/*
 * N84 - TI-84 key names (TK_* codes, ti8x.h).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_KEYNAMES_H
#define N84_KEYNAMES_H
#include <stdint.h>
#include "machine/ti8x/ti8x.h"

/* name: as used by the test runners and n84.keys.tns; label: as printed on
 * the TI-84 keypad (ASCII), shown in the key guide and accepted in the file */
static const struct { const char *name; uint8_t key; const char *label; } keynames[] = {
    {"DOWN",TK_DOWN,"down"},{"LEFT",TK_LEFT,"left"},{"RIGHT",TK_RIGHT,"right"},{"UP",TK_UP,"up"},{"ENTER",TK_ENTER,"ENTER"},
    {"ADD",TK_ADD,"+"},{"SUB",TK_SUB,"-"},{"MUL",TK_MUL,"*"},{"DIV",TK_DIV,"/"},{"POWER",TK_POWER,"^"},
    {"CLEAR",TK_CLEAR,"CLEAR"},{"CHS",TK_CHS,"(-)"},{"3",TK_3,"3"},{"6",TK_6,"6"},{"9",TK_9,"9"},{"RPAREN",TK_RPAREN,")"},
    {"TAN",TK_TAN,"TAN"},{"VARS",TK_VARS,"VARS"},{"DOT",TK_DOT,"."},{"2",TK_2,"2"},{"5",TK_5,"5"},{"8",TK_8,"8"},
    {"LPAREN",TK_LPAREN,"("},{"COS",TK_COS,"COS"},{"PRGM",TK_PRGM,"PRGM"},{"STAT",TK_STAT,"STAT"},{"0",TK_0,"0"},
    {"1",TK_1,"1"},{"4",TK_4,"4"},{"7",TK_7,"7"},{"COMMA",TK_COMMA,","},{"SIN",TK_SIN,"SIN"},{"APPS",TK_APPS,"APPS"},
    {"XTTN",TK_XTTN,"X,T,n"},{"STO",TK_STO,"STO>"},{"LN",TK_LN,"LN"},{"LOG",TK_LOG,"LOG"},{"SQUARE",TK_SQUARE,"x^2"},
    {"RECIP",TK_RECIP,"x^-1"},{"MATH",TK_MATH,"MATH"},{"ALPHA",TK_ALPHA,"ALPHA"},{"GRAPH",TK_GRAPH,"GRAPH"},
    {"TRACE",TK_TRACE,"TRACE"},{"ZOOM",TK_ZOOM,"ZOOM"},{"WINDOW",TK_WINDOW,"WINDOW"},{"YEQU",TK_YEQU,"Y="},
    {"2ND",TK_2ND,"2ND"},{"MODE",TK_MODE,"MODE"},{"DEL",TK_DEL,"DEL"},{"ON",TK_ON,"ON"},
};

#endif
