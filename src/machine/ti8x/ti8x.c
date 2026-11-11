/*
 * N84 - TI-8x emulator for the TI-Nspire
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TI-83 Plus / TI-83 Plus SE / TI-84 Plus / TI-84 Plus SE hardware.
 * The TI-83 Plus ("BE") is the older ASIC: 512 KiB Flash, 32 KiB RAM, 6 MHz
 * only, RAM selected by bit 6 of ports 6/7, no port 5, no crystal timers,
 * RTC, MD5 or USB, and port numbers decoded with mask 17h (mirrors).
 * Reference: WikiTI port documentation and TilEm (LGPL) emu/x4, emu/xz.
 */
#include <stdlib.h>
#include <string.h>
#include "machine/ti8x/ti8x.h"

#ifndef TI8X_LOG
#define TI8X_LOG(...) ((void)0)
#endif

#define PAGE_SIZE Z80_PAGE_SIZE

static uint8_t ff_page[PAGE_SIZE];   /* returned for read-protected areas */

typedef struct {
    unsigned flash_pages, ram_pages;
    uint8_t port2_base, port15, port23_reset;
    uint8_t protect_group;          /* group of protected sectors */
    bool    priv_low;               /* pages F-0x14..F-0x11 are privileged */
} model_info_t;

static const model_info_t models[] = {
    [TI8X_MODEL_83PSE] = { 0x80, 8, 0xC1, 0x33, 0x69, 2, false },
    [TI8X_MODEL_84P]   = { 0x40, 8, 0xE1, 0x45, 0x29, 1, true  },
    [TI8X_MODEL_84PSE] = { 0x80, 8, 0xE1, 0x45, 0x69, 2, true  },
    [TI8X_MODEL_83P]   = { 0x20, 2, 0x01, 0x00, 0x00, 1, false },
};

static const char *const model_names[] = {
    [TI8X_MODEL_NONE] = "unknown",
    [TI8X_MODEL_83PSE] = "TI-83 Plus SE",
    [TI8X_MODEL_84P] = "TI-84 Plus",
    [TI8X_MODEL_84PSE] = "TI-84 Plus SE",
    [TI8X_MODEL_83P] = "TI-83 Plus",
};

const char *ti8x_model_name(ti8x_model_t m) {
    return (unsigned)m < sizeof model_names / sizeof *model_names ? model_names[m] : "?";
}

static bool find_bytes(const uint8_t *buf, size_t len, const char *s, size_t slen) {
    if (len < slen) return false;
    for (size_t i = 0; i + slen <= len; i++)
        if (!memcmp(buf + i, s, slen)) return true;
    return false;
}

ti8x_model_t ti8x_detect_model_boot(size_t size, const uint8_t *boot_page) {
    if (size >= 0x80000 && size < 0xA4000) return TI8X_MODEL_83P;
    if (size >= 0x100000 && size < 0x124000) return TI8X_MODEL_84P;
    if (size >= 0x200000 && size < 0x224000) {
        /* Same test as TilEm: the 84+SE boot sector mentions "Operating".
         * BootFree (free boot code replacement) is only built for the 84+ family. */
        if (find_bytes(boot_page, 0x4000, "Operating", 9) ||
            find_bytes(boot_page, 0x4000, "BootFree", 8)) return TI8X_MODEL_84PSE;
        return TI8X_MODEL_83PSE;
    }
    return TI8X_MODEL_NONE;
}

ti8x_model_t ti8x_detect_model(const uint8_t *rom, size_t size) {
    if (size < 0x80000) return TI8X_MODEL_NONE;
    return ti8x_detect_model_boot(size, rom + ti8x_boot_page_offset(size));
}

static inline const model_info_t *info(const ti8x_t *m) { return &models[m->model]; }
static inline unsigned cert_page(const ti8x_t *m) { return m->flash_pages - 2; }
static inline unsigned boot_page(const ti8x_t *m) { return m->flash_pages - 1; }
static inline void update_irq(ti8x_t *m) { m->cpu.irq_line = m->int_pending != 0; }
static inline bool is_83p(const ti8x_t *m) { return m->model == TI8X_MODEL_83P; }

size_t ti8x_boot_page_offset(size_t size) {
    return size >= 0x200000 ? 0x1FC000 : size >= 0x100000 ? 0xFC000 : 0x7C000;
}

/* ======================================================================
 * Memory mapping
 * ==================================================================== */

static unsigned effective_page(const ti8x_t *m, uint16_t a) {
    unsigned page = m->bank_page[a >> 14];
    if (a & 0x8000) {
        if (a > 0xFFFF - 64u * m->port27) page = m->flash_pages + 0;
        else if (a < 0x8000 + 64u * m->port28) page = m->flash_pages + 1;
    }
    return page;
}

