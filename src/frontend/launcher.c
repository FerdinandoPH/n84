/*
 * N84 - launcher: ROM list, ROM information, settings
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ROMs are the NAME.rom.tns files in the folder that holds n84.tns.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend/launcher.h"
#include "frontend/session.h"
#include "frontend/config.h"
#include "frontend/ui.h"
#include "frontend/keyguide.h"
#include "frontend/version.h"
#include "platform/platform.h"
#include "machine/ti8x/ti8x.h"
#include "machine/ce/bootver.h"

#define MAX_ROMS 32
#define ROM_SUFFIX ".rom.tns"

typedef struct {
    char name[64];          /* without the suffix */
    char file[96];          /* NAME.rom.tns */
    long size;
    char model[24];         /* "TI-84 Plus CE", ... */
    char model_short[8];    /* "CE", "84+SE", ... */
    char boot[24];          /* boot code version, if known */
    long state_size;        /* -1 if no saved state */
} rom_entry_t;

static rom_entry_t roms[MAX_ROMS];
static int nroms;

static void full_path(char *buf, size_t size, const char *file) {
    snprintf(buf, size, "%s/%s", plat_app_dir(), file);
}

static bool read_at(FILE *f, long off, void *buf, size_t n) {
    return fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}

/* Fills model / boot fields from the image header (without loading it all). */
static void probe_rom(rom_entry_t *r) {
    char path[300];
    full_path(path, sizeof path, r->file);
    strcpy(r->model, "Unknown");
    strcpy(r->model_short, "?");
    r->boot[0] = 0;
    FILE *f = plat_fopen(path, "rb");
    if (!f) return;
    if (r->size >= 0x400000) {
        static uint8_t boot[SIZE_BOOTCODE];
        boot_ver_t bv;
        strcpy(r->model, "TI-84 Plus CE");
        strcpy(r->model_short, "CE");
        if (read_at(f, 0, boot, sizeof boot) && bootver_parse(boot, &bv))
            snprintf(r->boot, sizeof r->boot, "%u.%u.%u.%04u", bv.major, bv.minor, bv.revision, bv.build);
    } else if (r->size >= 0x100000) {
        static uint8_t page[0x4000];
        long off = r->size >= 0x200000 ? 0x1FC000 : 0xFC000;
        if (read_at(f, off, page, sizeof page)) {
            ti8x_model_t m = ti8x_detect_model_boot((size_t)r->size, page);
            if (m != TI8X_MODEL_NONE) {
                snprintf(r->model, sizeof r->model, "%s", ti8x_model_name(m));
                strcpy(r->model_short, m == TI8X_MODEL_84P ? "84+" : m == TI8X_MODEL_84PSE ? "84+SE" : "83+SE");
            }
            for (unsigned i = 0; i + 8 < sizeof page; i++)
                if (!memcmp(page + i, "BootFree", 8)) { strcpy(r->boot, "BootFree"); break; }
        }
    }
    fclose(f);
}

static bool collect(const char *name, void *ctx) {
    (void)ctx;
    size_t n = strlen(name), sl = strlen(ROM_SUFFIX);
    if (n <= sl || strcmp(name + n - sl, ROM_SUFFIX) || nroms >= MAX_ROMS) return true;
    rom_entry_t *r = &roms[nroms];
    if (n - sl >= sizeof r->name || n >= sizeof r->file) return true;
    memcpy(r->name, name, n - sl);
    r->name[n - sl] = 0;
    strcpy(r->file, name);
    nroms++;
    return true;
}

static int cmp_rom(const void *a, const void *b) {
    return strcmp(((const rom_entry_t *)a)->name, ((const rom_entry_t *)b)->name);
}

static void refresh_state(rom_entry_t *r) {
    char path[300], spath[316];
    full_path(path, sizeof path, r->file);
    session_state_path(path, spath, sizeof spath);
    r->state_size = plat_file_size(spath);
}

static void scan(void) {
    nroms = 0;
    plat_list_dir(plat_app_dir(), collect, NULL);
    qsort(roms, (size_t)nroms, sizeof *roms, cmp_rom);
    for (int i = 0; i < nroms; i++) {
        char path[300];
        full_path(path, sizeof path, roms[i].file);
        roms[i].size = plat_file_size(path);
        probe_rom(&roms[i]);
        refresh_state(&roms[i]);
    }
}

/* ---- screens -------------------------------------------------------- */

static void show_info(const rom_entry_t *r) {
    char text[400], state[48];
    if (r->state_size >= 0) snprintf(state, sizeof state, "yes (%ld KB)", r->state_size / 1024);
    else strcpy(state, "no");
    snprintf(text, sizeof text,
             "File:  %s\nModel: %s\nSize:  %ld KB\nBoot:  %s\nSaved state: %s",
             r->file, r->model, r->size / 1024, r->boot[0] ? r->boot : "unknown", state);
    ui_message(r->name, text, NULL);
}

