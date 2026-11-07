/*
 * N84 - TI variable files
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Layout: "**TI83F*" 1A 0A 00 (11 bytes), comment (42), data length (2),
 * then entries: header length (2: 0x0B or 0x0D), data length (2), type (1),
 * name (8), [version (1), flags (1, bit 7 = archived)], data length (2),
 * data; finally a 16-bit checksum (not verified).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link/tivar.h"
#include "platform/platform.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

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
    if (memcmp(b, "**TI83F*", 8)) {
        bool app = !memcmp(b, "**TIFL**", 8);
        tivar_free(out);
        return app ? "Flash applications and OS files are not supported yet" : "not a TI-83 Plus / TI-84 Plus variable file";
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
