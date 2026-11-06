/* =====================================================================
   arm64.inc — مُرمِّز AArch64 لواجهة jx_* نفسها (SOLAN_JIT == 2)
   المبدأ: jit.inc يولّد عبر نفس أسماء الدوال، وهذا الملف يترجمها إلى ARM64.
   كل الترميزات أدناه متحقق منها بتفكيك capstone (جولتا armcheck).

   خريطة السجلات (فهرس jit.inc → سجل ARM64):
     RAX→x0 RCX→x1 RDX→x2 RSI→x3 RDI→x4 R8→x5 R9→x6 R10→x9 R11→x10
     RBX→x19 (سلوتات) R12→x20 (قاعدة المكدس) R13→x21 (globals.e)
     R14→x22 (clo) R15→x23 (cx) — الخمس المخصصة محفوظة-للمستدعى ✓
     RSP→sp (أساس ذاكرة فقط، 31)  RBP→x24 (غير مستعمل)
   الفاصلة العائمة: الفهرس 0-15 → d0-d15 مباشرة (d8-d15 محفوظة-للمستدعى
     أصلاً على ARM ✓)، d16 = مخطوطة تحميل الثوابت/المقارنة بالذاكرة.
   الوسائط SysV: x0-x7 — RAX/RCX/RDX/RSI تغطي أول أربع وسائط ✓
   x9/x10 مخطوطات عابرة داخلية؛ x15 مخطوطة تحميل عناوين المساعدين
   (لا x16/x17 — محجوزة لقشور التفريع على ماك).
   ===================================================================== */
#if SOLAN_JIT == 2

#define JX_ARM 1
#define JX_WIN 0

/* علم سياق المقارنة: 1 = آخر مقارنة كانت فاصلة عائمة (fcmp).
   يضبطه مُصدر كل مقارنة ويقرؤه مُصدرا القفز الشرطي وsetcc —
   لأن CC_A/CC_AE/CC_B/CC_BE تترجمان differently: بعد cmp صحيحة
   HI/HS/LO/LS، وبعد fcmp GT/PL/MI/LE (unordered يُصتاد بشكل مطابق لـx86). */
static int jx_fpcc = 0;

/* تعريفات مسبقة: jx_rm يستعملهما قبل تعريفهما */
static void jx_ucomisd_mem(XB *x, int xr, int base, int32_t d);
static void jx_sse_rr(XB *x, int pfx, int op, int dst, int src);

/* شرط x86 → شرط ARM64 بعد مقارنة صحيحة (cmp/tst: أعلام ALU كاملة) */
static int jx_armcc(int cc) {
  switch (cc) {
    case CC_O: return 6;   /* VS (غير مستعمل) */
    case CC_NO: return 7;  /* VC */
    case CC_B: return 3;   /* LO  */
    case CC_AE: return 2;  /* HS  */
    case CC_E: return 0;   /* EQ  */
    case CC_NE: return 1;  /* NE  */
    case CC_BE: return 9;  /* LS  */
    case CC_A: return 8;   /* HI  */
    case CC_S: return 4;   /* MI  */
    case CC_NS: return 5;  /* PL  */
    case CC_P: return 6;   /* VS  */
    case CC_NP: return 7;  /* VC  */
    case CC_L: return 11;  /* LT  */
    case CC_GE: return 10; /* GE  */
    case CC_LE: return 13; /* LE  */
    case CC_G: return 12;  /* GT  */
    default: return 6;
  }
}
/* شرط x86 → شرط ARM64 بعد fcmp. أعلام fcmp على AArch64 (مختلفة عن x86 — متحقق منها
   بتنفيذ فعلي على Unicorn):
     أكبر: N=0 Z=0 C=1 V=0 | أصغر: N=1 Z=0 C=0 V=0
     يساوي: N=0 Z=1 C=1 V=0 | unordered: N=0 Z=0 C=0 V=1
   المطابقة لدلالة x86 ucomisd:
     CC_AE (CF=0: أكبر أو يساوي، بلا unordered) → GE (N==V) — PL كان سيمرّر unordered خطأً
     CC_B  (CF=1: أصغر أو unordered)            → LT (N!=V) — MI كان سيفوّت unordered
     CC_A  (CF=0 && ZF=0: أكبر فقط)             → GT (Z=0 && N==V)
     CC_BE (CF=1 || ZF=1)                       → LE (Z=1 || N!=V)
   تنبيه: CC_E/CC_NE بعد fcmp تخالف x86 في unordered (Z=0 لا Z=1) — كل مواضعها في
   jit.inc محروسة بفحص CC_P (VS) قبله أو بنمط setcc(E)&setcc(NP) فتسلك المسار الصحيح. */
