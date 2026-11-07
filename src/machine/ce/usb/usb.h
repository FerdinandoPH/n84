/*
 * N84 - USB controller stub for the CEmu-derived TI-84 Plus CE machine.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * No USB device is ever connected. Registers read as zero.
 */
#ifndef N84_CE_USB_H
#define N84_CE_USB_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../port.h"
static inline uint8_t usb_status(void) { return 0; }   /* no VBUS, not a B-device */
eZ80portrange_t init_usb(void);
void usb_reset(void);
#endif
