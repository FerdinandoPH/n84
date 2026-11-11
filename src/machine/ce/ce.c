/*
 * N84 - TI-84 Plus CE machine glue
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Replaces CEmu's asic.c/emu.c: plugs the CEmu-derived peripherals into
 * N84's eZ80 core.
 *
 *  - Memory: RAM and plainly-readable Flash are mapped straight into the
 *    core's 4 KiB page table (with CEmu's access costs). Everything else
 *    (Flash commands, MMIO, unmapped areas) goes through CEmu's
 *    mem_read_cpu/mem_write_cpu. The Flash part of the page table is rebuilt
 *    whenever the Flash controller's mapping or command state changes.
 *  - Time: the core's cycle counter is CEmu's cpu.cycles; its cycle target
 *    is the next scheduler event (cpu_restore_next).
 *  - Interrupts: the interrupt controller drives the core's IRQ line.
 *  - The Flash unlock sequence, which CEmu detects on instruction fetch, is
 *    detected when its IN0 A,(28h) executes (see detect_flash_unlock).
 */
#include <stdlib.h>
#include <string.h>
#include "ce.h"
#include "asic.h"
#include "cpu.h"
#include "mem.h"
#include "flash.h"
#include "control.h"
#include "lcd.h"
#include "keypad.h"
#include "interrupt.h"
#include "timers.h"
#include "realclock.h"
#include "sha256.h"
#include "backlight.h"
#include "misc.h"
#include "spi.h"
#include "port.h"
#include "schedule.h"
#include "bus.h"
#include "panel.h"
#include "bootver.h"
#include "usb/usb.h"
#include "emu.h"

ce_cpu_t cpu;
asic_state_t asic;
panel_state_t panel;

static uint32_t lcd_frame_count;
static uint32_t flash_generation;
static struct { uint32_t mask, wait; uint8_t command; bool valid; } flash_map;

/* ======================================================================
 * CPU interface used by the peripherals (see cpu.h)
 * ==================================================================== */

void cpu_restore_next(void) {
    cpu.z.cycle_target = cpu.signals ? 0 : (int32_t)sched_event_next_cycle();
}

void cpu_nmi(void) { ez80_nmi(&cpu.z); }

void cpu_set_signal(uint8_t signal) {
    cpu.signals |= signal;
    cpu.z.cycle_target = 0;           /* leave the core as soon as possible */
}

uint8_t cpu_clear_signals(void) {
    uint8_t s = cpu.signals;
    cpu.signals = 0;
    return s;
}

void cpu_crash(const char *msg) {
    (void)msg;
    gui_console_printf("[N84] CPU crash: %s\n", msg);
    cpu_set_signal(CPU_SIGNAL_RESET);
}

uint32_t cpu_address_mode(uint32_t address, bool mode) {
    return mode ? (address & 0xFFFFFF) : (((uint32_t)cpu.z.mbase << 16) | (address & 0xFFFF));
}

static void cpu_reset(void) {
    ez80_reset(&cpu.z);
    cpu.z.cycles = 0;
    cpu.z.irq_line = 0;
    cpu.seconds = cpu.eiDelay = 0;
    cpu.baseCycles = cpu.haltCycles = cpu.dmaCycles = 0;
    cpu.flashTotalAccesses = cpu.flashCacheMisses = 0;
    cpu.flashDelayCycles = 0;
    cpu_restore_next();
}

/* ======================================================================
 * ASIC (CEmu asic.c equivalents)
 * ==================================================================== */

void set_cpu_clock(uint32_t new_rate) { sched_set_clock(CLOCK_CPU, new_rate); }
void set_device_type(emu_device_t device) { asic.device = device; }
emu_device_t get_device_type(void) { return asic.device; }
asic_rev_t get_asic_revision(void) { return asic.revision; }
bool get_asic_python(void) { return asic.python; }

/* Stubs for hardware N84 does not emulate */
static uint8_t null_read(uint16_t pio, bool peek) { (void)pio; (void)peek; return 0; }
static void null_write(uint16_t pio, uint8_t value, bool poke) { (void)pio; (void)value; (void)poke; }
static const eZ80portrange_t null_device = { .read = null_read, .write = null_write };
eZ80portrange_t init_usb(void) { return null_device; }
void usb_reset(void) {}
static eZ80portrange_t init_uart(void) { return null_device; }
static void uart_reset(void) {}

