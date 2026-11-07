/*
 * N84 - global settings (n84.cfg.tns next to n84.tns, "key=value" lines)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_CONFIG_H
#define N84_CONFIG_H
#include <stdbool.h>

typedef struct {
    bool show_fps;        /* speed / FPS indicator in a corner */
    int  frameskip;       /* -1 = automatic, 0..3 = draw 1 frame of N+1 */
    char last_rom[128];   /* file name of the last ROM run (launcher cursor) */
    char last_dir[256];   /* folder last used in the file browser */
} n84_config_t;

extern n84_config_t config;

void config_load(void);
void config_save(void);

/* "<app dir>/<name>" into buf */
void app_path(char *buf, unsigned size, const char *name);

#endif