static void map_bank(ti8x_t *m, unsigned bank, unsigned page) {
    unsigned total = m->flash_pages + m->ram_pages;
    if (page >= total) page %= total;
    m->bank_page[bank] = (uint8_t)page;
    z80_t *z = &m->cpu;
    if (page < m->flash_pages) {
        z->rd[bank] = (page == cert_page(m) && !m->flash_unlocked) ? ff_page : m->mem + page * PAGE_SIZE;
        z->wr[bank] = NULL;
    } else {
        z->rd[bank] = z->wr[bank] = m->mem + page * PAGE_SIZE;
    }
}

static unsigned decode_ab(const ti8x_t *m, uint8_t v) {
    if (is_83p(m)) return (v & 0x40) ? m->flash_pages + (v & 1) : v & 0x1Fu;
    if (v & 0x80) return m->flash_pages + (v & (m->ram_pages - 1));
    return v & (m->flash_pages - 1);
}

static void update_mapping(ti8x_t *m) {
    unsigned a = decode_ab(m, m->port6), b = decode_ab(m, m->port7);
    unsigned c = m->flash_pages + (is_83p(m) ? 0 : m->port5 & (m->ram_pages - 1));
    map_bank(m, 0, 0);
    if (m->port4 & 1) {
        map_bank(m, 1, a & ~1u);
        map_bank(m, 2, a | 1u);
        map_bank(m, 3, b);
    } else {
        map_bank(m, 1, a);
        map_bank(m, 2, b);
        map_bank(m, 3, c);
    }
    /* Sub-page remapping (ports 27/28) goes through the slow path */
    if (m->port28) m->cpu.rd[2] = m->cpu.wr[2] = NULL;
    if (m->port27) m->cpu.rd[3] = m->cpu.wr[3] = NULL;
}

/* ======================================================================
 * Flash (AMD-style command set, operations complete instantly)
 * ==================================================================== */

enum { FL_READ, FL_AA, FL_55, FL_PROG, FL_ERASE, FL_ERAA, FL_ER55, FL_FAST, FL_FASTPROG, FL_FASTEXIT };

static void sector_of(const ti8x_t *m, uint32_t pa, uint32_t *start, uint32_t *size) {
    uint32_t end = m->flash_pages * PAGE_SIZE;
    if (pa >= end - 0x10000) {
        /* last 64K: 32K, 8K, 8K, 16K (boot) */
        uint32_t base = end - 0x10000, off = pa - base;
        if (off < 0x8000)      { *start = base;          *size = 0x8000; }
        else if (off < 0xA000) { *start = base + 0x8000; *size = 0x2000; }
        else if (off < 0xC000) { *start = base + 0xA000; *size = 0x2000; }
        else                   { *start = base + 0xC000; *size = 0x4000; }
    } else {
        *start = pa & ~0xFFFFu;
        *size = 0x10000;
    }
}

static bool sector_writable(const ti8x_t *m, uint32_t start) {
    uint32_t end = m->flash_pages * PAGE_SIZE;
    unsigned group = 0;
    if ((start == end - 0x50000 && !is_83p(m)) || start == end - 0x4000) group = info(m)->protect_group;
    return !(group & ~m->flash_override);
}

static void mark_dirty(ti8x_t *m, uint32_t pa) { m->flash_dirty |= 1u << (pa >> 16); }

static void flash_program(ti8x_t *m, uint32_t pa, uint8_t v) {
    uint32_t s, sz;
    sector_of(m, pa, &s, &sz);
    if (!sector_writable(m, s)) { TI8X_LOG("flash: program protected %06x\n", pa); return; }
    m->flash[pa] &= v;
    mark_dirty(m, pa);
}

static void flash_erase(ti8x_t *m, uint32_t pa) {
    uint32_t s, sz;
    sector_of(m, pa, &s, &sz);
    if (!sector_writable(m, s)) { TI8X_LOG("flash: erase protected %06x\n", pa); return; }
    memset(m->flash + s, 0xFF, sz);
    mark_dirty(m, s);
}

static void flash_write(ti8x_t *m, uint32_t pa, uint8_t v) {
    if (!m->flash_unlocked) return;
    unsigned st = m->flash_state;
    bool aaa = (pa & 0xFFF) == 0xAAA, x555 = (pa & 0xFFF) == 0x555;
    m->flash_state = FL_READ;
    switch (st) {
    case FL_READ:  if (aaa && v == 0xAA) m->flash_state = FL_AA; break;
    case FL_AA:    if (x555 && v == 0x55) m->flash_state = FL_55; break;
    case FL_55:
        if (aaa) {
            if (v == 0xA0) m->flash_state = FL_PROG;
            else if (v == 0x80) m->flash_state = FL_ERASE;
            else if (v == 0x20) m->flash_state = FL_FAST;
        }
        break;
    case FL_PROG:     flash_program(m, pa, v); break;
    case FL_FAST:
        if (v == 0x90) m->flash_state = FL_FASTEXIT;
        else if (v == 0xA0) m->flash_state = FL_FASTPROG;
        else m->flash_state = FL_FAST;
        break;
    case FL_FASTPROG: flash_program(m, pa, v); m->flash_state = FL_FAST; break;
    case FL_FASTEXIT: if (v != 0xF0) m->flash_state = FL_FAST; break;
    case FL_ERASE:    if (aaa && v == 0xAA) m->flash_state = FL_ERAA; break;
    case FL_ERAA:     if (x555 && v == 0x55) m->flash_state = FL_ER55; break;
    case FL_ER55:
        if (v == 0x30) {
            flash_erase(m, pa);
        } else if (aaa && v == 0x10) {
            for (uint32_t a = 0; a < m->flash_pages * PAGE_SIZE; ) {
                uint32_t s, sz;
                sector_of(m, a, &s, &sz);
                if (sector_writable(m, s)) { memset(m->flash + s, 0xFF, sz); mark_dirty(m, s); }
                a = s + sz;
            }
        }
        break;
    }
}

