/*
 * N84 - TI variable files
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Layout: "**TI83F*" 1A 0A 00 (11 bytes), comment (42), data length (2),
 * then entries: header length (2: 0x0B or 0x0D), data length (2), type (1),
 * name (8), [version (1), flags (1, bit 7 = archived)], data length (2),
 * data; finally a 16-bit checksum (not verified).
 *
 * Flash apps (.8xk): "**TIFL**" header (78 bytes: name at 11h, device 73h
 * at 30h, type 24h at 31h, data length at 4Ah), then Intel HEX records:
 * type 02 selects the app page (0, 1, ...), type 00 data at 4000h-7FFFh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link/tivar.h"
#include "platform/platform.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

#define APP_MAX_PAGES 64

static int hexval(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static const char *load_app(tivar_file_t *out, size_t n) {
    const uint8_t *b = out->buf;
    if (n < 0x4E + 11 || b[0x30] != 0x73 || b[0x31] != 0x24)
        return "not a TI-83 Plus / TI-84 Plus Flash application";
    uint8_t *pages = malloc(APP_MAX_PAGES * 0x4000u);
    if (!pages) return "out of memory";
    memset(pages, 0xFF, APP_MAX_PAGES * 0x4000u);
    unsigned page = 0, npages = 0;
    const char *err = NULL;
    for (size_t pos = 0x4E; pos < n && !err; ) {
        if (b[pos] != ':') { pos++; continue; }
        uint8_t rec[260];
        size_t len = 0;
        for (pos++; pos + 1 < n && hexval(b[pos]) >= 0 && hexval(b[pos + 1]) >= 0 && len < sizeof rec; pos += 2)
            rec[len++] = (uint8_t)(hexval(b[pos]) << 4 | hexval(b[pos + 1]));
        if (len < 5 || len != 5u + rec[0]) { err = "corrupt application (bad record)"; break; }
        uint8_t sum = 0;
        for (size_t i = 0; i < len; i++) sum = (uint8_t)(sum + rec[i]);
        if (sum) { err = "corrupt application (checksum)"; break; }
        unsigned addr = (unsigned)rec[1] << 8 | rec[2];
        if (rec[3] == 0x01) break;
        if (rec[3] == 0x02 && rec[0] == 2) page = (unsigned)rec[4] << 8 | rec[5];
        else if (rec[3] == 0x00) {
            if (page >= APP_MAX_PAGES || addr < 0x4000 || addr + rec[0] > 0x8000) { err = "corrupt application (address)"; break; }
            memcpy(pages + page * 0x4000u + (addr - 0x4000), rec + 4, rec[0]);
            if (page + 1 > npages) npages = page + 1;
        }
    }
    if (!err && (!npages || pages[0] != 0x80 || pages[1] != 0x0F)) err = "corrupt application (no header)";
    if (err) { free(pages); return err; }
    tivar_t *v = &out->vars[0];
    v->type = TIVAR_APP;
    memcpy(v->name, b + 0x11, 8);
    for (int i = 7; i >= 0 && v->name[i] == ' '; i--) v->name[i] = 0;
    v->archived = true;
    v->data = pages;
    v->size = npages * 0x4000u;
    free(out->buf);
    out->buf = pages;
    out->count = 1;
    return NULL;
}

const char *tivar_load(const char *path, tivar_file_t *out) {
    memset(out, 0, sizeof *out);
    FILE *f = plat_fopen(path, "rb");
    if (!f) return "cannot open the file";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0x37 + 2 || n > 0x200000) { fclose(f); return "not a TI variable file"; }
    out->buf = malloc((size_t)n);
    if (!out->buf) { fclose(f); return "out of memory"; }
    size_t got = fread(out->buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { tivar_free(out); return "read error"; }
    const uint8_t *b = out->buf;
    if (!memcmp(b, "**TIFL**", 8)) {
        const char *e = load_app(out, (size_t)n);
        if (e) tivar_free(out);
        return e;
    }
    if (memcmp(b, "**TI83F*", 8)) {
        tivar_free(out);
        return "not a TI-83 Plus / TI-84 Plus variable file";
    }
    size_t len = le16(b + 0x35), pos = 0x37, end = 0x37 + len;
    if (end > (size_t)n) end = (size_t)n;
    while (pos + 2 + 2 + 1 + 8 + 2 <= end && out->count < TIVAR_MAX) {
        uint16_t hdr = le16(b + pos);
        if (hdr != 0x0B && hdr != 0x0D) break;
        tivar_t *v = &out->vars[out->count];
        uint16_t dlen = le16(b + pos + 2);
        v->type = b[pos + 4];
        memcpy(v->name, b + pos + 5, 8);
        v->version = hdr == 0x0D ? b[pos + 13] : 0;
        v->archived = hdr == 0x0D && (b[pos + 14] & 0x80);
        size_t dpos = pos + 2 + hdr + 2;
        if (dpos + dlen > end) break;
        v->data = b + dpos;
        v->size = dlen;
        out->count++;
        pos = dpos + dlen;
    }
    if (!out->count) { tivar_free(out); return "the file contains no variables"; }
    return NULL;
}

void tivar_free(tivar_file_t *f) {
    free(f->buf);
    f->buf = NULL;
    f->count = 0;
}

bool tivar_has_size_word(uint8_t type) {
    switch (type & 0x1F) {
    case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x0B:
    case 0x15: case 0x17: case 0x1A: case 0x1B:
        return true;
    default:
        return false;
    }
}

void tivar_describe(const tivar_t *v, char *buf, size_t size) {
    char name[9];
    for (int i = 0; i < 8; i++) {
        uint8_t c = v->name[i];
        name[i] = c >= 0x20 && c < 0x7F ? (char)c : (c ? '?' : 0);
    }
    name[8] = 0;
    const char *kind;
    if (v->type == TIVAR_APP) { snprintf(buf, size, "app %s", name); return; }
    switch (v->type & 0x1F) {
    case 0x05: kind = "program"; break;
    case 0x06: kind = "program (protected)"; break;
    case 0x15: kind = "AppVar"; break;
    case 0x04: kind = "string"; break;
    case 0x07: kind = "picture"; break;
    case 0x17: kind = "group"; break;
    case 0x00: kind = "real"; break;
    case 0x01: kind = "list"; break;
    case 0x02: kind = "matrix"; break;
    default:   kind = "variable"; break;
    }
    snprintf(buf, size, "%s %s", kind, name);
}

const char *tivar_os_error(unsigned code, unsigned need, unsigned long free_ram) {
    static char msg[96];
    switch (code & 0x7F) {
    case 14:
        snprintf(msg, sizeof msg, "not enough free RAM (needs %u bytes, %lu free)", need, free_ram);
        break;
    case 47: snprintf(msg, sizeof msg, "a variable with that name is archived"); break;
    case 49: snprintf(msg, sizeof msg, "the archive is full"); break;
    default: snprintf(msg, sizeof msg, "the calculator OS reported error %u", code & 0x7F);
    }
    return msg;
}
