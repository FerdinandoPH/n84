/*
 * N84 - headless host runner for TI-84 Plus CE ROMs (testing / debugging).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   n84ce-host ROM SECONDS [-o FILE.ppm] [-s STATE] [-l STATE] [-t] [T:KEY | T:shot=FILE.ppm]...
 * Keys use the TI-84 names (ENTER, 2ND, ON, ...); the CE key matrix is the
 * TI-84 one with the rows reversed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "machine/ce/ce.h"
#include "frontend/keynames.h"
#include "platform/host/plat_host.h"
#include "platform/platform.h"
#include "lcd.h"
#include "ce_link.h"
#include "cpu.h"
#include "control.h"
#include "flash.h"
#include "interrupt.h"

typedef struct { double t; int key; const char *shot; const char *send; } event_t;

static int lookup_key(const char *s) {
    for (size_t i = 0; i < sizeof keynames / sizeof *keynames; i++)
        if (!strcmp(s, keynames[i].name)) return keynames[i].key;
    fprintf(stderr, "unknown key '%s'\n", s);
    exit(2);
}

static void tk_to_ce(int tk, unsigned *row, unsigned *col) {
    if (tk == TK_ON) { *row = 2; *col = 0; return; }
    *row = 7u - (unsigned)(tk >> 3);
    *col = (unsigned)(tk & 7);
}

static void screenshot(const char *path) {
    uint16_t *fb = plat_video_buffer();
    ce_lcd_render(fb);
    if (!plat_host_save_ppm(path)) perror(path);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s ROM SECONDS [-o out.ppm] [-t] [T:KEY|T:shot=F]...\n", argv[0]);
        return 2;
    }
    double seconds = atof(argv[2]);
    const char *out = NULL, *save = NULL, *load = NULL;
    bool timing = false;
    event_t ev[256];
    int nev = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-t")) timing = true;
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) load = argv[++i];
        else if (strchr(argv[i], ':') && nev < 256) {
            char *c = strchr(argv[i], ':');
            ev[nev].t = atof(argv[i]);
            ev[nev].shot = ev[nev].send = NULL;
            if (!strncmp(c + 1, "shot=", 5)) { ev[nev].shot = c + 6; ev[nev].key = -1; }
            else if (!strncmp(c + 1, "send=", 5)) { ev[nev].send = c + 6; ev[nev].key = -1; }
            else ev[nev].key = lookup_key(c + 1);
            nev++;
        } else { fprintf(stderr, "bad argument %s\n", argv[i]); return 2; }
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc(size);
    if (fread(rom, 1, size, f) != size) { perror("read"); return 1; }
    fclose(f);
    if (!ce_init(rom, size)) { fprintf(stderr, "unsupported ROM\n"); return 1; }
    free(rom);
    if (load) {
        FILE *sf = fopen(load, "rb");
        if (!sf || !ce_load_state(sf)) { fprintf(stderr, "could not load state %s\n", load); return 1; }
        fclose(sf);
    }
    plat_video_init();

    long frames = (long)(seconds * 60 + 0.5);
    clock_t c0 = clock();
    for (long fr = 0; fr < frames; fr++) {
        double t = fr / 60.0;
        for (int i = 0; i < nev; i++) {
            if (ev[i].key >= 0) {
                unsigned r, c;
                tk_to_ce(ev[i].key, &r, &c);
                if (t >= ev[i].t && t < ev[i].t + 1.0 / 60) ce_key(r, c, true);
                else if (t >= ev[i].t + 0.1 && t < ev[i].t + 0.1 + 1.0 / 60) ce_key(r, c, false);
            } else if (ev[i].shot && t >= ev[i].t && t < ev[i].t + 1.0 / 60) {
                screenshot(ev[i].shot);
            } else if (ev[i].send && t >= ev[i].t && t < ev[i].t + 1.0 / 60) {
                tivar_file_t vf;
                const char *e = tivar_load(ev[i].send, &vf);
                for (int k = 0; !e && k < vf.count; k++) e = ce_send_var(&vf.vars[k]);
                fprintf(stderr, "send %s: %s\n", ev[i].send, e ? e : "OK");
                tivar_free(&vf);
            }
        }
        ce_run(1);
        if (getenv("CE_TRACE") && (fr % atoi(getenv("CE_TRACE")) == 0))
            fprintf(stderr, "t=%.2f pc=%06X adl=%u halt=%u iff1=%u sp=%06X lcdctl=%08X upbase=%06X int=%06X/%06X cpuspd=%u flashws=%u\n",
                    t, cpu.z.pc, cpu.z.adl, cpu.z.halted, cpu.z.iff1, cpu.z.spl, lcd.control, lcd.upbase,
                    intrpt[0].status, intrpt[0].enabled, control.cpuSpeed, flash.waitStates);
    }
    double el = (double)(clock() - c0) / CLOCKS_PER_SEC;
    if (timing)
        fprintf(stderr, "%.2f emulated s in %.2f s (%.1fx), PC=%06X, %llu cycles, %u LCD frames\n",
                seconds, el, el > 0 ? seconds / el : 0, ce_pc(),
                (unsigned long long)ce_total_cycles(), ce_lcd_frames());
    if (getenv("CE_DUMP")) {
        uint32_t a = (uint32_t)strtoul(getenv("CE_DUMP"), NULL, 16);
        for (int r = 0; r < 8; r++) {
            fprintf(stderr, "%06X:", a + r * 16);
            for (int k = 0; k < 16; k++) fprintf(stderr, " %02X", ez80_peek(&cpu.z, a + r * 16 + k));
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "a=%02X f=%02X bc=%06X de=%06X hl=%06X ix=%06X iy=%06X mb=%02X\n", cpu.z.a, cpu.z.f,
                cpu.z.bc.w, cpu.z.de.w, cpu.z.hl.w, cpu.z.ix.w, cpu.z.iy.w, cpu.z.mbase);
    }
    if (out) screenshot(out);
    if (save) {
        FILE *sf = fopen(save, "wb");
        if (!sf || !ce_save_state(sf)) fprintf(stderr, "could not save state\n");
        if (sf) fclose(sf);
    }
    ce_free();
    return 0;
}