static uint8_t mem_read_slow(void *ctx, uint16_t a) {
    ti8x_t *m = ctx;
    unsigned page = effective_page(m, a);
    if (page == cert_page(m) && !m->flash_unlocked) return 0xFF;
    return m->mem[page * PAGE_SIZE + (a & (PAGE_SIZE - 1))];
}

static void mem_write_slow(void *ctx, uint16_t a, uint8_t v) {
    ti8x_t *m = ctx;
    unsigned page = effective_page(m, a);
    uint32_t pa = page * PAGE_SIZE + (a & (PAGE_SIZE - 1));
    if (page < m->flash_pages) flash_write(m, pa, v);
    else m->mem[pa] = v;
}

static bool page_privileged(const ti8x_t *m, unsigned page) {
    unsigned f = m->flash_pages;
    if (page >= f) return false;
    if (page >= f - 4) return true;
    return info(m)->priv_low && page >= f - 0x14 && page < f - 0x10;
}

/* Port 14 can only be written by the exact sequence
 *   nop / nop / im 1 / di / out (14h),a
 * executed from a privileged Flash page. Checked at OUT time instead of
 * tracking every memory read like the real ASIC. */
static bool unlock_sequence_ok(ti8x_t *m) {
    static const uint8_t seq[7] = { 0x00, 0x00, 0xED, 0x56, 0xF3, 0xD3, 0x14 };
    uint16_t pc = m->cpu.pc.w;
    if (!page_privileged(m, m->bank_page[(uint16_t)(pc - 1) >> 14])) return false;
    for (int i = 0; i < 7; i++)
        if (z80_peek(&m->cpu, (uint16_t)(pc - 7 + i)) != seq[i]) return false;
    return true;
}

static void set_flash_unlock(ti8x_t *m, bool u) {
    if (m->flash_unlocked == u) return;
    m->flash_unlocked = u;
    update_mapping(m);
}

/* ======================================================================
 * Timers
 * ==================================================================== */

/* hardware timer periods: 512/227.5/146.3/107.8 Hz (SE and 84 Plus),
 * 560/248/170/118 Hz (TI-83 Plus) */
static const uint16_t hw_timer_us[2][4] = { { 1953, 4395, 6836, 9277 }, { 1786, 4032, 5882, 8475 } };

static uint64_t hw_period(const ti8x_t *m) {
    return (uint64_t)hw_timer_us[is_83p(m)][(m->port4 >> 1) & 3] * TI8X_TICKS_PER_US;
}

static uint64_t crys_duration(const ti8x_t *m, uint8_t f, unsigned n) {
    if (f & 0x80) {
        unsigned div = (f & 0x20) ? 64 : (f & 0x10) ? 32 : (f & 0x08) ? 16 :
                       (f & 0x04) ? 8 : (f & 0x02) ? 4 : (f & 0x01) ? 2 : 1;
        return (uint64_t)div * n * m->cpu_mult;
    }
    if (f & 0x40) {
        static const uint64_t t[8] = { 3000000ull, 33000000ull, 328000000ull, 3277000000ull,
                                       1000000ull, 16000000ull, 256000000ull, 4096000000ull };
        uint64_t us = (t[f & 7] * n + 16384) / 32768;
        return us * TI8X_TICKS_PER_US;
    }
    return 0;
}

static uint64_t crys_normal(const ti8x_t *m, int i) {
    const ti8x_crystal_t *c = &m->crys[i];
    return crys_duration(m, c->freq, c->loopvalue ? c->loopvalue : 256);
}
static uint64_t crys_overflow(const ti8x_t *m, int i) {
    return crys_duration(m, m->crys[i].freq, 256);
}

static uint8_t crys_value(const ti8x_t *m, int i) {
    if (m->ev[TI8X_EV_CRYS0 + i] == TI8X_EV_OFF) return m->crys[i].loopvalue;
    uint64_t ovf = crys_overflow(m, i);
    if (!ovf) return 0;
    uint64_t rem = m->ev[TI8X_EV_CRYS0 + i] - m->now;
    return (uint8_t)((rem * 256 / ovf) & 0xFF);
}

