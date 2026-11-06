# -*- coding: utf-8 -*-
# armfj.py — تنفيذ تنفيذي لبايتات fnjit-ARM المولدة من fjgen (fjarm.bin + fjarm.lst)
# على محاكي Unicorn ARM64، على العناوين المضيفية نفسها (الكود يحمل عناوين مطلقة
# لـg_fj_* والمساعدات فلا إعادة تموضع ممكنة).
# يتحقق لكل حالة: النتيجة في d0، علم الرجوع المنطقي، راية التخفي، وتوازن العمق.
# التشغيل من مجلد fjarm.bin: python armfj.py
import struct, math, sys
from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM, UC_PROT_ALL, UC_HOOK_CODE
from unicorn.arm64_const import *

MAGIC = 0x100000      # عنوان التوقف السحري: ret السحري يقفز إليه فيتوقف emu_start
STACK_TOP = 0x20F000  # نفس قيمة armrun — محاذاة 16 ومساحة مليون بايت تحتها
FJ_STACK_BUDGET = 393216

funcs, tests, glob = {}, [], None
for line in open("fjarm.lst"):
    p = line.split()
    if p[0] == "GLOB":
        glob = dict(depth=int(p[1]), limit=int(p[2]), bailed=int(p[3]), ret_bool=int(p[4]),
                    h_mod=int(p[5]), h_pow=int(p[6]), frames_max=int(p[7]))
    elif p[0] == "FUNC":
        funcs[p[1]] = (int(p[2]), int(p[3]), int(p[4]), int(p[5]), int(p[6]))
    elif p[0] == "TEST":
        tests.append((p[1], int(p[2]), float(p[3]), float(p[4]), float(p[5]), int(p[6])))
if not glob or not funcs:
    print("بيان fjarm.lst ناقص"); sys.exit(1)
blob = open("fjarm.bin", "rb").read()

uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)

# 1) كود الدوال: نطاق واحد يغطي كل عناوين entry (العناوين متباعدة 64KB لكل mmap)
lo = min(v[0] for v in funcs.values()) & ~0xFFF
hi = (max(v[0] for v in funcs.values()) + 0x2000) & ~0xFFF
uc.mem_map(lo, hi - lo, UC_PROT_ALL)
for name, (entry, msz, arity, rt, off) in funcs.items():
    uc.mem_write(entry, blob[off:off + msz])

# 2) صفحة g_fj_* (أربعة عدادات متقاربة) — الذاكرة صفرية افتراضياً
glo = min(glob[k] for k in ("depth", "limit", "bailed", "ret_bool")) & ~0xFFF
uc.mem_map(glo, 0x2000, UC_PROT_ALL)

# 3) صفحات المساعدين (فجوة 16 بايت قبل العنوان) + ret احتياطي لو فات الـhook
hlo = (min(glob["h_mod"], glob["h_pow"]) - 16) & ~0xFFF
hhi = (max(glob["h_mod"], glob["h_pow"]) + 0x2000) & ~0xFFF
uc.mem_map(hlo, hhi - hlo, UC_PROT_ALL)
for h in (glob["h_mod"], glob["h_pow"]):
    uc.mem_write(h, struct.pack("<I", 0xD65F03C0))  # ret

# 4) عنوان التوقف السحري والمكدس
uc.mem_map(MAGIC, 0x1000, UC_PROT_ALL)
uc.mem_map(STACK_TOP - 0x100000, 0x100000, UC_PROT_ALL)

def rd_d(i):
    return struct.unpack("<d", struct.pack("<Q", uc.reg_read(globals()[f"UC_ARM64_REG_D{i}"])))[0]

def wr_d(i, v):
    bits = struct.unpack("<Q", struct.pack("<d", v))[0]  # unicorn: أصفار بتات int لا float
    uc.reg_write(globals()[f"UC_ARM64_REG_D{i}"], bits)

