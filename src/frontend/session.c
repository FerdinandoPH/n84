/*
 * N84 - emulation session: machine wrappers, main loop, in-game menu
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend/session.h"
#include "frontend/config.h"
#include "frontend/ui.h"
#include "frontend/ti8x_view.h"
#include "frontend/keymap_ti8x.h"
#include "platform/platform.h"
#include "machine/ti8x/ti8x.h"
#include "machine/ce/ce.h"

#define FRAME_HZ 60u

/* ----------------------------------------------------------------------
 * Machine interface
 * -------------------------------------------------------------------- */

typedef struct {
    const char *name;
    void (*run_frame)(void);
    void (*draw)(uint16_t *fb);
    void (*release_keys)(void);
    void (*reset)(void);
    bool (*save_state)(FILE *f);
    bool (*load_state)(FILE *f);
    bool (*flash_changed)(void);
    bool (*flush_flash)(FILE *rom_file);
    void (*shutdown)(void);
} machine_t;

/* --- TI-83 Plus SE / TI-84 Plus / TI-84 Plus SE --- */

static ti8x_t ti8x;

static void ti8x_frame(void) {
    ti8x_keymap_apply(&ti8x);
    ti8x_run(&ti8x, TI8X_TICKS_PER_SEC / FRAME_HZ);
}
static void ti8x_draw_fb(uint16_t *fb) {
    static uint8_t levels[TI8X_LCD_W * TI8X_LCD_H];
    ti8x_lcd_frame(&ti8x, levels);
    ti8x_view_render(levels, ti8x.lcd.contrast, ti8x_lcd_on(&ti8x), fb);
}
static void ti8x_release(void) { ti8x_release_all_keys(&ti8x); }
static void ti8x_do_reset(void) { ti8x_reset(&ti8x); }
static bool ti8x_save(FILE *f) { return ti8x_save_state(&ti8x, f); }
static bool ti8x_load(FILE *f) { return ti8x_load_state(&ti8x, f); }
static bool ti8x_flash_changed(void) { return ti8x.flash_dirty; }
static bool ti8x_flush(FILE *f) { return ti8x_flush_flash(&ti8x, f); }
static void ti8x_shutdown(void) { ti8x_free(&ti8x); }

static const machine_t ti8x_machine = {
    NULL, ti8x_frame, ti8x_draw_fb, ti8x_release, ti8x_do_reset, ti8x_save, ti8x_load,
    ti8x_flash_changed, ti8x_flush, ti8x_shutdown,
};

/* --- TI-84 Plus CE --- */

static bool ce_keys[0x41];
static uint32_t ce_flash_gen;

static void ce_frame(void) {
    bool down[0x41];
    ti8x_keymap_scan(down);
    for (unsigned k = 0; k < 0x41; k++) {
        if (down[k] == ce_keys[k]) continue;
        ce_keys[k] = down[k];
        if (k == TK_ON) ce_key(2, 0, down[k]);
        else ce_key(7u - (k >> 3), k & 7, down[k]);   /* CE rows = 7 - TI-84 group */
    }
    ce_run(1);
}
static void ce_draw_fb(uint16_t *fb) { ce_lcd_render(fb); }
static void ce_release(void) { memset(ce_keys, 0, sizeof ce_keys); ce_release_all_keys(); }
static bool ce_flash_changed(void) { return ce_flash_generation() != ce_flash_gen; }
static bool ce_flush(FILE *f) {
    size_t size;
    const uint8_t *flash = ce_flash(&size);
    if (fwrite(flash, 1, size, f) != size) return false;
    ce_flash_gen = ce_flash_generation();
    return true;
}

static const machine_t ce_machine = {
    "TI-84 Plus CE", ce_frame, ce_draw_fb, ce_release, ce_reset, ce_save_state, ce_load_state,
    ce_flash_changed, ce_flush, ce_free,
};

/* ----------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------- */

void session_state_path(const char *rom, char *buf, size_t size) {
    snprintf(buf, size, "%s", rom);
    char *dot = strstr(buf, ".rom");
    if (dot) memcpy(dot, ".sav", 4);
    else strncat(buf, ".sav.tns", size - strlen(buf) - 1);
}

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