static void crys_set_freq(ti8x_t *m, int i, uint8_t v) {
    m->crys[i].loopvalue = crys_value(m, i);
    m->ev[TI8X_EV_CRYS0 + i] = TI8X_EV_OFF;
    m->crys[i].freq = v;
}

static void crys_set_mode(ti8x_t *m, int i, uint8_t mode) {
    ti8x_crystal_t *c = &m->crys[i];
    c->status = (uint16_t)((c->status & 0x200) | (mode & 3));
    m->int_pending &= (uint8_t)~(0x20 << i);
    update_irq(m);
    if ((mode & 1) || c->loopvalue == 0) c->period = crys_normal(m, i);
    else c->period = 0;
}

static void crys_start(ti8x_t *m, int i, uint8_t v) {
    ti8x_crystal_t *c = &m->crys[i];
    c->loopvalue = v;
    uint64_t count = crys_normal(m, i);
    if (!count) return;
    if (!v) c->period = crys_overflow(m, i);
    else if (c->status & 0x100) c->period = crys_overflow(m, i);
    else if (!(c->status & 1)) c->period = 0;
    else c->period = count;
    m->ev[TI8X_EV_CRYS0 + i] = m->now + count;
    z80_end_timeslice(&m->cpu);
}

static void crys_expired(ti8x_t *m, int i) {
    ti8x_crystal_t *c = &m->crys[i];
    if (c->loopvalue) {
        if (c->status & 0x100) c->status |= 4;
        c->status |= 0x100;
        if ((c->status & 2) && (!(c->status & 0x200) || !m->cpu.halted)) {
            m->int_pending |= (uint8_t)(0x20 << i);
            update_irq(m);
        }
        if (c->status & 1) c->period = crys_overflow(m, i);
    }
    if (c->period) m->ev[TI8X_EV_CRYS0 + i] += c->period;
    else m->ev[TI8X_EV_CRYS0 + i] = TI8X_EV_OFF;
}

/* ======================================================================
 * LCD (Toshiba T6A04)
 * ==================================================================== */

static void lcd_reset(t6a04_t *l) {
    memset(l, 0, sizeof *l);
    l->contrast = 32;
    l->mode = 1;
    l->inc = 7;
}

static void lcd_clip(t6a04_t *l) {
    int xlimit = l->mode ? TI8X_LCD_STRIDE : (TI8X_LCD_STRIDE * 8 + 5) / 6;
    if ((int8_t)l->x < 0) l->x = (uint8_t)(xlimit - 1);
    else if (l->x >= xlimit) l->x = 0;
    if ((int8_t)l->y < 0) l->y = 0x3F;
    else if (l->y >= 0x40) l->y = 0;
}

static void lcd_advance(t6a04_t *l) {
    switch (l->inc) {
    case 4: l->y--; break;
    case 5: l->y++; break;
    case 6: l->x--; break;
    case 7: l->x++; break;
    }
}

static void lcd_control(t6a04_t *l, uint8_t v) {
    if (v <= 1) l->mode = v;
    else if (v == 2) l->active = 0;
    else if (v == 3) l->active = 1;
    else if (v <= 7) l->inc = v;
    else if (v >= 0x20 && v <= 0x3F) l->x = v - 0x20;
    else if (v >= 0x40 && v <= 0x7F) l->rowshift = v - 0x40;
    else if (v >= 0x80 && v <= 0xBF) l->y = v - 0x80;
    else if (v >= 0xC0) l->contrast = v - 0xC0;
}

static uint8_t lcd_status(const t6a04_t *l) {
    return (uint8_t)((l->mode << 6) | (l->active << 5) | (l->inc & 3));
}

static uint8_t lcd_read(t6a04_t *l) {
    uint8_t ret = l->nextbyte;
    lcd_clip(l);
    if (l->mode) {
        l->nextbyte = l->ram[l->x + TI8X_LCD_STRIDE * l->y];
    } else {
        int col = 6 * l->x, ofs = l->y * TI8X_LCD_STRIDE + (col >> 3), shift = 10 - (col & 7);
        int next = (col >> 3) + 1 < TI8X_LCD_STRIDE ? l->ram[ofs + 1] : 0;
        l->nextbyte = (uint8_t)(((l->ram[ofs] << 8) | next) >> shift) & 0x3F;
    }
    lcd_advance(l);
    return ret;
}