static int jx_armcc_fp(int cc) {
  switch (cc) {
    case CC_A: return 12;  /* GT */
    case CC_AE: return 10; /* GE */
    case CC_B: return 11;  /* LT */
    case CC_BE: return 13; /* LE */
    default: return jx_armcc(cc);
  }
}

static void jx_emit(XB *x, uint32_t insn) {
  xb_b(x, (int)(insn & 0xFF)); xb_b(x, (int)((insn >> 8) & 0xFF));
  xb_b(x, (int)((insn >> 16) & 0xFF)); xb_b(x, (int)((insn >> 24) & 0xFF));
}

/* فهرس GP → رقم سجل ARM64 (31 = sp كأساس ذاكرة فقط) */
static int jx_g(int r) {
  static const int m[16] = {0,1,2,19,31,24,3,4,5,6,9,10,20,21,22,23};
  return m[r & 15];
}

/* تحميل فوري 32-بت في w16: movz دائماً ثم movk للجزء العلوي (hw=1 → lsl #16) */
static void jx_ldi32(XB *x, uint32_t u) {
  jx_emit(x, 0x52800000u | ((u & 0xFFFFu) << 5) | 16u);
  if (u >> 16) jx_emit(x, 0x72800000u | (1u << 21) | ((u >> 16) << 5) | 16u);
}
/* تحميل فوري 64-بت في سجل ARM مباشر: movz بأول جزء غير صفري ثم movk للبقية */
static void jx_ldi64(XB *x, int rd, uint64_t imm) {
  if (imm == 0) { jx_emit(x, 0xD2800000u | (uint32_t)rd); return; }
  int lo = -1;
  for (int h = 0; h < 4; h++) if (((imm >> (16 * h)) & 0xFFFFu) && lo < 0) lo = h;
  jx_emit(x, 0xD2800000u | ((uint32_t)lo << 21) | (((uint32_t)(imm >> (16 * lo)) & 0xFFFFu) << 5) | (uint32_t)rd);
  for (int h = lo + 1; h < 4; h++) if ((imm >> (16 * h)) & 0xFFFFu)
    jx_emit(x, 0xF2800000u | ((uint32_t)h << 21) | (((uint32_t)(imm >> (16 * h)) & 0xFFFFu) << 5) | (uint32_t)rd);
}
static void jx_mov_ri(XB *x, int r, uint64_t imm) { jx_ldi64(x, jx_g(r), imm); }

/* حساب عنوان [أساس + إزاحة] قابل للترميز مباشرة؛ إن لم يمكن حُسب في x16.
   sz: حجم الوصول 8/4/1. يرجع سجل الأساس ويضع الإزاحة القابلة للترميز في *out */
static int jx_addr(XB *x, int baseidx, int32_t off, int sz, int32_t *out) {
  int b = jx_g(baseidx);
  int maxu = sz == 8 ? 32760 : sz == 4 ? 16380 : 4095;
  if (off >= 0 && off <= maxu && (sz == 1 || off % sz == 0)) { *out = off; return b; }
  if (off >= -256 && off <= 255) { *out = off; return b; } /* النمط غير المُقاس */
  if (off < 0 && off >= -4095) { /* sub x16, base, #-off */
    jx_emit(x, 0xD1000000u | ((uint32_t)(-off) << 10) | ((uint32_t)b << 5) | 16u);
    *out = 0; return 16;
  }
  jx_ldi64(x, 16, (uint64_t)(int64_t)off);
  jx_emit(x, 0x8B000000u | (16u << 16) | ((uint32_t)b << 5) | 16u); /* add x16, b, x16 */
  *out = 0; return 16;
}

