static void disasm_fun(Fun *f, int depth) {
  fprintf(stdout, "%*s== %s ==\n", depth * 2, "", f->name ? f->name->chars : "<الملف>");
  const uint8_t *code = f->ch.code, *end = code + f->ch.n;
  for (const uint8_t *ip = code; ip < end; ip = op_next(f, ip)) {
    int len = op_operand_len(f, ip);
    fprintf(stdout, "%*s%04d  %-16s", depth * 2, "", (int)(ip - code), op_names[*ip]);
    if (len == 1) fprintf(stdout, " %d", ip[1]);
    else if (len >= 2) {
      int a = (ip[1] << 8) | ip[2];
      Op o = (Op)*ip;
      if (o == OP_JUMP || o == OP_JIF || o == OP_JIT || o == OP_JNN || o == OP_JNIL || o == OP_TRY) fprintf(stdout, " ->%04d", (int)(ip - code) + 3 + a);
      else if (o == OP_LOOP) fprintf(stdout, " ->%04d", (int)(ip - code) + 3 - a);
      else if (o == OP_CHECK) fprintf(stdout, " mask=%d  ; %s", a, AS_STR(AS_LIST(f->ch.consts.v[(ip[3] << 8) | ip[4]])->a.v[0])->chars);
      else if (o == OP_FOREACH) fprintf(stdout, " slot=%d nv=%d ->%04d", ip[1], ip[2], (int)(ip - code) + 5 + ((ip[3] << 8) | ip[4]));
      else {
        fprintf(stdout, " %d", a);
        if (a < f->ch.consts.n && o != OP_LIST && o != OP_MAP && o != OP_INVOKEV) {
          Buf b; buf_init(&b); Value c = f->ch.consts.v[a];
          if (IS_T(c, O_FUN)) buf_fmt(&b, "<fn>"); else repr(c, &b, 0, true);
          fprintf(stdout, "  ; %s", buf_cstr(&b)); free(b.p);
        }
      }
    }
    fputc('\n', stdout);
  }
  for (int i = 0; i < f->ch.consts.n; i++) if (IS_T(f->ch.consts.v[i], O_FUN)) disasm_fun(AS_FUN(f->ch.consts.v[i]), depth + 1);
}