void panel_spi_select(bool low) { (void)low; }
uint8_t panel_spi_peek(uint32_t *rxData) { (void)rxData; return 9; }     /* as CEmu */
uint8_t panel_spi_transfer(uint32_t txData, uint32_t *rxData) { (void)txData; (void)rxData; return 9; }

static void (*const reset_procs[])(void) = {
    sched_reset, flash_reset, mem_reset, lcd_reset, keypad_reset, gpt_reset, rtc_reset,
    watchdog_reset, cpu_reset, intrpt_reset, sha256_reset, usb_reset, control_reset,
    backlight_reset, panel_reset, spi_reset, uart_reset,
};

static void plug_devices(void) {
    port_map[0x0] = init_control();
    port_map[0x1] = init_flash();
    port_map[0x2] = init_sha256();
    port_map[0x3] = init_usb();
    port_map[0x4] = init_lcd();
    port_map[0x5] = init_intrpt();
    port_map[0x6] = init_watchdog();
    port_map[0x7] = init_gpt();
    port_map[0x8] = init_rtc();
    port_map[0x9] = init_protected();
    port_map[0xA] = init_keypad();
    port_map[0xB] = init_backlight();
    port_map[0xC] = init_cxxx();
    port_map[0xD] = init_spi();
    port_map[0xE] = init_uart();
    port_map[0xF] = init_fxxx();
    init_panel();
}

static asic_rev_t detect_revision(void) {
    /* Same rule as CEmu: newest revision compatible with the boot code */
    boot_ver_t bv;
    asic_rev_t rev = ASIC_REV_PRE_A;
    if (bootver_parse(mem.flash.block, &bv)) {
        for (int r = ASIC_REV_PRE_A; r <= ASIC_REV_M; r++)
            if (bootver_check_rev(&bv, (asic_rev_t)r, asic.device)) rev = (asic_rev_t)r;
    }
    return rev;
}

/* ======================================================================
 * Memory map for the core
 * ==================================================================== */

#define RAM_READ_COST  4    /* CEmu: sched_process_pending_dma(4) on reads */
#define RAM_WRITE_COST 2    /* ... and (2) on writes */

static void map_flash(void) {
    ez80_t *z = &cpu.z;
    uint32_t wait = flash.waitStates > 255 ? 255 : flash.waitStates;
    bool plain = !asic.serFlash && mem.flash.command == FLASH_NO_COMMAND;
    for (uint32_t pg = 0; pg < 0xD00; pg++) {
        uint32_t a = pg << EZ80_PAGE_SHIFT;
        if (plain && a + EZ80_PAGE_SIZE <= flash.mask) {
            z->rd[pg] = mem.flash.block + (a & (mem.flash.size - 1));
            z->cost[pg] = (uint8_t)wait;
        } else {
            z->rd[pg] = NULL;
            z->cost[pg] = 0;              /* mem_read_cpu charges the cycles */
        }
        z->wr[pg] = NULL;
        z->wcost[pg] = 0;
    }
    flash_map.mask = flash.mask;
    flash_map.wait = flash.waitStates;
    flash_map.command = mem.flash.command;
    flash_map.valid = true;
}

static inline void check_flash_map(void) {
    if (!flash_map.valid || flash_map.mask != flash.mask || flash_map.wait != flash.waitStates ||
        flash_map.command != mem.flash.command)
        map_flash();
}

static void map_all(void) {
    ez80_t *z = &cpu.z;
    flash_map.valid = false;
    map_flash();
    /* C programs copy hot routines into the LCD cursor SRAM (E30800h-E30BFFh,
     * no wait states): let the core fetch code there directly, at the 3 cycles
     * per byte an MMIO read of that block costs. Data accesses still go
     * through lcd_read/lcd_write. */
    z->xpage = 0xE30;
    z->xbase = (uintptr_t)lcd.crsrImageBytes - 0x800;
    z->xcost = 3;
    for (uint32_t pg = 0xD00; pg < 0x1000; pg++) {
        uint32_t a = pg << EZ80_PAGE_SHIFT;
        uint32_t ram = a & 0x7FFFF;
        if (pg < 0xE00 && ram + EZ80_PAGE_SIZE <= SIZE_RAM_HOST) {
            z->rd[pg] = z->wr[pg] = mem.ram.block + ram;
            z->cost[pg] = RAM_READ_COST;
            z->wcost[pg] = RAM_WRITE_COST;
        } else {
            z->rd[pg] = z->wr[pg] = NULL;
            z->cost[pg] = z->wcost[pg] = 0;
        }
    }
}

