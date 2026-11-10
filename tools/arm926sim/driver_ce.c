/*
 * N84 - TI-84 Plus CE machine driver for tools/arm926sim
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The whole CE machine (core, peripherals, frame conversion), built with the
 * Nspire compiler flags and newlib, so arm926sim.py can profile real
 * workloads (the OS, games). The simulator places ROM, state and variable
 * files in memory and calls these entry points.
 *
 * Saved states hold host pointers, so a state from the 64-bit PC build cannot
 * be loaded here: arm926sim.py --prepare builds one with this driver.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine/ce/ce.h"
#include "machine/ce/ce_link.h"
#include "link/tivar.h"
#include "frontend/keynames.h"

static uint16_t frame[320 * 240];

/* tivar_load reads through plat_fopen: serve the image passed to sim_ce_send */
static const uint8_t *file_data;
static uint32_t file_size;
FILE *plat_fopen(const char *path, const char *mode) {
    (void)path;
    if (mode[0] != 'r' || !file_data) return NULL;
    return fmemopen((void *)file_data, file_size, "rb");
}

/* returns 1 on success */
int sim_ce_init(const uint8_t *rom, uint32_t size) { return ce_init(rom, size); }

int sim_ce_load(void *state, uint32_t size) {
    FILE *f = fmemopen(state, size, "rb");
    if (!f) return 0;
    int ok = ce_load_state(f);
    fclose(f);
    return ok;
}

/* returns the state size, 0 on failure */
uint32_t sim_ce_save(void *buf, uint32_t max) {
    FILE *f = fmemopen(buf, max, "wb");
    if (!f) return 0;
    bool ok = ce_save_state(f);
    long n = ftell(f);
    fclose(f);
    return ok && n > 0 ? (uint32_t)n : 0;
}

/* send every variable of a .8x? image; returns 1 on success */
int sim_ce_send(const uint8_t *data, uint32_t size) {
    tivar_file_t f;
    file_data = data;
    file_size = size;
    const char *e = tivar_load("sim", &f);
    file_data = NULL;
    if (e) return 0;
    for (int k = 0; !e && k < f.count; k++) e = ce_send_var(&f.vars[k], NULL);
    tivar_free(&f);
    return e == NULL;
}

/* press (down = 1) or release a key by its keynames.h name; 0 if unknown */
int sim_ce_key(const char *name, int down) {
    for (size_t i = 0; i < sizeof keynames / sizeof *keynames; i++) {
        if (strcmp(name, keynames[i].name)) continue;
        int tk = keynames[i].key;
        if (tk == TK_ON) ce_key(2, 0, down);
        else ce_key(7u - (unsigned)(tk >> 3), (unsigned)(tk & 7), down);
        return 1;
    }
    return 0;
}

/* run `frames` 60 Hz frames, converting every `draw_every`-th (0 = never);
 * returns the emulated cycles run */
uint32_t sim_ce_run(uint32_t frames, uint32_t draw_every) {
    uint64_t c0 = ce_total_cycles();
    for (uint32_t i = 0; i < frames; i++) {
        ce_run(1);
        if (draw_every && (i + 1) % draw_every == 0) ce_lcd_render(frame);
    }
    return (uint32_t)(ce_total_cycles() - c0);
}

/* Flash is not part of the state (N84 writes it back to the ROM file): the
 * simulator saves and restores it next to the state through this pointer */
uint8_t *sim_ce_flash(uint32_t *size) {
    size_t n;
    uint8_t *p = (uint8_t *)ce_flash(&n);
    *size = (uint32_t)n;
    return p;
}

/* the last converted frame (RGB565), for screenshots */
const uint16_t *sim_ce_frame(void) { ce_lcd_render(frame); return frame; }

void _start(void) { for (;;) {} }