static void jx_mov_rr(XB *x, int dst, int src) { jx_emit(x, 0xAA0003E0u | ((uint32_t)jx_g(src) << 16) | (uint32_t)jx_g(dst)); }

/* ذاكرة 64-بت */
static void jx_mov_rm(XB *x, int r, int base, int32_t d) {
  int32_t o; int b = jx_addr(x, base, d, 8, &o);
  if (o >= 0 && o % 8 == 0) jx_emit(x, 0xF9400000u | ((uint32_t)o / 8 << 10) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
  else jx_emit(x, 0xF8400000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
}
static void jx_mov_mr(XB *x, int base, int32_t d, int r) {
  int32_t o; int b = jx_addr(x, base, d, 8, &o);
  if (o >= 0 && o % 8 == 0) jx_emit(x, 0xF9000000u | ((uint32_t)o / 8 << 10) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
  else jx_emit(x, 0xF8000000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
}
/* ذاكرة 32-بت */
static void jx_mov32_rm(XB *x, int r, int base, int32_t d) {
  int32_t o; int b = jx_addr(x, base, d, 4, &o);
  if (o >= 0 && o % 4 == 0) jx_emit(x, 0xB9400000u | ((uint32_t)o / 4 << 10) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
  else jx_emit(x, 0xB8400000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
}
static void jx_mov32_mr(XB *x, int base, int32_t d, int r) {
  int32_t o; int b = jx_addr(x, base, d, 4, &o);
  if (o >= 0 && o % 4 == 0) jx_emit(x, 0xB9000000u | ((uint32_t)o / 4 << 10) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
  else jx_emit(x, 0xB8000000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
}
/* فوري 32-بت إلى ذاكرة: w16 = imm ثم str w16 */
static void jx_mov32_mi(XB *x, int base, int32_t d, uint32_t imm) {
  jx_ldi32(x, imm);
  int32_t o; int b = jx_addr(x, base, d, 4, &o);
  if (o >= 0 && o % 4 == 0) jx_emit(x, 0xB9000000u | ((uint32_t)o / 4 << 10) | ((uint32_t)b << 5) | 16u);
  else jx_emit(x, 0xB8000000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | 16u);
}
/* ذاكرة 8-بت */
static void jx_mov8_mr(XB *x, int base, int32_t d, int r) {
  int32_t o; int b = jx_addr(x, base, d, 1, &o);
  if (o >= 0 && o <= 4095) jx_emit(x, 0x39000000u | ((uint32_t)o << 10) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
  else jx_emit(x, 0x38000000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
}
static void jx_movzx8_rm(XB *x, int r, int base, int32_t d) {
  int32_t o; int b = jx_addr(x, base, d, 1, &o);
  if (o >= 0 && o <= 4095) jx_emit(x, 0x39400000u | ((uint32_t)o << 10) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
  else jx_emit(x, 0x38400000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | (uint32_t)jx_g(r));
}
/* القيم المنطقية 0/1 أصلاً في كامل السجل (CSET Wd يصفر العلوي) — النسخ تكفي */
static void jx_movzx8_rr(XB *x, int r, int rm) { if (r != rm) jx_mov_rr(x, r, rm); }

/* lea: rd = base + idx*2^sc + d (idx = -1 إن غاب) — x10 مخطوطة عند الحاجة */
static void jx_lea(XB *x, int r, int base, int idx, int sc, int32_t d) {
  int rd = jx_g(r);
  if (idx >= 0) {
    jx_emit(x, 0x8B000000u | ((uint32_t)jx_g(idx) << 16) | ((uint32_t)(sc & 7) << 10) | ((uint32_t)jx_g(base) << 5) | 10u); /* add x10, base, idx, lsl sc */
    if (d) {
      if (d >= 0 && d <= 4095) jx_emit(x, 0x91000000u | ((uint32_t)d << 10) | (10u << 5) | 10u);
      else if (d < 0 && d >= -4095) jx_emit(x, 0xD1000000u | ((uint32_t)(-d) << 10) | (10u << 5) | 10u);
      else { jx_ldi64(x, 16, (uint64_t)(int64_t)d); jx_emit(x, 0x8B000000u | (16u << 16) | (10u << 5) | 10u); } /* add x10, x10, x16 */
    }
    jx_mov_rr(x, r, 11); /* mov rd, x10 */
    return;
  }
  if (d >= 0 && d <= 4095) { jx_emit(x, 0x91000000u | ((uint32_t)d << 10) | ((uint32_t)jx_g(base) << 5) | (uint32_t)rd); return; }
  if (d < 0 && d >= -4095) { jx_emit(x, 0xD1000000u | ((uint32_t)(-d) << 10) | ((uint32_t)jx_g(base) << 5) | (uint32_t)rd); return; }
  jx_ldi64(x, 10, (uint64_t)(int64_t)d);
  jx_emit(x, 0x8B000000u | (10u << 16) | ((uint32_t)jx_g(base) << 5) | (uint32_t)rd); /* add rd, base, x10 */
}

/* مقارنات صحيحة (تضبط jx_fpcc = 0) */
static void jx_cmp_rr(XB *x, int a, int b) { jx_emit(x, 0xEB000000u | ((uint32_t)jx_g(b) << 16) | ((uint32_t)jx_g(a) << 5) | 31u); jx_fpcc = 0; }
static void jx_cmp32_ri(XB *x, int r, int32_t imm) {
  jx_fpcc = 0;
  if (imm >= 0 && imm <= 4095) { jx_emit(x, 0x71000000u | ((uint32_t)imm << 10) | ((uint32_t)jx_g(r) << 5) | 31u); return; }
  jx_ldi32(x, (uint32_t)imm);
  jx_emit(x, 0x6B000000u | (16u << 16) | ((uint32_t)jx_g(r) << 5) | 31u);
}
static void jx_cmp_rm(XB *x, int r, int base, int32_t d) {
  jx_mov_rm(x, 11, base, d);
  jx_emit(x, 0xEB000000u | ((uint32_t)jx_g(11) << 16) | ((uint32_t)jx_g(r) << 5) | 31u);
  jx_fpcc = 0;
}
static void jx_cmp32_rm(XB *x, int r, int base, int32_t d) {
  jx_mov32_rm(x, 11, base, d);
  jx_emit(x, 0x6B000000u | ((uint32_t)jx_g(11) << 16) | ((uint32_t)jx_g(r) << 5) | 31u);
  jx_fpcc = 0;
}
static void jx_cmp32_mi(XB *x, int base, int32_t d, int32_t imm) { jx_mov32_rm(x, 11, base, d); jx_cmp32_ri(x, 11, imm); }
static void jx_cmp8_mi(XB *x, int base, int32_t d, int imm) {
  jx_movzx8_rm(x, 11, base, d);
  jx_fpcc = 0;
  if (imm == 0) jx_emit(x, 0x71000000u | ((uint32_t)jx_g(11) << 5) | 31u);
  else { jx_ldi32(x, (uint32_t)imm); jx_emit(x, 0x6B000000u | (16u << 16) | ((uint32_t)jx_g(11) << 5) | 31u); }
}
static void jx_test8_rr(XB *x, int a, int b) { jx_emit(x, 0x6A000000u | ((uint32_t)jx_g(b) << 16) | ((uint32_t)jx_g(a) << 5) | 31u); jx_fpcc = 0; }
static void jx_test32_rr(XB *x, int a, int b) { jx_emit(x, 0x6A000000u | ((uint32_t)jx_g(b) << 16) | ((uint32_t)jx_g(a) << 5) | 31u); jx_fpcc = 0; }

/* setcc: CSET Wd, cond = CSINC Wd,WZR,WZR,cond^1 (كتابة W تُصفّر علوي السجل) */
static void jx_setcc(XB *x, int cc, int r) {
  int c = (jx_fpcc ? jx_armcc_fp(cc) : jx_armcc(cc)) ^ 1;
  jx_emit(x, 0x1A800400u | (31u << 16) | ((uint32_t)c << 12) | (31u << 5) | (uint32_t)jx_g(r));
}
static void jx_and8_rr(XB *x, int dst, int src) { jx_emit(x, 0x0A000000u | ((uint32_t)jx_g(src) << 16) | ((uint32_t)jx_g(dst) << 5) | (uint32_t)jx_g(dst)); }
static void jx_xor8_ri(XB *x, int r, int imm) { jx_ldi32(x, (uint32_t)imm); jx_emit(x, 0x4A000000u | (16u << 16) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r)); }

/* عمليات أعداد صحيحة (op = ترميز x86): dst = dst op src */
static void jx_alu_rr(XB *x, int op, int dst, int src) {
  if (op == 0x3B) { jx_cmp_rr(x, dst, src); return; }
  uint32_t base = op == 0x21 ? 0x8A000000u : op == 0x09 ? 0xAA000000u : op == 0x31 ? 0xCA000000u
                : op == 0x01 ? 0x8B000000u : 0xCB000000u; /* 0x29 sub */
  jx_emit(x, base | ((uint32_t)jx_g(src) << 16) | ((uint32_t)jx_g(dst) << 5) | (uint32_t)jx_g(dst));
}
static void jx_alu32_rr(XB *x, int op, int dst, int src) {
  if (op == 0x89) { jx_emit(x, 0x2A0003E0u | ((uint32_t)jx_g(src) << 16) | (uint32_t)jx_g(dst)); return; } /* mov wd, ws */
  if (op == 0x3B) { jx_emit(x, 0x6B000000u | ((uint32_t)jx_g(src) << 16) | ((uint32_t)jx_g(dst) << 5) | 31u); jx_fpcc = 0; return; }
  uint32_t base = op == 0x21 ? 0x0A000000u : op == 0x09 ? 0x2A000000u : op == 0x31 ? 0x4A000000u
                : op == 0x01 ? 0x0B000000u : 0x4B000000u; /* 0x29 sub */
  jx_emit(x, base | ((uint32_t)jx_g(src) << 16) | ((uint32_t)jx_g(dst) << 5) | (uint32_t)jx_g(dst));
}
static void jx_shl_cl(XB *x, int r) { jx_emit(x, 0x9AC02000u | ((uint32_t)jx_g(1) << 16) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r)); } /* lslv Xd,Xd,x1 */
static void jx_sar_cl(XB *x, int r) { jx_emit(x, 0x9AC02800u | ((uint32_t)jx_g(1) << 16) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r)); } /* asrv */
static void jx_shl_ri(XB *x, int r, int imm) { /* lsl #imm = ubfm Xd,Xn,#(64-i),#(63-i) */
  if (imm <= 0) { jx_mov_rr(x, r, r); return; }
  jx_emit(x, 0xD3400000u | ((uint32_t)((64 - imm) & 63) << 16) | ((uint32_t)(63 - imm) << 10) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r));
}
static void jx_shr64_ri(XB *x, int r, int imm) { jx_emit(x, 0xD3400000u | ((uint32_t)imm << 16) | (63u << 10) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r)); } /* lsr */
static void jx_add32_ri(XB *x, int r, int32_t imm) {
  if (imm >= 0 && imm <= 4095) { jx_emit(x, 0x11000000u | ((uint32_t)imm << 10) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r)); return; }
  jx_ldi32(x, (uint32_t)imm);
  jx_emit(x, 0x0B000000u | (16u << 16) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r));
}
static void jx_sub32_ri(XB *x, int r, int32_t imm) { /* sub wr, wr, #imm (لعداد العمق في fnjit) */
  if (imm >= 0 && imm <= 4095) { jx_emit(x, 0x51000000u | ((uint32_t)imm << 10) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r)); return; }
  jx_ldi32(x, (uint32_t)imm);
  jx_emit(x, 0x4B000000u | (16u << 16) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r));
}
static void jx_and32_ri(XB *x, int r, int32_t imm) {
  jx_ldi32(x, (uint32_t)imm);
  jx_emit(x, 0x0A000000u | (16u << 16) | ((uint32_t)jx_g(r) << 5) | (uint32_t)jx_g(r));
}
static void jx_imul64_rr(XB *x, int dst, int src) { jx_emit(x, 0x9B000000u | ((uint32_t)jx_g(src) << 16) | (31u << 10) | ((uint32_t)jx_g(dst) << 5) | (uint32_t)jx_g(dst)); } /* mul */
static void jx_imul32_rri(XB *x, int dst, int src, int32_t imm) {
  jx_ldi32(x, (uint32_t)imm);
  jx_emit(x, 0x1B000000u | (16u << 16) | (31u << 10) | ((uint32_t)jx_g(src) << 5) | (uint32_t)jx_g(dst)); /* madd wd,ws,w16,wzr */
}
/* قسمة وباقي 32-بت مقتطع (نسخة ARM لcdq+idiv في jc_mod) */
static void jx_sdiv32(XB *x, int d, int n, int m) { jx_emit(x, 0x1AC00C00u | ((uint32_t)jx_g(m) << 16) | ((uint32_t)jx_g(n) << 5) | (uint32_t)jx_g(d)); }
/* msub: d = a - n*m (ترتيب حقول العتاد Rd,Rn,Rm,Ra) */
static void jx_msub32(XB *x, int d, int n, int m, int a) { jx_emit(x, 0x1B008000u | ((uint32_t)jx_g(m) << 16) | ((uint32_t)jx_g(a) << 10) | ((uint32_t)jx_g(n) << 5) | (uint32_t)jx_g(d)); }

/* المكدس والاستدعاء */
static void jx_sub_rsp(XB *x, int32_t v) { jx_emit(x, 0xD1000000u | ((uint32_t)v << 10) | (31u << 5) | 31u); }
static void jx_add_rsp(XB *x, int32_t v) { jx_emit(x, 0x91000000u | ((uint32_t)v << 10) | (31u << 5) | 31u); }
static void jx_push(XB *x, int r) { jx_emit(x, 0xF81F0FE0u | (uint32_t)jx_g(r)); } /* str x,[sp,#-16]! */
static void jx_pop(XB *x, int r) { jx_emit(x, 0xF84107E0u | (uint32_t)jx_g(r)); }  /* ldr x,[sp],#16 */
static void jx_ret(XB *x) { jx_emit(x, 0xD65F03C0u); }
static void jx_call_rax(XB *x) { jx_emit(x, 0xD63F0000u); } /* blr x0 */
static void jx_blr(XB *x, int armr) { jx_emit(x, 0xD63F0000u | ((uint32_t)armr << 5)); }
static void jx_cdq(XB *x) { (void)x; }
static void jx_idiv32(XB *x, int r) { (void)x; (void)r; }

/* أدوات خام للبرولوج/الإبيلوج (أرقام سجلات ARM مباشرة) */
static void jx_stp_pre64(XB *x, int a, int b, int32_t frame) { /* stp xa,xb,[sp,#-frame]! */
  jx_emit(x, 0xA9800000u | ((uint32_t)((-frame / 8) & 0x7F) << 15) | ((uint32_t)b << 10) | (31u << 5) | (uint32_t)a);
}
static void jx_stp64(XB *x, int a, int b, int rn, int32_t off) { jx_emit(x, 0xA9000000u | ((uint32_t)(off / 8) << 15) | ((uint32_t)b << 10) | ((uint32_t)rn << 5) | (uint32_t)a); }
static void jx_ldp64(XB *x, int a, int b, int rn, int32_t off) { jx_emit(x, 0xA9400000u | ((uint32_t)(off / 8) << 15) | ((uint32_t)b << 10) | ((uint32_t)rn << 5) | (uint32_t)a); }
static void jx_stpd(XB *x, int a, int b, int rn, int32_t off) { jx_emit(x, 0x6D000000u | ((uint32_t)(off / 8) << 15) | ((uint32_t)b << 10) | ((uint32_t)rn << 5) | (uint32_t)a); }
static void jx_ldpd(XB *x, int a, int b, int rn, int32_t off) { jx_emit(x, 0x6D400000u | ((uint32_t)(off / 8) << 15) | ((uint32_t)b << 10) | ((uint32_t)rn << 5) | (uint32_t)a); }
static void jx_str64_raw(XB *x, int rt, int rn, int32_t off) { jx_emit(x, 0xF9000000u | ((uint32_t)(off / 8) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt); }
static void jx_ldr64_raw(XB *x, int rt, int rn, int32_t off) { jx_emit(x, 0xF9400000u | ((uint32_t)(off / 8) << 10) | ((uint32_t)rn << 5) | (uint32_t)rt); }

/* الفاصلة العائمة: الفهرس = d0-d15 مباشرة */
static void jx_movsd_rm(XB *x, int xr, int base, int32_t d) {
  int32_t o; int b = jx_addr(x, base, d, 8, &o);
  if (o >= 0 && o % 8 == 0) jx_emit(x, 0xFD400000u | ((uint32_t)o / 8 << 10) | ((uint32_t)b << 5) | (uint32_t)xr);
  else jx_emit(x, 0xFC400000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | (uint32_t)xr); /* ldur */
}
static void jx_movsd_mr(XB *x, int base, int32_t d, int xr) {
  int32_t o; int b = jx_addr(x, base, d, 8, &o);
  if (o >= 0 && o % 8 == 0) jx_emit(x, 0xFD000000u | ((uint32_t)o / 8 << 10) | ((uint32_t)b << 5) | (uint32_t)xr);
  else jx_emit(x, 0xFC000000u | (((uint32_t)o & 0x1FF) << 12) | ((uint32_t)b << 5) | (uint32_t)xr); /* stur */
}
static void jx_movaps(XB *x, int dst, int src) { if (dst != src) jx_emit(x, 0x1E604000u | ((uint32_t)src << 5) | (uint32_t)dst); } /* fmov */
static void jx_movups_rm(XB *x, int xr, int base, int idx, int32_t d) {
  if (idx >= 0) { jx_lea(x, 11, base, idx, 0, d); jx_movsd_rm(x, xr, 11, 0); return; }
  jx_movsd_rm(x, xr, base, d);
}
static void jx_movups_mr(XB *x, int base, int idx, int32_t d, int xr) {
  if (idx >= 0) { jx_lea(x, 11, base, idx, 0, d); jx_movsd_mr(x, 11, 0, xr); return; }
  jx_movsd_mr(x, base, d, xr);
}
static void jx_movq_xr(XB *x, int xr, int r) { jx_emit(x, 0x9E670000u | ((uint32_t)jx_g(r) << 5) | (uint32_t)xr); } /* fmov Dd, Xn */

/* عمليات FP سجل-سجل: dst = dst op src (وبعد fcmp يُضبط علم السياق) */
static void jx_sse_rr(XB *x, int pfx, int op, int dst, int src) {
  (void)pfx;
  uint32_t base;
  switch (op) {
    case 0x0F58: base = 0x1E602800u; break; /* fadd */
    case 0x0F5C: base = 0x1E603800u; break; /* fsub */
    case 0x0F59: base = 0x1E600800u; break; /* fmul */
    case 0x0F5E: base = 0x1E601800u; break; /* fdiv */
    case 0x0F2E: case 0x0F2F: base = 0x1E602000u; jx_fpcc = 1; break; /* fcmp */
    default: jx_movaps(x, dst, src); return;
  }
  jx_emit(x, base | ((uint32_t)src << 16) | ((uint32_t)dst << 5) | (uint32_t)dst);
}
static void jx_addsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F58, d, s); }
static void jx_subsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F5C, d, s); }
static void jx_mulsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F59, d, s); }
static void jx_divsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F5E, d, s); }
static void jx_ucomisd(XB *x, int a, int b) { jx_emit(x, 0x1E602000u | ((uint32_t)b << 16) | ((uint32_t)a << 5)); jx_fpcc = 1; }
static void jx_xorpd(XB *x, int d, int s) { jx_emit(x, 0x6E201C00u | ((uint32_t)s << 16) | ((uint32_t)d << 5) | (uint32_t)d); } /* eor v.16b (d==s → صفير) */
static void jx_cvttsd2si64(XB *x, int r, int xr) { jx_emit(x, 0x9E780000u | ((uint32_t)xr << 5) | (uint32_t)jx_g(r)); } /* fcvtzs Xd,Dn */
static void jx_cvttsd2si32(XB *x, int r, int xr) { jx_emit(x, 0x1E780000u | ((uint32_t)xr << 5) | (uint32_t)jx_g(r)); } /* fcvtzs Wd,Dn */
static void jx_cvtsi2sd64(XB *x, int xr, int r) { jx_emit(x, 0x9E620000u | ((uint32_t)jx_g(r) << 5) | (uint32_t)xr); } /* scvtf Dd,Xn */
static void jx_cvtsi2sd32(XB *x, int xr, int r) { jx_emit(x, 0x1E620000u | ((uint32_t)jx_g(r) << 5) | (uint32_t)xr); } /* scvtf Dd,Wn */
static void jx_cvtsi2sd32_m(XB *x, int xr, int base, int32_t d) {
  jx_mov32_rm(x, 11, base, d);
  jx_emit(x, 0x1E620000u | (10u << 5) | (uint32_t)xr); /* scvtf Dd,W10 */
}
/* roundsd: 9 = تقريب لأسفل (FRINTM) — 0xA = لسقف (FRINTP) — 0xB = اقتطاع (FRINTZ) — غيره أقرب (FRINTN) */
static void jx_roundsd(XB *x, int d, int s, int mode) {
  uint32_t op = mode == 9 ? 0x1E654000u : mode == 0x0A ? 0x1E64C000u : mode == 0x0B ? 0x1E65C000u : 0x1E644000u;
  jx_emit(x, op | ((uint32_t)s << 5) | (uint32_t)d);
}
static void jx_sqrtsd(XB *x, int d, int s) { jx_emit(x, 0x1E61C000u | ((uint32_t)s << 5) | (uint32_t)d); }

