# -*- coding: utf-8 -*-
# armrun.py — ينفّذ نوى armt.bin على محاكي Unicorn ARM64 ويتحقق من النتائج
import struct, sys
from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM, UC_PROT_ALL
from unicorn.arm64_const import *

BASE  = 0x10000
STACK = 0x200000
DATA  = 0x300000   # أعداد عائمة: D[i] عند DATA + 8*i
DATA2 = 0x401000   # أعداد صحيحة (0x400000 + 4096 لإتاحة إزاحات سالبة)
CTX   = 0x500000
BUF   = 0x600000

uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
uc.mem_map(BASE, 0x10000, UC_PROT_ALL)
uc.mem_write(BASE, open("armt.bin", "rb").read())
uc.mem_map(STACK, 0x10000, UC_PROT_ALL)
uc.mem_map(DATA, 0x10000, UC_PROT_ALL)
uc.mem_map(0x400000, 0x80000, UC_PROT_ALL)
uc.mem_map(CTX, 0x1000, UC_PROT_ALL)
uc.mem_map(BUF, 0x1000, UC_PROT_ALL)

vals = [3.5, 2.25, -1.5, float("nan"), 0.0, 1.0, 9.0, -2.75, 1.5, 3.0, 5.0, 2.0]
uc.mem_write(DATA, b"".join(struct.pack("<d", v) for v in vals))

def wq(a, v): uc.mem_write(a, struct.pack("<Q", v & (2**64 - 1)))
wq(DATA2 + 64, 100)            # cmp_rm
wq(DATA2 + 24, 0x5555)         # mem_indexed (idx)
wq(DATA2 + 32, 0x5555)         # mem_indexed (العنوان الفعلي)
wq(DATA2 + 100000, 777)        # mem_big
wq(DATA2 - 1000, 0xABCD)       # mem_neg
wq(DATA2 + 7, 0x1122334455667788)  # mem_ldur
uc.mem_write(DATA2 + 1, b"\x05")   # cmp8

uc.mem_write(CTX, struct.pack("<QQQ", DATA2, DATA, DATA2))  # slots/base/clo

kernels, patches = {}, []
for line in open("armt.lst"):
    p = line.split()
    if p[0] == "K":
        kernels[p[1]] = (int(p[2]), int(p[3]), int(p[4]), int(p[5]), int(p[6], 16), int(p[7], 16))
    elif p[0] == "P":
        patches.append((int(p[1]), int(p[2])))

# ترقيع عنوان المساعد في ldi64 x15 (كلمتان: movz + movk)
for ldi_off, hoff in patches:
    addr = BASE + hoff
    w1 = 0xD2800000 | ((addr & 0xFFFF) << 5) | 15
    w2 = 0xF2800000 | (1 << 21) | (((addr >> 16) & 0xFFFF) << 5) | 15
    uc.mem_write(BASE + ldi_off, struct.pack("<II", w1, w2))

def XR(i):
    return globals()[f"UC_ARM64_REG_X{i}"]

def dbits(v):
    return struct.unpack("<Q", struct.pack("<d", v))[0]

def reset(name):
    uc.reg_write(XR(9), BUF)
    for r, v in ((19, DATA2), (20, DATA), (21, DATA2), (22, DATA2), (23, CTX)):
        uc.reg_write(XR(r), v)
    for i in range(8):
        uc.reg_write(globals()[f"UC_ARM64_REG_D{i}"], 0)
    if name == "frame":
        for r, v in ((19, 0x1111111111111111), (20, 0x2222222222222222),
                     (21, 0x3333333333333333), (22, 0x4444444444444444),
                     (23, 0x5555555555555555)):
            uc.reg_write(XR(r), v)
        uc.reg_write(UC_ARM64_REG_D8, dbits(8.25))
        uc.reg_write(UC_ARM64_REG_D15, dbits(-13.5))
        uc.reg_write(XR(0), CTX)
    else:
        for i in range(6):
            uc.reg_write(XR(i), 0)

fails = 0
for name, (off, size, cx, cd, ex, ed) in kernels.items():
    reset(name)
    uc.mem_write(BUF, b"\x00" * 16)
    end = BASE + off + size
    uc.reg_write(UC_ARM64_REG_LR, end)
    uc.reg_write(UC_ARM64_REG_SP, 0x20F000)
    try:
        uc.emu_start(BASE + off, end, 2_000_000, 500000)
    except UcError as e:
        print(f"فشل {name}: استثناء محاكي {e}")
        fails += 1
        continue
    ok, msgs = True, []
    rx, rd = struct.unpack("<QQ", uc.mem_read(BUF, 16))
    if cx and rx != ex:
        ok = False; msgs.append(f"x: {rx:#x} != {ex:#x}")
    if cd and rd != ed:
        ok = False; msgs.append(f"d: {struct.unpack('<d', struct.pack('<Q', rd))[0]!r} != {struct.unpack('<d', struct.pack('<Q', ed))[0]!r}")
    if name == "frame":
        for r, sv in ((19, 0x1111111111111111), (20, 0x2222222222222222),
                      (21, 0x3333333333333333), (22, 0x4444444444444444),
                      (23, 0x5555555555555555)):
            if uc.reg_read(XR(r)) != sv:
                ok = False; msgs.append(f"x{r} لم تُستعد ({uc.reg_read(XR(r)):#x})")
        if uc.reg_read(UC_ARM64_REG_D8) != dbits(8.25):
            ok = False; msgs.append("d8 لم تُستعد")
        if uc.reg_read(UC_ARM64_REG_D15) != dbits(-13.5):
            ok = False; msgs.append("d15 لم تُستعد")
    if ok:
        print(f"نجح {name}")
    else:
        print(f"فشل {name}: " + "; ".join(msgs))
        fails += 1

print(f"\n== {len(kernels)} نواة، فشل: {fails} ==")
sys.exit(1 if fails else 0)
