/*
 * N84 - file browser (picks a TI variable file)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend/browser.h"
#include "frontend/config.h"
#include "frontend/ui.h"
#include "platform/platform.h"

#define MAX_ENTRIES 128

typedef struct { char name[64]; bool dir; } entry_t;

static entry_t entries[MAX_ENTRIES];
static int nentries;
static char cur_dir[256];

/* NAME.8xp.tns, NAME.8cv.tns, ... (and without .tns on the host) */
static bool is_var_file(const char *n) {
    size_t len = strlen(n);
    const char *e = n + len;
    if (len > 4 && !strcmp(e - 4, ".tns")) { e -= 4; len -= 4; }
    if (len < 5) return false;
    const char *dot = e - 4;
    return dot[0] == '.' && dot[1] == '8' && (dot[2] == 'x' || dot[2] == 'c' || dot[2] == 'X' || dot[2] == 'C');
}

static bool collect(const char *name, void *ctx) {
    (void)ctx;
    if (nentries >= MAX_ENTRIES || strlen(name) >= sizeof entries[0].name) return true;
    char path[512];
    snprintf(path, sizeof path, "%s/%s", cur_dir, name);
    bool dir = plat_is_dir(path);
    if (!dir && !is_var_file(name)) return true;
    snprintf(entries[nentries].name, sizeof entries[0].name, "%s", name);
    entries[nentries].dir = dir;
    nentries++;
    return true;
}

static int cmp_entry(const void *a, const void *b) {
    const entry_t *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return strcmp(x->name, y->name);
}

static void scan(void) {
    nentries = 0;
    if (strcmp(cur_dir, "/") && cur_dir[0]) {
        strcpy(entries[0].name, "..");
        entries[0].dir = true;
        nentries = 1;
    }
    int first = nentries;
    plat_list_dir(cur_dir, collect, NULL);
    qsort(entries + first, (size_t)(nentries - first), sizeof *entries, cmp_entry);
}

static void go_up(void) {
    char *slash = strrchr(cur_dir, '/');
    if (!slash) return;
    if (slash == cur_dir) cur_dir[1] = 0;
    else *slash = 0;
}

bool browser_pick(const char *title, char *out, size_t size, const uint16_t *backdrop) {
    snprintf(cur_dir, sizeof cur_dir, "%s", config.last_dir[0] && plat_is_dir(config.last_dir) ? config.last_dir : plat_app_dir());
    int sel = 0;
    for (;;) {
        scan();
        const char *items[MAX_ENTRIES];
        static char labels[MAX_ENTRIES][72];
        for (int i = 0; i < nentries; i++) {
            snprintf(labels[i], sizeof labels[i], entries[i].dir ? "[%s]" : "%s", entries[i].name);
            items[i] = labels[i];
        }
        char head[300];
        const char *shown = strrchr(cur_dir, '/');
        snprintf(head, sizeof head, "%s: %s", title, shown && shown[1] ? shown + 1 : cur_dir);
        ui_list_t l = { head, items, NULL, nentries, sel, "enter: open/choose   esc: cancel",
                        "No TI files (.8xp, .8xv, ...) here.", backdrop };
        int c = ui_list(&l, NULL);
        if (c < 0) return false;
        if (entries[c].dir) {
            if (!strcmp(entries[c].name, "..")) go_up();
            else {
                size_t n = strlen(cur_dir);
                snprintf(cur_dir + n, sizeof cur_dir - n, "%s%s", n > 1 ? "/" : "", entries[c].name);
            }
            sel = 0;
            continue;
        }
        snprintf(out, size, "%s/%s", cur_dir, entries[c].name);
        snprintf(config.last_dir, sizeof config.last_dir, "%s", cur_dir);
        config_save();
        return true;
    }
}