/* The unlock sequence as it sits in memory:
 *   di / jr $+2 / di / im 2 / im 1 / out0 (28h),a / in0 a,(28h) / bit 2,a
 * (CEmu matches it on its fetch stream, where the JR prefetch duplicates the
 * second DI; see detect_flash_unlock_sequence in mem.c). When IN0 A,(28h)
 * runs, PC points at the final CB 57. */
static void detect_flash_unlock(void) {
    static const uint8_t seq[] = { 0xF3, 0x18, 0x00, 0xF3, 0xED, 0x7E, 0xED, 0x56,
                                   0xED, 0x39, 0x28, 0xED, 0x38, 0x28, 0xCB, 0x57 };
    if (!protected_ports_unlocked() || unprivileged_code()) return;
    uint32_t start = cpu.z.pc - 14;
    for (unsigned i = 0; i < sizeof seq; i++)
        if (mem_peek_byte(cpu_address_mode(start + i, cpu.z.adl)) != seq[i]) return;
    control.flashUnlocked |= 1 << 3;
}

/*
 * Busy-wait skipping. Programs wait for hardware in tight loops such as
 * KEYPADC's kb_Scan ("ld (hl),2 / xor a / cp (hl) / jr nz,$-1" on F50000h) or
 * GRAPHX's buffer-swap wait, which reads LcdUpcurr and LcdLpbase (three
 * addresses, several bytes each) until the LCD starts the new frame.
 *
 * Between two scheduler events every I/O register this code can see keeps its
 * value. So when an instruction (same PC) reads the same I/O address again,
 * gets the same value, all registers equal those of its previous read, and no
 * event fired in between, the CPU is back in the same state and will repeat
 * the same iteration until the next event. The clock then jumps (in whole
 * iterations) to just before that event. The program sees the same result;
 * the host saves the iterations. R advances by the same amount per iteration,
 * so it is moved on too and the result matches running every iteration. The
 * last few reads are remembered so that loops reading several registers are
 * recognised too.
 */
#define POLL_SLOTS      8
#define POLL_MAX_PERIOD 256
static struct {
    uint32_t addr, pc, regs[8];
    int32_t cycles;
    uint32_t next_event;
    uint8_t value, r;
} poll[POLL_SLOTS];
static unsigned poll_head;

static void poll_regs(uint32_t *r) {
    r[0] = (uint32_t)cpu.z.a << 8 | cpu.z.f;
    r[1] = cpu.z.bc.w; r[2] = cpu.z.de.w; r[3] = cpu.z.hl.w;
    r[4] = cpu.z.ix.w; r[5] = cpu.z.iy.w; r[6] = cpu.z.spl; r[7] = cpu.z.sps;
}

static void poll_check(uint32_t a, uint8_t v) {
    uint32_t regs[8];
    uint32_t next_event = sched_event_next_cycle();
    poll_regs(regs);
    unsigned slot = POLL_SLOTS;
    for (unsigned i = 0; i < POLL_SLOTS; i++)
        if (poll[i].addr == a && poll[i].pc == cpu.z.pc) { slot = i; break; }
    if (slot < POLL_SLOTS) {
        int32_t period = cpu.z.cycles - poll[slot].cycles;
        if (v == poll[slot].value && period > 0 && period <= POLL_MAX_PERIOD &&
            next_event == poll[slot].next_event && !cpu.signals &&
            !memcmp(regs, poll[slot].regs, sizeof regs)) {
            int32_t room = (int32_t)next_event - cpu.z.cycles - 1;
            if (room > period) {
                int32_t n = room / period;
                cpu.z.cycles += n * period;
                cpu.z.r = (uint8_t)(cpu.z.r + (uint8_t)(cpu.z.r - poll[slot].r) * (uint32_t)n);
            }
        }
    } else {
        slot = poll_head;
        poll_head = (poll_head + 1) % POLL_SLOTS;
    }
    poll[slot].addr = a; poll[slot].pc = cpu.z.pc; poll[slot].value = v;
    poll[slot].cycles = cpu.z.cycles; poll[slot].next_event = sched_event_next_cycle();
    poll[slot].r = cpu.z.r;
    memcpy(poll[slot].regs, regs, sizeof regs);
}

