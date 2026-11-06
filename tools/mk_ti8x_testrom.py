#!/usr/bin/env python3
# N84 - generates a synthetic TI-84 Plus (1 MB) ROM image used to test the
# ti8x machine without any TI code. SPDX-License-Identifier: GPL-3.0-or-later
#
# Layout:
#   page 00h, offset 0038h : IM 1 interrupt handler (counts timer interrupts)
#   page 3Fh (boot page)   : test program, entered at 8000h after reset
#   page 10h               : scratch page for Flash program/erase tests
#
# Results written to RAM page 0 (mapped at C000h):
#   C000h  interrupt counter (16-bit)
#   C002h  last key group 0 read (arrows)
#   C010h  Flash byte after program (expect 42h)
#   C011h  Flash byte after erase   (expect FFh)
#   C012h  port 02h value read after unlock (bit 2 = Flash unlocked)
#   C020h  signature 'N','8','4' when setup completed
import sys

PAGE = 0x4000


class Asm:
    def __init__(self, org):
        self.org = org
        self.buf = bytearray()
        self.labels = {}
        self.fix = []          # (pos, label, kind)

    @property
    def pc(self):
        return self.org + len(self.buf)

    def label(self, name):
        self.labels[name] = self.pc

    def db(self, *bs):
        for b in bs:
            self.buf.append(b & 0xFF)

    def dw(self, w):
        self.db(w & 0xFF, w >> 8)

    def _ref16(self, opcode, target):
        self.db(*opcode)
        if isinstance(target, str):
            self.fix.append((len(self.buf), target, 'abs'))
            self.dw(0)
        else:
            self.dw(target)

    def jp(self, t): self._ref16([0xC3], t)
    def jpnz(self, t): self._ref16([0xC2], t)
    def jpz(self, t): self._ref16([0xCA], t)
    def call(self, t): self._ref16([0xCD], t)

    def jr(self, t, op=0x18):
        self.db(op)
        self.fix.append((len(self.buf), t, 'rel'))
        self.db(0)

    def jrnz(self, t): self.jr(t, 0x20)
    def jrz(self, t): self.jr(t, 0x28)
    def djnz(self, t): self.jr(t, 0x10)

    # common instructions
    def ld_a(self, n): self.db(0x3E, n)
    def ld_b(self, n): self.db(0x06, n)
    def ld_hl(self, nn): self._ref16([0x21], nn)
    def ld_de(self, nn): self._ref16([0x11], nn)
    def ld_sp(self, nn): self._ref16([0x31], nn)
    def ld_mem_a(self, nn): self._ref16([0x32], nn)     # ld (nn),a
    def ld_a_mem(self, nn): self._ref16([0x3A], nn)     # ld a,(nn)
    def out(self, port): self.db(0xD3, port)            # out (n),a
    def inp(self, port): self.db(0xDB, port)            # in a,(n)
    def outv(self, port, v): self.ld_a(v); self.out(port)

    def resolve(self):
        for pos, name, kind in self.fix:
            addr = self.labels[name]
            if kind == 'abs':
                self.buf[pos] = addr & 0xFF
                self.buf[pos + 1] = addr >> 8
            else:
                off = addr - (self.org + pos + 1)
                assert -128 <= off <= 127, name
                self.buf[pos] = off & 0xFF
        return bytes(self.buf)


def interrupt_handler():
    a = Asm(0x0038)
    a.db(0xF5)                      # push af
    a.db(0xE5)                      # push hl
    a.ld_hl(0xC000)
    a.db(0x34)                      # inc (hl)
    a.jrnz('noc')
    a.db(0x23, 0x34)                # inc hl / inc (hl)
    a.label('noc')
    a.outv(0x03, 0x00)              # acknowledge all
    a.outv(0x03, 0x03)              # re-enable ON + timer 1
    a.db(0xE1, 0xF1)                # pop hl / pop af
    a.db(0xFB, 0xC9)                # ei / ret
    return a.resolve()