static void lcd_write(t6a04_t *l, uint8_t v) {
    lcd_clip(l);
    if (l->mode) {
        l->ram[l->x + TI8X_LCD_STRIDE * l->y] = v;
    } else {
        int col = 6 * l->x, ofs = l->y * TI8X_LCD_STRIDE + (col >> 3), shift = col & 7;
        unsigned sprite = (unsigned)(v << 2) & 0xFC;
        l->ram[ofs] = (uint8_t)((l->ram[ofs] & ~(0xFC >> shift)) | (sprite >> shift));
        if (shift > 2 && (col >> 3) < TI8X_LCD_STRIDE - 1) {
            shift = 8 - shift;
            l->ram[ofs + 1] = (uint8_t)((l->ram[ofs + 1] & ~(0xFC << shift)) | (sprite << shift));
        }
    }
    lcd_advance(l);
}

static void lcd_sample(ti8x_t *m) {
    const t6a04_t *l = &m->lcd;
    uint8_t *acc = m->lcd_acc;
    for (int y = 0; y < TI8X_LCD_H; y++) {
        const uint8_t *row = l->ram + ((y + l->rowshift) & 63) * TI8X_LCD_STRIDE;
        for (int bx = 0; bx < TI8X_LCD_W / 8; bx++) {
            uint8_t b = row[bx];
            if (!b) { acc += 8; continue; }
            for (int k = 7; k >= 0; k--) *acc++ += (b >> k) & 1;
        }
    }
    m->lcd_samples++;
}

bool ti8x_lcd_on(const ti8x_t *m) { return m->lcd.active; }

void ti8x_lcd_frame(ti8x_t *m, uint8_t *out) {
    if (!m->lcd_samples) lcd_sample(m);
    unsigned n = m->lcd_samples;
    for (int i = 0; i < TI8X_LCD_W * TI8X_LCD_H; i++)
        out[i] = (uint8_t)(m->lcd_acc[i] * 255u / n);
    memset(m->lcd_acc, 0, sizeof m->lcd_acc);
    m->lcd_samples = 0;
}

/* ======================================================================
 * I/O ports
 * ==================================================================== */

static uint32_t rtc_seconds(const ti8x_t *m) {
    return (uint32_t)(m->now / TI8X_TICKS_PER_SEC);
}

static uint32_t md5_value(const ti8x_t *m) {
    uint32_t a = m->md5[0], b = m->md5[1], c = m->md5[2], d = m->md5[3], x = m->md5[4], t = m->md5[5], r;
    switch (m->md5_mode & 3) {
    case 0: r = (b & c) | (~b & d); break;
    case 1: r = (b & d) | (c & ~d); break;
    case 2: r = b ^ c ^ d; break;
    default: r = c ^ (b | ~d); break;
    }
    r += a + x + t;
    unsigned s = m->md5_shift & 31;
    if (s) r = (r << s) | (r >> (32 - s));
    return r + b;
}

static uint8_t read_keys(const ti8x_t *m) {
    uint8_t keys = 0, old;
    for (int i = 0; i < 8; i++)
        if (!(m->keygroup & (1 << i))) keys |= m->keys[i];
    do { /* key ghosting through the matrix */
        old = keys;
        for (int i = 0; i < 8; i++)
            if (keys & m->keys[i]) keys |= m->keys[i];
    } while (keys != old);
    return (uint8_t)~keys;
}

/* TI-83 Plus: ports 00-07 and 10-14 only, mirrored through the whole range */
static uint8_t port83p(uint16_t port) {
    uint8_t p = port & 0x17;
    return p == 0x05 || p > 0x14 ? 0xFF : p;     /* FF: no such port */
}

