/*
 * N84 - main program (TI-Nspire). Opens a ROM image and runs it.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Usage on the calculator:
 *   - run n84.tns once to register the ".rom" extension
 *   - open NAME.rom.tns from "My Documents"; the model is detected from the
 *     image (1-2 MB: TI-83+SE/84+/84+SE, 4 MB: TI-84 Plus CE)
 *   - the [?!>] key (or ctrl+esc) opens the emulator menu
 *     (continue / turbo / save & exit). [?!>] has no TI-84 equivalent, and a
 *     single key also works in Firebird, where ctrl+esc is taken by Windows.
 * State is kept in NAME.sav.tns; Flash changes are written back to the ROM.
 */
#include <stdlib.h>
#include <string.h>
#include "platform/platform.h"
#include "machine/ti8x/ti8x.h"
#include "machine/ce/ce.h"
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

/* ----------------------------------------------------------------------
 * Machine-independent main loop: menu, pacing, frame skipping
 * -------------------------------------------------------------------- */

typedef struct {
    const char *name;
    void (*run_frame)(void *m);          /* input + 1/60 s of emulation */
    void (*draw)(void *m, uint16_t *fb); /* render the current screen */
    void (*release_keys)(void *m);
    void (*redraw_static)(void *m, uint16_t *fb);  /* after a dialog (borders) */
} machine_ops_t;

static void main_loop(const machine_ops_t *ops, void *m) {
    uint16_t *fb = plat_video_buffer();
    bool turbo = false, quit = false;
    uint32_t t0 = plat_ticks();
    uint32_t frames = 0;

    if (ops->redraw_static) ops->redraw_static(m, fb);
    while (!quit) {
        plat_input_poll();
        if (plat_key_down(PK_QUESEXCL) || (plat_key_down(PK_CTRL) && plat_key_down(PK_ESC))) {
            int r = plat_menu3("N84", ops->name, "Continue", turbo ? "Turbo: off" : "Turbo: on", "Save & exit");
            if (r == 2) turbo = !turbo;
            if (r == 3) quit = true;
            ops->release_keys(m);
            if (ops->redraw_static) ops->redraw_static(m, fb);
            t0 = plat_ticks();
            frames = 0;
            continue;
        }
        ops->run_frame(m);
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
            ops->draw(m, fb);
            plat_video_present();
        }
    }
}

/* ----------------------------------------------------------------------
 * TI-83 Plus SE / TI-84 Plus / TI-84 Plus SE
 * -------------------------------------------------------------------- */

static void ti8x_run_frame(void *mp) {
    ti8x_t *m = mp;
    ti8x_keymap_apply(m);
    ti8x_run(m, TI8X_TICKS_PER_SEC / FRAME_HZ);
}
static void ti8x_draw(void *mp, uint16_t *fb) {
    static uint8_t levels[TI8X_LCD_W * TI8X_LCD_H];
    ti8x_t *m = mp;
    ti8x_lcd_frame(m, levels);
    ti8x_view_render(levels, m->lcd.contrast, ti8x_lcd_on(m), fb);
}
static void ti8x_release(void *mp) { ti8x_release_all_keys(mp); }
static void ti8x_static(void *mp, uint16_t *fb) { (void)mp; ti8x_view_init(fb); }

static int run_ti8x(uint8_t *rom, size_t rom_size, const char *rom_path, const char *state_path) {
    static ti8x_t m;
    ti8x_model_t model = ti8x_detect_model(rom, rom_size);
    if (model == TI8X_MODEL_NONE || !ti8x_init(&m, model, rom, rom_size)) {
        free(rom);
        plat_msgbox("N84", "Unsupported ROM image (expected a TI-83+SE / 84+ / 84+SE or TI-84 Plus CE dump), or out of memory.");
        return 1;
    }
    free(rom);
    FILE *sf = state_path ? plat_fopen(state_path, "rb") : NULL;
    if (sf) {
        if (!ti8x_load_state(&m, sf)) {
            plat_msgbox("N84", "Saved state is incompatible; starting fresh.");
            ti8x_reset(&m);
        }
        fclose(sf);
    }
    if (!plat_video_init()) { plat_msgbox("N84", "LCD init failed."); return 1; }

    static const machine_ops_t ops = { NULL, ti8x_run_frame, ti8x_draw, ti8x_release, ti8x_static };
    machine_ops_t o = ops;
    o.name = ti8x_model_name(m.model);
    main_loop(&o, &m);
    plat_video_shutdown();

    FILE *f = state_path ? plat_fopen(state_path, "wb") : NULL;
    if (f) {
        if (!ti8x_save_state(&m, f)) plat_log("state save failed\n");
        fclose(f);
    }
    if (m.flash_dirty) {
        FILE *r = plat_fopen(rom_path, "r+b");
        if (!r || !ti8x_flush_flash(&m, r)) plat_msgbox("N84", "Could not write Flash changes back to the ROM file.");
        if (r) fclose(r);
    }
    ti8x_free(&m);
    return 0;
}

/* ----------------------------------------------------------------------
 * TI-84 Plus CE
 * -------------------------------------------------------------------- */

static bool ce_keys[0x41];

static void ce_run_frame(void *unused) {
    (void)unused;
    bool down[0x41];
    ti8x_keymap_scan(down);
    for (unsigned k = 0; k < 0x41; k++) {
        if (down[k] == ce_keys[k]) continue;
        ce_keys[k] = down[k];
        if (k == TK_ON) ce_key(2, 0, down[k]);
        else ce_key(7u - (k >> 3), k & 7, down[k]);
    }
    ce_run(1);
}
static void ce_draw(void *unused, uint16_t *fb) { (void)unused; ce_lcd_render(fb); }
static void ce_release(void *unused) {
    (void)unused;
    memset(ce_keys, 0, sizeof ce_keys);
    ce_release_all_keys();
}

static int run_ce(uint8_t *rom, size_t rom_size, const char *rom_path, const char *state_path) {
    bool ok = ce_init(rom, rom_size);
    free(rom);
    if (!ok) { plat_msgbox("N84", "Could not start the TI-84 Plus CE (out of memory?)."); return 1; }
    FILE *sf = state_path ? plat_fopen(state_path, "rb") : NULL;
    if (sf) {
        if (!ce_load_state(sf)) {
            plat_msgbox("N84", "Saved state is incompatible; starting fresh.");
            ce_reset();
        }
        fclose(sf);
    }
    if (!plat_video_init()) { plat_msgbox("N84", "LCD init failed."); ce_free(); return 1; }
    uint32_t flash_gen = ce_flash_generation();

    static const machine_ops_t ops = { "TI-84 Plus CE", ce_run_frame, ce_draw, ce_release, NULL };
    main_loop(&ops, NULL);
    plat_video_shutdown();

    FILE *f = state_path ? plat_fopen(state_path, "wb") : NULL;
    if (f) {
        if (!ce_save_state(f)) plat_log("state save failed\n");
        fclose(f);
    }
    if (ce_flash_generation() != flash_gen) {
        size_t size;
        const uint8_t *flash = ce_flash(&size);
        FILE *r = plat_fopen(rom_path, "r+b");
        if (!r || fwrite(flash, 1, size, r) != size)
            plat_msgbox("N84", "Could not write Flash changes back to the ROM file.");
        if (r) fclose(r);
    }
    ce_free();
    return 0;
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

    char *state_path = state_path_for(rom_path);
    int rc = rom_size >= 0x400000 ? run_ce(rom, rom_size, rom_path, state_path)
                                  : run_ti8x(rom, rom_size, rom_path, state_path);
    free(state_path);
    plat_shutdown();
    return rc;
}
