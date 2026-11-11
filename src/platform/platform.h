/*
 * N84 - TI-8x emulator for the TI-Nspire
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Platform abstraction. Everything outside src/platform/ must only use this
 * interface so the emulator core can be built and tested on a PC.
 */
#ifndef N84_PLATFORM_H
#define N84_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define PLAT_SCREEN_W 320
#define PLAT_SCREEN_H 240

/* Monotonic tick counter (wraps). */
#define PLAT_TICKS_PER_SEC 32768u

/* Host-independent identifiers for the TI-Nspire CX / CX II keys. */
typedef enum {
    PK_NONE = 0,
    PK_ESC, PK_SCRATCHPAD, PK_TAB, PK_HOME, PK_DOC, PK_MENU,
    PK_CTRL, PK_SHIFT, PK_VAR, PK_DEL, PK_CAT, PK_FRAC,
    PK_UP, PK_DOWN, PK_LEFT, PK_RIGHT, PK_CLICK,
    PK_EQU, PK_TRIG, PK_EXP, PK_SQU, PK_TENX, PK_EEXP, PK_EE, PK_FLAG,
    PK_LP, PK_RP, PK_COMMA, PK_QUESEXCL, PK_PI,
    PK_SIN, PK_COS, PK_TAN,
    PK_PLUS, PK_MINUS, PK_MULTIPLY, PK_DIVIDE, PK_NEGATIVE, PK_PERIOD,
    PK_ENTER, PK_RET, PK_SPACE,
    PK_0, PK_1, PK_2, PK_3, PK_4, PK_5, PK_6, PK_7, PK_8, PK_9,
    PK_A, PK_B, PK_C, PK_D, PK_E, PK_F, PK_G, PK_H, PK_I, PK_J, PK_K, PK_L, PK_M,
    PK_N, PK_O, PK_P, PK_Q, PK_R, PK_S, PK_T, PK_U, PK_V, PK_W, PK_X, PK_Y, PK_Z,
    PK_COUNT
} plat_key_t;

/* Lifecycle */
bool     plat_init(int argc, char **argv);
void     plat_shutdown(void);

/* Video: one 320x240 RGB565 back buffer, presented with plat_video_present */
bool      plat_video_init(void);
uint16_t *plat_video_buffer(void);
void      plat_video_present(void);
void      plat_video_shutdown(void);
/* Memory the display shows, writable as a 320x240 RGB565 frame (no present
 * needed), or NULL when frames must go through the back buffer. */
uint16_t *plat_video_screen(void);

/* Input: plat_input_poll() latches the state read by plat_key_down() */
void     plat_input_poll(void);
bool     plat_key_down(plat_key_t k);
bool     plat_on_down(void);

/* Time */
uint32_t plat_ticks(void);
void     plat_wait_until(uint32_t tick);  /* idle until plat_ticks() >= tick */

/* UI / diagnostics */
void     plat_msgbox(const char *title, const char *msg);
/* Modal dialog with up to 3 buttons; returns the 1-based button chosen. */
int      plat_menu3(const char *title, const char *msg, const char *b1, const char *b2, const char *b3);
void     plat_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Files: paths are given without the Nspire ".tns" suffix handling; the
 * platform layer is responsible for any naming convention. */
FILE    *plat_fopen(const char *path, const char *mode);
bool     plat_remove(const char *path);
/* Size of a file in bytes, or -1 if it does not exist. */
long     plat_file_size(const char *path);
/* Directory that holds the program (n84.tns), without a trailing slash. */
const char *plat_app_dir(void);
/* Calls cb(name) for each entry of dir (no "." / ".."); stops early when cb
 * returns false. Returns false if the directory cannot be opened. */
bool     plat_list_dir(const char *dir, bool (*cb)(const char *name, void *ctx), void *ctx);
bool     plat_is_dir(const char *path);

#endif
