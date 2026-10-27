#ifndef SOLAN_JIT
#if defined(__x86_64__) && defined(__linux__)
#define SOLAN_JIT 1
#else
#define SOLAN_JIT 0
#endif
#endif

#if SOLAN_JIT
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
enum { CC_O, CC_NO, CC_B, CC_AE, CC_E, CC_NE, CC_BE, CC_A, CC_S, CC_NS, CC_P, CC_NP, CC_L, CC_GE, CC_LE, CC_G };

typedef struct { uint8_t *p; int n, cap; } XB;

static void xb_b(XB *x, int v) { if (x->n < x->cap) x->p[x->n] = (uint8_t)v; x->n++; }
static void xb_u32(XB *x, uint32_t v) { for (int i = 0; i < 4; i++) xb_b(x, (v >> (8 * i)) & 0xFF); }
static void xb_u64(XB *x, uint64_t v) { for (int i = 0; i < 8; i++) xb_b(x, (int)((v >> (8 * i)) & 0xFF)); }

static void jx_rex(XB *x, int w, int r, int xi, int b) {
  int v = 0x40 | (w ? 8 : 0) | ((r & 8) ? 4 : 0) | ((xi & 8) ? 2 : 0) | ((b & 8) ? 1 : 0);
  if (v != 0x40) xb_b(x, v);
}
static void jx_opc(XB *x, int op) {
  if (op > 0xFFFF) { xb_b(x, 0x0F); xb_b(x, (op >> 8) & 0xFF); xb_b(x, op & 0xFF); }
  else if (op > 0xFF) { xb_b(x, 0x0F); xb_b(x, op & 0xFF); }
  else xb_b(x, op);
}
static void jx_modrm_m(XB *x, int reg, int base, int idx, int sc, int32_t disp) {
  if (idx >= 0) { xb_b(x, 0x84 | ((reg & 7) << 3)); xb_b(x, (sc << 6) | ((idx & 7) << 3) | (base & 7)); }
  else if ((base & 7) == 4) { xb_b(x, 0x84 | ((reg & 7) << 3)); xb_b(x, 0x24); }
  else xb_b(x, 0x80 | ((reg & 7) << 3) | (base & 7));
  xb_u32(x, (uint32_t)disp);
}
static void jx_rm(XB *x, int pfx, int w, int op, int reg, int base, int idx, int sc, int32_t disp) {
  if (pfx) xb_b(x, pfx);
  jx_rex(x, w, reg, idx < 0 ? 0 : idx, base);
  jx_opc(x, op);
  jx_modrm_m(x, reg, base, idx, sc, disp);
}
static void jx_rr(XB *x, int pfx, int w, int op, int reg, int rm) {
  if (pfx) xb_b(x, pfx);
  jx_rex(x, w, reg, 0, rm);
  jx_opc(x, op);
  xb_b(x, 0xC0 | ((reg & 7) << 3) | (rm & 7));
}

