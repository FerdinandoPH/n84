/*
 * N84 - host (Linux) platform layer: headless.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Video frames can be dumped as PPM; keys are driven programmatically through
 * plat_host_set_key() (scripted tests).
 *
 * Script mode (env N84_SCRIPT=file) drives the real frontend: time is virtual
 * (each plat_input_poll advances it by 1/60 s, waits return immediately) and
 * the script is consumed one step per poll:
 *     wait N            do nothing for N polls
 *     press KEY / release KEY / tap KEY  (tap = press for 3 polls)
 *     on / off          ON key (home)
 *     shot FILE.ppm     save the last presented frame
 *     end               stop the program (exit)
 * KEY is a plat_key_t name without the PK_ prefix (ENTER, ESC, QUESEXCL...).
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "platform/platform.h"
#include "platform/host/plat_host.h"

static uint16_t fb[PLAT_SCREEN_W * PLAT_SCREEN_H], shown[PLAT_SCREEN_W * PLAT_SCREEN_H];
static uint8_t keys[PK_COUNT], keys_latched[PK_COUNT];
static bool on_key, on_latched;
static unsigned frames_presented;

/* ---- script mode ---- */
static FILE *script;
static uint32_t vticks;
static int script_wait;
static plat_key_t tap_key;
static int tap_left;

static const char *const key_names[PK_COUNT] = {
    "NONE", "ESC", "SCRATCHPAD", "TAB", "HOME", "DOC", "MENU",
    "CTRL", "SHIFT", "VAR", "DEL", "CAT", "FRAC",
    "UP", "DOWN", "LEFT", "RIGHT", "CLICK",
    "EQU", "TRIG", "EXP", "SQU", "TENX", "EEXP", "EE", "FLAG",
    "LP", "RP", "COMMA", "QUESEXCL", "PI",
    "SIN", "COS", "TAN",
    "PLUS", "MINUS", "MULTIPLY", "DIVIDE", "NEGATIVE", "PERIOD",
    "ENTER", "RET", "SPACE",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
};

static plat_key_t key_by_name(const char *n) {
    for (int k = 1; k < PK_COUNT; k++) if (!strcmp(n, key_names[k])) return (plat_key_t)k;
    fprintf(stderr, "script: unknown key %s\n", n);
    exit(2);
}

bool plat_host_save_ppm_buf(const uint16_t *buf, const char *path);

static void script_step(void) {
    if (tap_left && --tap_left == 0) keys[tap_key] = 0;
    if (script_wait > 0) { script_wait--; return; }
    char line[256];
    while (fgets(line, sizeof line, script)) {
        char cmd[32] = "", arg[200] = "";
        if (sscanf(line, "%31s %199s", cmd, arg) < 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "wait")) { script_wait = atoi(arg) - 1; return; }
        if (!strcmp(cmd, "press")) keys[key_by_name(arg)] = 1;
        else if (!strcmp(cmd, "release")) keys[key_by_name(arg)] = 0;
        else if (!strcmp(cmd, "tap")) { tap_key = key_by_name(arg); keys[tap_key] = 1; tap_left = 3; return; }
        else if (!strcmp(cmd, "on")) on_key = true;
        else if (!strcmp(cmd, "off")) on_key = false;
        else if (!strcmp(cmd, "shot")) plat_host_save_ppm_buf(shown, arg);
        else if (!strcmp(cmd, "end")) exit(0);
        else { fprintf(stderr, "script: bad command %s\n", cmd); exit(2); }
    }
    exit(0);   /* script exhausted */
}

static const char *app_dir_host = ".";

bool plat_init(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *s = getenv("N84_SCRIPT");
    if (s && !(script = fopen(s, "r"))) { perror(s); return false; }
    const char *d = getenv("N84_DIR");
    if (d) app_dir_host = d;
    return true;
}

const char *plat_app_dir(void) { return app_dir_host; }

bool plat_remove(const char *path) { return remove(path) == 0; }

long plat_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

bool plat_is_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return false;
    closedir(d);
    return true;
}

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
void plat_shutdown(void) {}

bool plat_video_init(void) { memset(fb, 0, sizeof fb); return true; }
uint16_t *plat_video_buffer(void) { return fb; }
void plat_video_present(void) { memcpy(shown, fb, sizeof fb); frames_presented++; }
void plat_video_shutdown(void) {}
unsigned plat_host_frames(void) { return frames_presented; }

void plat_host_set_key(plat_key_t k, bool down) { if (k > 0 && k < PK_COUNT) keys[k] = down; }
void plat_host_set_on(bool down) { on_key = down; }
void plat_input_poll(void) {
    if (script) {
        vticks += PLAT_TICKS_PER_SEC / 60;
        script_step();
    }
    memcpy(keys_latched, keys, sizeof keys);
    on_latched = on_key;
}
bool plat_key_down(plat_key_t k) { return k > 0 && k < PK_COUNT && keys_latched[k]; }
bool plat_on_down(void) { return on_latched; }

uint32_t plat_ticks(void) {
    if (script) return vticks;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * PLAT_TICKS_PER_SEC +
                      (uint64_t)ts.tv_nsec * PLAT_TICKS_PER_SEC / 1000000000u);
}

void plat_wait_until(uint32_t tick) {
    if (script) { if ((int32_t)(tick - vticks) > 0) vticks = tick; return; }
    while ((int32_t)(tick - plat_ticks()) > 0) {
        struct timespec ts = { 0, 200000 };
        nanosleep(&ts, NULL);
    }
}

void plat_msgbox(const char *title, const char *msg) { fprintf(stderr, "[%s] %s\n", title, msg); }

int plat_menu3(const char *title, const char *msg, const char *b1, const char *b2, const char *b3) {
    fprintf(stderr, "[%s] %s (%s/%s/%s) -> 1\n", title, msg, b1, b2, b3);
    return 1;
}

void plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

FILE *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }

bool plat_host_save_ppm(const char *path) { return plat_host_save_ppm_buf(fb, path); }

bool plat_host_save_ppm_buf(const uint16_t *buf, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", PLAT_SCREEN_W, PLAT_SCREEN_H);
    for (int i = 0; i < PLAT_SCREEN_W * PLAT_SCREEN_H; i++) {
        uint16_t p = buf[i];
        uint8_t rgb[3] = {
            (uint8_t)(((p >> 11) & 0x1f) * 255 / 31),
            (uint8_t)(((p >> 5) & 0x3f) * 255 / 63),
            (uint8_t)((p & 0x1f) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}
