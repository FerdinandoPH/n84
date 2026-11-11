/*
 * N84 - main program (TI-Nspire)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Running n84.tns opens the launcher, which lists the ROM images
 * (NAME.rom.tns) found next to it. A ROM path given as argument starts it
 * directly (N84 does not register ".rom": other programs use it).
 * In game, [?!>] (or ctrl+esc) opens N84's menu.
 */
#include "platform/platform.h"
#include "frontend/config.h"
#include "frontend/keymap_ti8x.h"
#include "frontend/launcher.h"
#include "frontend/session.h"
#include "frontend/ui.h"

int main(int argc, char **argv) {
    if (!plat_init(argc, argv)) return 1;
    config_load();
    if (!plat_video_init()) { plat_msgbox("N84", "LCD init failed."); plat_shutdown(); return 1; }

    char path[300], err[96];
    app_path(path, sizeof path, KEYMAP_FILE);
    if (keymap_load(path, err, sizeof err) < 0) ui_message("Key file", err, NULL);

    char rom_path[300];
    bool direct = argc >= 2;
    for (;;) {
        if (direct) {
            snprintf(rom_path, sizeof rom_path, "%s", argv[1]);
            direct = false;
        } else if (!launcher_run(rom_path, sizeof rom_path)) {
            break;
        }
        if (session_run(rom_path) == SESSION_QUIT) break;
    }
    plat_video_shutdown();
    plat_shutdown();
    return 0;
}