static uint8_t ce_read_slow(void *ctx, uint32_t a) {
    (void)ctx;
    uint8_t v = mem_read_cpu(a, false);
    if (a >= 0xE00000) poll_check(a, v);
    if (a < 0xD00000) check_flash_map();
    return v;
}

static void ce_write_slow(void *ctx, uint32_t a, uint8_t v) {
    (void)ctx;
    mem_write_cpu(a, v);
    if (a < 0xD00000) flash_generation++;
    check_flash_map();
}

static uint8_t ce_port_in(void *ctx, uint16_t port) {
    (void)ctx;
    uint8_t v = port_read_byte(port);
    if ((port & 0xF0FF) == 0x0028) detect_flash_unlock();
    return v;
}

static void ce_port_out(void *ctx, uint16_t port, uint8_t v) {
    (void)ctx;
    port_write_byte(port, v);
    check_flash_map();
}

/* ======================================================================
 * Public API
 * ==================================================================== */

static bool lcd_frame_cb(void *data) {
    (void)data;
    lcd_frame_count++;
    return true;
}

void ce_reset(void) {
    asic.device = TI84PCE;
    asic.python = false;
    asic.revision = detect_revision();
    asic.im2 = asic.revision < ASIC_REV_I;
    asic.serFlash = asic.revision >= ASIC_REV_M;
    asic.hasWorkingSPIReads = false;
    for (size_t i = 0; i < sizeof reset_procs / sizeof *reset_procs; i++) reset_procs[i]();
    emu_set_lcd_callback(lcd_frame_cb, NULL);
    emu_set_lcd_dma(0);
    map_all();
    lcd_frame_count = 0;
}

bool ce_init(const uint8_t *rom, size_t size) {
    if (size < SIZE_FLASH_MIN || size > SIZE_FLASH_MAX_PAR) return false;
    memset(&cpu, 0, sizeof cpu);
    mem_init();
    if (!mem.ram.block) return false;
    mem.flash.size = SIZE_FLASH_MIN;
    while (mem.flash.size < size) mem.flash.size <<= 1;
    mem.flash.block = malloc(mem.flash.size);
    if (!mem.flash.block) { mem_free(); return false; }
    memset(mem.flash.block, 0xFF, mem.flash.size);
    memcpy(mem.flash.block, rom, size);

    sched_init();
    bus_init_rand(0x12, 0x34, 0x56);
    ez80_init(&cpu.z);
    cpu.z.ctx = NULL;
    cpu.z.read_slow = ce_read_slow;
    cpu.z.write_slow = ce_write_slow;
    cpu.z.port_in = ce_port_in;
    cpu.z.port_out = ce_port_out;
    plug_devices();
    ce_reset();
    return true;
}

void ce_free(void) {
    lcd_free();
    mem_free();
}

void ce_run(uint32_t ticks) {
    sched.run_event_triggered = false;
    sched_repeat(SCHED_RUN, ticks);
    for (;;) {
        uint8_t signals = cpu_clear_signals();
        if (signals & CPU_SIGNAL_ON_KEY) keypad_on_check();
        if (signals & CPU_SIGNAL_ANY_KEY) keypad_any_check();
        sched_process_pending_events();
        if (signals & CPU_SIGNAL_RESET) ce_reset();
        if (sched.run_event_triggered) break;
        cpu_restore_next();
        ez80_execute(&cpu.z);
    }
}

void ce_key(unsigned row, unsigned col, bool down) { emu_keypad_event(row, col, down); }

void ce_release_all_keys(void) {
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++) emu_keypad_event(r, c, false);
}

uint32_t ce_lcd_frames(void) { return lcd_frame_count; }

const uint8_t *ce_flash(size_t *size) { *size = mem.flash.size; return mem.flash.block; }
uint32_t ce_flash_generation(void) { return flash_generation; }

