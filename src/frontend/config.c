/*
 * N84 - global settings
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend/config.h"
#include "platform/platform.h"

#define CONFIG_FILE "n84.cfg.tns"

n84_config_t config = { false, -1, false, "", "" };

void app_path(char *buf, unsigned size, const char *name) {
    snprintf(buf, size, "%s/%s", plat_app_dir(), name);
}

void config_load(void) {
    char path[300], line[200];
    app_path(path, sizeof path, CONFIG_FILE);
    FILE *f = plat_fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(line, "show_fps")) config.show_fps = atoi(v) != 0;
        else if (!strcmp(line, "welcome_done")) config.welcome_done = atoi(v) != 0;
        else if (!strcmp(line, "frameskip")) {
            config.frameskip = atoi(v);
            if (config.frameskip < -1 || config.frameskip > 3) config.frameskip = -1;
        } else if (!strcmp(line, "last_dir")) {
            strncpy(config.last_dir, v, sizeof config.last_dir - 1);
            config.last_dir[sizeof config.last_dir - 1] = 0;
        } else if (!strcmp(line, "last_rom")) {
            strncpy(config.last_rom, v, sizeof config.last_rom - 1);
            config.last_rom[sizeof config.last_rom - 1] = 0;
        }
    }
    fclose(f);
}

void config_save(void) {
    char path[300];
    app_path(path, sizeof path, CONFIG_FILE);
    FILE *f = plat_fopen(path, "w");
    if (!f) return;
    fprintf(f, "show_fps=%d\nframeskip=%d\nwelcome_done=%d\nlast_rom=%s\nlast_dir=%s\n", config.show_fps,
            config.frameskip, config.welcome_done, config.last_rom, config.last_dir);
    fclose(f);
}
