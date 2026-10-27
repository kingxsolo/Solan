typedef enum { V_NIL, V_BOOL, V_NUM, V_OBJ, V_EMPTY } VT;
typedef enum { O_STR, O_FUN, O_CLO, O_UPV, O_NAT, O_CLS, O_INST, O_BOUND, O_LIST, O_MAP, O_CORO } OT;

typedef struct Obj Obj;

typedef struct { VT t; union { bool b; double n; Obj *o; } as; } Value;
struct Obj { OT t; bool mark; Obj *next; };

#define NIL ((Value){V_NIL, {.b = false}})
#define EMPTYV ((Value){V_EMPTY, {.b = false}})
#define BOOLV(x) ((Value){V_BOOL, {.b = (x)}})
#define NUMV(x) ((Value){V_NUM, {.n = (x)}})
#define OBJV(x) ((Value){V_OBJ, {.o = (Obj *)(x)}})
#define IS_NIL(v) ((v).t == V_NIL)
#define IS_NUM(v) ((v).t == V_NUM)
#define IS_OBJ(v) ((v).t == V_OBJ)
#define IS_T(v, T) (IS_OBJ(v) && (v).as.o->t == (T))
#define AS_STR(v) ((Str *)(v).as.o)
#define AS_FUN(v) ((Fun *)(v).as.o)
#define AS_CLO(v) ((Closure *)(v).as.o)
#define AS_NAT(v) ((Native *)(v).as.o)
#define AS_CLS(v) ((Class *)(v).as.o)
#define AS_INST(v) ((Instance *)(v).as.o)
#define AS_BOUND(v) ((Bound *)(v).as.o)
#define AS_LIST(v) ((List *)(v).as.o)
#define AS_MAP(v) ((Map *)(v).as.o)
#define AS_CORO(v) ((Coro *)(v).as.o)

typedef struct SBuf { int refs; int cap; int used; char data[]; } SBuf;
typedef struct Str { Obj o; int len; uint32_t hash; struct Str *hnext; char *chars; SBuf *sb; } Str;

typedef struct { Value k, v; } Ent;
typedef struct { Ent *e; int n, cap, live; int *idx; int icap; } Dict;
typedef struct { Value *v; int n, cap; } VArr;

typedef struct { uint8_t *code; int *lines; int n, cap; VArr consts; } Chunk;
typedef struct Map { Obj o; Dict d; bool module; } Map;
typedef struct { Obj o; VArr a; } List;

typedef struct Fun { Obj o; int arity, minar, upc, kind; bool variadic; bool getter; Chunk ch; Str *name; int *gc; int ndbg, capdbg; Str **dbgname; int *dbgslot; Str *file; uint16_t *lc; struct JitRegion *jreg; struct FjInfo *fj; uint8_t fjst; uint16_t fjcnt; } Fun;

static void fj_free(struct Fun *f);
static void fj_mark(struct Fun *f);
typedef struct Upv { Obj o; Value *loc; Value closed; struct Upv *next; } Upv;

typedef struct { Obj o; Fun *f; Upv **upv; int upc; Map *globals; } Closure;

typedef bool (*NativeFn)(int argc, Value *a, Value *res);
typedef struct { Obj o; NativeFn fn; Str *name; int minar, maxar; void *ud; } Native;
static Native *g_cur_native;
typedef struct Class { Obj o; Str *name; Dict methods, statics; struct Class *super; } Class;
typedef struct { Obj o; Class *cls; Dict fields; bool fnf;   } Instance;
#define CALLABLE_OBJ(v) (IS_OBJ(v) && ((v).as.o->t == O_CLO || (v).as.o->t == O_NAT || (v).as.o->t == O_BOUND || (v).as.o->t == O_CLS || (v).as.o->t == O_FUN))
typedef struct { Obj o; Value recv, method; } Bound;

typedef struct { Closure *clo; uint8_t *ip; Value *slots; } Frame;

typedef struct { int fi; uint8_t *ip; Value *sp; } Handler;