/* مقارنة FP بذاكرة: تحميل إلى d16 ثم fcmp (fcmp لا يقبل ذاكرة مباشرة) */
static void jx_ucomisd_mem(XB *x, int xr, int base, int32_t d) {
  jx_movsd_rm(x, 16, base, d);
  jx_emit(x, 0x1E602000u | (16u << 16) | ((uint32_t)xr << 5));
  jx_fpcc = 1;
}

/* jx_rm: المُصدِّر العام x86 (pfx/w/op/reg/base/idx/sc/disp) → ARM64 */
static void jx_rm(XB *x, int pfx, int w, int op, int reg, int base, int idx, int sc, int32_t disp) {
  if (pfx == 0xF2 || pfx == 0x66) { /* عمليات FP بذاكرة */
    switch (op) {
      case 0x0F10: jx_movsd_rm(x, reg, base, disp); return;      /* movsd r, m */
      case 0x0F11: jx_movsd_mr(x, base, disp, reg); return;      /* movsd m, r */
      case 0x0F2E: case 0x0F2F: jx_ucomisd_mem(x, reg, base, disp); return;
      case 0x0F58: case 0x0F5C: case 0x0F59: case 0x0F5E:
        jx_movsd_rm(x, 16, base, disp); jx_sse_rr(x, pfx, op, reg, 16); return;
      default: return; /* لا يُستعمل */
    }
  }
  if (idx >= 0) { /* عنوان مفهرس: x10 = base + idx*2^sc + disp */
    jx_lea(x, 11, base, idx, sc, disp);
    if (op == 0x8B) { if (w) jx_mov_rm(x, reg, 11, 0); else jx_mov32_rm(x, reg, 11, 0); }
    else { if (w) jx_mov_mr(x, 11, 0, reg); else jx_mov32_mr(x, 11, 0, reg); }
    return;
  }
  if (op == 0x8B) { if (w) jx_mov_rm(x, reg, base, disp); else jx_mov32_rm(x, reg, base, disp); }
  else if (op == 0x89) { if (w) jx_mov_mr(x, base, disp, reg); else jx_mov32_mr(x, base, disp, reg); }
}

/* قفزات: 4 بايت ثابتة تُرقَّع في نهاية التجميع (حلقة الترقيع في jit.inc) */
static void jx_rel32_jmp(XB *x) { jx_emit(x, 0x14000000u); }                                                  /* b placeholder */
static void jx_rel32_jcc(XB *x, int cc) { jx_emit(x, 0x54000000u | (uint32_t)(jx_fpcc ? jx_armcc_fp(cc) : jx_armcc(cc))); } /* b.cond placeholder */

#endif /* SOLAN_JIT == 2 */