static uint8_t port_in(void *ctx, uint16_t port) {
    ti8x_t *m = ctx;
    uint8_t v;
    if (is_83p(m)) {
        port = port83p(port);
        if (port == 0xFF) return 0xFF;
    }
    switch (port & 0xFF) {
    case 0x00: return (uint8_t)((~m->linkout & 3) | (m->linkout << 4));
    case 0x01: return read_keys(m);
    case 0x02: return (uint8_t)(info(m)->port2_base | 2 | (m->flash_unlocked << 2));
    case 0x03: return m->port3;
    case 0x04:
        v = (uint8_t)((m->on_down ? 0 : 0x08) | (m->int_pending & 0x17));
        for (int i = 0; i < 3; i++) if (m->crys[i].status & 0x100) v |= (uint8_t)(0x20 << i);
        return v;
    case 0x05: return m->port5 & 0x0F;
    case 0x06: return m->port6;
    case 0x07: return m->port7;
    case 0x08: return m->port8;
    case 0x09: return 0x20;
    case 0x0E: return m->porte & 3;
    case 0x0F: return m->portf & 3;
    case 0x10: case 0x12: return lcd_status(&m->lcd);
    case 0x11: case 0x13: return lcd_read(&m->lcd);
    case 0x15: return info(m)->port15;
    case 0x1C: return (uint8_t)md5_value(m);
    case 0x1D: return (uint8_t)(md5_value(m) >> 8);
    case 0x1E: return (uint8_t)(md5_value(m) >> 16);
    case 0x1F: return (uint8_t)(md5_value(m) >> 24);
    case 0x20: return m->port20 & 3;
    case 0x21: return m->port21 & 0x33;
    case 0x22: return m->port22;
    case 0x23: return m->port23;
    case 0x25: return m->port25;
    case 0x26: return m->port26;
    case 0x27: return m->port27;
    case 0x28: return m->port28;
    case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2E: case 0x2F:
        return m->port29_2f[(port & 0xFF) - 0x29];
    case 0x2D: return m->port29_2f[4] & 3;
    case 0x30: return m->crys[0].freq;
    case 0x31: return (uint8_t)m->crys[0].status;
    case 0x32: return crys_value(m, 0);
    case 0x33: return m->crys[1].freq;
    case 0x34: return (uint8_t)m->crys[1].status;
    case 0x35: return crys_value(m, 1);
    case 0x36: return m->crys[2].freq;
    case 0x37: return (uint8_t)m->crys[2].status;
    case 0x38: return crys_value(m, 2);
    case 0x39: return 0xF0;
    case 0x40: return m->clock_mode;
    case 0x41: return (uint8_t)m->clock_input;
    case 0x42: return (uint8_t)(m->clock_input >> 8);
    case 0x43: return (uint8_t)(m->clock_input >> 16);
    case 0x44: return (uint8_t)(m->clock_input >> 24);
    case 0x45: case 0x46: case 0x47: case 0x48: {
        uint32_t t = ((m->clock_mode & 1) ? rtc_seconds(m) : 0) + m->clock_base;
        return (uint8_t)(t >> (((port & 0xFF) - 0x45) * 8));
    }
    /* USB controller: report "no cable" (values from TilEm / Dan Englender) */
    case 0x4C: return 0x22;
    case 0x4D: return 0xA5;
    case 0x55: return 0x1F;
    case 0x56: return 0x00;
    case 0x57: return 0x50;
    default:
        return 0x00;
    }
}

static void port_out(void *ctx, uint16_t port, uint8_t v) {
    ti8x_t *m = ctx;
    if (is_83p(m) && (port = port83p(port)) == 0xFF) return;
    switch (port & 0xFF) {
    case 0x00: m->linkout = v & 3; break;
    case 0x01: m->keygroup = v; break;
    case 0x03:
        if (!(v & 1)) m->int_pending &= (uint8_t)~1;
        if (!(v & 2)) m->int_pending &= (uint8_t)~2;
        if (!(v & 4)) m->int_pending &= (uint8_t)~4;
        for (int i = 0; i < 3; i++) {
            if (v & 6) m->crys[i].status &= (uint16_t)~0x200;
            else m->crys[i].status |= 0x200;
        }
        m->port3 = v;
        update_irq(m);
        break;
    case 0x04:
        m->port4 = v;
        update_mapping(m);
        break;
    case 0x05: m->port5 = v & 0x0F; update_mapping(m); break;
    case 0x06: m->port6 = v; update_mapping(m); break;
    case 0x07: m->port7 = v; update_mapping(m); break;
    case 0x08: m->port8 = v; break;
    case 0x0E: m->porte = v; break;
    case 0x0F: m->portf = v; break;
    case 0x10: case 0x12: lcd_control(&m->lcd, v); break;
    case 0x11: case 0x13: lcd_write(&m->lcd, v); break;
    case 0x14:
        if (unlock_sequence_ok(m)) set_flash_unlock(m, v & 1);
        else TI8X_LOG("port 14 write rejected at %04x\n", m->cpu.pc.w);
        break;
    case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: {
        int r = (port & 0xFF) - 0x18;
        m->md5[r] = (m->md5[r] >> 8) | ((uint32_t)v << 24);
        break;
    }
    case 0x1E: m->md5_shift = v & 0x1F; break;
    case 0x1F: m->md5_mode = v & 3; break;
    case 0x20:
        m->port20 = v;
        m->cpu_mult = (v & 3) ? 2 : 5;
        z80_end_timeslice(&m->cpu);
        break;
    case 0x21: if (m->flash_unlocked) { m->port21 = v; m->flash_override = v & 3; } break;
    case 0x22: if (m->flash_unlocked) m->port22 = v; break;
    case 0x23: if (m->flash_unlocked) m->port23 = v; break;
    case 0x25: if (m->flash_unlocked) m->port25 = v; break;
    case 0x26: if (m->flash_unlocked) m->port26 = v; break;
    case 0x27: m->port27 = v; update_mapping(m); break;
    case 0x28: m->port28 = v; update_mapping(m); break;
    case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
        m->port29_2f[(port & 0xFF) - 0x29] = v;
        break;
    case 0x30: crys_set_freq(m, 0, v); break;
    case 0x31: crys_set_mode(m, 0, v); break;
    case 0x32: crys_start(m, 0, v); break;
    case 0x33: crys_set_freq(m, 1, v); break;
    case 0x34: crys_set_mode(m, 1, v); break;
    case 0x35: crys_start(m, 1, v); break;
    case 0x36: crys_set_freq(m, 2, v); break;
    case 0x37: crys_set_mode(m, 2, v); break;
    case 0x38: crys_start(m, 2, v); break;
    case 0x40: {
        uint32_t cur = rtc_seconds(m);
        if ((m->clock_mode & 1) != (v & 1)) {
            if (v & 1) m->clock_base -= cur;
            else m->clock_base += cur;
        }
        if (!(m->clock_mode & 2) && (v & 2)) {
            m->clock_base = m->clock_input;
            if (v & 1) m->clock_base -= cur;
        }
        m->clock_mode = v & 3;
        break;
    }
    case 0x41: m->clock_input = (m->clock_input & 0xFFFFFF00u) | v; break;
    case 0x42: m->clock_input = (m->clock_input & 0xFFFF00FFu) | ((uint32_t)v << 8); break;
    case 0x43: m->clock_input = (m->clock_input & 0xFF00FFFFu) | ((uint32_t)v << 16); break;
    case 0x44: m->clock_input = (m->clock_input & 0x00FFFFFFu) | ((uint32_t)v << 24); break;
    default: break;
    }
}