typedef struct Coro { Obj o; Value *stack, *stack_end, *sp; Frame *frames; int fcount, frames_max; Handler *handlers; int hcount, hmax; Upv *open; int state; Closure *fn; List *args; } Coro;
typedef struct { Value *stack, *stack_end, *sp; Frame *frames; int fcount, frames_max; Handler *handlers; int hcount, hmax; Upv *open; Coro *cur; } Ctx;
typedef struct CtxNode { Ctx c; struct CtxNode *next; } CtxNode;

typedef struct { char *p; int n, cap; } Buf;
static Value mkstr(const char *s, int n);

typedef struct {
  Obj *objs; size_t nobj, next_gc; bool gc_pending; size_t sbytes;
  Obj **gray; int gn, gcap;
  Str **sb; int sbcap, sbcount;
  Value *stack, *stack_end, *sp;
  Frame *frames; int fcount, frames_max;
  Handler *handlers; int hcount, hmax;
  Upv *open;
  struct Coro *cur_co; struct CtxNode *ctxs; Value yielded; bool nowrap, pending_throw; struct Class *promise_cls; Value fn_block, fn_start; int dbg_mode; bool dbg_step; int bps[64], nbps; const char *dbg_src; FILE *dbg_in;
  Dict builtins, natmods, modcache, kw, mid, ctxw, stdlibd;
  List *pins;
  Value exc; char *trace; int nest; char tr_file[512]; int tr_line;
  Str *init_str, *tostr_str, *s_len, *s_this, *s_super, *s_from, *s_case, *s_default, *s_static, *s_prop, *s_enum;
  char *script_dir;
} VM;
static VM vm;

static bool g_safe = false;

enum { PM_READ = 1, PM_WRITE = 2, PM_NET = 4, PM_EXEC = 8, PM_ENV = 16, PM_NATIVE = 32, PM_ALL = 63 };
static int g_perm = PM_ALL;
static char *g_pdirs[2][16]; static int g_npdirs[2];
static char *g_phosts[32]; static int g_nphosts;
static bool perm_has(int bit) { return (g_perm & bit) != 0; }
static bool path_in_scope(const char *base, const char *p) {
  size_t n = strlen(base); if (strncmp(p, base, n) != 0) return false;
  return p[n] == 0 || p[n] == '/' || (n > 0 && base[n - 1] == '/');
}
static bool path_allowed(int bit, const char *path) {
  if (!(g_perm & bit)) return false;
  int k = bit == PM_READ ? 0 : 1; if (g_npdirs[k] == 0) return true;
  char rp[PATH_MAX]; const char *use = NULL;
  if (realpath(path, rp)) use = rp;
  else {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", path); char *sl = strrchr(tmp, '/');
    if (sl) { *sl = 0; if (!realpath(tmp[0] ? tmp : "/", rp)) return false; size_t L = strlen(rp); snprintf(rp + L, sizeof rp - L, "/%s", sl + 1); use = rp; }
    else { if (!getcwd(rp, sizeof rp)) return false; size_t L = strlen(rp); snprintf(rp + L, sizeof rp - L, "/%s", path); use = rp; }
  }
  for (int i = 0; i < g_npdirs[k]; i++) if (path_in_scope(g_pdirs[k][i], use)) return true;
  return false;
}
static bool host_allowed(const char *host) {
  if (!(g_perm & PM_NET)) return false;
  if (g_nphosts == 0) return true;
  size_t hl = strlen(host);
  for (int i = 0; i < g_nphosts; i++) {
    size_t n = strlen(g_phosts[i]);
    if (hl == n && !strcasecmp(host, g_phosts[i])) return true;
    if (hl > n && host[hl - n - 1] == '.' && !strcasecmp(host + hl - n, g_phosts[i])) return true;
  }
  return false;
}
struct JitRegion;
static void jit_free_regions(Fun *f);

static inline double sol_pow(double a, double b) { return b == 2.0 ? a * a : pow(a, b); }

static struct { Dict *d; int n, live; Str *name; } g_mfn_memo;