uint32_t ce_pc(void) { return cpu.z.pc; }
uint64_t ce_total_cycles(void) { return sched_total_cycles(); }

/* ======================================================================
 * Save states
 *
 * Layout: "N84C", version, Flash size, then the CPU registers (the part of
 * ez80_t before the page tables), N84 CPU extras, and each CEmu module's
 * own save block, then the mem_state_t and RAM. Flash contents are not
 * included: they are written back to the ROM file at the same time.
 * ==================================================================== */

#define CE_STATE_MAGIC   "N84C"
#define CE_STATE_VERSION 1u

bool ce_save_state(FILE *f) {
    uint32_t hdr[2] = { CE_STATE_VERSION, mem.flash.size };
    if (fwrite(CE_STATE_MAGIC, 4, 1, f) != 1 || fwrite(hdr, sizeof hdr, 1, f) != 1) return false;
    if (fwrite(&asic, sizeof asic, 1, f) != 1) return false;
    if (fwrite(&cpu.z, offsetof(ez80_t, cost), 1, f) != 1) return false;
    if (fwrite(&cpu.seconds, sizeof cpu - offsetof(ce_cpu_t, seconds), 1, f) != 1) return false;
    return backlight_save(f) && control_save(f) && flash_save(f) && intrpt_save(f) &&
           keypad_save(f) && lcd_save(f) && watchdog_save(f) && protect_save(f) &&
           rtc_save(f) && sha256_save(f) && gpt_save(f) && cxxx_save(f) && spi_save(f) &&
           sched_save(f) &&
           fwrite(&mem, sizeof mem, 1, f) == 1 &&
           fwrite(mem.ram.block, SIZE_RAM, 1, f) == 1;
}

bool ce_load_state(FILE *f) {
    char magic[4];
    uint32_t hdr[2];
    if (fread(magic, 4, 1, f) != 1 || memcmp(magic, CE_STATE_MAGIC, 4)) return false;
    if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != CE_STATE_VERSION || hdr[1] != mem.flash.size) return false;

    /* everything below overwrites live state: on failure the caller resets */
    if (fread(&asic, sizeof asic, 1, f) != 1) return false;
    if (fread(&cpu.z, offsetof(ez80_t, cost), 1, f) != 1) return false;
    if (fread(&cpu.seconds, sizeof cpu - offsetof(ce_cpu_t, seconds), 1, f) != 1) return false;
    mem_state_t saved_ptrs = mem;
    bool ok = backlight_restore(f) && control_restore(f) && flash_restore(f) && intrpt_restore(f) &&
              keypad_restore(f) && lcd_restore(f) && watchdog_restore(f) && protect_restore(f) &&
              rtc_restore(f) && sha256_restore(f) && gpt_restore(f) && cxxx_restore(f) &&
              spi_restore(f) && sched_restore(f) &&
              fread(&mem, sizeof mem, 1, f) == 1;
    mem.flash.block = saved_ptrs.flash.block;
    mem.flash.size = saved_ptrs.flash.size;
    mem.ram.block = saved_ptrs.ram.block;
    ok = ok && fread(mem.ram.block, SIZE_RAM, 1, f) == 1;
    if (!ok) return false;
    emu_set_lcd_callback(lcd_frame_cb, NULL);
    lcd_update();
    map_all();
    cpu_restore_next();
    return true;
}

/* ======================================================================
 * Frame conversion (after CEmu's emu_lcd_drawmem, producing RGB565)
 * ==================================================================== */

static inline uint32_t swap_rb565(uint32_t c) {   /* swap the 5-bit R and B fields */
    return (c & 0x07E0) | (c >> 11 & 0x1F) | (c << 11 & 0xF800);
}

bool ce_lcd_render(uint16_t *out) { return ce_lcd_render_rows(out, 0, CE_LCD_H); }