def boot_program():
    a = Asm(0x8000)
    a.db(0xF3)                      # di
    a.outv(0x04, 0x06)              # memory mode 0, slowest HW timer
    a.outv(0x05, 0x00)              # RAM page 0 at C000h
    a.ld_sp(0x0000)
    a.ld_a(0)
    a.ld_mem_a(0xC000)
    a.ld_mem_a(0xC001)
    a.outv(0x20, 0x01)              # 15 MHz

    # ---- Flash: unlock (must run from a privileged page: this one) ----
    a.ld_a(0x01)
    a.db(0x00, 0x00, 0xED, 0x56, 0xF3, 0xD3, 0x14)   # nop nop im1 di out(14h),a
    a.inp(0x02)
    a.ld_mem_a(0xC012)
    a.outv(0x06, 0x10)              # Flash page 10h at 4000h
    # program 42h at 4000h
    a.outv(0x06, 0x10)
    a.ld_a(0xAA); a.ld_mem_a(0x4AAA)
    a.ld_a(0x55); a.ld_mem_a(0x4555)
    a.ld_a(0xA0); a.ld_mem_a(0x4AAA)
    a.ld_a(0x42); a.ld_mem_a(0x4000)
    a.ld_a_mem(0x4000); a.ld_mem_a(0xC010)
    # erase sector
    a.ld_a(0xAA); a.ld_mem_a(0x4AAA)
    a.ld_a(0x55); a.ld_mem_a(0x4555)
    a.ld_a(0x80); a.ld_mem_a(0x4AAA)
    a.ld_a(0xAA); a.ld_mem_a(0x4AAA)
    a.ld_a(0x55); a.ld_mem_a(0x4555)
    a.ld_a(0x30); a.ld_mem_a(0x4000)
    a.ld_a_mem(0x4000); a.ld_mem_a(0xC011)
    a.ld_a(0x00)
    a.db(0x00, 0x00, 0xED, 0x56, 0xF3, 0xD3, 0x14)   # lock again

    # ---- LCD: 8-bit mode, on, Y auto-increment, draw a pattern ----
    a.outv(0x10, 0x01)              # 8-bit
    a.outv(0x10, 0x03)              # display on
    a.outv(0x10, 0x05)              # auto-increment Y
    a.outv(0x10, 0x40)              # z-address 0
    a.outv(0x10, 0xF0)              # contrast 30h
    a.ld_b(0)                       # column counter in C
    a.db(0x0E, 0x00)                # ld c,0
    a.label('col')
    a.db(0x79)                      # ld a,c
    a.db(0xC6, 0x20)                # add a,20h
    a.out(0x10)                     # set column
    a.outv(0x10, 0x80)              # row 0
    a.ld_b(64)
    a.label('row')
    a.db(0x78)                      # ld a,b
    a.db(0xA9)                      # xor c  (pattern depends on row/col)
    a.db(0xE6, 0x01)                # and 1
    a.jrz('blank')
    a.ld_a(0xF0)
    a.jr('wr')
    a.label('blank')
    a.ld_a(0x0F)
    a.label('wr')
    a.out(0x11)
    a.djnz('row')
    a.db(0x0C)                      # inc c
    a.db(0x79)                      # ld a,c
    a.db(0xFE, 12)                  # cp 12
    a.jrnz('col')

    # ---- interrupts ----
    a.outv(0x03, 0x03)              # ON + timer 1
    a.db(0xED, 0x56)                # im 1
    a.ld_a(ord('N')); a.ld_mem_a(0xC020)
    a.ld_a(ord('8')); a.ld_mem_a(0xC021)
    a.ld_a(ord('4')); a.ld_mem_a(0xC022)
    a.db(0xFB)                      # ei
    a.label('loop')
    a.db(0x76)                      # halt
    a.outv(0x01, 0xFE)              # select arrow group
    a.db(0x00, 0x00)
    a.inp(0x01)
    a.ld_mem_a(0xC002)
    a.jr('loop')
    return a.resolve()


def main(out):
    rom = bytearray(b'\xFF' * 0x100000)
    rom[0:0x38] = b'\x00' * 0x38
    ih = interrupt_handler()
    rom[0x38:0x38 + len(ih)] = ih
    boot = boot_program()
    base = 0x3F * PAGE
    rom[base:base + len(boot)] = boot
    with open(out, 'wb') as f:
        f.write(rom)


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'ti84p_test.rom')