static void jx_mov_ri(XB *x, int r, uint64_t imm) {
  if (imm <= 0xFFFFFFFFull) { jx_rex(x, 0, 0, 0, r); xb_b(x, 0xB8 + (r & 7)); xb_u32(x, (uint32_t)imm); }
  else { jx_rex(x, 1, 0, 0, r); xb_b(x, 0xB8 + (r & 7)); xb_u64(x, imm); }
}
static void jx_mov_rm(XB *x, int r, int base, int32_t d) { jx_rm(x, 0, 1, 0x8B, r, base, -1, 0, d); }
static void jx_mov_mr(XB *x, int base, int32_t d, int r) { jx_rm(x, 0, 1, 0x89, r, base, -1, 0, d); }
static void jx_mov32_rm(XB *x, int r, int base, int32_t d) { jx_rm(x, 0, 0, 0x8B, r, base, -1, 0, d); }
static void jx_mov32_mr(XB *x, int base, int32_t d, int r) { jx_rm(x, 0, 0, 0x89, r, base, -1, 0, d); }
static void jx_mov_rr(XB *x, int dst, int src) { jx_rr(x, 0, 1, 0x89, src, dst); }
static void jx_mov32_mi(XB *x, int base, int32_t d, uint32_t imm) { jx_rm(x, 0, 0, 0xC7, 0, base, -1, 0, d); xb_u32(x, imm); }
static void jx_mov8_mr(XB *x, int base, int32_t d, int r) { jx_rm(x, 0, 0, 0x88, r, base, -1, 0, d); }
static void jx_movzx8_rm(XB *x, int r, int base, int32_t d) { jx_rm(x, 0, 0, 0x0FB6, r, base, -1, 0, d); }
static void jx_movzx8_rr(XB *x, int r, int rm) { jx_rr(x, 0, 0, 0x0FB6, r, rm); }
static void jx_lea(XB *x, int r, int base, int idx, int sc, int32_t d) { jx_rm(x, 0, 1, 0x8D, r, base, idx, sc, d); }
static void jx_cmp32_mi(XB *x, int base, int32_t d, int32_t imm) { jx_rm(x, 0, 0, 0x81, 7, base, -1, 0, d); xb_u32(x, (uint32_t)imm); }
static void jx_cmp8_mi(XB *x, int base, int32_t d, int imm) { jx_rm(x, 0, 0, 0x80, 7, base, -1, 0, d); xb_b(x, imm); }
static void jx_cmp_rm(XB *x, int r, int base, int32_t d) { jx_rm(x, 0, 1, 0x3B, r, base, -1, 0, d); }
static void jx_cmp32_rm(XB *x, int r, int base, int32_t d) { jx_rm(x, 0, 0, 0x3B, r, base, -1, 0, d); }
static void jx_cmp_rr(XB *x, int a, int b) { jx_rr(x, 0, 1, 0x39, b, a); }
static void jx_cmp32_ri(XB *x, int r, int32_t imm) { jx_rex(x, 0, 0, 0, r); xb_b(x, 0x81); xb_b(x, 0xC0 | (7 << 3) | (r & 7)); xb_u32(x, (uint32_t)imm); }
static void jx_test8_rr(XB *x, int a, int b) { jx_rr(x, 0, 0, 0x84, b, a); }
static void jx_test32_rr(XB *x, int a, int b) { jx_rr(x, 0, 0, 0x85, b, a); }
static void jx_setcc(XB *x, int cc, int r) { jx_rr(x, 0, 0, 0x0F90 + cc, 0, r); }
static void jx_and8_rr(XB *x, int dst, int src) { jx_rr(x, 0, 0, 0x20, src, dst); }
static void jx_xor8_ri(XB *x, int r, int imm) { jx_rex(x, 0, 0, 0, r); xb_b(x, 0x80); xb_b(x, 0xC0 | (6 << 3) | (r & 7)); xb_b(x, imm); }
static void jx_alu_rr(XB *x, int op, int dst, int src) { jx_rr(x, 0, 1, op, src, dst); }
static void jx_alu32_rr(XB *x, int op, int dst, int src) { jx_rr(x, 0, 0, op, src, dst); }
static void jx_shl_cl(XB *x, int r) { jx_rr(x, 0, 1, 0xD3, 4, r); }
static void jx_sar_cl(XB *x, int r) { jx_rr(x, 0, 1, 0xD3, 7, r); }
static void jx_shl_ri(XB *x, int r, int imm) { jx_rex(x, 1, 0, 0, r); xb_b(x, 0xC1); xb_b(x, 0xC0 | (4 << 3) | (r & 7)); xb_b(x, imm); }
static void jx_add32_ri(XB *x, int r, int32_t imm) { jx_rex(x, 0, 0, 0, r); xb_b(x, 0x81); xb_b(x, 0xC0 | (r & 7)); xb_u32(x, (uint32_t)imm); }
static void jx_and32_ri(XB *x, int r, int32_t imm) { jx_rex(x, 0, 0, 0, r); xb_b(x, 0x81); xb_b(x, 0xC0 | (4 << 3) | (r & 7)); xb_u32(x, (uint32_t)imm); }
static void jx_cdq(XB *x) { xb_b(x, 0x99); }
static void jx_idiv32(XB *x, int r) { jx_rr(x, 0, 0, 0xF7, 7, r); }
static void jx_push(XB *x, int r) { jx_rex(x, 0, 0, 0, r); xb_b(x, 0x50 + (r & 7)); }
static void jx_pop(XB *x, int r) { jx_rex(x, 0, 0, 0, r); xb_b(x, 0x58 + (r & 7)); }
static void jx_ret(XB *x) { xb_b(x, 0xC3); }
static void jx_call_rax(XB *x) { xb_b(x, 0xFF); xb_b(x, 0xD0); }

