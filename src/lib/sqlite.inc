typedef struct {
  void *h;
  int (*sqlite3_open)(const char *, void **); int (*sqlite3_close)(void *);
  int (*sqlite3_prepare_v2)(void *, const char *, int, void **, const char **); int (*sqlite3_step)(void *); int (*sqlite3_finalize)(void *);
  int (*sqlite3_bind_text)(void *, int, const char *, int, void *); int (*sqlite3_bind_double)(void *, int, double); int (*sqlite3_bind_null)(void *, int);
  int (*sqlite3_bind_int64)(void *, int, long long); int (*sqlite3_column_count)(void *); const char *(*sqlite3_column_name)(void *, int);
  int (*sqlite3_column_type)(void *, int); const unsigned char *(*sqlite3_column_text)(void *, int); double (*sqlite3_column_double)(void *, int);
  int (*sqlite3_column_bytes)(void *, int); const void *(*sqlite3_column_blob)(void *, int);
  const char *(*sqlite3_errmsg)(void *); int (*sqlite3_changes)(void *); long long (*sqlite3_last_insert_rowid)(void *);
} SQL;
static SQL sq; static int sq_state = 0; static void *dbs[64];
#ifdef SOLAN_SQLITE_EMBED

extern int sqlite3_open(const char *, void **); extern int sqlite3_close(void *);
extern int sqlite3_prepare_v2(void *, const char *, int, void **, const char **); extern int sqlite3_step(void *); extern int sqlite3_finalize(void *);
extern int sqlite3_bind_text(void *, int, const char *, int, void *); extern int sqlite3_bind_double(void *, int, double); extern int sqlite3_bind_null(void *, int);
extern int sqlite3_bind_int64(void *, int, long long); extern int sqlite3_column_count(void *); extern const char *sqlite3_column_name(void *, int);
extern int sqlite3_column_type(void *, int); extern const unsigned char *sqlite3_column_text(void *, int); extern double sqlite3_column_double(void *, int);
extern int sqlite3_column_bytes(void *, int); extern const void *sqlite3_column_blob(void *, int);
extern const char *sqlite3_errmsg(void *); extern int sqlite3_changes(void *); extern long long sqlite3_last_insert_rowid(void *);
static bool sql_load(void) {
  if (sq_state) return sq_state > 0;
#define SL(n) sq.n = n
  SL(sqlite3_open); SL(sqlite3_close); SL(sqlite3_prepare_v2); SL(sqlite3_step); SL(sqlite3_finalize); SL(sqlite3_bind_text); SL(sqlite3_bind_double);
  SL(sqlite3_bind_null); SL(sqlite3_bind_int64); SL(sqlite3_column_count); SL(sqlite3_column_name); SL(sqlite3_column_type); SL(sqlite3_column_text);
  SL(sqlite3_column_double); SL(sqlite3_column_bytes); SL(sqlite3_column_blob); SL(sqlite3_errmsg); SL(sqlite3_changes); SL(sqlite3_last_insert_rowid);
#undef SL
  sq_state = 1; return true;
}
#else
static bool sql_load(void) {
  if (sq_state) return sq_state > 0;
  #if SOLAN_WIN
  static const char *names[] = {"sqlite3.dll", "libsqlite3-0.dll", NULL};
#else
  static const char *names[] = {"libsqlite3.so.0", "libsqlite3.so", "libsqlite3.so.30", "/usr/lib/x86_64-linux-gnu/libsqlite3.so.0", "/usr/lib/aarch64-linux-gnu/libsqlite3.so.0", "/usr/lib64/libsqlite3.so.0", "/usr/local/lib/libsqlite3.so", "libsqlite3.dylib", "/opt/homebrew/opt/sqlite/lib/libsqlite3.dylib", "/usr/lib/libsqlite3.dylib", NULL};
#endif
  for (int i = 0; names[i] && !sq.h; i++) sq.h = dlopen(names[i], RTLD_NOW);
  if (!sq.h) { sq_state = -1; return false; }
#define SL(n) do { *(void **)&sq.n = dlsym(sq.h, #n); if (!sq.n) { sq_state = -1; return false; } } while (0)
  SL(sqlite3_open); SL(sqlite3_close); SL(sqlite3_prepare_v2); SL(sqlite3_step); SL(sqlite3_finalize); SL(sqlite3_bind_text); SL(sqlite3_bind_double);
  SL(sqlite3_bind_null); SL(sqlite3_bind_int64); SL(sqlite3_column_count); SL(sqlite3_column_name); SL(sqlite3_column_type); SL(sqlite3_column_text);
  SL(sqlite3_column_double); SL(sqlite3_column_bytes); SL(sqlite3_column_blob); SL(sqlite3_errmsg); SL(sqlite3_changes); SL(sqlite3_last_insert_rowid);
#undef SL
  sq_state = 1; return true;
}
#endif
static bool sql_dbarg(Value v, void **db) {
  if (!IS_NUM(v) || v.as.n < 0 || v.as.n >= 64 || !dbs[(int)v.as.n]) { rt_error("مقبض قاعدة بيانات غير صالح"); return false; }
  *db = dbs[(int)v.as.n]; return true;
}
NAT(db_open) {
  STRA(0); if (!sql_load()) FAIL("SQLite غير متاح في هذا البناء: أعد البناء بـ sh install.sh لتضمينها (أو ثبّت مكتبة النظام: pkg install sqlite)");
  int slot = -1; for (int i = 0; i < 64; i++) if (!dbs[i]) { slot = i; break; } if (slot < 0) FAIL("فتحت قواعد بيانات كثيرة");
  void *db = NULL; if (sq.sqlite3_open(AS_STR(a[0])->chars, &db) != 0) { Str *m = intern_c(db ? sq.sqlite3_errmsg(db) : "فشل الفتح"); if (db) sq.sqlite3_close(db); FAIL("%s", m->chars); }
  dbs[slot] = db; *res = NUMV(slot); return true;
}
NAT(db_close) { void *db; if (!sql_dbarg(a[0], &db)) return false; sq.sqlite3_close(db); dbs[(int)a[0].as.n] = NULL; *res = NIL; return true; }
static bool db_bind(void *db, void *stmt, Value params) {
  if (!IS_T(params, O_LIST)) return true; List *l = AS_LIST(params);
  for (int i = 0; i < l->a.n; i++) {
    Value v = l->a.v[i]; int r;
    if (IS_NIL(v)) r = sq.sqlite3_bind_null(stmt, i + 1);
    else if (IS_NUM(v)) { if (v.as.n == floor(v.as.n) && fabs(v.as.n) < 9e15) r = sq.sqlite3_bind_int64(stmt, i + 1, (long long)v.as.n); else r = sq.sqlite3_bind_double(stmt, i + 1, v.as.n); }
    else if (v.t == V_BOOL) r = sq.sqlite3_bind_int64(stmt, i + 1, v.as.b ? 1 : 0);
    else { Str *s = tostr(v); r = sq.sqlite3_bind_text(stmt, i + 1, s->chars, s->len, (void *)-1); }
    if (r != 0) { rt_error("فشل ربط المعامل %d: %s", i + 1, sq.sqlite3_errmsg(db)); return false; }
  }
  return true;
}
static bool db_run(Value *a, int argc, Value *res, bool query) {
  void *db; if (!sql_dbarg(a[0], &db)) return false; if (!IS_T(a[1], O_STR)) { rt_error("استعلام SQL يجب أن يكون نصا"); return false; }
  const char *sql = AS_STR(a[1])->chars; Value params = argc > 2 ? a[2] : NIL; List *rows = new_list(); push(OBJV(rows)); long long changes = 0;
  while (*sql) {
    while (*sql == ' ' || *sql == '\n' || *sql == '\t' || *sql == '\r' || *sql == ';') sql++; if (!*sql) break;
    void *stmt = NULL; const char *tail = NULL;
    if (sq.sqlite3_prepare_v2(db, sql, -1, &stmt, &tail) != 0) { vm.sp--; rt_error("SQL: %s", sq.sqlite3_errmsg(db)); return false; }
    if (!stmt) { sql = tail; continue; }
    if (!db_bind(db, stmt, params)) { sq.sqlite3_finalize(stmt); vm.sp--; return false; }
    int rc; int nc = sq.sqlite3_column_count(stmt);
    while ((rc = sq.sqlite3_step(stmt)) == 100) {
      if (!query) continue; Map *row = new_map(); varr_push(&rows->a, OBJV(row));
      for (int c = 0; c < nc; c++) {
        const char *nm = sq.sqlite3_column_name(stmt, c); Value v; int t = sq.sqlite3_column_type(stmt, c);
        if (t == 1 || t == 2) v = NUMV(sq.sqlite3_column_double(stmt, c)); else if (t == 3) v = mkstr((const char *)sq.sqlite3_column_text(stmt, c), sq.sqlite3_column_bytes(stmt, c));
        else if (t == 4) v = mkstr((const char *)sq.sqlite3_column_blob(stmt, c), sq.sqlite3_column_bytes(stmt, c)); else v = NIL;
        dset(&row->d, mkstr(nm, (int)strlen(nm)), v);
      }
    }
    if (rc != 101) { sq.sqlite3_finalize(stmt); vm.sp--; rt_error("SQL: %s", sq.sqlite3_errmsg(db)); return false; }
    changes += sq.sqlite3_changes(db); sq.sqlite3_finalize(stmt); sql = tail; params = NIL;
  }
  vm.sp--;
  if (query) *res = OBJV(rows);
  else { Map *m = new_map(); dset(&m->d, KX("متأثرة"), NUMV((double)changes)); dset(&m->d, KN("معرف_اخير"), NUMV((double)sq.sqlite3_last_insert_rowid(db))); *res = OBJV(m); }
  return true;
}
NAT(db_exec) { return db_run(a, argc, res, false); }
NAT(db_query) { return db_run(a, argc, res, true); }