static bool save_state_file(const machine_t *m, const char *path) {
    FILE *f = plat_fopen(path, "wb");
    if (!f) return false;
    bool ok = m->save_state(f);
    fclose(f);
    return ok;
}

/* 0 = no file, 1 = loaded, -1 = incompatible (machine was reset) */
static int load_state_file(const machine_t *m, const char *path) {
    FILE *f = plat_fopen(path, "rb");
    if (!f) return 0;
    bool ok = m->load_state(f);
    fclose(f);
    if (!ok) m->reset();
    return ok ? 1 : -1;
}

static bool flush_flash(const machine_t *m, const char *rom_path) {
    if (!m->flash_changed()) return true;
    FILE *f = plat_fopen(rom_path, "r+b");
    bool ok = f && m->flush_flash(f);
    if (f) fclose(f);
    return ok;
}

/* small "NN fps  NNN%" box in the top-right corner */
static void draw_fps(uint16_t *fb, unsigned fps, unsigned speed) {
    (void)fb;
    char s[24];
    snprintf(s, sizeof s, "%u fps %u%%", fps, speed);
    int w = ui_text_width(s) + 6;
    ui_rect(UI_W - w, 0, w, ui_line_height() + 2, UI_RGB(0, 0, 0));
    ui_text(UI_W - w + 3, 1, s, speed >= 95 ? UI_FG : UI_ACCENT);
}

/* ----------------------------------------------------------------------
 * In-game menu
 * -------------------------------------------------------------------- */

enum { M_RESUME, M_SAVE, M_LOAD, M_RESET, M_TURBO, M_FPS, M_LAUNCHER, M_QUIT, M_COUNT };

typedef enum { ACT_RESUME, ACT_LAUNCHER, ACT_QUIT } menu_act_t;

static menu_act_t ingame_menu(const machine_t *m, const char *state_path, bool *turbo) {
    static uint16_t backdrop[UI_W * UI_H];
    memcpy(backdrop, plat_video_buffer(), sizeof backdrop);
    m->release_keys();
    int sel = 0;
    for (;;) {
        const char *items[M_COUNT] = {
            "Resume", "Save state", "Load state", "Reset calculator",
            *turbo ? "Turbo: on" : "Turbo: off", config.show_fps ? "Show speed: on" : "Show speed: off",
            "Exit to launcher", "Quit N84",
        };
        ui_list_t l = { m->name, items, NULL, M_COUNT, sel, NULL, NULL, backdrop };
        int r = ui_list(&l, NULL);
        if (r >= 0) sel = r;
        switch (r) {
        case -1: case M_RESUME: return ACT_RESUME;
        case M_SAVE:
            ui_message("Save state", save_state_file(m, state_path) ? "State saved." : "Could not write the state file.", backdrop);
            return ACT_RESUME;
        case M_LOAD: {
            int lr = load_state_file(m, state_path);
            if (lr == 1) return ACT_RESUME;
            ui_message("Load state", lr == 0 ? "There is no saved state for this ROM." :
                       "The saved state is incompatible; the calculator was reset.", backdrop);
            if (lr < 0) return ACT_RESUME;
            break;
        }
        case M_RESET:
            if (ui_confirm("Reset calculator", "Reset the emulated calculator? RAM contents will be lost (Archive is kept).", backdrop)) {
                m->reset();
                return ACT_RESUME;
            }
            break;
        case M_TURBO: *turbo = !*turbo; break;
        case M_FPS: config.show_fps = !config.show_fps; config_save(); break;
        case M_LAUNCHER: return ACT_LAUNCHER;
        case M_QUIT: return ACT_QUIT;
        default: break;
        }
    }
}

/* ----------------------------------------------------------------------
 * Session
 * -------------------------------------------------------------------- */