static void jx_movsd_rm(XB *x, int xr, int base, int32_t d) { jx_rm(x, 0xF2, 0, 0x0F10, xr, base, -1, 0, d); }
static void jx_movsd_mr(XB *x, int base, int32_t d, int xr) { jx_rm(x, 0xF2, 0, 0x0F11, xr, base, -1, 0, d); }
static void jx_movaps(XB *x, int dst, int src) { if (dst != src) jx_rr(x, 0, 0, 0x0F28, dst, src); }
static void jx_movups_rm(XB *x, int xr, int base, int idx, int32_t d) { jx_rm(x, 0, 0, 0x0F10, xr, base, idx, 0, d); }
static void jx_movups_mr(XB *x, int base, int idx, int32_t d, int xr) { jx_rm(x, 0, 0, 0x0F11, xr, base, idx, 0, d); }
static void jx_movq_xr(XB *x, int xr, int r) { jx_rr(x, 0x66, 1, 0x0F6E, xr, r); }
static void jx_sse_rr(XB *x, int pfx, int op, int dst, int src) { jx_rr(x, pfx, 0, op, dst, src); }
static void jx_addsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F58, d, s); }
static void jx_subsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F5C, d, s); }
static void jx_mulsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F59, d, s); }
static void jx_divsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F5E, d, s); }
static void jx_ucomisd(XB *x, int a, int b) { jx_sse_rr(x, 0x66, 0x0F2E, a, b); }
static void jx_xorpd(XB *x, int d, int s) { jx_sse_rr(x, 0x66, 0x0F57, d, s); }
static void jx_cvttsd2si64(XB *x, int r, int xr) { jx_rr(x, 0xF2, 1, 0x0F2C, r, xr); }
static void jx_cvttsd2si32(XB *x, int r, int xr) { jx_rr(x, 0xF2, 0, 0x0F2C, r, xr); }
static void jx_cvtsi2sd64(XB *x, int xr, int r) { jx_rr(x, 0xF2, 1, 0x0F2A, xr, r); }
static void jx_cvtsi2sd32(XB *x, int xr, int r) { jx_rr(x, 0xF2, 0, 0x0F2A, xr, r); }
static void jx_cvtsi2sd32_m(XB *x, int xr, int base, int32_t d) { jx_rm(x, 0xF2, 0, 0x0F2A, xr, base, -1, 0, d); }
static void jx_roundsd(XB *x, int d, int s, int mode) { jx_rr(x, 0x66, 0, 0x0F3A0B, d, s); xb_b(x, mode); }

static void jx_sub_rsp(XB *x, int32_t v) { xb_b(x, 0x48); xb_b(x, 0x81); xb_b(x, 0xEC); xb_u32(x, (uint32_t)v); }
static void jx_add_rsp(XB *x, int32_t v) { xb_b(x, 0x48); xb_b(x, 0x81); xb_b(x, 0xC4); xb_u32(x, (uint32_t)v); }
static void jx_imul64_rr(XB *x, int dst, int src) { jx_rr(x, 0, 1, 0x0FAF, dst, src); }
static void jx_imul32_rri(XB *x, int dst, int src, int32_t imm) { jx_rr(x, 0, 0, 0x69, dst, src); xb_u32(x, (uint32_t)imm); }
static void jx_shr64_ri(XB *x, int r, int imm) { jx_rex(x, 1, 0, 0, r); xb_b(x, 0xC1); xb_b(x, 0xC0 | (5 << 3) | (r & 7)); xb_b(x, imm); }
static void jx_sqrtsd(XB *x, int d, int s) { jx_sse_rr(x, 0xF2, 0x0F51, d, s); }

static void jx_rel32_jcc(XB *x, int cc) { xb_b(x, 0x0F); xb_b(x, 0x80 + cc); }
static void jx_rel32_jmp(XB *x) { xb_b(x, 0xE9); }
#endif
