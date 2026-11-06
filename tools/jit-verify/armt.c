/* armt.c — مولّد نوى اختبار دلالية: يبني دوال ARM64 صغيرة عبر arm64.inc الحقيقية
   ويكتب armt.bin + armt.lst (الإزاحات والتوقعات) لتنفيذها على محاكي Unicorn
   والتحقق من النتائج — تفحص الدلالات لا التفكيك فقط */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define SOLAN_JIT 2
#include "src/jit/x64.inc"
#include "src/jit/arm64.inc"

static XB x;
static FILE *mf;
static int koff;

static uint64_t dbits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }

static void kstart(const char *name) {
  while (x.n & 7) xb_u32(&x, 0);
  koff = x.n;
  fprintf(mf, "K %s %d ", name, koff);
}
static void kend(int chk_x, int chk_d, uint64_t ex, uint64_t ed) {
  fprintf(mf, "%d %d %d %016" PRIx64 " %016" PRIx64 "\n", x.n - koff, chk_x, chk_d, ex, ed);
}
/* تخزين النتيجة: x0 → [x9]، d0 → [x9+8] (x9 = R10 يضبطه المنفّذ) */
static void st_x(void) { jx_mov_mr(&x, R10, 0, RAX); }
static void st_d(void) { jx_movsd_mr(&x, R10, 8, 0); }

/* ترقيع b.cond / ldr literal بنفس رياضيات jit.inc */
static void patch19(int fpos, int target) {
  int ipos = fpos - 4;
  int32_t rel = target - ipos;
  uint32_t w; memcpy(&w, x.p + ipos, 4);
  w = (w & ~0x00FFFFE0u) | (((uint32_t)(rel / 4) & 0x7FFFFu) << 5);
  memcpy(x.p + ipos, &w, 4);
}