/* ======================================================================
 * Public API
 * ==================================================================== */

bool ti8x_init(ti8x_t *m, ti8x_model_t model, const uint8_t *rom, size_t size) {
    memset(m, 0, sizeof *m);
    if (model <= TI8X_MODEL_NONE || model >= TI8X_MODEL_COUNT) return false;
    m->model = model;
    m->flash_pages = models[model].flash_pages;
    m->ram_pages = models[model].ram_pages;
    size_t flash_size = (size_t)m->flash_pages * PAGE_SIZE;
    if (size < flash_size) return false;
    m->mem = malloc(flash_size + (size_t)m->ram_pages * PAGE_SIZE);
    if (!m->mem) return false;
    m->flash = m->mem;
    m->ram = m->mem + flash_size;
    memcpy(m->flash, rom, flash_size);
    memset(m->ram, 0, (size_t)m->ram_pages * PAGE_SIZE);
    memset(ff_page, 0xFF, sizeof ff_page);
    m->lcd_samples_per_frame = 4;

    z80_init(&m->cpu);
    m->cpu.ctx = m;
    m->cpu.read_slow = mem_read_slow;
    m->cpu.write_slow = mem_write_slow;
    m->cpu.port_in = port_in;
    m->cpu.port_out = port_out;
    ti8x_reset(m);
    return true;
}

void ti8x_free(ti8x_t *m) {
    free(m->mem);
    m->mem = NULL;
}

void ti8x_reset(ti8x_t *m) {
    z80_reset(&m->cpu);
    m->cpu.irq_line = 0;
    m->flash_unlocked = 0;
    m->flash_state = FL_READ;
    m->flash_override = 0;

    m->port3 = 0x0B;
    m->port4 = 0x07;
    m->port5 = 0;
    m->port6 = m->port7 = (uint8_t)boot_page(m);
    m->port8 = 0x80;
    m->port20 = 0; m->port21 = 0; m->port22 = 0x08; m->port23 = info(m)->port23_reset;
    m->port25 = 0x10; m->port26 = 0x20; m->port27 = 0; m->port28 = 0;
    static const uint8_t p29[7] = { 0x14, 0x27, 0x2F, 0x3B, 0x01, 0x44, 0x4A };
    memcpy(m->port29_2f, p29, sizeof p29);
    m->keygroup = 0xFF;
    m->linkout = 0;
    m->int_pending = 0;
    m->cpu_mult = 5;
    update_mapping(m);
    m->cpu.pc.w = 0x8000;           /* boot page is mapped at 8000h */

    lcd_reset(&m->lcd);
    memset(m->lcd_acc, 0, sizeof m->lcd_acc);
    m->lcd_samples = 0;

    memset(m->crys, 0, sizeof m->crys);
    for (int i = 0; i < TI8X_EV_COUNT; i++) m->ev[i] = TI8X_EV_OFF;
    m->ev[TI8X_EV_TIMER1]  = m->now + 1600u * TI8X_TICKS_PER_US;
    m->ev[TI8X_EV_TIMER2A] = m->now + 1300u * TI8X_TICKS_PER_US;
    m->ev[TI8X_EV_TIMER2B] = m->now + 1000u * TI8X_TICKS_PER_US;
    m->ev[TI8X_EV_LCD_SAMPLE] = m->now;
}

void ti8x_set_key(ti8x_t *m, uint8_t key, bool down) {
    if (key == TK_ON) {
        if (down && !m->on_down && (m->port3 & 1)) {
            m->int_pending |= 1;
            update_irq(m);
        }
        m->on_down = down;
        return;
    }
    if (key >= 0x40) return;
    uint8_t bit = (uint8_t)(1u << (key & 7));
    if (down) m->keys[key >> 3] |= bit;
    else m->keys[key >> 3] &= (uint8_t)~bit;
}

