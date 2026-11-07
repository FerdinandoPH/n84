/*
 * N84 - headless host runner for TI-8x ROMs (testing / debugging).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 *   n84-host ROM SECONDS [options] [EVENTS...]
 *     -o FILE.ppm     screenshot at the end (default: none)
 *     -s FILE         save state at the end      -l FILE  load state first
 *     -t              print timing information
 *     -m FILE         dump Flash then RAM (raw) at the end
 *   EVENTS: T:KEY presses KEY at time T (seconds) for 100 ms, e.g. 3.5:ENTER
 *           T:shot=FILE.ppm takes a screenshot at time T
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "machine/ti8x/ti8x.h"
#include "frontend/ti8x_view.h"
#include "platform/host/plat_host.h"
#include "machine/ti8x/ti8x_link.h"

#include "frontend/keynames.h"

typedef struct { double t; int key; const char *shot; const char *send; } event_t;

static uint8_t lookup_key(const char *s) {
    for (size_t i = 0; i < sizeof keynames / sizeof *keynames; i++)
        if (!strcmp(s, keynames[i].name)) return keynames[i].key;
    fprintf(stderr, "unknown key '%s'\n", s);
    exit(2);
}

static void screenshot(ti8x_t *m, const char *path) {
    static uint8_t lv[TI8X_LCD_W * TI8X_LCD_H];
    uint16_t *fb = plat_video_buffer();
    ti8x_view_init(fb);
    ti8x_lcd_frame(m, lv);
    ti8x_view_render(lv, m->lcd.contrast, ti8x_lcd_on(m), fb);
    if (!plat_host_save_ppm(path)) perror(path);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s ROM SECONDS [-o out.ppm] [-s state] [-l state] [-t] [T:KEY|T:shot=F]...\n", argv[0]);
        return 2;
    }
    const char *rom_path = argv[1];
    double seconds = atof(argv[2]);
    const char *out = NULL, *save = NULL, *load = NULL, *dump = NULL;
    bool timing = false;
    event_t ev[256];
    int nev = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) load = argv[++i];
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "-t")) timing = true;
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

    FILE *f = fopen(rom_path, "rb");
    if (!f) { perror(rom_path); return 1; }
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc(size);
    if (fread(rom, 1, size, f) != size) { perror("read"); return 1; }
    fclose(f);

    static ti8x_t m;
    ti8x_model_t model = ti8x_detect_model(rom, size);
    if (!ti8x_init(&m, model, rom, size)) { fprintf(stderr, "unsupported ROM\n"); return 1; }
    free(rom);
    fprintf(stderr, "model: %s\n", ti8x_model_name(model));
    if (load) {
        FILE *sf = fopen(load, "rb");
        if (!sf || !ti8x_load_state(&m, sf)) { fprintf(stderr, "could not load state %s\n", load); return 1; }
        fclose(sf);
    }
    plat_video_init();

    const uint32_t step = TI8X_TICKS_PER_SEC / 100;  /* 10 ms */
    long steps = (long)(seconds * 100 + 0.5);
    clock_t c0 = clock();
    for (long s = 0; s < steps; s++) {
        double t = s / 100.0;
        for (int i = 0; i < nev; i++) {
            if (ev[i].key >= 0) {
                if (t >= ev[i].t && t < ev[i].t + 0.1) ti8x_set_key(&m, (uint8_t)ev[i].key, true);
                else if (t >= ev[i].t + 0.1 && t < ev[i].t + 0.11) ti8x_set_key(&m, (uint8_t)ev[i].key, false);
            } else if (ev[i].shot && t >= ev[i].t && t < ev[i].t + 0.01) {
                screenshot(&m, ev[i].shot);
            } else if (ev[i].send && t >= ev[i].t && t < ev[i].t + 0.01) {
                tivar_file_t vf;
                const char *e = tivar_load(ev[i].send, &vf);
                for (int k = 0; !e && k < vf.count; k++) e = ti8x_send_var(&m, &vf.vars[k], NULL);
                fprintf(stderr, "send %s: %s\n", ev[i].send, e ? e : "OK");
                tivar_free(&vf);
            }
        }
        ti8x_run(&m, step);
    }
    double el = (double)(clock() - c0) / CLOCKS_PER_SEC;
    if (timing)
        fprintf(stderr, "%.2f emulated s in %.2f s (%.0fx), PC=%04X, %llu cycles\n", seconds, el,
                el > 0 ? seconds / el : 0, m.cpu.pc.w, (unsigned long long)m.cpu.total_cycles);
    if (out) screenshot(&m, out);
    if (save) {
        FILE *sf = fopen(save, "wb");
        if (!sf || !ti8x_save_state(&m, sf)) fprintf(stderr, "could not save state\n");
        if (sf) fclose(sf);
    }
    if (dump) {
        FILE *df = fopen(dump, "wb");
        if (df) { fwrite(m.mem, 0x4000, m.flash_pages + m.ram_pages, df); fclose(df); }
    }
    ti8x_free(&m);
    return 0;
}