def h_mod(a, b):  # نفس دلالة jit_h_mod: fmod مع تطبيع الإشارة، والقسمة على صفر NaN
    if b == 0: return float("nan")
    r = math.fmod(a, b)
    if r != 0 and ((r < 0) != (b < 0)): r += b
    return r

resume = None
def hook_mod(uc_, address, size_, ud):
    global resume
    wr_d(0, h_mod(rd_d(0), rd_d(1)))
    resume = uc_.reg_read(UC_ARM64_REG_LR)  # blr ضبط x30 على العنوان التالي — نكمل منه
    uc_.emu_stop()

def hook_pow(uc_, address, size_, ud):
    global resume
    wr_d(0, math.pow(rd_d(0), rd_d(1)))
    resume = uc_.reg_read(UC_ARM64_REG_LR)
    uc_.emu_stop()

uc.hook_add(UC_HOOK_CODE, hook_mod, begin=glob["h_mod"], end=glob["h_mod"])
uc.hook_add(UC_HOOK_CODE, hook_pow, begin=glob["h_pow"], end=glob["h_pow"])

def run(name, args):
    entry, msz, arity, rt, off = funcs[name]
    for k in ("depth", "limit", "bailed", "ret_bool"):
        uc.mem_write(glob[k], struct.pack("<I", 0))
    lim = FJ_STACK_BUDGET // (336 + 64)  # fs الأقصى النظري — حد كومة C متحفظ
    lim2 = glob["frames_max"] - 4
    uc.mem_write(glob["limit"], struct.pack("<I", min(lim, lim2)))
    for i in range(8):
        wr_d(i, args[i] if i < len(args) else 0.0)
    global resume
    resume = None
    uc.reg_write(UC_ARM64_REG_SP, STACK_TOP)
    uc.reg_write(UC_ARM64_REG_LR, MAGIC)
    pc = entry
    try:
        for _ in range(10000):  # كل استئناف = استدعاء مساعد واحد
            uc.emu_start(pc, MAGIC, 20_000_000, 2_000_000)
            if resume is None: break
            pc = resume; resume = None
    except UcError as e:
        return None, None, None, None, f"استثناء محاكي: {e}"
    depth = struct.unpack("<I", uc.mem_read(glob["depth"], 4))[0]
    bailed = struct.unpack("<I", uc.mem_read(glob["bailed"], 4))[0]
    rbool = struct.unpack("<I", uc.mem_read(glob["ret_bool"], 4))[0]
    return rd_d(0), depth, bailed, rbool, None

ok = fails = 0
for name, nargs, a0, a1, exp, kind in tests:
    args = [a0, a1][:nargs]
    d0, depth, bailed, rbool, err = run(name, args)
    label = f"{name}({', '.join(repr(a) for a in args)})"
    if err:
        print(f"فشل {label}: {err}"); fails += 1; continue
    msgs = []
    if depth != 0: msgs.append(f"العمق {depth} != 0")
    if kind == 2:
        if bailed != 1: msgs.append(f"تخفي متوقع لكن bailed={bailed}")
    else:
        if bailed: msgs.append("خفّى بلا سبب")
        elif kind == 1:
            if rbool != 1: msgs.append(f"ret_bool={rbool}")
            want = 1.0 if exp != 0 else 0.0
            if d0 != want: msgs.append(f"d0={d0} != {want}")
        else:
            if rbool: msgs.append(f"ret_bool={rbool} لرجوع عددي")
            if d0 is None or abs(d0 - exp) > 1e-9: msgs.append(f"d0={d0!r} != {exp!r}")
    if msgs:
        print(f"فشل {label}: " + "; ".join(msgs)); fails += 1
    else:
        print(f"نجح {label}" + (f" = {d0!r}" if kind != 2 else " (تخفي)")); ok += 1

print(f"\n== {ok + fails} حالة، نجاح: {ok}، فشل: {fails} ==")
sys.exit(1 if fails else 0)