void ti8x_release_all_keys(ti8x_t *m) {
    memset(m->keys, 0, sizeof m->keys);
    m->on_down = false;
}

static void handle_event(ti8x_t *m, int e) {
    switch (e) {
    case TI8X_EV_TIMER1:
        if (m->port3 & 2) { m->int_pending |= 2; update_irq(m); }
        m->ev[e] += hw_period(m);
        break;
    case TI8X_EV_TIMER2A:
    case TI8X_EV_TIMER2B:
        if (m->port3 & 4) { m->int_pending |= 4; update_irq(m); }
        m->ev[e] += hw_period(m);
        break;
    case TI8X_EV_CRYS0: case TI8X_EV_CRYS1: case TI8X_EV_CRYS2:
        crys_expired(m, e - TI8X_EV_CRYS0);
        break;
    case TI8X_EV_LCD_SAMPLE:
        /* Nobody consumed the last frames: restart the blending window so a
         * late reader sees recent content rather than a stale/saturated one. */
        if (m->lcd_samples >= 2u * (m->lcd_samples_per_frame ? m->lcd_samples_per_frame : 1)) {
            memset(m->lcd_acc, 0, sizeof m->lcd_acc);
            m->lcd_samples = 0;
        }
        lcd_sample(m);
        m->ev[e] += TI8X_TICKS_PER_SEC / 60u / (m->lcd_samples_per_frame ? m->lcd_samples_per_frame : 1);
        break;
    }
}

void ti8x_run(ti8x_t *m, uint32_t ticks) {
    uint64_t end = m->now + ticks;
    while (m->now < end) {
        uint64_t next = end;
        for (int i = 0; i < TI8X_EV_COUNT; i++)
            if (m->ev[i] < next) next = m->ev[i];
        if (next > m->now) {
            uint64_t budget = (next - m->now + m->cpu_mult - 1) / m->cpu_mult;
            if (budget > 1000000) budget = 1000000;
            int32_t ran = z80_run(&m->cpu, (int32_t)budget);
            m->now += (uint64_t)ran * m->cpu_mult;
        }
        for (int i = 0; i < TI8X_EV_COUNT; i++)
            while (m->ev[i] <= m->now) handle_event(m, i);
    }
}

/* ======================================================================
 * Persistence
 * ==================================================================== */

#define STATE_MAGIC   0x5338344Eu   /* "N84S" */
#define STATE_VERSION 1u

typedef struct {
    uint32_t magic, version, model, struct_size, ram_size;
} state_header_t;

bool ti8x_save_state(const ti8x_t *m, FILE *f) {
    state_header_t h = { STATE_MAGIC, STATE_VERSION, (uint32_t)m->model,
                         (uint32_t)sizeof *m, m->ram_pages * PAGE_SIZE };
    return fwrite(&h, sizeof h, 1, f) == 1 &&
           fwrite(m, sizeof *m, 1, f) == 1 &&
           fwrite(m->ram, h.ram_size, 1, f) == 1;
}

bool ti8x_load_state(ti8x_t *m, FILE *f) {
    state_header_t h;
    if (fread(&h, sizeof h, 1, f) != 1) return false;
    if (h.magic != STATE_MAGIC || h.version != STATE_VERSION || h.model != (uint32_t)m->model ||
        h.struct_size != sizeof *m || h.ram_size != m->ram_pages * PAGE_SIZE)
        return false;
    ti8x_t *tmp = malloc(sizeof *tmp);
    if (!tmp) return false;
    bool ok = fread(tmp, sizeof *tmp, 1, f) == 1;
    if (ok) {
        uint8_t *ram = malloc(h.ram_size);
        ok = ram && fread(ram, h.ram_size, 1, f) == 1;
        if (ok) {
            /* take everything, then re-establish this instance's pointers */
            uint8_t *mem = m->mem, *flash = m->flash, *mram = m->ram;
            *m = *tmp;
            m->mem = mem; m->flash = flash; m->ram = mram;
            m->cpu.ctx = m;
            m->cpu.read_slow = mem_read_slow;
            m->cpu.write_slow = mem_write_slow;
            m->cpu.port_in = port_in;
            m->cpu.port_out = port_out;
            memcpy(m->ram, ram, h.ram_size);
            update_mapping(m);
        }
        free(ram);
    }
    free(tmp);
    return ok;
}

bool ti8x_flush_flash(ti8x_t *m, FILE *rom) {
    bool ok = true;
    for (unsigned s = 0; s < 32; s++) {
        if (!(m->flash_dirty & (1u << s))) continue;
        uint32_t off = s * 0x10000u;
        if (off >= m->flash_pages * PAGE_SIZE) break;
        if (fseek(rom, (long)off, SEEK_SET) != 0 || fwrite(m->flash + off, 0x10000, 1, rom) != 1) ok = false;
        else m->flash_dirty &= ~(1u << s);
    }
    fflush(rom);
    return ok;
}
