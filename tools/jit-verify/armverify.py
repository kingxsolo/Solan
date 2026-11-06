# -*- coding: utf-8 -*-
# فاحص armgen.bin: تفكيك كامل + تأكيدات على الترميزات الحرجة وخرائط الشروط
import capstone, sys
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_LITTLE_ENDIAN)
data = open(r"C:\Users\KING\AppData\Local\Temp\opencode\armgen.bin", "rb").read()
assert len(data) % 4 == 0, f"الطول ليس مضاعف 4: {len(data)}"
lines, fails = [], 0
for i in range(0, len(data), 4):
    w = data[i:i + 4]
    try:
        ins = next(md.disasm(w, i))
        lines.append(f"{i:04x}: {ins.bytes.hex()}  {ins.mnemonic} {ins.op_str}")
    except StopIteration:
        fails += 1
        lines.append(f"{i:04x}: {w.hex()}  ** فشل التفكيك **")
text = "\n".join(lines)
print(text)
print(f"\n== {len(data)//4} تعليمة، فشل تفكيك: {fails} ==")

checks = [
    ("stp x19, x20, [sp, #-0xa0]!", "برولوج: pre-index"),
    ("stp x21, x22, [sp, #0x40]", "برولوج: x21/x22"),
    ("str x23, [sp, #0x50]", "برولوج: x23"),
    ("stp d8, d9, [sp, #0x58]", "برولوج: d8/d9"),
    ("stp d14, d15, [sp, #0x88]", "برولوج: d14/d15"),
    ("str x30, [sp, #0x98]", "برولوج: إنقاذ LR (x30)"),
    ("mov x23, x0", "R15 = cx"),
    ("ldr x19, [x23]", "RBX من slots"),
    ("cset w0, ge", "CC_AE بعد fcmp → GE (unordered يُستبعد)"),
    ("cset w1, lt", "CC_B بعد fcmp → LT (unordered يُصتاد)"),
    ("b.ge", "jcc CC_AE بعد fcmp"),
    ("b.gt", "jcc CC_A/CC_G"),
    ("b.le", "jcc CC_BE بعد fcmp"),
    ("b.ne", "jcc CC_NE بعد cmp"),
    ("b.lt", "jcc CC_L بعد cmp"),
    ("b.eq", "jcc CC_E بعد cmp"),
    ("cset w0, eq", "CC_E بعد cmp → EQ"),
    ("cset w2, pl", "CC_NS بعد tst → PL"),
    ("sdiv w2, w0, w1", "jc_mod: قسمة"),
    ("msub w2, w2, w1, w0", "jc_mod: باقي"),
    ("mul w1, w1, w16", "imul32_rri (MADD مع WZR = mul)"),
    ("mul x1, x1, x2", "imul64_rr"),
    ("blr x15", "استدعاء مساعد"),
    ("fmov d2, x0", "movq_xr"),
    ("fcmp d1, d16", "ucomisd_mem"),
    ("ldr d16, #", "تحميل ثابت من البركة"),
    ("ldr d3, #", "تحميل ثابت من البركة"),
    ("ret", "الإبيلوج"),
    ("ldp d14, d15, [sp, #0x88]", "إبيلوج: استعادة"),
    ("ldp x19, x20, [sp]", "إبيلوج: x19/x20"),
    ("ldr x30, [sp, #0x98]", "إبيلوج: استعادة LR"),
    ("add sp, sp, #0xa0", "إبيلوج: إغلاق الإطار"),
    ("eor v2.16b, v2.16b, v2.16b", "xorpd صفير"),
    ("eor v3.16b, v3.16b, v2.16b", "xorpd إشارة"),
    ("frintm d4, d4", "roundsd 9 = أرضية"),
    ("frintp d4, d4", "roundsd 0xA = سقف"),
    ("fcvtzs x0, d0", "cvttsd2si64"),
    ("fcvtzs w1, d0", "cvttsd2si32"),
    ("scvtf d5, x0", "cvtsi2sd64"),
    ("scvtf d5, w1", "cvtsi2sd32"),
    ("movk x15, #0x7ff6, lsl #32", "ldi64 عنوان المساعد"),
    ("mov x0, #-0x8000000000000000", "mov_ri 0x8000... (movz واحدة)"),
    ("ldr w0, [x16]", "إزاحة ضخمة عبر x16"),
    ("ldr x0, [x16]", "إزاحة كبيرة غير مُحاذاة عبر x16"),
    ("ldr d7, [x16]", "إزاحة ضخمة FP عبر x16"),
    ("stur d7, [x20, #0xcc]", "إزاحة 204 غير مُقاسة → stur"),
    # نوى fnjit-ARM (القسم 13)
    ("stp x30, xzr, [sp, #-0x150]!", "fnjit: برولوج حفظ LR والخصم في تعليمة"),
    ("ldr w1, [x0]", "fnjit: قراءة g_fj_depth"),
    ("add w1, w1, #1", "fnjit: زيادة العمق"),
    ("str w1, [x0]", "fnjit: كتابة العمق"),
    ("ldr w10, [x2]", "fnjit: قراءة g_fj_limit (الفهرس 11 → x10)"),
    ("cmp w1, w10", "fnjit: مقارنة العمق بالحد"),
    ("str d0, [sp, #0x10]", "fnjit: تخزين الوسيط الأول في خلية الإطار"),
    ("str d7, [sp, #0x48]", "fnjit: الوسيط الثامن (d7)"),
    ("bl #", "fnjit: استدعاء ذاتي (عنصر نائب bl)"),
    ("blr x0", "fnjit: استدعاء مساعد عبر blr x0"),
    ("sub w1, w1, #1", "fnjit: إنزال العمق عند الرجوع"),
    ("ldr x30, [sp]", "fnjit: استعادة LR من [sp+0]"),
    ("add sp, sp, #0x150", "fnjit: إغلاق إطار 336 بايت"),
]
missing = []
for pat, why in checks:
    if pat not in text:
        missing.append(f"  مفقود: {pat!r}  ({why})")
if missing:
    print("== تأكيدات مفقودة ==")
    print("\n".join(missing))
    sys.exit(1)
print(f"== كل التأكيدات الـ{len(checks)} موجودة ✓ ==")