bool ce_lcd_render_rows(uint16_t *out, unsigned y0, unsigned y1) {
    if (y1 > CE_LCD_H) y1 = CE_LCD_H;
    if (y0 >= y1) return true;
    uint16_t *o = out + y0 * CE_LCD_W, *end = out + y1 * CE_LCD_W;
    uint32_t ctrl = lcd.control;
    if (!(ctrl & 1 << 11) || !lcd.data || lcd.data >= lcd.data_end) {
        memset(o, 0, (size_t)(end - o) * sizeof *out);
        return false;
    }
    bool swap = !(ctrl & 1 << 8);           /* CEmu swaps R/B when BGR is clear */
    bool bebo = ctrl & 1 << 9;
    unsigned mode = ctrl >> 1 & 7;
    /* source words per row: 1/2/4/8 bpp, 16 bpp (modes 4, 6, 7), 24 bpp in 32 */
    size_t row_words = mode < 4 ? (CE_LCD_W << mode) / 32 : mode == 5 ? CE_LCD_W : CE_LCD_W / 2;
    const uint32_t *src = lcd.data + y0 * row_words;
    if (src >= lcd.data_end) { memset(o, 0, (size_t)(end - o) * sizeof *out); return true; }
    const uint8_t *d = (const uint8_t *)src, *dend = (const uint8_t *)lcd.data_end;

    if (mode == 6 && !bebo) {                /* 16 bpp 5:6:5, the usual case */
        const uint16_t *s = (const uint16_t *)d;
        size_t n = (size_t)(dend - d) / 2;
        if (n > (size_t)(end - o)) n = (size_t)(end - o);
        if (!swap) memcpy(o, s, n * 2);
        else for (size_t i = 0; i < n; i++) o[i] = (uint16_t)swap_rb565(s[i]);
        o += n;
    } else if (mode == 3 && !bebo) {         /* 8 bpp palette (GRAPHX): pixels in byte order */
        const uint16_t *pal = lcd.palettes[swap ? 1 : 0];
        const uint32_t *w = src;
        size_t words = (size_t)(lcd.data_end - src);
        size_t n = (size_t)(end - o) / 4;
        if (words < n) n = words;
        uint32_t *o32 = (uint32_t *)o;       /* out is 4-byte aligned, as is each row of 4 */
        for (size_t i = 0; i < n; i++) {
            uint32_t word = w[i];
            o32[2 * i]     = pal[word & 0xFF] | (uint32_t)pal[word >> 8 & 0xFF] << 16;
            o32[2 * i + 1] = pal[word >> 16 & 0xFF] | (uint32_t)pal[word >> 24] << 16;
        }
        o += 4 * n;
    } else if (mode < 4) {                   /* 1/2/4/8 bpp palette */
        const uint16_t *pal = lcd.palettes[swap ? 1 : 0];
        unsigned bpp = 1u << mode, mask = (1u << bpp) - 1;
        unsigned bi = bebo ? 0 : 24;
        if (!(ctrl & 1 << 10)) bi ^= 8 - bpp;
        const uint32_t *w = src;
        while (w < lcd.data_end && o < end) {
            uint32_t word = *w++;
            unsigned bitpos = 32;
            do {
                bitpos -= bpp;
                *o++ = pal[word >> (bitpos ^ bi) & mask];
            } while (bitpos && o < end);
        }
    } else {                                 /* 1555, 888, 565 big-endian, 444 */
        const uint32_t *w = src;
        while (w < lcd.data_end && o < end) {
            uint32_t word = *w++, c0, c1;
            if (mode == 5) {                 /* 24 bpp: one pixel per word */
                uint32_t r = word & 0xFF, g = word >> 8 & 0xFF, b = word >> 16 & 0xFF;
                if (!swap) { uint32_t t = r; r = b; b = t; }
                *o++ = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
                continue;
            }
            if (bebo) word = (word >> 16) | (word << 16);
            if (mode == 4) {                 /* 1:5:5:5 */
                word = (word & ~0x80008000u) + (word & 0xFFE07FE0u) + (word >> 10 & 0x00200020u);
            } else if (mode == 7) {          /* 4:4:4 */
                word = (word << 4 & 0xF000F000u) | (word << 3 & 0x07800780u) | (word << 1 & 0x001E001Eu);
            }
            c0 = word & 0xFFFF;
            c1 = word >> 16;
            if (swap) { c0 = swap_rb565(c0); c1 = swap_rb565(c1); }
            *o++ = (uint16_t)c0;
            if (o < end) *o++ = (uint16_t)c1;
        }
    }
    while (o < end) *o++ = 0;
    return true;
}