session_result_t session_run(const char *rom_path) {
    size_t rom_size;
    uint8_t *rom = load_file(rom_path, &rom_size);
    if (!rom) { ui_message("Error", "Could not read the ROM file.", NULL); return SESSION_ERROR; }

    machine_t m;
    if (rom_size >= 0x400000) {
        m = ce_machine;
        bool ok = ce_init(rom, rom_size);
        free(rom);
        if (!ok) { ui_message("Error", "Could not start the TI-84 Plus CE (unsupported image or out of memory).", NULL); return SESSION_ERROR; }
        memset(ce_keys, 0, sizeof ce_keys);
        ce_flash_gen = ce_flash_generation();
    } else {
        m = ti8x_machine;
        ti8x_model_t model = ti8x_detect_model(rom, rom_size);
        bool ok = model != TI8X_MODEL_NONE && ti8x_init(&ti8x, model, rom, rom_size);
        free(rom);
        if (!ok) { ui_message("Error", "Unsupported ROM image (expected a TI-83 Plus SE, TI-84 Plus, TI-84 Plus SE or TI-84 Plus CE dump), or out of memory.", NULL); return SESSION_ERROR; }
        m.name = ti8x_model_name(model);
    }

    char state_path[300];
    session_state_path(rom_path, state_path, sizeof state_path);
    if (load_state_file(&m, state_path) < 0)
        ui_message("Saved state", "The saved state is incompatible with this version; starting fresh.", NULL);

    uint16_t *fb = plat_video_buffer();
    ui_clear(UI_RGB(0, 0, 0));
    if (m.draw == ti8x_draw_fb) ti8x_view_init(fb);
    bool turbo = false;
    menu_act_t act = ACT_RESUME;
    uint32_t t0 = plat_ticks(), frames = 0;
    uint32_t stat_t = t0, stat_emu = 0, stat_drawn = 0;
    unsigned fps = 0, speed = 0;

    ui_wait_release();
    while (act == ACT_RESUME) {
        plat_input_poll();
        if (plat_key_down(PK_QUESEXCL) || (plat_key_down(PK_CTRL) && plat_key_down(PK_ESC))) {
            act = ingame_menu(&m, state_path, &turbo);
            if (act != ACT_RESUME) break;
            ui_clear(UI_RGB(0, 0, 0));
            if (m.draw == ti8x_draw_fb) ti8x_view_init(fb);
            ui_wait_release();
            t0 = stat_t = plat_ticks();
            frames = stat_emu = stat_drawn = 0;
            continue;
        }
        m.run_frame();
        frames++;
        stat_emu++;

        bool draw = true;
        if (turbo) {
            draw = (frames % 8) == 0;
        } else {
            uint32_t target = t0 + (uint32_t)((uint64_t)frames * PLAT_TICKS_PER_SEC / FRAME_HZ);
            int32_t ahead = (int32_t)(target - plat_ticks());
            if (config.frameskip >= 0) draw = (frames % (unsigned)(config.frameskip + 1)) == 0;
            if (ahead > 0) plat_wait_until(target);
            else if (ahead < -(int32_t)(PLAT_TICKS_PER_SEC / 4)) { t0 = plat_ticks(); frames = 0; }
            else if (config.frameskip < 0 && ahead < -(int32_t)(PLAT_TICKS_PER_SEC / FRAME_HZ)) draw = (frames % 2) == 0;
        }
        if (draw) {
            m.draw(fb);
            stat_drawn++;
            if (config.show_fps) draw_fps(fb, fps, speed);
            plat_video_present();
        }
        uint32_t now = plat_ticks();
        if (now - stat_t >= PLAT_TICKS_PER_SEC) {
            uint32_t dt = now - stat_t;
            fps = (unsigned)((uint64_t)stat_drawn * PLAT_TICKS_PER_SEC / dt);
            speed = (unsigned)((uint64_t)stat_emu * 100 * PLAT_TICKS_PER_SEC / dt / FRAME_HZ);
            stat_t = now;
            stat_emu = stat_drawn = 0;
        }
    }

    /* leaving: save state and write Flash back */
    ui_clear(UI_BG);
    ui_text(110, 112, "Saving...", UI_FG);
    ui_present();
    bool ok_state = save_state_file(&m, state_path);
    bool ok_flash = flush_flash(&m, rom_path);
    if (!ok_state || !ok_flash)
        ui_message("Warning", !ok_state ? "Could not write the saved state." :
                   "Could not write Flash (Archive) changes back to the ROM file.", NULL);
    m.shutdown();
    return act == ACT_QUIT ? SESSION_QUIT : SESSION_TO_LAUNCHER;
}
