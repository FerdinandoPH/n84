/*
 * N84 - TI variable files (.8xp, .8xv, .8xg, .8xs, ...: "**TI83F*" format)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_TIVAR_H
#define N84_TIVAR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint8_t type;            /* 0x05 program, 0x06 protected program, 0x15 AppVar, ... */
    uint8_t name[8];         /* as stored on the calculator (zero padded) */
    uint8_t version;
    bool archived;           /* the file asks for the variable to be archived */
    const uint8_t *data;     /* variable data, including its own size field */
    uint16_t size;
} tivar_t;

#define TIVAR_MAX 64

typedef struct {
    uint8_t *buf;            /* owned file contents */
    tivar_t vars[TIVAR_MAX];
    int count;
} tivar_file_t;

/* Parses a file. Returns NULL on success or a short error message. */
const char *tivar_load(const char *path, tivar_file_t *out);
void tivar_free(tivar_file_t *f);

/* Human-readable name ("HELLO", "appvar FOO", ...) into buf */
void tivar_describe(const tivar_t *v, char *buf, size_t size);

/* Variable types whose data starts with a 16-bit byte count (programs,
 * AppVars, strings, equations, pictures, groups, ...). */
bool tivar_has_size_word(uint8_t type);

/* Message for an OS error code met while creating a variable of `need`
 * bytes with `free_ram` bytes free (14 = ERR:MEMORY, ...). */
const char *tivar_os_error(unsigned code, unsigned need, unsigned long free_ram);

#endif
