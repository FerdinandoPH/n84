#!/usr/bin/env python3
# N84 - approximate ARM926EJ-S cycle model for the emulator cores
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs the cores (built by the Makefile next to this script with the Nspire
# compiler flags) inside Unicorn and charges ARM926 costs per executed
# instruction: 1-cycle issue, load-use interlocks (1 cycle for words, 2 for
# bytes/halfwords), 3 cycles for taken branches and PC writes, 5 for LDR PC,
# LDM/STM by register count, multiplies, register-specified shifts, and
# set-associative I/D caches (16 KB / 8 KB, 4-way, 32-byte lines: the CX II's
# cache type register, as Firebird reports it) with a fixed miss penalty.
#
# Firebird charges ~1 cycle per instruction, which overstates speed ~1.8x on
# the real CX II; this model is meant to rank optimisations, and is
# calibrated against measurements on the calculator (see --help).
#
# Requires: pip install unicorn capstone pyelftools
#
# Usage: arm926sim.py [--elf build/arm926sim/cores.elf] [--core ez80|z80|both]
#                     [--cycles N] [--profile]

import argparse
import collections
import random
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM
from capstone import arm as A
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn import arm_const as U

CPU_HZ = 396_000_000
STACK_TOP = 0x0800_0000
STACK_SIZE = 0x10_0000
RET_MAGIC = 0x0000_F000           # return address that stops emulation

# ---------------------------------------------------------------------------
# caches


class Cache:
    """ARM926 caches replace a random way by default (CP15 c1 RR bit clear);
    `lru` is available for comparison."""

    def __init__(self, size, ways=4, line=32, policy="random", seed=1):
        self.policy = policy
        self.rng = random.Random(seed)
        self.line_shift = line.bit_length() - 1
        self.sets = size // (ways * line)
        self.ways = ways
        self.data = [collections.OrderedDict() for _ in range(self.sets)]
        self.misses = 0
        self.accesses = 0

    def access(self, addr):
        """True on hit."""
        self.accesses += 1
        ln = addr >> self.line_shift
        s = self.data[ln % self.sets]
        if ln in s:
            if self.policy == "lru":
                s.move_to_end(ln)
            return True
        self.misses += 1
        if len(s) >= self.ways:
            if self.policy == "lru":
                s.popitem(last=False)
            else:
                del s[self.rng.choice(list(s))]
        s[ln] = True
        return False


# ---------------------------------------------------------------------------
# instruction classification (cached per address)

LOAD_WORD = {A.ARM_INS_LDR, A.ARM_INS_LDRT, A.ARM_INS_LDREX}
LOAD_SUB = {A.ARM_INS_LDRB, A.ARM_INS_LDRBT, A.ARM_INS_LDRH, A.ARM_INS_LDRSB,
            A.ARM_INS_LDRSH, A.ARM_INS_LDRHT, A.ARM_INS_LDRSBT, A.ARM_INS_LDRSHT}
LOAD_DUAL = {A.ARM_INS_LDRD}
STORES = {A.ARM_INS_STR, A.ARM_INS_STRB, A.ARM_INS_STRH, A.ARM_INS_STRT,
          A.ARM_INS_STRBT, A.ARM_INS_STREX}
STORE_DUAL = {A.ARM_INS_STRD}
LDM = {A.ARM_INS_LDM, A.ARM_INS_LDMDA, A.ARM_INS_LDMDB, A.ARM_INS_LDMIB, A.ARM_INS_POP}
STM = {A.ARM_INS_STM, A.ARM_INS_STMDA, A.ARM_INS_STMDB, A.ARM_INS_STMIB, A.ARM_INS_PUSH}
BRANCH = {A.ARM_INS_B, A.ARM_INS_BL, A.ARM_INS_BX, A.ARM_INS_BLX}
MUL = {A.ARM_INS_MUL, A.ARM_INS_MLA, A.ARM_INS_SMULL, A.ARM_INS_UMULL,
       A.ARM_INS_SMLAL, A.ARM_INS_UMLAL, A.ARM_INS_SMULBB, A.ARM_INS_SMLABB}
PC = A.ARM_REG_PC


class Info:
    __slots__ = ("kind", "reads", "writes", "cc", "extra", "nregs", "lat", "pcdst", "text", "tags")


def classify(md, code, addr):
    insn = next(md.disasm(code, addr))
    inf = Info()
    inf.text = f"{insn.mnemonic} {insn.op_str}"
    rd, wr = insn.regs_access()
    inf.reads = tuple(r for r in rd if r not in (PC, A.ARM_REG_CPSR))
    inf.writes = tuple(r for r in wr if r != A.ARM_REG_CPSR)
    inf.cc = insn.cc
    inf.extra = 0
    inf.nregs = 0
    inf.lat = 0
    inf.pcdst = PC in wr
    inf.tags = []
    i = insn.id
    scaled = False
    for op in insn.operands:
        if op.type == A.ARM_OP_MEM and op.mem.index != 0 and op.shift.type != 0 and op.shift.value:
            scaled = True
        if op.type == A.ARM_OP_MEM and op.mem.index != 0:
            inf.tags.append("mem_regoff_shift" if op.shift.type != 0 and op.shift.value else "mem_regoff")
        if op.type == A.ARM_OP_MEM:
            inf.tags.append("sp" if op.mem.base == A.ARM_REG_SP else "nonsp")
    if i in LOAD_WORD or i in LOAD_SUB or i in LOAD_DUAL:
        inf.kind = "load"
        inf.lat = 1 if i in LOAD_WORD else 2
        if i in LOAD_DUAL or scaled:          # measured: [r, r, lsl #n] costs 1 more
            inf.extra = 1
    elif i in STORES or i in STORE_DUAL:
        inf.kind = "store"
        if i in STORE_DUAL or scaled:
            inf.extra = 1
    elif i in LDM:
        inf.kind = "ldm"
        inf.nregs = sum(1 for op in insn.operands if op.type == A.ARM_OP_REG) - (0 if i == A.ARM_INS_POP else 1)
        inf.lat = 1
    elif i in STM:
        inf.kind = "stm"
        inf.nregs = sum(1 for op in insn.operands if op.type == A.ARM_OP_REG) - (0 if i == A.ARM_INS_PUSH else 1)
    elif i in BRANCH:
        inf.kind = "branch"
    elif i in MUL:
        inf.kind = "mul"
        inf.extra = 1 if i in (A.ARM_INS_MUL, A.ARM_INS_SMULBB) else 2
        inf.lat = 1
    else:
        inf.kind = "alu"
        for op in insn.operands:
            if op.type == A.ARM_OP_REG and op.shift.type in (A.ARM_SFT_ASR_REG, A.ARM_SFT_LSL_REG,
                                                               A.ARM_SFT_LSR_REG, A.ARM_SFT_ROR_REG):
                inf.extra = 1
    return inf


def cond_passes(cc, cpsr):
    n, z, c, v = (cpsr >> 31) & 1, (cpsr >> 30) & 1, (cpsr >> 29) & 1, (cpsr >> 28) & 1
    return {
        A.ARM_CC_INVALID: True, A.ARM_CC_AL: True,
        A.ARM_CC_EQ: z, A.ARM_CC_NE: not z, A.ARM_CC_HS: c, A.ARM_CC_LO: not c,
        A.ARM_CC_MI: n, A.ARM_CC_PL: not n, A.ARM_CC_VS: v, A.ARM_CC_VC: not v,
        A.ARM_CC_HI: c and not z, A.ARM_CC_LS: (not c) or z,
        A.ARM_CC_GE: n == v, A.ARM_CC_LT: n != v,
        A.ARM_CC_GT: (not z) and n == v, A.ARM_CC_LE: z or n != v,
    }[cc]


# ---------------------------------------------------------------------------
# simulator


class Sim:
    def __init__(self, elf_path, icache, dcache, miss, policy="random"):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
        self.md.detail = True
        self.syms = {}
        with open(elf_path, "rb") as f:
            elf = ELFFile(f)
            for seg in elf.iter_segments():
                if seg["p_type"] != "PT_LOAD":
                    continue
                base = seg["p_vaddr"] & ~0xFFF
                end = (seg["p_vaddr"] + seg["p_memsz"] + 0xFFF) & ~0xFFF
                self.uc.mem_map(base, end - base)
                self.uc.mem_write(seg["p_vaddr"], seg.data())
            for sym in elf.get_section_by_name(".symtab").iter_symbols():
                if sym["st_info"]["type"] == "STT_FUNC":
                    self.syms[sym.name] = sym["st_value"]
        self.funcs = sorted((a, n) for n, a in self.syms.items())
        self.uc.mem_map(STACK_TOP - STACK_SIZE, STACK_SIZE)
        self.uc.mem_map(RET_MAGIC & ~0xFFF, 0x1000)
        self.info = {}
        self.ready = collections.defaultdict(int)
        self.t = 0
        self.prev = None          # (addr, Info, executed)
        self.icache = Cache(icache, policy=policy) if icache else None
        self.dcache = Cache(dcache, policy=policy) if dcache else None
        self.miss = miss
        self.profile = None
        self.stats = collections.Counter()
        self.uc.hook_add(UC_HOOK_CODE, self._code)
        if self.dcache:
            self.uc.hook_add(UC_HOOK_MEM_READ, self._read)

    def _read(self, uc, access, addr, size, value, user):
        if not self.dcache.access(addr):
            self.t += self.miss

    def _finish_prev(self, next_addr):
        addr, inf, executed = self.prev
        if inf.kind == "branch" and executed:
            self.t += 2                       # taken branch: 3 cycles total
        elif inf.pcdst and executed:
            self.t += 4 if inf.kind in ("load", "ldm") else 3     # measured: add pc = 4

    def _code(self, uc, addr, size, user):
        if self.prev is not None:
            self._finish_prev(addr)
        inf = self.info.get(addr)
        if inf is None:
            inf = classify(self.md, bytes(uc.mem_read(addr, 4)), addr)
            self.info[addr] = inf
        t0 = self.t
        if self.icache and not self.icache.access(addr):
            self.t += self.miss
        executed = inf.cc in (A.ARM_CC_AL, A.ARM_CC_INVALID) or cond_passes(inf.cc, uc.reg_read(U.UC_ARM_REG_CPSR))
        # operands must be ready (interlock), even for a failed condition
        ready = self.ready
        before = start = self.t
        for r in inf.reads:
            if ready[r] > start:
                start = ready[r]
        self.t = start + 1
        if executed:
            k = inf.kind
            if k == "load":
                self.t += inf.extra
                for r in inf.writes:
                    ready[r] = self.t + inf.lat
                # base writeback is ready at once
                for r in inf.reads:
                    if r in inf.writes:
                        ready[r] = self.t
            elif k == "ldm":
                self.t += max(inf.nregs, 2) - 1
                for r in inf.writes:
                    ready[r] = self.t + inf.lat
            elif k == "stm":
                self.t += max(inf.nregs, 2) - 1
            elif k == "store":
                self.t += inf.extra
            elif k == "mul":
                self.t += inf.extra
                for r in inf.writes:
                    ready[r] = self.t + inf.lat
            else:
                self.t += inf.extra
        self.prev = (addr, inf, executed)
        st = self.stats
        st["insns"] += 1
        st["stall"] += start - before
        if executed:
            st[inf.kind + (".sub" if inf.kind == "load" and inf.lat == 2 else "")] += 1
            for tg in inf.tags:
                st[inf.kind + "." + tg] += 1
            if inf.pcdst:
                st["pc_write"] += 1
        else:
            st["cond_failed"] += 1
        if self.profile is not None:
            self.profile[addr] += self.t - t0

    def call(self, name, *args):
        uc = self.uc
        regs = (U.UC_ARM_REG_R0, U.UC_ARM_REG_R1, U.UC_ARM_REG_R2, U.UC_ARM_REG_R3)
        for r, v in zip(regs, args):
            uc.reg_write(r, v & 0xFFFFFFFF)
        uc.reg_write(U.UC_ARM_REG_SP, STACK_TOP - 16)
        uc.reg_write(U.UC_ARM_REG_LR, RET_MAGIC)
        uc.emu_start(self.syms[name], RET_MAGIC)
        if self.prev is not None:
            self._finish_prev(RET_MAGIC)
            self.prev = None
        v = uc.reg_read(U.UC_ARM_REG_R0)
        return v - (1 << 32) if v & 0x8000_0000 else v

    def func_of(self, addr):
        name = "?"
        for a, n in self.funcs:
            if a > addr:
                break
            name = n
        return name


def run_core(sim, core, cycles, slice_cycles, profile, stats, placement=0):
    if core == "ez80":
        sim.call("sim_setup_ez80", placement)
        core_label = f"ez80@{placement}"
    else:
        sim.call("sim_setup_z80")
        core_label = core
    sim.call(f"sim_run_{core}", slice_cycles)           # warm up caches and state
    if profile:
        sim.profile = collections.Counter()
    t0 = sim.t
    sim.stats = collections.Counter()
    emu = 0
    while emu < cycles:
        emu += sim.call(f"sim_run_{core}", slice_cycles)
    arm = sim.t - t0
    mhz = emu * CPU_HZ / arm / 1e6
    target = 15.0 if core == "z80" else 48.0
    print(f"{core_label:7s}: {arm / emu:6.2f} ARM cycles per emulated cycle -> "
          f"{mhz:6.2f} MHz equiv ({100 * mhz / target:4.0f}% of {target:g} MHz)")
    if stats:
        for nm, c in (("icache", sim.icache), ("dcache", sim.dcache)):
            if c:
                print(f"  {nm}: {c.misses} misses / {c.accesses} accesses")
                c.misses = c.accesses = 0
        print("  per 100 emulated cycles: " + ", ".join(
            f"{k} {100 * v / emu:.1f}" for k, v in sorted(sim.stats.items())))
    if profile:
        by_func = collections.Counter()
        for a, c in sim.profile.items():
            by_func[sim.func_of(a)] += c
        tot = sum(sim.profile.values())
        print("  by function:")
        for n, c in by_func.most_common(8):
            print(f"    {100 * c / tot:5.1f}%  {n}")
        print("  hottest instructions:")
        for a, c in sim.profile.most_common(profile):
            print(f"    {100 * c / tot:5.1f}%  {a:08x}  {sim.info[a].text}")
        sim.profile = None


ARM_TEST_NAMES = ["alu", "ldr+use", "ldrb+use", "ldrb+1+use", "ldr", "ldr sp", "ldr[r,r,lsl2]",
                  "ldrb[r,r]", "ldr[r,r,lsr]", "ldr>base", "alu>shift", "shift reg", "str", "4strb",
                  "4strb sp", "b", "add pc", "ldr pc", "ldrh+add pc", "push/pop4"]
ARM_TEST_ITERS = 2000          # BENCH_ARM_ITERS in the Makefile


def run_arm_tests(sim):
    """Same patterns as n84bench's 'ARM cycles per pattern' block."""
    n = sim.call("sim_arm_count")
    names = ARM_TEST_NAMES if len(ARM_TEST_NAMES) == n else [str(i) for i in range(n)]
    for i in range(n):
        sim.call("sim_arm_test", i)                      # warm caches
        t0 = sim.t
        sim.call("sim_arm_test", i)
        per = ((sim.t - t0) / ARM_TEST_ITERS - 4) / 8
        print(f"  {names[i]:14s} {per:5.1f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--elf", default="build/arm926sim/cores.elf")
    ap.add_argument("--core", choices=("ez80", "z80", "both"), default="both")
    ap.add_argument("--cycles", type=int, default=300_000, help="emulated cycles to measure")
    ap.add_argument("--slice", type=int, default=20_000, help="cycles per *_run call")
    ap.add_argument("--icache", type=int, default=16384, help="I-cache bytes (0 = perfect)")
    ap.add_argument("--dcache", type=int, default=8192, help="D-cache bytes (0 = perfect)")
    ap.add_argument("--miss", type=int, default=96, help="cache miss penalty in cycles (measured on the CX II: ~96)")
    ap.add_argument("--profile", type=int, nargs="?", const=25, default=0,
                    help="show the N hottest ARM instructions")
    ap.add_argument("--stats", action="store_true", help="instruction mix per emulated cycle")
    ap.add_argument("--placement", type=int, default=-1, help="eZ80 memory placement (-1 = all)")
    ap.add_argument("--policy", choices=("random", "lru"), default="random", help="cache replacement")
    ap.add_argument("--arm-tests", action="store_true", help="run the ARM pattern micro-benchmarks")
    a = ap.parse_args()
    sim = Sim(a.elf, a.icache, a.dcache, a.miss, a.policy)
    if a.arm_tests:
        run_arm_tests(sim)
        return 0
    for core in (("z80", "ez80") if a.core == "both" else (a.core,)):
        if core == "ez80":
            places = range(sim.call("sim_placements")) if a.placement < 0 else (a.placement,)
            for pl in places:
                run_core(sim, core, a.cycles, a.slice, a.profile, a.stats, pl)
        else:
            run_core(sim, core, a.cycles, a.slice, a.profile, a.stats)


if __name__ == "__main__":
    sys.exit(main())
