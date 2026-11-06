/*
 * N84 - host (Linux) platform layer: headless.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Video frames can be dumped as PPM; keys are driven programmatically through
 * plat_host_set_key() (scripted tests).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "platform/platform.h"
#include "platform/host/plat_host.h"

static uint16_t fb[PLAT_SCREEN_W * PLAT_SCREEN_H];
static uint8_t keys[PK_COUNT], keys_latched[PK_COUNT];
static bool on_key, on_latched;
static unsigned frames_presented;

bool plat_init(int argc, char **argv) { (void)argc; (void)argv; return true; }
void plat_shutdown(void) {}

bool plat_video_init(void) { memset(fb, 0, sizeof fb); return true; }
uint16_t *plat_video_buffer(void) { return fb; }
void plat_video_present(void) { frames_presented++; }
void plat_video_shutdown(void) {}
unsigned plat_host_frames(void) { return frames_presented; }

void plat_host_set_key(plat_key_t k, bool down) { if (k > 0 && k < PK_COUNT) keys[k] = down; }
void plat_host_set_on(bool down) { on_key = down; }
void plat_input_poll(void) { memcpy(keys_latched, keys, sizeof keys); on_latched = on_key; }
bool plat_key_down(plat_key_t k) { return k > 0 && k < PK_COUNT && keys_latched[k]; }
bool plat_on_down(void) { return on_latched; }

uint32_t plat_ticks(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * PLAT_TICKS_PER_SEC +
                      (uint64_t)ts.tv_nsec * PLAT_TICKS_PER_SEC / 1000000000u);
}

void plat_wait_until(uint32_t tick) {
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

void plat_register_ext(const char *ext, const char *prog) { (void)ext; (void)prog; }

void plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

FILE *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }

bool plat_host_save_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", PLAT_SCREEN_W, PLAT_SCREEN_H);
    for (int i = 0; i < PLAT_SCREEN_W * PLAT_SCREEN_H; i++) {
        uint16_t p = fb[i];
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