static void settings(void) {
    int sel = 0;
    for (;;) {
        char fs[32];
        if (config.frameskip < 0) strcpy(fs, "Frame skip: auto");
        else snprintf(fs, sizeof fs, "Frame skip: %d", config.frameskip);
        const char *items[] = { config.show_fps ? "Show speed: on" : "Show speed: off", fs };
        ui_list_t l = { "Settings", items, NULL, 2, sel, "enter: change   esc: back", NULL, NULL };
        int r = ui_list(&l, NULL);
        if (r < 0) break;
        sel = r;
        if (r == 0) config.show_fps = !config.show_fps;
        else config.frameskip = config.frameskip >= 3 ? -1 : config.frameskip + 1;
        config_save();
    }
}

static void about(void) {
    ui_message("About N84",
               "N84 " N84_VERSION " by FerdinandoPH\n"
               "TI-83/84 Plus and TI-84 Plus CE emulator for the TI-Nspire CX II. "
               "GPLv3. CE hardware derived from CEmu.\n\n"
               "Put your ROM dumps (NAME.rom.tns) next to n84.tns. "
               "In game, press ?!> (or ctrl+esc) for the menu. "
               "Keys can be changed in n84.keys.tns.", NULL);
}

/* options for the highlighted ROM; returns true if it should be run */
static bool rom_options(rom_entry_t *r) {
    for (;;) {
        bool has_state = r && r->state_size >= 0;
        const char *items[8];
        int ids[8], n = 0;
        enum { O_RUN, O_INFO, O_TRANSFER, O_DELSTATE, O_KEYS, O_SETTINGS, O_ABOUT };
        if (r) {
            items[n] = "Run"; ids[n++] = O_RUN;
            items[n] = "Transfer files..."; ids[n++] = O_TRANSFER;
            items[n] = "Information"; ids[n++] = O_INFO;
        }
        if (r && has_state) { items[n] = "Delete saved state"; ids[n++] = O_DELSTATE; }
        items[n] = "Key guide"; ids[n++] = O_KEYS;
        items[n] = "Settings"; ids[n++] = O_SETTINGS;
        items[n] = "About N84"; ids[n++] = O_ABOUT;
        ui_list_t l = { r ? r->name : "N84", items, NULL, n, 0, "enter: select   esc: back", NULL, NULL };
        int c = ui_list(&l, NULL);
        if (c < 0) return false;
        switch (ids[c]) {
        case O_RUN: return true;
        case O_INFO: show_info(r); break;
        case O_TRANSFER: {
            char path[300];
            full_path(path, sizeof path, r->file);
            session_transfer(path);
            refresh_state(r);
            break;
        }
        case O_DELSTATE:
            if (ui_confirm("Delete saved state",
                           "The emulated calculator will start fresh: RAM contents are lost, Archive is kept.", NULL)) {
                char path[300], spath[316];
                full_path(path, sizeof path, r->file);
                session_state_path(path, spath, sizeof spath);
                plat_remove(spath);
                refresh_state(r);
                return false;
            }
            break;
        case O_KEYS: keyguide_show(); break;
        case O_SETTINGS: settings(); break;
        case O_ABOUT: about(); break;
        }
    }
}

bool launcher_run(char *rom_path, size_t size) {
    scan();
    int sel = 0;
    for (int i = 0; i < nroms; i++)
        if (!strcmp(roms[i].file, config.last_rom)) sel = i;

    for (;;) {
        const char *items[MAX_ROMS], *detail[MAX_ROMS];
        static char details[MAX_ROMS][16];
        for (int i = 0; i < nroms; i++) {
            items[i] = roms[i].name;
            snprintf(details[i], sizeof details[i], "%s%s", roms[i].model_short, roms[i].state_size >= 0 ? " *" : "");
            detail[i] = details[i];
        }
        ui_list_t l = { "N84", items, detail, nroms, sel,
                        "enter: run   menu: options   esc: quit",
                        "No ROMs found. Copy NAME.rom.tns next to n84.tns.", NULL };
        int highlighted = sel;
        int c = ui_list(&l, &highlighted);
        if (c == -1) return false;
        if (c == -2) {                         /* options */
            sel = highlighted;
            if (!rom_options(nroms ? &roms[sel] : NULL)) continue;
            c = sel;
        }
        if (c >= 0 && c < nroms) {
            sel = c;
            snprintf(config.last_rom, sizeof config.last_rom, "%s", roms[c].file);
            config_save();
            full_path(rom_path, size, roms[c].file);
            return true;
        }
    }
}