int main(void) {
  x.cap = 1 << 16; x.p = (uint8_t *)malloc((size_t)x.cap); x.n = 0;
  mf = fopen("armt.lst", "w");
  if (!mf) { fprintf(stderr, "فشل فتح armt.lst\n"); return 1; }

  /* ===== فوريات 64-بت ===== */
  kstart("ldi64_min"); jx_mov_ri(&x, RAX, 0x8000000000000000ull); st_x(); jx_ret(&x);
  kend(1,0, 0x8000000000000000ull, 0);
  kstart("ldi64_mix"); jx_mov_ri(&x, RAX, 0x123456789ABCDEF0ull); st_x(); jx_ret(&x);
  kend(1,0, 0x123456789ABCDEF0ull, 0);
  kstart("ldi64_zero"); jx_mov_ri(&x, RAX, 0); st_x(); jx_ret(&x);
  kend(1,0, 0, 0);

  /* ===== حساب صحيح ===== */
  kstart("add64"); jx_mov_ri(&x, RAX, 100); jx_mov_ri(&x, RCX, 7);
  jx_alu_rr(&x, 0x01, RAX, RCX); st_x(); jx_ret(&x); kend(1,0, 107, 0);
  kstart("sub64"); jx_mov_ri(&x, RAX, 100); jx_mov_ri(&x, RCX, 7);
  jx_alu_rr(&x, 0x29, RAX, RCX); st_x(); jx_ret(&x); kend(1,0, 93, 0);
  kstart("mul64"); jx_mov_ri(&x, RAX, 100000); jx_mov_ri(&x, RCX, 3);
  jx_imul64_rr(&x, RAX, RCX); st_x(); jx_ret(&x); kend(1,0, 300000, 0);
  /* نمط jc_mod بالضبط: قسمة مقتطعة سالبة */
  kstart("sdivmod_n"); jx_mov_ri(&x, RAX, (uint64_t)-17); jx_mov_ri(&x, RCX, 5);
  jx_sdiv32(&x, RDX, RAX, RCX); jx_msub32(&x, RDX, RDX, RCX, RAX);
  jx_mov_rr(&x, RAX, RDX); st_x(); jx_ret(&x); kend(1,0, 0xFFFFFFFEull, 0);
  kstart("sdivmod_p"); jx_mov_ri(&x, RAX, 17); jx_mov_ri(&x, RCX, 5);
  jx_sdiv32(&x, RDX, RAX, RCX); jx_msub32(&x, RDX, RDX, RCX, RAX);
  jx_mov_rr(&x, RAX, RDX); st_x(); jx_ret(&x); kend(1,0, 2, 0);
  kstart("imul32"); jx_mov_ri(&x, RCX, 7); jx_imul32_rri(&x, RCX, RCX, 1000);
  jx_mov_rr(&x, RAX, RCX); st_x(); jx_ret(&x); kend(1,0, 7000, 0);
  kstart("shl_ri"); jx_mov_ri(&x, RAX, 1); jx_shl_ri(&x, RAX, 4); st_x(); jx_ret(&x);
  kend(1,0, 16, 0);
  kstart("shr64"); jx_mov_ri(&x, RAX, 0x8000000000000000ull); jx_shr64_ri(&x, RAX, 63);
  st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("shl_cl"); jx_mov_ri(&x, RAX, 3); jx_mov_ri(&x, RCX, 4);
  jx_shl_cl(&x, RAX); st_x(); jx_ret(&x); kend(1,0, 48, 0);
  kstart("sar_cl"); jx_mov_ri(&x, RAX, (uint64_t)-32); jx_mov_ri(&x, RCX, 2);
  jx_sar_cl(&x, RAX); st_x(); jx_ret(&x); kend(1,0, 0xFFFFFFFFFFFFFFF8ull, 0);
  kstart("and32"); jx_mov_ri(&x, RAX, 0xFF); jx_and32_ri(&x, RAX, 0x0F);
  st_x(); jx_ret(&x); kend(1,0, 15, 0);

  /* ===== شروط صحيحة ===== */
  kstart("setcc_eq"); jx_mov_ri(&x, RAX, 5); jx_cmp32_ri(&x, RAX, 5);
  jx_setcc(&x, CC_E, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("setcc_lt"); jx_mov_ri(&x, RAX, 3); jx_cmp32_ri(&x, RAX, 5);
  jx_setcc(&x, CC_L, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("setcc_gt"); jx_mov_ri(&x, RAX, 5); jx_mov_ri(&x, RCX, 3);
  jx_cmp_rr(&x, RAX, RCX); jx_setcc(&x, CC_G, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("cmp32_big"); jx_mov_ri(&x, RAX, 0x7FFFFFFF); jx_cmp32_ri(&x, RAX, (int32_t)0x80000000);
  jx_setcc(&x, CC_B, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("tst_pl"); jx_mov_ri(&x, RDX, 0xF0); jx_test32_rr(&x, RDX, RDX);
  jx_setcc(&x, CC_NS, RDX); jx_mov_rr(&x, RAX, RDX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("cmp_rm"); jx_mov_ri(&x, RAX, 100); jx_cmp_rm(&x, RAX, RBX, 64);
  jx_setcc(&x, CC_E, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);

  /* ===== فاصلة عائمة حسابية (DATA: 0:3.5 1:2.25 2:-1.5 6:9.0 7:-2.75 8:1.5) ===== */
  kstart("fadd"); jx_movsd_rm(&x, 0, R12, 0); jx_movsd_rm(&x, 1, R12, 8);
  jx_addsd(&x, 0, 1); st_d(); jx_ret(&x); kend(0,1, 0, dbits(5.75));
  kstart("fsub"); jx_movsd_rm(&x, 0, R12, 0); jx_movsd_rm(&x, 1, R12, 8);
  jx_subsd(&x, 0, 1); st_d(); jx_ret(&x); kend(0,1, 0, dbits(1.25));
  kstart("fmul"); jx_movsd_rm(&x, 0, R12, 0); jx_movsd_rm(&x, 1, R12, 8);
  jx_mulsd(&x, 0, 1); st_d(); jx_ret(&x); kend(0,1, 0, dbits(7.875));
  kstart("fdiv"); jx_movsd_rm(&x, 0, R12, 0); jx_movsd_rm(&x, 1, R12, 8);
  jx_divsd(&x, 0, 1); st_d(); jx_ret(&x); kend(0,1, 0, dbits(3.5 / 2.25));
  kstart("fsqrt"); jx_movsd_rm(&x, 0, R12, 48); jx_sqrtsd(&x, 0, 0);
  st_d(); jx_ret(&x); kend(0,1, 0, dbits(3.0));
  kstart("frintm"); jx_movsd_rm(&x, 0, R12, 64); jx_roundsd(&x, 0, 0, 9);
  st_d(); jx_ret(&x); kend(0,1, 0, dbits(1.0));
  kstart("frintp"); jx_movsd_rm(&x, 0, R12, 64); jx_roundsd(&x, 0, 0, 0x0A);
  st_d(); jx_ret(&x); kend(0,1, 0, dbits(2.0));
  kstart("frintz"); jx_movsd_rm(&x, 0, R12, 56); jx_roundsd(&x, 0, 0, 0x0B);
  st_d(); jx_ret(&x); kend(0,1, 0, dbits(-2.0));
  kstart("frintn"); jx_movsd_rm(&x, 0, R12, 64); jx_roundsd(&x, 0, 0, 0);
  st_d(); jx_ret(&x); kend(0,1, 0, dbits(2.0)); /* 1.5 → الأقرب الزوجي */

  /* ===== تحويلات ===== */
  kstart("fcvtzs64"); jx_movsd_rm(&x, 0, R12, 16); jx_cvttsd2si64(&x, RAX, 0);
  st_x(); jx_ret(&x); kend(1,0, 0xFFFFFFFFFFFFFFFFull, 0); /* -1.5 → -1 */
  kstart("fcvtzs32"); jx_movsd_rm(&x, 0, R12, 16); jx_cvttsd2si32(&x, RAX, 0);
  st_x(); jx_ret(&x); kend(1,0, 0xFFFFFFFFull, 0);
  kstart("scvtf64"); jx_mov_ri(&x, RAX, (uint64_t)-5); jx_cvtsi2sd64(&x, 0, RAX);
  st_d(); jx_ret(&x); kend(0,1, 0, dbits(-5.0));
  kstart("scvtf32"); jx_mov_ri(&x, RCX, 7); jx_cvtsi2sd32(&x, 0, RCX);
  st_d(); jx_ret(&x); kend(0,1, 0, dbits(7.0));
  kstart("movq"); jx_mov_ri(&x, RAX, 0x3FF0000000000000ull); jx_movq_xr(&x, 2, RAX);
  jx_movaps(&x, 0, 2); st_d(); jx_ret(&x); kend(0,1, 0, dbits(1.0));
  kstart("xorpd_zero"); jx_movsd_rm(&x, 0, R12, 16); jx_xorpd(&x, 0, 0);
  st_d(); jx_ret(&x); kend(0,1, 0, dbits(0.0));
  kstart("xorpd_flip"); jx_movsd_rm(&x, 0, R12, 64); jx_mov_ri(&x, RAX, 0x8000000000000000ull);
  jx_movq_xr(&x, 2, RAX); jx_xorpd(&x, 0, 2); st_d(); jx_ret(&x); kend(0,1, 0, dbits(-1.5));
  kstart("roundtrip"); jx_mov_ri(&x, RAX, 123456789); jx_cvtsi2sd64(&x, 0, RAX);
  jx_cvttsd2si64(&x, RAX, 0); st_x(); jx_ret(&x); kend(1,0, 123456789, 0);

  /* ===== خريطة شروط FP الحرجة (بعد fcmp) =====
     DATA: 9:3.0 10:5.0 11:2.0 3:NaN 5:1.0 */
  kstart("fcmp_lt"); jx_movsd_rm(&x, 0, R12, 72); jx_movsd_rm(&x, 1, R12, 80);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_B, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("fcmp_gt"); jx_movsd_rm(&x, 0, R12, 80); jx_movsd_rm(&x, 1, R12, 72);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_A, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("fcmp_eq"); jx_movsd_rm(&x, 0, R12, 88); jx_movsd_rm(&x, 1, R12, 88);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_E, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("fcmp_ae_pos"); jx_movsd_rm(&x, 0, R12, 80); jx_movsd_rm(&x, 1, R12, 72);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_AE, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("fcmp_ae_nan"); jx_movsd_rm(&x, 0, R12, 24); jx_movsd_rm(&x, 1, R12, 40);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_AE, RAX); st_x(); jx_ret(&x); kend(1,0, 0, 0);
  kstart("fcmp_b_nan"); jx_movsd_rm(&x, 0, R12, 24); jx_movsd_rm(&x, 1, R12, 40);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_B, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("fcmp_a_nan"); jx_movsd_rm(&x, 0, R12, 24); jx_movsd_rm(&x, 1, R12, 40);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_A, RAX); st_x(); jx_ret(&x); kend(1,0, 0, 0);
  kstart("fcmp_be_nan"); jx_movsd_rm(&x, 0, R12, 24); jx_movsd_rm(&x, 1, R12, 40);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_BE, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("fcmp_eq_nan"); jx_movsd_rm(&x, 0, R12, 24); jx_movsd_rm(&x, 1, R12, 40);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_E, RAX); st_x(); jx_ret(&x);
  /* الخريطة الخام: EQ يستبعد unordered (Z=0 على ARM لا Z=1 كـx86) — jit.inc يحرس كل CC_E بـCC_P قبله */
  kend(1,0, 0, 0);
  kstart("fcmp_ne_nan"); jx_movsd_rm(&x, 0, R12, 24); jx_movsd_rm(&x, 1, R12, 40);
  jx_ucomisd(&x, 0, 1); jx_setcc(&x, CC_NE, RAX); st_x(); jx_ret(&x);
  kend(1,0, 1, 0);

  /* ===== ذاكرة ===== */
  kstart("mem_big"); jx_mov_rm(&x, RAX, RBX, 100000); st_x(); jx_ret(&x);
  kend(1,0, 777, 0);
  kstart("mem_neg"); jx_mov_rm(&x, RAX, RBX, -1000); st_x(); jx_ret(&x);
  kend(1,0, 0xABCD, 0);
  kstart("mem_ldur"); jx_mov_rm(&x, RAX, RBX, 7); st_x(); jx_ret(&x);
  kend(1,0, 0x1122334455667788ull, 0);
  kstart("mem32"); jx_mov32_mi(&x, RBX, 72, 0xDEADBEEFu); jx_mov32_rm(&x, RAX, RBX, 72);
  st_x(); jx_ret(&x); kend(1,0, 0xDEADBEEFull, 0);
  kstart("mem8"); jx_mov_ri(&x, RAX, 0xAB); jx_mov8_mr(&x, RBX, 0, RAX);
  jx_movzx8_rm(&x, RAX, RBX, 0); st_x(); jx_ret(&x); kend(1,0, 0xAB, 0);
  kstart("mem_indexed"); jx_mov_rr(&x, RAX, RBX); jx_mov_ri(&x, RCX, 24);
  jx_rm(&x, 0, 1, 0x8B, R8, RAX, RCX, 0, 8); jx_mov_rr(&x, RAX, R8);
  st_x(); jx_ret(&x); kend(1,0, 0x5555, 0);
  kstart("mem_stur_d"); jx_movsd_rm(&x, 7, R12, 0); jx_movsd_mr(&x, R12, 204, 7);
  jx_movsd_rm(&x, 0, R12, 204); st_d(); jx_ret(&x); kend(0,1, 0, dbits(3.5));
  kstart("cmp8"); jx_cmp8_mi(&x, RBX, 1, 5); jx_setcc(&x, CC_E, RAX);
  st_x(); jx_ret(&x); kend(1,0, 1, 0);
  kstart("ucomisd_mem"); jx_movsd_rm(&x, 1, R12, 0);
  jx_rm(&x, 0x66, 0, 0x0F2E, 1, R12, -1, 0, 8);
  jx_setcc(&x, CC_A, RAX); st_x(); jx_ret(&x); kend(1,0, 1, 0);

  /* ===== حلقة بترقيع b.cond (نمط jc_fixup ARM) ===== */
  kstart("branch_loop");
  jx_mov_ri(&x, RAX, 0); jx_mov_ri(&x, RCX, 1);
  int lpos = x.n; /* ربط التسمية هنا */
  jx_alu32_rr(&x, 0x01, RAX, RCX);
  jx_add32_ri(&x, RCX, 1);
  jx_cmp32_ri(&x, RCX, 11);
  jx_rel32_jcc(&x, CC_L);
  patch19(x.n, lpos);
  st_x(); jx_ret(&x);
  kend(1,0, 55, 0); /* مجموع 1..10 */

  /* ===== تحميل ثابت من البركة بنمط jc_rip (LDR literal يُرقَّع) ===== */
  kstart("ldr_lit");
  jx_movsd_rm(&x, 5, R12, 8);          /* d5 = 2.25 */
  jx_emit(&x, 0x5C000000u | 16u);      /* ldr d16, lit — عنصر نائب */
  int fpos = x.n;
  jx_sse_rr(&x, 0xF2, 0x0F58, 5, 16);  /* fadd d5, d5, d16 */
  jx_movaps(&x, 0, 5);                 /* d0 = d5 (st_d يخزن d0) */
  st_d(); jx_ret(&x);
  while (x.n & 7) xb_u32(&x, 0);
  int pool = x.n;
  xb_u64(&x, 0x3FF8000000000000ull);   /* 1.5 */
  patch19(fpos, pool);
  kend(0,1, 0, dbits(3.75));

  /* ===== إطار كامل: برولوج + جسم + مساعد + إبيلوج (نمط jit_compile) ===== */
  kstart("frame");
  jx_stp_pre64(&x, 19, 20, 160);
  jx_stp64(&x, 21, 22, 31, 64);
  jx_str64_raw(&x, 23, 31, 80);
  for (int p = 0; p < 4; p++) jx_stpd(&x, 8 + 2 * p, 9 + 2 * p, 31, 88 + 16 * p);
  jx_str64_raw(&x, 30, 31, 152);
  jx_mov_rr(&x, R15, RAX);       /* x23 = ctx */
  jx_mov_rm(&x, RBX, R15, 0);    /* x19 = slots */
  jx_mov_rm(&x, R12, R15, 8);    /* x20 = base (يشير إلى DATA) */
  jx_mov_rm(&x, R14, R15, 16);   /* x22 = clo */
  jx_movsd_rm(&x, 3, R12, 0);    /* d3 = 3.5 */
  for (int r = 3; r <= 7; r++) jx_movsd_mr(&x, RSP, 16 + (r - 3) * 8, r); /* نمط jc_save_regs */
  int ldi_off = x.n;
  jx_ldi64(&x, 15, 0x00010002ull); /* عنصر نائب بكلمتين — يرقعه المنفّذ بعنوان المساعد */
  jx_blr(&x, 15);
  for (int r = 3; r <= 7; r++) jx_movsd_rm(&x, r, RSP, 16 + (r - 3) * 8); /* نمط jc_restore_regs */
  st_x();                               /* x0 = 77 من المساعد */
  jx_movaps(&x, 0, 3);                  /* d0 = d3 المستعادة (st_d يخزن d0) */
  st_d();  /* d3 = 3.5 بعد الاستعادة (المساعد أسحقها) */
  for (int p = 3; p >= 0; p--) jx_ldpd(&x, 8 + 2 * p, 9 + 2 * p, 31, 88 + 16 * p);
  jx_ldp64(&x, 21, 22, 31, 64);
  jx_ldr64_raw(&x, 23, 31, 80);
  jx_ldp64(&x, 19, 20, 31, 0);
  jx_ldr64_raw(&x, 30, 31, 152);
  jx_add_rsp(&x, 160);
  jx_ret(&x);
  kend(1,1, 77, dbits(3.5));
  /* المساعد: يسحق d3 ويعيد 77 — فجوة صريحة قبله حتى لا يطابق عنوانه عنوان توقف النواة (until) */
  xb_u32(&x, 0); xb_u32(&x, 0); xb_u32(&x, 0); xb_u32(&x, 0);
  while (x.n & 7) xb_u32(&x, 0);
  int hoff = x.n;
  jx_ldi64(&x, 0, 0x3FF0000000000000ull);
  jx_movq_xr(&x, 3, 0); /* fmov d3, x0 — إسحاق مقصود */
  jx_ldi64(&x, 0, 77);
  jx_ret(&x);
  fprintf(mf, "P %d %d\n", ldi_off, hoff);

  fclose(mf);
  FILE *f = fopen("armt.bin", "wb");
  if (!f) { fprintf(stderr, "فشل فتح armt.bin\n"); return 1; }
  fwrite(x.p, 1, (size_t)x.n, f);
  fclose(f);
  printf("bytes=%d\n", x.n);
  free(x.p);
  return 0;
}
