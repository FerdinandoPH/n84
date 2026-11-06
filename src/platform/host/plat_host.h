/* N84 - host-only platform extensions (tests). SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef N84_PLAT_HOST_H
#define N84_PLAT_HOST_H
#include "platform/platform.h"

void     plat_host_set_key(plat_key_t k, bool down);
void     plat_host_set_on(bool down);
bool     plat_host_save_ppm(const char *path);
unsigned plat_host_frames(void);

#endif
