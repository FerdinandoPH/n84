/*
 * N84 - ti8x machine test using a synthetic ROM (tools/mk_ti8x_testrom.py)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine/ti8x/ti8x.h"
#include "frontend/ti8x_view.h"
#include "platform/host/plat_host.h"

static int failures;
#define CHECK(cond, ...) do { if (cond) printf("  ok   "); else { printf("  FAIL "); failures++; } \
                              printf(__VA_ARGS__); printf("\n"); } while (0)

static uint8_t *load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*size);
    if (fread(b, 1, *size, f) != *size) exit(2);
    fclose(f);
    return b;
}

static uint8_t ram(ti8x_t *m, uint16_t addr) { return m->ram[addr - 0xC000]; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s test.rom [out.ppm]\n", argv[0]); return 2; }
    size_t size;
    uint8_t *rom = load(argv[1], &size);
    ti8x_model_t model = ti8x_detect_model(rom, size);
    printf("model: %s\n", ti8x_model_name(model));
    CHECK(model == TI8X_MODEL_84P, "ROM detected as TI-84 Plus");

    static ti8x_t m;
    if (!ti8x_init(&m, model, rom, size)) { printf("init failed\n"); return 1; }

    ti8x_run(&m, TI8X_TICKS_PER_SEC);          /* 1 emulated second */
    CHECK(!memcmp(&m.ram[0x20], "N84", 3), "test program reached main loop");
    CHECK(ram(&m, 0xC010) == 0x42, "Flash program -> %02X (expect 42)", ram(&m, 0xC010));
    CHECK(ram(&m, 0xC011) == 0xFF, "Flash erase   -> %02X (expect FF)", ram(&m, 0xC011));
    CHECK(ram(&m, 0xC012) & 0x04, "port 02h reports Flash unlocked (%02X)", ram(&m, 0xC012));
    CHECK(!m.flash_unlocked, "Flash locked again");
    CHECK(m.flash_dirty != 0, "dirty sector bitmap set (%08X)", m.flash_dirty);
    unsigned ints = (unsigned)(ram(&m, 0xC000) | ram(&m, 0xC001) << 8);
    /* slowest HW timer: 9277 us period -> ~107 Hz */
    CHECK(ints >= 100 && ints <= 112, "timer 1 interrupts in 1 s: %u (expect ~107)", ints);

    CHECK(ram(&m, 0xC002) == 0xFF, "no arrow pressed -> %02X", ram(&m, 0xC002));
    ti8x_set_key(&m, TK_UP, true);
    ti8x_run(&m, TI8X_TICKS_PER_SEC / 20);
    CHECK(ram(&m, 0xC002) == 0xF7, "UP pressed     -> %02X (expect F7)", ram(&m, 0xC002));
    ti8x_set_key(&m, TK_UP, false);
    ti8x_run(&m, TI8X_TICKS_PER_SEC / 20);
    CHECK(ram(&m, 0xC002) == 0xFF, "UP released    -> %02X", ram(&m, 0xC002));

    uint8_t lv[TI8X_LCD_W * TI8X_LCD_H];
    ti8x_lcd_frame(&m, lv);
    CHECK(ti8x_lcd_on(&m), "LCD on");
    /* row y, column byte c: pattern F0 when ((64-y) ^ c) & 1 else 0F (rows written with b=64..1) */
    int bad = 0;
    for (int y = 0; y < TI8X_LCD_H; y++)
        for (int x = 0; x < TI8X_LCD_W; x++) {
            int c = x / 8, b = 64 - y;
            uint8_t pat = ((b ^ c) & 1) ? 0xF0 : 0x0F;
            int expect = (pat >> (7 - (x & 7))) & 1;
            if ((lv[y * TI8X_LCD_W + x] != 0) != expect) bad++;
        }
    CHECK(bad == 0, "LCD pattern matches (%d bad pixels)", bad);
    CHECK(m.lcd.contrast == 0x30, "contrast %02X", m.lcd.contrast);

    if (argc > 2) {
        plat_video_init();
        uint16_t *fb = plat_video_buffer();
        ti8x_view_init(fb);
        ti8x_view_render(lv, m.lcd.contrast, ti8x_lcd_on(&m), fb);
        plat_host_save_ppm(argv[2]);
        printf("frame saved to %s\n", argv[2]);
    }

    /* savestate round trip: save, run, load, run again -> identical results */
    FILE *sf = tmpfile();
    CHECK(sf && ti8x_save_state(&m, sf), "state saved");
    ti8x_run(&m, TI8X_TICKS_PER_SEC / 2);
    uint16_t pc1 = m.cpu.pc.w;
    unsigned ints1 = (unsigned)(ram(&m, 0xC000) | ram(&m, 0xC001) << 8);
    rewind(sf);
    CHECK(ti8x_load_state(&m, sf), "state loaded");
    fclose(sf);
    ti8x_run(&m, TI8X_TICKS_PER_SEC / 2);
    unsigned ints2 = (unsigned)(ram(&m, 0xC000) | ram(&m, 0xC001) << 8);
    CHECK(pc1 == m.cpu.pc.w && ints1 == ints2, "replay after load is deterministic (PC %04X/%04X, ints %u/%u)",
          pc1, m.cpu.pc.w, ints1, ints2);

    printf("CPU cycles: %llu\n", (unsigned long long)m.cpu.total_cycles);
    ti8x_free(&m);
    free(rom);
    printf(failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
    return failures != 0;
}
