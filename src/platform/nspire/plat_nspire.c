/*
 * N84 - TI-Nspire (Ndless) platform layer
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Design rule (see docs/SPEC.md RNF-02): no IRQ/vector/MMU tampering. We only
 * use libndls APIs plus one SP804 timer half in polling mode, whose registers
 * are saved on init and restored on shutdown.
 */
#include <os.h>
#include <libndls.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "platform/platform.h"

/* ---- time: second half of the SP804 at 0x900D0000 (32 kHz on CX/CX II) --
 * msleep() in libndls uses the first half (0x900D0000); we use 0x900D0020 so
 * both can coexist. Free-running, 32-bit, no interrupt. */
#define TMR_BASE     0x900D0020u
#define TMR_LOAD     (*(volatile uint32_t *)(TMR_BASE + 0x00))
#define TMR_VALUE    (*(volatile uint32_t *)(TMR_BASE + 0x04))
#define TMR_CONTROL  (*(volatile uint32_t *)(TMR_BASE + 0x08))
#define TMR_INTCLR   (*(volatile uint32_t *)(TMR_BASE + 0x0C))
#define TMR_BGLOAD   (*(volatile uint32_t *)(TMR_BASE + 0x18))

static uint32_t tmr_saved_load, tmr_saved_control, tmr_saved_bgload;
static bool tmr_active;

static void timer_start(void) {
    if (is_classic) return;                 /* not supported (non-CX) */
    tmr_saved_control = TMR_CONTROL;
    tmr_saved_load = TMR_LOAD;
    tmr_saved_bgload = TMR_BGLOAD;
    TMR_CONTROL = 0;                        /* disable */
    TMR_INTCLR = 1;
    TMR_LOAD = 0xFFFFFFFFu;
    /* enable | periodic=0 (free running) | int disabled | prescale 1 | 32-bit */
    TMR_CONTROL = 0x80 | 0x02;
    tmr_active = true;
}

static void timer_stop(void) {
    if (!tmr_active) return;
    TMR_CONTROL = 0;
    TMR_INTCLR = 1;
    TMR_LOAD = tmr_saved_load;
    TMR_BGLOAD = tmr_saved_bgload;
    TMR_CONTROL = tmr_saved_control;
    tmr_active = false;
}

uint32_t plat_ticks(void) {
    /* SP804 counts down */
    return tmr_active ? (uint32_t)(0xFFFFFFFFu - TMR_VALUE) : 0;
}

void plat_wait_until(uint32_t tick) {
    while ((int32_t)(tick - plat_ticks()) > 0) {
        /* busy wait: no IRQ of ours exists to wake a WFI reliably */
    }
}

/* ---- video ---------------------------------------------------------- */

static uint16_t *fb;
static bool lcd_ready;

bool plat_video_init(void) {
    if (!fb) fb = calloc(PLAT_SCREEN_W * PLAT_SCREEN_H, sizeof(uint16_t));
    if (!fb) return false;
    lcd_ready = lcd_init(SCR_320x240_565);
    return lcd_ready;
}

uint16_t *plat_video_buffer(void) { return fb; }

void plat_video_present(void) {
    if (lcd_ready) lcd_blit(fb, SCR_320x240_565);
}

void plat_video_shutdown(void) {
    if (lcd_ready) lcd_init(SCR_TYPE_INVALID);
    lcd_ready = false;
    free(fb);
    fb = NULL;
}

/* ---- input ---------------------------------------------------------- */

