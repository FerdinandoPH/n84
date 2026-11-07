/*
 * N84 - launcher (ROM list, information, settings)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef N84_LAUNCHER_H
#define N84_LAUNCHER_H
#include <stdbool.h>
#include <stddef.h>

/* Shows the launcher. Returns true with the chosen ROM's path in rom_path,
 * or false if the user quits. */
bool launcher_run(char *rom_path, size_t size);

#endif
