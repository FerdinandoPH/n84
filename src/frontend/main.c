/*
 * N84 - main program (TI-Nspire). Opens a ROM image and runs it.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage on the calculator:
 *   - run n84.tns once to register the ".rom" extension
 *   - open NAME.rom.tns from "My Documents"
 *   - ctrl+esc opens the emulator menu (continue / turbo / save & exit)
 * State is kept in NAME.sav.tns; Flash changes are written back to the ROM.
 */
#include <stdlib.h>
#include <string.h>
#include "platform/platform.h"
#include "machine/ti8x/ti8x.h"
#include "frontend/ti8x_view.h"
#include "frontend/keymap_ti8x.h"

#define FRAME_HZ 60u

static uint8_t *load_file(const char *path, size_t *size) {
    FILE *f = plat_fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = buf ? (size_t)n : 0;
    return buf;
}

/* "dir/name.rom.tns" -> "dir/name.sav.tns" */
static char *state_path_for(const char *rom) {
    size_t n = strlen(rom);
    char *p = malloc(n + 16);
    if (!p) return NULL;
    strcpy(p, rom);
    char *dot = strstr(p, ".rom");
    if (dot) memcpy(dot, ".sav", 4);
    else strcat(p, ".sav.tns");
    return p;
}

static void save_all(ti8x_t *m, const char *rom_path, const char *state_path) {
    FILE *f = plat_fopen(state_path, "wb");
    if (f) {
        if (!ti8x_save_state(m, f)) plat_log("state save failed\n");
        fclose(f);
    }
    if (m->flash_dirty) {
        FILE *r = plat_fopen(rom_path, "r+b");
        if (!r || !ti8x_flush_flash(m, r)) plat_msgbox("N84", "Could not write Flash changes back to the ROM file.");
        if (r) fclose(r);
    }
}

int main(int argc, char **argv) {
    if (!plat_init(argc, argv)) return 1;
    if (argc < 2) {
        plat_register_ext("rom", "n84");
        plat_msgbox("N84", "Extension registered.\nOpen a ROM image (NAME.rom.tns) from My Documents to start.");
        plat_shutdown();
        return 0;
    }
    const char *rom_path = argv[1];
    size_t rom_size;
    uint8_t *rom = load_file(rom_path, &rom_size);
    if (!rom) { plat_msgbox("N84", "Could not read the ROM file."); plat_shutdown(); return 1; }

    ti8x_model_t model = ti8x_detect_model(rom, rom_size);
    static ti8x_t m;
    if (model == TI8X_MODEL_NONE || !ti8x_init(&m, model, rom, rom_size)) {
        free(rom);
        plat_msgbox("N84", "Unsupported ROM image (expected a TI-83+SE / 84+ / 84+SE dump), or out of memory.");
        plat_shutdown();
        return 1;
    }
    free(rom);

    char *state_path = state_path_for(rom_path);
    FILE *sf = state_path ? plat_fopen(state_path, "rb") : NULL;
    if (sf) {
        if (!ti8x_load_state(&m, sf)) {
            plat_msgbox("N84", "Saved state is incompatible; starting fresh.");
            ti8x_reset(&m);
        }
        fclose(sf);
    }

    if (!plat_video_init()) { plat_msgbox("N84", "LCD init failed."); plat_shutdown(); return 1; }
    uint16_t *fb = plat_video_buffer();
    ti8x_view_init(fb);

    static uint8_t levels[TI8X_LCD_W * TI8X_LCD_H];
    bool turbo = false, quit = false;
    uint32_t t0 = plat_ticks();
    uint32_t frames = 0;

    while (!quit) {
        plat_input_poll();
        if (plat_key_down(PK_CTRL) && plat_key_down(PK_ESC)) {
            int r = plat_menu3("N84", ti8x_model_name(m.model), "Continue",
                               turbo ? "Turbo: off" : "Turbo: on", "Save & exit");
            if (r == 2) turbo = !turbo;
            if (r == 3) quit = true;
            ti8x_release_all_keys(&m);
            ti8x_view_init(fb);
            t0 = plat_ticks();
            frames = 0;
            continue;
        }
        ti8x_keymap_apply(&m);
        ti8x_run(&m, TI8X_TICKS_PER_SEC / FRAME_HZ);
        frames++;

        bool draw = true;
        if (turbo) {
            draw = (frames % 8) == 0;
        } else {
            uint32_t target = t0 + (uint32_t)((uint64_t)frames * PLAT_TICKS_PER_SEC / FRAME_HZ);
            int32_t ahead = (int32_t)(target - plat_ticks());
            if (ahead > 0) plat_wait_until(target);
            else if (ahead < -(int32_t)(PLAT_TICKS_PER_SEC / 4)) { t0 = plat_ticks(); frames = 0; }
            else if (ahead < -(int32_t)(PLAT_TICKS_PER_SEC / FRAME_HZ)) draw = (frames % 2) == 0;
        }
        if (draw) {
            ti8x_lcd_frame(&m, levels);
            ti8x_view_render(levels, m.lcd.contrast, ti8x_lcd_on(&m), fb);
            plat_video_present();
        }
    }

    plat_video_shutdown();
    save_all(&m, rom_path, state_path);
    free(state_path);
    ti8x_free(&m);
    plat_shutdown();
    return 0;
}
