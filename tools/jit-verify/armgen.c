/* armgen.c — يبني تسلسلات ARM64 تمثيلية عبر arm64.inc الحقيقية ويكتبها لملف
   لتفكيكها بـcapstone والتحقق من صحة كل تعليمة قبل الاعتماد عليها */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOLAN_JIT 2
#include "src/jit/x64.inc"
#include "src/jit/arm64.inc"

int main(void) {
  XB x; x.cap = 1 << 16; x.p = (uint8_t *)malloc((size_t)x.cap); x.n = 0;

  /* 1) البرولوج كما في jit_compile */
  jx_stp_pre64(&x, 19, 20, 160);
  jx_stp64(&x, 21, 22, 31, 64);
  jx_str64_raw(&x, 23, 31, 80);
  for (int p = 0; p < 4; p++) jx_stpd(&x, 8 + 2 * p, 9 + 2 * p, 31, 88 + 16 * p);
  jx_str64_raw(&x, 30, 31, 152); /* إنقاذ LR */
  jx_mov_rr(&x, R15, RAX);
  jx_mov_rm(&x, RBX, R15, 0);
  jx_mov_rm(&x, R12, R15, 8);
  jx_mov_rm(&x, R14, R15, 16);

  /* 2) حراسات: lea بإزاحة كبيرة + مقارنات + فروع */
  jx_lea(&x, RAX, RBX, -1, 0, 1234 * 16);
  jx_cmp_rr(&x, RAX, R12);
  jx_rel32_jcc(&x, CC_NE);
  jx_cmp32_mi(&x, RSI, 16, 5);
  jx_rel32_jcc(&x, CC_L);
  jx_cmp32_ri(&x, RAX, 0x80000000); /* فوري سلبي → ldi32 */
  jx_rel32_jcc(&x, CC_E);

  /* 3) مقارنة FP + setcc (خريطة PL/MI/GT/LE) */
  jx_movsd_rm(&x, 0, R12, 24);
  jx_movsd_rm(&x, 1, R12, 40);
  jx_ucomisd(&x, 1, 0);
  jx_setcc(&x, CC_AE, RAX);  /* بعد fcmp → cset pl */
  jx_setcc(&x, CC_B, RCX);   /* بعد fcmp → cset mi */
  jx_and8_rr(&x, RAX, RCX);
  jx_mov_mr(&x, R12, 64, RAX);
  jx_ucomisd(&x, 2, 0);
  jx_rel32_jcc(&x, CC_AE);   /* b.pl */
  jx_ucomisd(&x, 2, 1);
  jx_rel32_jcc(&x, CC_B);    /* b.mi */
  jx_ucomisd(&x, 2, 0);
  jx_rel32_jcc(&x, CC_A);    /* b.gt */
  jx_ucomisd(&x, 2, 1);
  jx_rel32_jcc(&x, CC_BE);   /* b.le */

  /* مقارنة صحيحة + setcc (خريطة EQ/PL العادية) */
  jx_cmp32_ri(&x, RAX, 7);
  jx_setcc(&x, CC_E, RAX);   /* بعد cmp → cset eq */
  jx_test32_rr(&x, RDX, RDX);
  jx_setcc(&x, CC_NS, RDX);  /* بعد tst → cset pl */
  jx_cmp_rr(&x, RAX, RCX);
  jx_rel32_jcc(&x, CC_G);    /* b.gt */

  /* 4) حسابات صحيحة (نمط jc_mod وjc_bitop) */
  jx_sdiv32(&x, RDX, RAX, RCX);
  jx_msub32(&x, RDX, RDX, RCX, RAX);
  jx_test32_rr(&x, RDX, RDX);
  jx_imul32_rri(&x, RCX, RCX, 1000);
  jx_imul64_rr(&x, RCX, RDX);
  jx_shl_ri(&x, RCX, 4);
  jx_shr64_ri(&x, RCX, 32);
  jx_and32_ri(&x, RCX, 63);
  jx_alu32_rr(&x, 0x89, RAX, RDX);
  jx_alu32_rr(&x, 0x31, RAX, RCX);
  jx_test32_rr(&x, RAX, RAX);
  jx_alu_rr(&x, 0x21, RAX, RCX);
  jx_alu_rr(&x, 0x09, RAX, RCX);
  jx_alu_rr(&x, 0x31, RAX, RCX);
  jx_alu_rr(&x, 0x01, RAX, RCX);
  jx_alu_rr(&x, 0x29, RAX, RCX);
  jx_alu32_rr(&x, 0x01, RDX, RCX);

  /* 5) FP حساب وتحويل (نمط jc_pow/jc_bitop) */
  jx_addsd(&x, 0, 1);
  jx_subsd(&x, 0, 1);
  jx_mulsd(&x, 0, 1);
  jx_divsd(&x, 0, 1);
  jx_movaps(&x, 2, 0);
  jx_xorpd(&x, 2, 2);
  jx_xorpd(&x, 3, 2);
  jx_roundsd(&x, 4, 4, 9);
  jx_roundsd(&x, 4, 4, 0x0A);
  jx_cvttsd2si64(&x, RAX, 0);
  jx_cvttsd2si32(&x, RCX, 0);
  jx_cvtsi2sd64(&x, 5, RAX);
  jx_cvtsi2sd32(&x, 5, RCX);
  jx_cvtsi2sd32_m(&x, 6, RAX, 100);
  jx_movq_xr(&x, 2, RAX);
  jx_mov_ri(&x, RAX, 0x8000000000000000ull);
  jx_movq_xr(&x, 2, RAX);
  jx_xorpd(&x, 3, 2);
  jx_mov_ri(&x, RAX, 0x3FF0000000000000ull);
  jx_movq_xr(&x, 2, RAX);
  jx_movaps(&x, 0, 3);
  jx_addsd(&x, 0, 2);

  /* 6) ذاكرة متنوعة (إزاحات حدّية) */
  jx_mov32_rm(&x, RAX, R12, 100000); /* ضخمة → x16 */
  jx_mov32_mr(&x, RBX, 50000, RAX);
  jx_mov32_mi(&x, R12, 8, 0x3FF00000);
  jx_mov8_mr(&x, R12, 0, RAX);
  jx_movzx8_rm(&x, RAX, R12, 1);
  jx_cmp8_mi(&x, R12, 0, 0);
  jx_cmp8_mi(&x, R12, 0, 5);
  jx_mov_rm(&x, RAX, R13, 12345);    /* غير مُحاذاة → ldur */
  jx_movsd_rm(&x, 7, R13, 99999);
  jx_movsd_mr(&x, R12, 204, 7);      /* غير مُقاسة → stur */
  jx_mov_mr(&x, R15, 24, RAX);
  jx_mov_rm(&x, RAX, R14, 8);

  /* 7) عنوان مفهرس عبر jx_rm (نمط jc_copyx) */
  jx_rm(&x, 0, 0, 0x8B, R8, RAX, RCX, 0, 0);
  jx_rm(&x, 0, 1, 0x8B, R9, RAX, RCX, 0, 8);

  /* 8) استدعاء مساعد (نمط jc_call_helper) */
  jx_movsd_mr(&x, RSP, 16, 3);
  jx_movsd_mr(&x, RSP, 24, 4);
  jx_ldi64(&x, 15, 0x00007FF600001234ull);
  jx_blr(&x, 15);
  jx_movsd_rm(&x, 3, RSP, 16);
  jx_movsd_rm(&x, 4, RSP, 24);

  /* 9) تحميل ثابت من البركة (نمط jc_rip) */
  jx_emit(&x, 0x5C000000u | 16); /* ldr d16, lit */
  jx_sse_rr(&x, 0xF2, 0x0F58, 5, 16);
  jx_emit(&x, 0x5C000000u | 3);  /* ldr d3, lit */
  jx_movsd_mr(&x, RBX, 72, 3);

  /* 10) ucomisd بذاكرة عبر jx_rm (نمط jc_op_rm SK_MEM) */
  jx_rm(&x, 0x66, 0, 0x0F2E, 1, R12, -1, 0, 40);
  jx_rm(&x, 0xF2, 0, 0x0F58, 0, R12, -1, 0, 48);

  /* 11) قفزة غير شرطية (نمط jc_jmp) */
  jx_rel32_jmp(&x);

  /* 12) الإبيلوج */
  for (int p = 3; p >= 0; p--) jx_ldpd(&x, 8 + 2 * p, 9 + 2 * p, 31, 88 + 16 * p);
  jx_ldp64(&x, 21, 22, 31, 64);
  jx_ldr64_raw(&x, 23, 31, 80);
  jx_ldp64(&x, 19, 20, 31, 0);
  jx_ldr64_raw(&x, 30, 31, 152); /* استعادة LR */
  jx_add_rsp(&x, 160);
  jx_ret(&x);

  /* 13) نوى fnjit-ARM (نمط fc_emit الجديد): برولوج stp x30/xzr + عداد العمق
     + تخزين الوسائط d0-d7 + استدعاء ذاتي/مساعد + إنزال العمق + إبيلوج */
  jx_stp_pre64(&x, 30, 31, 336);              /* stp x30, xzr, [sp, #-0x150]! */
  jx_mov_ri(&x, RAX, 0x00007FF600001C04ull);  /* &g_fj_depth */
  jx_mov32_rm(&x, RCX, RAX, 0);               /* ldr w1, [x0] */
  jx_add32_ri(&x, RCX, 1);                    /* add w1, w1, #1 */
  jx_mov32_mr(&x, RAX, 0, RCX);               /* str w1, [x0] */
  jx_mov_ri(&x, RDX, 0x00007FF600001C08ull);  /* &g_fj_limit */
  jx_cmp32_rm(&x, RCX, RDX, 0);               /* ldr w11, [x2]؛ cmp w1, w11 */
  jx_rel32_jcc(&x, CC_G);                     /* b.gt عنصر نائب (تجاوز الحد → تخفي) */
  for (int k = 0; k < 8; k++) jx_movsd_mr(&x, RSP, 16 + 8 * k, k); /* وسائط d0-d7 → خلايا */
  jx_emit(&x, 0x94000000u);                   /* bl عنصر نائب (fc_call ذاتي) */
  jx_ldi64(&x, 0, 0x00007FF600001234ull);     /* عنوان مساعد (jit_h_mod) */
  jx_call_rax(&x);                            /* blr x0 */
  jx_mov32_rm(&x, RCX, RAX, 0);               /* OP_RETURN: قراءة العمق */
  jx_sub32_ri(&x, RCX, 1);                    /* sub w1, w1, #1 */
  jx_mov32_mr(&x, RAX, 0, RCX);               /* كتابة العمق */
  jx_ldr64_raw(&x, 30, 31, 0);                /* ldr x30, [sp] — استعادة LR من [sp+0] */
  jx_add_rsp(&x, 336);                        /* add sp, sp, #0x150 */
  jx_ret(&x);

  FILE *f = fopen("armgen.bin", "wb");
  if (!f) { fprintf(stderr, "فشل فتح المخرج\n"); return 1; }
  fwrite(x.p, 1, (size_t)x.n, f);
  fclose(f);
  printf("bytes=%d words=%d\n", x.n, x.n / 4);
  free(x.p);
  return 0;
}
