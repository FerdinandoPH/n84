/*
 * N84 - one emulation session (ROM loaded, running, in-game menu)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_SESSION_H
#define N84_SESSION_H

#include <stddef.h>

typedef enum { SESSION_TO_LAUNCHER, SESSION_QUIT, SESSION_ERROR } session_result_t;

/* Runs the ROM at rom_path until the user leaves. State is loaded from and
 * saved to the matching NAME.sav.tns; Flash changes go back into the ROM. */
session_result_t session_run(const char *rom_path);

/* "dir/name.rom.tns" -> "dir/name.sav.tns" (buf must hold strlen+16) */
void session_state_path(const char *rom_path, char *buf, size_t size);

#endif
