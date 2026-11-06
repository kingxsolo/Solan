/* fjgen.c — مولّد اختبار fnjit: يبني دوال سولان بالمترجم الحقيقي ويجمعها بـfj_compile
   الحقيقية ثم:
     - بناء x86 (SOLAN_JIT == 1): ينفّذ كل حالة بـfj_run ويتحقق من النتائج مباشرة
     - بناء ARM (SOLAN_JIT == 2): يُفرّغ بايتات الكود المولَّد وعناوين g_fj_* والمساعدين
       في fjarm.bin + fjarm.lst لتنفيذها على محاكي Unicorn (armfj.py) — لا تنفيذ محلي
   البناء: gcc fjgen.c sqlite3.o -o fjgen.exe -DSOLAN_SQLITE_EMBED -O2 -lws2_32 -lshell32 -ladvapi32 -lm */
#include "src/config.inc"
#include "src/jit/x64.inc"
#include "src/jit/arm64.inc"
#include "src/core/types.inc"
#include "src/core/memory.inc"
#include "src/core/gc.inc"
#include "src/core/tostr.inc"
#include "src/compiler/opcodes.inc"
#include "src/compiler/lexer.inc"
#include "src/compiler/highlight.inc"
#include "src/compiler/compiler.inc"
#include "src/compiler/disasm.inc"
#include "src/vm/vm_core.inc"
#include "src/jit/jit.inc"
#include "src/jit/fnjit.inc"
#include "src/vm/mids.inc"
#include "src/vm/vm_run.inc"
#include "src/vm/stdlib_embed.inc"
#include "src/vm/modules.inc"
#include "src/vm/methods.inc"
#include "src/lib/natives_core.inc"
#include "src/lib/json.inc"
#include "src/lib/pkgs.inc"
#include "src/lib/hash.inc"
#include "src/lib/tls_net.inc"
#include "src/lib/http_ws.inc"
#include "src/lib/timers.inc"
#include "src/lib/bigint_time.inc"
#include "src/lib/sqlite.inc"
#include "src/lib/zlib.inc"
#include "src/lib/ffi.inc"
#include "src/lib/regex.inc"
#include "src/runtime/init.inc"

static bool load_native_module(const char *file, Value *out) { (void)file; (void)out; return false; } /* بديل harness — capi.inc غير مضمن */

static FILE *mf, *bf;

/* kind: 0 = عدد، 1 = منطقي، 2 = تخفي متوقع (قسمة على صفر) */
typedef struct { const char *name; const char *src; int nargs; double a[2]; double exp; int kind; } TJ;
static const TJ fns[] = {
  {"ف", "دالة ف(أ, ب) { ارجع أ + ب * 2 }", 2, {3, 4}, 11, 0},
  {"ج", "دالة ج(أ, ب) { ارجع أ / ب }", 2, {7, 2}, 3.5, 0},
  {"ج", "دالة ج(أ, ب) { ارجع أ / ب }", 2, {1, 0}, 0, 2},
  {"ق", "دالة ق(أ, ب) { ارجع أ < ب }", 2, {3, 4}, 1, 1},
  {"ق", "دالة ق(أ, ب) { ارجع أ < ب }", 2, {4, 3}, 0, 1},
  {"باقي7", "دالة باقي7(أ) { ارجع أ % 7 }", 1, {19}, 5, 0},
  {"باقي7", "دالة باقي7(أ) { ارجع أ % 7 }", 1, {14}, 0, 0},
  {"ت", "دالة ت(ن) { اذا ن <= 0 { ارجع 0 } ارجع ن + ت(ن - 1) }", 1, {10}, 55, 0},
  {"ر", "دالة ر(أ) { خل ب = أ > 0; خل جـ = 5; ارجع ب }", 1, {1}, 1, 1},
  {"ر", "دالة ر(أ) { خل ب = أ > 0; خل جـ = 5; ارجع ب }", 1, {-1}, 0, 1},
  {"سالب", "دالة سالب(أ) { ارجع -أ }", 1, {5}, -5, 0},
  {"سالب", "دالة سالب(أ) { ارجع -أ }", 1, {-2.5}, 2.5, 0},
  {"يساوي", "دالة يساوي(أ) { ارجع أ == 3 }", 1, {3}, 1, 1},
  {"يساوي", "دالة يساوي(أ) { ارجع أ == 3 }", 1, {4}, 0, 1},
  {"فرع", "دالة فرع(أ) { اذا أ > 10 { ارجع 1 } والا { ارجع 2 } }", 1, {15}, 1, 0},
  {"فرع", "دالة فرع(أ) { اذا أ > 10 { ارجع 1 } والا { ارجع 2 } }", 1, {5}, 2, 0},
  {"عبارة", "دالة عبارة(أ) { أ * 2; ارجع أ + 1 }", 1, {7}, 8, 0},
};
#define NFNS (int)(sizeof(fns) / sizeof(fns[0]))