static const t_key *const keymap[PK_COUNT] = {
    [PK_ESC] = &KEY_NSPIRE_ESC, [PK_SCRATCHPAD] = &KEY_NSPIRE_SCRATCHPAD,
    [PK_TAB] = &KEY_NSPIRE_TAB, [PK_HOME] = &KEY_NSPIRE_HOME,
    [PK_DOC] = &KEY_NSPIRE_DOC, [PK_MENU] = &KEY_NSPIRE_MENU,
    [PK_CTRL] = &KEY_NSPIRE_CTRL, [PK_SHIFT] = &KEY_NSPIRE_SHIFT,
    [PK_VAR] = &KEY_NSPIRE_VAR, [PK_DEL] = &KEY_NSPIRE_DEL,
    [PK_CAT] = &KEY_NSPIRE_CAT, [PK_FRAC] = &KEY_NSPIRE_FRAC,
    [PK_UP] = &KEY_NSPIRE_UP, [PK_DOWN] = &KEY_NSPIRE_DOWN,
    [PK_LEFT] = &KEY_NSPIRE_LEFT, [PK_RIGHT] = &KEY_NSPIRE_RIGHT,
    [PK_CLICK] = &KEY_NSPIRE_CLICK,
    [PK_EQU] = &KEY_NSPIRE_EQU, [PK_TRIG] = &KEY_NSPIRE_TRIG,
    [PK_EXP] = &KEY_NSPIRE_EXP, [PK_SQU] = &KEY_NSPIRE_SQU,
    [PK_TENX] = &KEY_NSPIRE_TENX, [PK_EEXP] = &KEY_NSPIRE_eEXP,
    [PK_EE] = &KEY_NSPIRE_EE, [PK_FLAG] = &KEY_NSPIRE_FLAG,
    [PK_LP] = &KEY_NSPIRE_LP, [PK_RP] = &KEY_NSPIRE_RP,
    [PK_COMMA] = &KEY_NSPIRE_COMMA, [PK_QUESEXCL] = &KEY_NSPIRE_QUESEXCL,
    [PK_PI] = &KEY_NSPIRE_PI,
    [PK_SIN] = &KEY_NSPIRE_SIN, [PK_COS] = &KEY_NSPIRE_COS, [PK_TAN] = &KEY_NSPIRE_TAN,
    [PK_PLUS] = &KEY_NSPIRE_PLUS, [PK_MINUS] = &KEY_NSPIRE_MINUS,
    [PK_MULTIPLY] = &KEY_NSPIRE_MULTIPLY, [PK_DIVIDE] = &KEY_NSPIRE_DIVIDE,
    [PK_NEGATIVE] = &KEY_NSPIRE_NEGATIVE, [PK_PERIOD] = &KEY_NSPIRE_PERIOD,
    [PK_ENTER] = &KEY_NSPIRE_ENTER, [PK_RET] = &KEY_NSPIRE_RET,
    [PK_SPACE] = &KEY_NSPIRE_SPACE,
    [PK_0] = &KEY_NSPIRE_0, [PK_1] = &KEY_NSPIRE_1, [PK_2] = &KEY_NSPIRE_2,
    [PK_3] = &KEY_NSPIRE_3, [PK_4] = &KEY_NSPIRE_4, [PK_5] = &KEY_NSPIRE_5,
    [PK_6] = &KEY_NSPIRE_6, [PK_7] = &KEY_NSPIRE_7, [PK_8] = &KEY_NSPIRE_8,
    [PK_9] = &KEY_NSPIRE_9,
    [PK_A] = &KEY_NSPIRE_A, [PK_B] = &KEY_NSPIRE_B, [PK_C] = &KEY_NSPIRE_C,
    [PK_D] = &KEY_NSPIRE_D, [PK_E] = &KEY_NSPIRE_E, [PK_F] = &KEY_NSPIRE_F,
    [PK_G] = &KEY_NSPIRE_G, [PK_H] = &KEY_NSPIRE_H, [PK_I] = &KEY_NSPIRE_I,
    [PK_J] = &KEY_NSPIRE_J, [PK_K] = &KEY_NSPIRE_K, [PK_L] = &KEY_NSPIRE_L,
    [PK_M] = &KEY_NSPIRE_M, [PK_N] = &KEY_NSPIRE_N, [PK_O] = &KEY_NSPIRE_O,
    [PK_P] = &KEY_NSPIRE_P, [PK_Q] = &KEY_NSPIRE_Q, [PK_R] = &KEY_NSPIRE_R,
    [PK_S] = &KEY_NSPIRE_S, [PK_T] = &KEY_NSPIRE_T, [PK_U] = &KEY_NSPIRE_U,
    [PK_V] = &KEY_NSPIRE_V, [PK_W] = &KEY_NSPIRE_W, [PK_X] = &KEY_NSPIRE_X,
    [PK_Y] = &KEY_NSPIRE_Y, [PK_Z] = &KEY_NSPIRE_Z,
};

static uint8_t keystate[PK_COUNT];
static bool on_state;

void plat_input_poll(void) {
    for (int k = 1; k < PK_COUNT; k++)
        keystate[k] = keymap[k] ? (uint8_t)isKeyPressed(*keymap[k]) : 0;
    on_state = on_key_pressed();
}

bool plat_key_down(plat_key_t k) { return k > 0 && k < PK_COUNT && keystate[k]; }
bool plat_on_down(void) { return on_state; }

/* ---- misc ----------------------------------------------------------- */

void plat_msgbox(const char *title, const char *msg) {
    bool was = lcd_ready;
    if (was) lcd_init(SCR_TYPE_INVALID);
    show_msgbox(title, msg);
    if (was) lcd_init(SCR_320x240_565);
}

int plat_menu3(const char *title, const char *msg, const char *b1, const char *b2, const char *b3) {
    bool was = lcd_ready;
    if (was) lcd_init(SCR_TYPE_INVALID);
    wait_no_key_pressed();
    int r = (int)show_msgbox_3b(title, msg, b1, b2, b3);
    wait_no_key_pressed();
    if (was) lcd_init(SCR_320x240_565);
    return r;
}

void plat_register_ext(const char *ext, const char *prog) { cfg_register_fileext(ext, prog); }

void plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);   /* goes to the serial console (visible in Firebird) */
    va_end(ap);
}

FILE *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }

bool plat_remove(const char *path) { return remove(path) == 0; }

long plat_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static char app_dir[256] = ".";
const char *plat_app_dir(void) { return app_dir; }

bool plat_list_dir(const char *dir, bool (*cb)(const char *name, void *ctx), void *ctx) {
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!cb(e->d_name, ctx)) break;
    }
    closedir(d);
    return true;
}

bool plat_init(int argc, char **argv) {
    (void)argc;
    if (argv && argv[0]) {
        enable_relative_paths(argv);
        strncpy(app_dir, argv[0], sizeof app_dir - 1);
        char *slash = strrchr(app_dir, '/');
        if (slash && slash != app_dir) *slash = 0;
        else strcpy(app_dir, "/documents");
    }
    timer_start();
    return true;
}

void plat_shutdown(void) {
    plat_video_shutdown();
    timer_stop();
}