/* يجمع نص الدالة ويشغّله ليتعرّف في globals ثم يجمّعها بـfj_compile — يرجع الإغلاق أو NULL */
static Closure *build(const TJ *t) {
  Fun *fn = compile(t->src, "<fnjit-test>", false);
  if (!fn) { fprintf(stderr, "فشل تجميع %s\n", t->name); return NULL; }
  Map *g = new_map(); pin((Obj *)g);
  if (!run_fun(fn, g)) { fprintf(stderr, "فشل تشغيل تعريف %s\n", t->name); return NULL; }
  Value v;
  if (!dget(&g->d, OBJV(normstr(t->name)), &v) || !IS_T(v, O_CLO)) { fprintf(stderr, "لم يُعرَّف %s في globals\n", t->name); return NULL; }
  Closure *clo = AS_CLO(v);
  if (clo->f->fjst == 0 && !fj_compile(clo)) { fprintf(stderr, "رفض fnjit لـ%s\n", t->name); return NULL; }
  if (clo->f->fjst != 1) { fprintf(stderr, "fjst=%d لـ%s\n", clo->f->fjst, t->name); return NULL; }
  return clo;
}

int main(void) {
  g_jit_trace = 1;
  vm_init();
#if SOLAN_JIT == 2
  mf = fopen("fjarm.lst", "w"); bf = fopen("fjarm.bin", "wb");
  if (!mf || !bf) { fprintf(stderr, "فشل فتح المخرجات\n"); return 1; }
  fprintf(mf, "GLOB %llu %llu %llu %llu %llu %llu %d\n",
      (unsigned long long)(uintptr_t)&g_fj_depth, (unsigned long long)(uintptr_t)&g_fj_limit,
      (unsigned long long)(uintptr_t)&g_fj_bailed, (unsigned long long)(uintptr_t)&g_fj_ret_bool,
      (unsigned long long)(uintptr_t)(void *)jit_h_mod, (unsigned long long)(uintptr_t)(void *)jit_h_pow,
      vm.frames_max);
  long off = 0;
  /* كل نص يُبنى مرة واحدة — الحالات المتعددة تشير لنفس الإغلاق */
  for (int i = 0; i < NFNS; i++) {
    int j;
    for (j = 0; j < i; j++) if (fns[j].name == fns[i].name || strcmp(fns[j].name, fns[i].name) == 0) break;
    if (j < i) continue; /* مُجمع سابقاً */
    Closure *clo = build(&fns[i]);
    if (!clo) { fprintf(mf, "REJECT %s\n", fns[i].name); continue; }
    FjInfo *fj = clo->f->fj;
    fwrite(fj->mem, 1, fj->msz, bf);
    fprintf(mf, "FUNC %s %llu %d %d %d %ld\n", fns[i].name,
        (unsigned long long)(uintptr_t)fj->entry, (int)fj->msz, clo->f->arity, fj->rt, off);
    off += (long)fj->msz;
  }
  for (int i = 0; i < NFNS; i++)
    fprintf(mf, "TEST %s %d %.17g %.17g %.17g %d\n", fns[i].name, fns[i].nargs, fns[i].a[0], fns[i].a[1], fns[i].exp, fns[i].kind);
  fclose(mf); fclose(bf);
  printf("ARM dump جاهز: %d حالة\n", NFNS);
#else
  /* بناء x86: تحقق تنفيذي مباشر لكل حالة عبر fj_run — يصطاد انحدارات إعادة الهيكلة والإصلاحات */
  int ok = 0, bad = 0;
  for (int i = 0; i < NFNS; i++) {
    int j;
    for (j = 0; j < i; j++) if (strcmp(fns[j].name, fns[i].name) == 0) break;
    if (j < i) continue;
    Closure *clo = build(&fns[i]);
    if (!clo) { bad++; continue; }
    for (int k = i; k < NFNS; k++) {
      if (strcmp(fns[k].name, fns[i].name) != 0) continue;
      Value args[2] = { NUMV(fns[k].a[0]), NUMV(fns[k].a[1]) };
      Value out;
      bool ran = fj_run(clo, args, fns[k].nargs, &out);
      bool pass = false;
      if (fns[k].kind == 2) pass = !ran; /* التخفي المتوقع: fj_run يرجع false */
      else if (ran) {
        if (fns[k].kind == 1) pass = out.t == V_BOOL && (out.as.b != 0) == (fns[k].exp != 0);
        else pass = IS_NUM(out) && out.as.n == fns[k].exp;
      }
      if (pass) { ok++; printf("نجح %s(%.17g%s%.17g) = %.17g\n", fns[k].name, fns[k].a[0], fns[k].nargs > 1 ? ", " : "", fns[k].nargs > 1 ? fns[k].a[1] : 0.0, fns[k].kind == 2 ? -1.0 : fns[k].exp); }
      else { bad++; printf("فشل %s حالة %d (ran=%d t=%d b=%d g_fj_ret_bool=%d)\n", fns[k].name, k, ran, out.t, out.t == V_BOOL ? (int)out.as.b : -1, g_fj_ret_bool); }
    }
  }
  printf("\nالنتيجة: %d نجاح، %d فشل\n", ok, bad);
  return bad ? 1 : 0;
#endif
  return 0;
}
